"""Container-only TCP network probe. No protocol/secret sharing is executed.
All rates use exact received payload and receiver completion wall time.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import socket
import statistics
import subprocess
import threading
import time

HOST = '127.0.0.1'
CHUNK = b'x' * (64 * 1024)


def snapshot(dev):
    out = {}
    for kind in ('qdisc', 'class', 'filter'):
        out[kind] = subprocess.check_output(['tc', '-s', kind, 'show', 'dev', dev], text=True)
        raw = subprocess.check_output(['tc', '-j', '-s', kind, 'show', 'dev', dev], text=True)
        # Some container iproute2 builds accept -j but still print text.
        try:
            out[kind + '_json'] = json.loads(raw.strip() or '[]')
        except json.JSONDecodeError:
            out[kind + '_json'] = []
            out[kind + '_json_unavailable'] = True
    return out


def class_bytes(state):
    result = {}
    for item in state['class_json']:
        key = item.get('handle', item.get('classid'))
        if key is not None:
            result[key] = item.get('bytes', item.get('stats', {}).get('bytes', 0))
    if not result:
        result = {key: int(count) for key, count in re.findall(
            r'^class\s+\S+\s+(\S+)[^\n]*\n\s*Sent\s+(\d+)\s+bytes',
            state['class'], flags=re.MULTILINE)}
    return result


def connection(port):
    """Source port has opposite parity: ACKs use the opposite rate class."""
    listener = socket.socket()
    client = socket.socket()
    try:
        for s in (listener, client):
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.settimeout(30)
        listener.bind((HOST, port))
        listener.listen(1)
        client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client.bind((HOST, port + 1001))
        client.connect((HOST, port))
        server, _ = listener.accept()
        server.settimeout(30)
        server.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return client, server
    except BaseException:
        client.close()
        raise
    finally:
        listener.close()


def receive_exact(conn, size):
    data = bytearray()
    while len(data) < size:
        part = conn.recv(size - len(data))
        if not part:
            raise RuntimeError(f'EOF: expected {size}, received {len(data)}')
        data.extend(part)
    return data


def transfer(ports, size):
    pairs = [connection(p) for p in ports]
    gate = threading.Barrier(2 * len(pairs) + 1, timeout=10)
    started = [0.0]

    def sender(sock):
        gate.wait()
        remaining = size
        while remaining:
            count = min(len(CHUNK), remaining)
            sock.sendall(CHUNK[:count])
            remaining -= count
        # Wait for application completion, not merely send-buffer acceptance.
        if receive_exact(sock, 1) != b'd':
            raise RuntimeError('missing completion acknowledgment')

    def receiver(sock):
        gate.wait()
        received = 0
        while received < size:
            data = sock.recv(min(64 * 1024, size - received))
            if not data:
                raise RuntimeError(f'short transfer: {received}/{size}')
            received += len(data)
        seconds = time.perf_counter() - started[0]
        sock.sendall(b'd')
        return {'bytes': received, 'seconds': seconds,
                'receiver_mbps': received * 8 / seconds / 1e6}

    try:
        with ThreadPoolExecutor(max_workers=2 * len(pairs)) as pool:
            senders = [pool.submit(sender, c) for c, _ in pairs]
            receivers = [pool.submit(receiver, s) for _, s in pairs]
            started[0] = time.perf_counter()
            gate.wait()
            rows = [f.result(timeout=90) for f in receivers]
            for f in senders:
                f.result(timeout=90)
        return {'ports': ports, 'lanes': rows,
                'aggregate_receiver_mbps': sum(x['bytes'] for x in rows) * 8 /
                max(x['seconds'] for x in rows) / 1e6}
    finally:
        for pair in pairs:
            for sock in pair:
                sock.close()


def rtt(port, samples):
    client, server = connection(port)

    def echo():
        for _ in range(samples + 5):
            server.sendall(receive_exact(server, 64))

    try:
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(echo)
            times = []
            for i in range(samples + 5):
                begin = time.perf_counter()
                client.sendall(b'p' * 64)
                if receive_exact(client, 64) != b'p' * 64:
                    raise RuntimeError('echo mismatch')
                if i >= 5:
                    times.append((time.perf_counter() - begin) * 1000)
            future.result(timeout=30)
        return {'port': port, 'samples_ms': times, 'median_ms': statistics.median(times),
                'min_ms': min(times), 'max_ms': max(times)}
    finally:
        client.close()
        server.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--profile', default=os.environ.get('PROFILE', 'unknown'))
    p.add_argument('--base', type=int, required=True)
    p.add_argument('--mib', type=int, default=64)
    p.add_argument('--samples', type=int, default=50)
    p.add_argument('--dev', default='lo')
    p.add_argument('--json-output')
    p.add_argument('--latency-only', action='store_true')
    args = p.parse_args()
    if args.base % 2 or not 1024 <= args.base <= 64000 or args.mib < 1 or args.samples < 2:
        p.error('base must be even within 1024..64000; positive MiB and >=2 samples required')
    result = {'profile': args.profile, 'bytes_per_lane': args.mib * 1024 * 1024,
              'affinity': sorted(os.sched_getaffinity(0)), 'before': snapshot(args.dev)}
    result['rtt_odd'] = rtt(args.base + 21, args.samples)
    result['rtt_even'] = rtt(args.base + 22, args.samples)
    if not args.latency_only:
        result['transfers'] = {}
        for phase, ports in [('odd', [args.base + 1]), ('even', [args.base + 2]),
                             ('simultaneous', [args.base + 11, args.base + 12])]:
            before = snapshot(args.dev)
            row = transfer(ports, result['bytes_per_lane'])
            after = snapshot(args.dev)
            old, new = class_bytes(before), class_bytes(after)
            row.update(before=before, after=after,
                       class_sent_bytes_delta={k: new[k] - old.get(k, 0) for k in new})
            result['transfers'][phase] = row
            print('PHASE', args.profile, phase,
                  'receiver_mbps=' + ','.join(f"{x['receiver_mbps']:.3f}" for x in row['lanes']), flush=True)
    result['after'] = snapshot(args.dev)
    if args.json_output:
        Path(args.json_output).write_text(json.dumps(result, indent=2) + '\n')
    print(f'PROFILE={args.profile}')
    print(f"rtt_odd_median_ms={result['rtt_odd']['median_ms']:.6f}")
    print(f"rtt_even_median_ms={result['rtt_even']['median_ms']:.6f}")


if __name__ == '__main__':
    main()
