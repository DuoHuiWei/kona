#!/usr/bin/env python3
"""Small container-only integration regression; never configures qdisc.
Run from a scratch cwd containing Player-Data/2-fss test keys.
Protocol implementations live in C++; this script only launches/checks them.
"""
import argparse
import csv
import json
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--binary', default='/usr/src/Garnet/knn-bench.x')
p.add_argument('--before', default='/tmp/kona-matrix-refactor-before')
p.add_argument('--output', required=True)
a = p.parse_args()
root = pathlib.Path(a.output)
root.mkdir(parents=True, exist_ok=True)
subprocess.run(['tc', '-s', 'qdisc', 'show', 'dev', 'lo'], stdout=(root/'qdisc.txt').open('w'), check=True)
base = [(200,5,'old-hybrid',10), (200,5,'full-matrix-hybrid',10),
        (200,5,'pruned-matrix-hybrid',10), (200,17,'full-matrix-hybrid',190),
        (200,17,'pruned-matrix-hybrid',190), (200,5,'full-matrix-hybrid',1000000),
        (200,5,'pruned-matrix-hybrid',1000000), (300,17,'pruned-matrix-hybrid',10)]
checks = []

def parse(path, prefix='RESULT '):
    return [dict(t.split('=', 1) for t in line.split()[1:] if '=' in t)
            for line in path.read_text().splitlines() if line.startswith(prefix)]

def pair(name, n, k, mode, threshold=10, extra=(), binary=None):
    processes, files = [], []
    try:
        for party, cpu in [(0,0),(1,2)]:
            f = (root/f'{name}-P{party}.log').open('w'); files.append(f)
            selected = binary or a.binary
            dispatch = ['--scheme','hybrid','--scope','topk'] if pathlib.Path(selected).name=='knn-bench.x' else []
            command = ['taskset','-c',str(cpu),selected,*dispatch,'-p',str(party),'-pn',str(26500+len(checks)*10),
                       '-n',str(n),'-k',str(k),'-m',mode,'--threshold',str(threshold),'-r','1',*extra]
            processes.append(subprocess.Popen(command, stdout=f, stderr=subprocess.STDOUT))
        for process in processes:
            if process.wait(timeout=45):
                raise RuntimeError(f'{name} failed: read {root}/{name}-P*.log')
    finally:
        for process in processes:
            if process.poll() is None: process.kill()
        for process in processes: process.wait()
        for f in files: f.close()
    rows = []
    for party in [0,1]:
        log = root/f'{name}-P{party}.log'
        result = parse(log)
        assert len(result)==1, (name,result)
        row=result[0]; assert row['correctness']=='PASS',row
        assert int(row['transport_rounds']) == 2*int(row['logical_rounds']),row
        assert int(row['pcr_comparators'])+int(row['dcf_comparators'])==int(row['total_comparators']),row
        assert float(row['online_ms'])>=0 and int(row['sent_bytes'])>=0,row
        rows.append(row)
        for vote in parse(log,'VOTE '): assert vote['correctness']=='PASS',vote
    checks.append(dict(name=name, **{'max_online_ms':max(float(r['online_ms']) for r in rows)},
                       sent_bytes=rows[0]['sent_bytes'], comparators=rows[0]['total_comparators'],
                       depth=rows[0]['structural_depth']))
    return rows

for i,(n,k,mode,t) in enumerate(base):
    rows=pair(f'baseline-{i}',n,k,mode,t)
    for party,row in enumerate(rows):
        before=parse(pathlib.Path(a.before)/f'{i}-P{party}.log')[0]
        for field in ['total_comparators','structural_depth','sent_bytes','transport_rounds',
                      'logical_rounds','dcf_evaluate_calls','pcr_comparators','dcf_comparators']:
            assert row[field]==before[field],(i,party,field,row[field],before[field])

# Standalone matrix terminal and full composition, different backends/fixtures.
for mode in ['full','pruned']:
    for pattern in ['duplicates','interleaved','concentrated']:
        pair(f'{mode}-{pattern}',200,5,mode,extra=['--input-pattern',pattern,'--vote-labels','16'])
    pair(f'{mode}-terminal',200,17,mode,extra=['--scope','terminal','--comparator','pcr','--vote-labels','16'])
    pair(f'{mode}-dcf-terminal',64,5,mode,extra=['--scope','terminal','--comparator','dcf'])
    pair(f'{mode}-short-tail',257,31,mode)
    pair(f'{mode}-k1',2,1,mode,extra=['--vote-labels','16'])
for mode in ['two-block-reverse-hybrid','reverse-prefilter-matrix-only','obssn-matrix-only','octm-matrix-only']:
    pair(mode,200,5,mode,extra=['--vote-labels','16'])
# Multi-block reverse, small N but K=1 creates >2 blocks. No large experiments.
pair('prefilter-multi',512,1,'reverse-prefilter-matrix-only')
# Dataset fixture verifies read/ESD excluded, majority and tied vote behavior.
dataset = pathlib.Path('matrix-fixture').resolve(); dataset.mkdir(exist_ok=True)
(dataset/'Knn-meta').write_text('1 6 1\n')
(dataset/'P0-0-X-Train').write_text('1\n2\n3\n4\n5\n9\n')
(dataset/'P1-0-X-Test').write_text('0\n')
(dataset/'P1-0-Y-Test').write_text('2\n')
for tag,labels in [('majority','2\n1\n2\n3\n2\n0\n'),('tied','2\n1\n2\n1\n3\n0\n')]:
    (dataset/'P0-0-Y-Train').write_text(labels)
    for mode in ['full','pruned']:
        pair(f'dataset-{tag}-{mode}',6,5,mode,extra=['--dataset',str(dataset),'--vote-labels','4'])
# Historical pure-mode name dispatches to the same maintained C++ body.
pair('legacy-mode',64,5,'new-pcr')
(root/'checks.json').write_text(json.dumps(checks,indent=2)+'\n')
print(f'PASS: {len(checks)} small two-party cases; baseline counts/bytes/exchanges equal in all 8 cases')
