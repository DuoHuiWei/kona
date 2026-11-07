#!/usr/bin/env python3
"""One experiment organizer, one C++ body: Machines/knn-bench.cpp -> knn-bench.x.

Usage:
  python3 -u Scripts/run-knn-experiment.py --scheme kona --scope full --datasets mnist iris --ks 5 --repeats 3 --profiles lan wan --e2e-only

Ours six-dataset formal run (one existing C++ body + this launcher):
  python3 -u Scripts/run-knn-experiment.py --scheme hybrid --scope full --datasets iris arcene tcga-pancan spambase mnist dota2-games-results --ks 5 --profiles lan wan --repeats 3 --monitor
Each dataset has exactly one stored query; all stored training rows are used.
Repetition outermost, rotate the first dataset, LAN then WAN for each dataset.
Keep original partition/cutoff and vote_dcf_optimized; thresholds default to
LAN180/WAN6000 (LAN confirmed 2026-11-06), recorded explicitly; C++ binds
P0/CPU0 and P1/CPU2. Full ONLINE (not offline-inclusive E2E) is independently
timed through winner shares; retain distance/block_cong/terminal/topk/vote,
actual per-party counters and theoretical rounds. No input sharing over WAN
is added to offline L2 preparation. No thresholds/algorithms are retuned.
--monitor measures both bandwidth lanes and RTT ONCE per profile before
protocols, using the existing TCP probe (64 MiB/lane, including simultaneous
lanes). No probe traffic overlaps protocol timing. Receiver throughput is a
network check, not protocol bytes divided by total wall time. During each job
a CPU4 observer samples /proc (RSS/CPU/affinity), memory/swap, TCP and tc every
5 seconds, checks qdisc/rate/delay/offload consistency, and records entry/exit
snapshots. Short jobs may finish between samples; C++ affinity checks still
apply to every job. CPU affinity is not system-level core exclusivity.
Monitoring is read-only and outside C++ timers, but its system overhead is
present in this monitored batch; use same monitored setting for comparisons.
Failures stop the batch, preserve partial results and clear network settings;
no automatic retries, no new benchmark/launcher/readme files are needed.

KONA two-dataset mode calls the unchanged original KONA full-flow bench:
original ABY2 distance, original per-layer Top1 and original batch k^2 vote.
Use --kona-backend batched-dcf explicitly to select the historical cached
batch comparator/graph/chunk policy from kona-mainpath-esd-topk-dcf-bench.cpp;
full flow also uses the original vote logic through that batch DCF backend.
Default original retains native per-evaluation key reading for old controls.
MNIST69999x784 and Iris149x4 each have one extra query. Trial outermost,
rotate first dataset, alternate LAN/WAN per dataset; CPU0/2 in C++.
Original ABY2 distance runs online locally; input sharing/keys/graph preparation
are outside online time. Input sharing still obeys the configured network and
can be much slower in wall time. Only measured knn_online is in the headline;
raw phases/bytes/rounds stay for correctness and chunk checks. No new launcher.

  python3 Scripts/run-knn-experiment.py --scheme hybrid --dry-run
  python3 -u Scripts/run-knn-experiment.py --scheme hybrid
  python3 -u Scripts/run-knn-experiment.py --scheme kona --scope full

Hybrid --scope vote reruns only voting; prepares exact plan candidates and
secret mask from distinct plaintext distances outside measurement. No secure
distance/Cong/matrix execution. Original stage results remain unchanged.

Shao: --scheme shao defaults to WAN only, Arcene N199, six k values x3,
plus NHSS distance x3 (21 pairs). Each repetition: distance ONCE, then all six
k values ONCE, rotating the first k across repetitions. Votes run per k (18),
not only three votes in total. Default staged scope prepares plaintext
distance fixtures outside Top-k/vote timing; distance jobs execute real NHSS.
--scope full executes real continuous NHSS+Cong+vote per k (18 WAN pairs).
ALL Cong layers use Rotated-MSB, without a DCF width cutoff. Vote uses batched DCF,
public-category counts and smallest-label tie rule. Existing L2 limits persist.

ConstantPP performance default is paper-local-l2; always pass it explicitly to
the C++ bench and verify the emitted mode on full runs. Legacy is reserved
for explicitly requested historical/control runs; no implicit fallback.

Schemes: constantpp (paper-local-l2), secknn (original scalar scan only),
kona (original), hybrid (pruned matrix, quarter cutoff).
2026-11-06: historical SecKNN layered mode changed the Top-1 graph and is
disabled. Preserve native scan/tie order in Top-k AND vote maximum. Only
independent message send/receive overlap is permitted as transport engineering.
One scheme runs at a time; no child experiment scripts or shell entry files.

Default --scope experiment reproduces the established experiment plan:
* ConstantPP: full single-k KNN for every k/profile/repeat plus separate k=5
  pre-vote measurements (42 pairs). Headline vote is its isolated vote phase.
* SecKNN/KONA: separate distance pairs, then one continuous Top-max(k) per
  profile/repeat with cumulative checkpoints and separate votes (12 pairs).
* Hybrid: separate KONA distance, then six k plans sharing exact Cong prefixes
  per profile/repeat, each with its own terminal/vote (12 pairs). k100 uses
  original Cong for N199 because 4*k>candidate_Lmax=128. Shared Top-k is
  COMPOSED; no staged/composed total is mislabeled as measured E2E.
--scope full runs uninterrupted, actually measured KNN for EACH k (36 pairs);
--scope distance runs distance only (6 pairs). Defaults: Arcene199x10000,
one fixed query, k1/2/3/5/20/100, LAN/WAN x3, no warmup or auto retry.

Repetition outermost, LAN/WAN alternate, first k rotates. Fixed CPU0/CPU2 in
C++; no alternate CPUs or claimed physical exclusivity. Raw party metrics,
network/offload/topology, actual order, hashes and partial summaries retained.
Time/bytes per trial max(P0,P1), then mean/median/sample SD/CV/min/max; retain
both parties' rounds. exchange_rounds means send/receive-actions/2, NOT always
protocol dependency rounds (e.g. ConstantPP shuffle); never replace with depth.
Prepare/build/share material/barriers/validation/log/Python wall time excluded
from online metrics. Existing L2/zero auxiliary security limitations persist.

Build/sync once through the existing kona-dev. Reuse existing test keys via
isolated symlinks; no image/dependency/key generation. LAN1Gbps/direction0.5ms,
WAN40Mbps/direction20ms one-way. --profiles native leaves qdisc unchanged and
uses LAN comparator policy, only for unshaped checks. Default hybrid thresholds
LAN180/WAN6000 and public Arcene label domain2. Clear shaping/restore offloads on
handled completion/failure; forced kill/power loss cannot guarantee cleanup.

A process lock prevents concurrent launches. Related files must stay frozen;
source/binary/data identity checks fail explicitly instead of mixing builds.
Failed jobs are preserved and not silently retried. New result directory only.
"""
import argparse
import csv
import datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import signal
import statistics
import subprocess
import sys
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
REMOTE='/usr/src/Garnet'
TARGET='knn-bench.x'
# The same header supplies C++ protocol and Python launch defaults.
CONFIG_HEADER=ROOT/'Machines/kona-bench-config.hpp'
DEFAULTS={k:int(v) for k,v in re.findall(r'constexpr std::size_t (\w+) = (\d+);',CONFIG_HEADER.read_text())}
if not {'DCF_CHUNK','MUL_CHUNK','LAN_THRESHOLD','WAN_THRESHOLD'} <= DEFAULTS.keys():
    raise RuntimeError('Missing shared benchmark defaults in '+str(CONFIG_HEADER))
KS=[1,2,3,5,20,100]

# The observer runs in the existing container on CPU4, separate from P0/P1.
# It writes JSON lines directly to the result archive, never from timed C++.
MONITOR_PROGRAM=r'''
import json,os,pathlib,re,select,subprocess,sys,time
os.sched_setaffinity(0,{4})
profile,marker=sys.argv[1:]
violations=[];baseline=None;samples=0;seen={}
def command(args):
 return subprocess.check_output(args,text=True,timeout=4)
def snapshot():
 global baseline,samples
 row=dict(time_ns=time.time_ns(),monotonic=time.monotonic(),observer_cpu=4)
 config={kind:command(['tc',kind,'show','dev','lo']) for kind in ['qdisc','class','filter']}
 flags=command(['ethtool','-k','lo'])
 offloads={k:re.search(r'^'+k+r': (\S+)',flags,re.M).group(1) for k in ['tcp-segmentation-offload','generic-segmentation-offload','generic-receive-offload']}
 rates={k:v for k,v in re.findall(r'^class htb (1:(?:10|20)) .*? rate (\S+)',config['class'],re.M)}
 delays={k:(v or '0ms') for k,v in re.findall(r'^qdisc netem (10:|20:)[^\n]*?limit \d+(?: delay (\S+))?',config['qdisc'],re.M)}
 expected_rate='1Gbit' if profile=='lan' else '40Mbit'
 expected_delay_us=500 if profile=='lan' else 20000
 def delay_us(value):
  match=re.fullmatch(r'([0-9.]+)(us|ms|s)',value)
  if not match:return None
  return float(match[1])*{'us':1,'ms':1000,'s':1000000}[match[2]]
 if rates!={'1:10':expected_rate,'1:20':expected_rate}:violations.append('rate mismatch '+str(rates))
 # tc text formatting / kernel time quantization can render 0.5ms as 499us.
 # Keep raw settings in the log; permit only a two-microsecond difference.
 parsed_delays={k:delay_us(v) for k,v in delays.items()}
 if set(parsed_delays)!={'10:','20:'} or any(v is None or abs(v-expected_delay_us)>2 for v in parsed_delays.values()):violations.append('delay mismatch '+str(delays))
 if set(offloads.values())!={'off'}:violations.append('offload mismatch '+str(offloads))
 signature=dict(config=config,offloads=offloads)
 if baseline is None:baseline=signature
 elif baseline!=signature:violations.append('network configuration changed')
 row.update(network_config=signature,tc_stats=command(['tc','-s','class','show','dev','lo']),
            qdisc_stats=command(['tc','-s','qdisc','show','dev','lo']),
            tcp=command(['ss','-tin']),meminfo=pathlib.Path('/proc/meminfo').read_text(),
            cpu_stat=pathlib.Path('/proc/stat').read_text(),processes=[])
 for p in pathlib.Path('/proc').glob('[0-9]*'):
  try:
   if (p/'comm').read_text().strip()!='knn-bench.x':continue
   env=(p/'environ').read_bytes().split(b'\0')
   if ('KONA_RUN_ID='+marker).encode() not in env:
    violations.append('unrelated benchmark process '+p.name);continue
   args=(p/'cmdline').read_bytes().decode().split('\0');party=int(args[args.index('-p')+1])
   stat=(p/'stat').read_text().split(') ',1)[1].split();affinity=sorted(os.sched_getaffinity(int(p.name)))
   bound=affinity==[0 if party==0 else 2]
   if p.name in seen and seen[p.name] and not bound:violations.append('affinity changed '+p.name)
   seen[p.name]=seen.get(p.name,False) or bound
   row['processes'].append(dict(pid=int(p.name),party=party,affinity=affinity,
      state=stat[0],utime_ticks=int(stat[11]),stime_ticks=int(stat[12]),
      last_cpu=int(stat[36]),status=(p/'status').read_text()))
  except (FileNotFoundError,ProcessLookupError):pass
 samples+=1;row['violations']=list(dict.fromkeys(violations));print(json.dumps(row),flush=True)
snapshot()
if violations:sys.exit(1)
print(json.dumps(dict(event='ready')),flush=True)
while not select.select([sys.stdin],[],[],5)[0]:snapshot()
sys.stdin.readline();snapshot()
print(json.dumps(dict(event='finished',samples=samples,observed_pids=seen,violations=list(dict.fromkeys(violations)))),flush=True)
sys.exit(bool(violations))
'''

def network_probe(a,out,profile):
    # The profile is already applied. Reuse the tested existing TCP diagnostic.
    remote='/tmp/knn-probe-'+uuid.uuid4().hex+'.json'
    logpath=out/('network-probe-'+profile+'.log')
    with logpath.open('w') as log:
        subprocess.run(['docker','exec',a.container,'taskset','-c','4','python3',
            REMOTE+'/Scripts/measure-container-profile-throughput.py','--profile',profile,
            '--base','42000','--mib','64','--samples','10','--json-output',remote],
            check=True,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    r=json.loads(subprocess.check_output(['docker','exec',a.container,'cat',remote],text=True))
    (out/('network-probe-'+profile+'.json')).write_text(json.dumps(r,indent=2)+'\n')
    target=1000 if profile=='lan' else 40
    rates=[v['receiver_mbps'] for part in r['transfers'].values() for v in part['lanes']]
    # Wide tolerances detect a missing lane or half-rate shaping, not noise.
    if not all(.65*target<v<1.3*target for v in rates):
        raise RuntimeError('Network bandwidth preflight failed: '+str(rates))
    rtts=[r[k]['median_ms'] for k in ['rtt_odd','rtt_even']]
    if not all((32<v<60) if profile=='wan' else (.7<v<5) for v in rtts):
        raise RuntimeError('Network latency preflight failed: '+str(rtts))
    print(f'NETWORK PASS {profile}: rates_Mbps={rates}, RTT_ms={rtts}',flush=True)

def start_monitor(a,out,job,profile):
    handle=(out/(job+'-monitor.jsonl')).open('w')
    proc=subprocess.Popen(['docker','exec','-i',a.container,'python3','-u','-c',
        MONITOR_PROGRAM,profile,out.name+'/'+job],stdin=subprocess.PIPE,stdout=handle,stderr=handle,text=True)
    # Wait for the initial capture so no protocol starts before validation.
    path=Path(handle.name);deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        if proc.poll() is not None:break
        if '"event": "ready"' in path.read_text():return proc,handle
        time.sleep(.05)
    if proc.poll() is None:proc.terminate()
    proc.wait(timeout=10);handle.close()
    raise RuntimeError('Monitor did not start: '+str(path))

def stop_monitor(monitor):
    proc,handle=monitor
    try:
        if proc.poll() is None:
            proc.stdin.write('stop\n');proc.stdin.flush();proc.stdin.close()
        code=proc.wait(timeout=15)
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
        handle.close()
    if code:raise RuntimeError('Process/network monitor failed; inspect '+handle.name)

def write_csv(path,rows):
    if rows:
        with path.open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def source_hashes():
    names=['Machines/kona-bench-config.hpp','Machines/knn-bench.cpp','Scripts/run-knn-experiment.py','Makefile',
        'Networking/Player.h','Networking/Player.cpp','Networking/Exchanger.h','Networking/sockets.cpp',
        'Tools/octetStream.h','Tools/octetStream.cpp',
        'Scripts/codex-container-build.sh','Scripts/container-network.sh','Scripts/kona-bench-log.sh',
        'Scripts/measure-container-profile-throughput.py',
        'Machines/kona-bench-affinity.hpp','Machines/secknn.cpp','Machines/kona-upstream.cpp',
        'Machines/kona-cong-topk.hpp','Machines/kona-reduced-depth-cong-topk.hpp',
        'Machines/kona-reduced-depth-hybrid-adapter.hpp','Machines/kona-mask-vote.hpp',
        'Machines/kona-pcr-compare.hpp','Machines/kona-dcf-compare.hpp',
        'Machines/kona-share-conversion.hpp','Machines/kona-cong-kona-adapter.hpp',
        'Machines/kona-ordered-block-terminal-networks.hpp',
        'Machines/kona-nhss-distance.hpp','Machines/kona-nhss-cong-pipeline.hpp',
        'Machines/kona-cong-rotated-msb-adapter.hpp','Machines/kona-rotated-msb-compare.hpp']
    names += [str(p.relative_to(ROOT)) for pattern in ['*.hpp','*.cpp'] for p in (ROOT/'ConstantPP').glob(pattern)]
    return {n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sorted(set(names))}

def schedule(a):
    jobs=[]
    def add(part,scope,k,profile,trial,order):
        engine='kona' if a.scheme=='hybrid' and scope=='distance' else a.scheme
        jobs.append(dict(part=part,scope=scope,k=k,trial=trial,network_profile=profile,
            bench_scheme=engine,ks=','.join(map(str,order)),
            job=f'{part}-t{trial:03d}-k{k:03d}-{profile}'))
    if getattr(a,'datasets',None):
        for trial in range(1,a.repeats+1):
            shift=(trial-1)%len(a.dataset_specs)
            ordered=a.dataset_specs[shift:]+a.dataset_specs[:shift]
            for spec in ordered:
                for k in a.ks:
                    for profile in a.profiles:
                        add(a.scope,a.scope,k,profile,trial,[])
                        jobs[-1].update(spec)
                        jobs[-1]['job']=a.scope+'-'+spec['dataset_name']+'-'+jobs[-1]['job']
        return jobs
    if a.scope=='vote':
        for trial in range(1,a.repeats+1):
            offset=(trial-1)%len(a.ks);order=a.ks[offset:]+a.ks[:offset]
            for k in order:
                for profile in a.profiles:add('vote','vote',k,profile,trial,order)
        return jobs
    # Shao staged sweep: one public distance measurement per repetition,
    # then EACH k once, rotating the first k. Never rerun distance per k.
    if a.scheme=='shao' and a.scope=='experiment':
        for trial in range(1,a.repeats+1):
            offset=(trial-1)%len(a.ks);order=a.ks[offset:]+a.ks[:offset]
            for profile in a.profiles:add('distance','distance',min(5,a.n),profile,trial,[])
            for k in order:
                for profile in a.profiles:add('topk-vote','topk-vote',k,profile,trial,order)
        return jobs
    if a.scheme=='constantpp' and a.scope=='experiment':
        # Distance/shuffle/rank/threshold/select work is invariant for 1<=k<N.
        # Measure it once per repetition; reuse exact prepared Top-k labels for votes.
        for trial in range(1,a.repeats+1):
            offset=(trial-1)%len(a.ks);order=a.ks[offset:]+a.ks[:offset]
            for profile in a.profiles:add('pre-vote','pre-vote',min(5,a.n-1),profile,trial,[])
            for k in order:
                for profile in a.profiles:add('vote','vote',k,profile,trial,order)
        return jobs
    if a.scope=='topk-vote':
        # Missing Shao k points only: never schedule NHSS distance here.
        for trial in range(1,a.repeats+1):
            offset=(trial-1)%len(a.ks);order=a.ks[offset:]+a.ks[:offset]
            for k in order:
                for profile in a.profiles:add('topk-vote','topk-vote',k,profile,trial,order)
        return jobs
    if a.scope=='topk':
        for trial in range(1,a.repeats+1):
            for k in a.ks:
                for profile in a.profiles:add('topk','topk',k,profile,trial,[])
        return jobs
    if a.scope=='full':
        for trial in range(1,a.repeats+1):
            offset=(trial-1)%len(a.ks);order=a.ks[offset:]+a.ks[:offset]
            for k in order:
                for profile in a.profiles:add('full','full',k,profile,trial,order)
    if a.scope=='distance' or (a.scope=='experiment' and a.scheme!='constantpp'):
        for trial in range(1,a.repeats+1):
            for profile in a.profiles:add('distance','distance',min(5,a.n),profile,trial,[])
    if a.scope=='experiment':
        if a.scheme=='constantpp':
            for trial in range(1,a.repeats+1):
                for profile in a.profiles:add('pre-vote','pre-vote',min(5,a.n-1),profile,trial,[])
        else:
            scope='shared' if a.scheme=='hybrid' else 'checkpoints'
            for trial in range(1,a.repeats+1):
                offset=(trial-1)%len(a.ks);order=a.ks[offset:]+a.ks[:offset]
                for profile in a.profiles:add(scope,scope,max(a.ks),profile,trial,order)
    return jobs

def command_arguments(a,item):
    policy='lan' if item['network_profile']=='native' else item['network_profile']
    args=['--scheme',item['bench_scheme'],'--scope',item['scope'],'--dataset',a.dataset,
          '--k',str(item['k']),'--network-profile',policy]
    if item['scope'] in ['checkpoints','shared']:args += ['--ks',item['ks']]
    if item['scope']=='checkpoints':args += ['--vote-order',item['ks']]
    if item['bench_scheme']=='kona':args += ['--kona-backend',a.kona_backend]
    if item['bench_scheme']=='secknn':args += ['--topk-mode',a.topk_mode]
    if item['bench_scheme']=='constantpp':args += ['--secmax-mode',a.secmax_mode]
    if item['bench_scheme']=='shao':args += ['--vote-labels',str(a.label_domain)]
    if item['bench_scheme']=='hybrid':
        args += ['-n',str(a.n),'-m','pruned','-r','1','--warmup','0','--vote-labels',str(a.label_domain)]
        if item['scope']!='vote':
            args += ['--comparator',a.comparator,'--matrix-backend',a.matrix_backend,
                '--lan-threshold',str(a.lan_threshold),'--wan-threshold',str(a.wan_threshold)]
    return args

def parse(path,item):
    text=path.read_text()
    meta=dict(l.split('=',1) for l in text.splitlines() if '=' in l and ' ' not in l.split('=',1)[0])
    phases={}
    for line in text.splitlines():
        if line.startswith('BENCH_PHASE '):
            r=dict(v.split('=',1) for v in line.split()[1:]);key=(r['phase'],int(r.get('k',item['k'])))
            if 'transport_actions' not in r and 'transport_rounds' in r:r['transport_actions']=r['transport_rounds']
            if min(int(r['exchange_rounds']),int(r['theoretical_rounds']))<0:raise RuntimeError('Unknown/negative round count: '+str(key))
            if key in phases:raise RuntimeError(f'duplicate phase {key}: {path}')
            phases[key]=r
    for (phase,_),r in phases.items():
        # A missing counter is unknown, never a fabricated zero.
        counter='dcf_evaluate_calls' if phase=='knn_online' else phase+'_dcf_evaluate_calls'
        r.setdefault('dcf_evaluate_calls',meta.get(counter,'-1'))
    return meta,phases

def expected(a,item):
    k=item['k'];scope=item['scope']
    if scope=='topk':return {('topk',k)}
    if scope=='vote':return {('vote',k)}
    if scope=='distance':return {('distance',k)}
    if scope=='pre-vote':return {(p,k) for p in ['distance','shuffle','rank_matrix','threshold','indicator_select','online_other','pre_vote_online']}
    if scope=='checkpoints':return {(p,k) for k in a.ks for p in ['topk','vote_frequency','vote_max','vote']}
    if scope=='shared':return {(p,k) for k in a.ks for p in ['block_cong','terminal','topk','vote']}
    if scope=='topk-vote' and a.scheme=='shao':return {(p,k) for p in ['topk','vote_frequency','vote_max','vote','online_other','topk_vote_online']}
    stages=['distance','topk','vote','online_other','knn_online']
    if a.scheme=='hybrid':stages += ['block_cong','terminal','topk_other']
    if a.scheme=='shao':stages += ['vote_frequency','vote_max']
    if a.scheme in ['secknn','kona']:stages += ['vote_frequency','vote_max','topk_vote_online']
    return {(p,k) for p in stages}

def check_closure(phases,total,leaves,k):
    for key in ['sent_bytes','transport_actions','exchange_rounds','theoretical_rounds']:
        if sum(int(phases[p,k][key]) for p in leaves)!=int(phases[total,k][key]):raise RuntimeError(f'{total}: {key} closure failed')
    if abs(sum(float(phases[p,k]['online_ms']) for p in leaves)-float(phases[total,k]['online_ms']))>1e-5:
        raise RuntimeError(f'{total}: time closure failed')

def summarize(out,rows):
    write_csv(out/'phase_trials.csv',rows);summary=[]
    if rows and rows[0].get('headline_e2e_only'):rows=[r for r in rows if r['phase']=='knn_online']
    for key in sorted({(r.get('dataset',''),r['part'],r['k'],r['network_profile'],r['phase']) for r in rows}):
        group=[r for r in rows if (r.get('dataset',''),r['part'],r['k'],r['network_profile'],r['phase'])==key]
        values=[r['online_ms'] for r in group];mean=statistics.mean(values)
        sd=statistics.stdev(values) if len(values)>1 else None
        r=dict(scheme=group[0]['scheme'],dataset=key[0],part=key[1],k=key[2],network_profile=key[3],phase=key[4],
            samples=len(group),mean_ms=mean,median_ms=statistics.median(values),sample_sd_ms=sd,
            cv=sd/mean if sd is not None and mean else None,min_ms=min(values),max_ms=max(values),
            mean_sent_bytes=statistics.mean(g['sent_bytes'] for g in group),timing_kind=group[0]['timing_kind'])
        for party in [0,1]:
            for metric in ['online_ms','sent_bytes','transport_actions','exchange_rounds','theoretical_rounds','dcf_evaluate_calls']:
                r[f'P{party}_{metric}_trials']=json.dumps([g[f'P{party}_{metric}'] for g in group])
        summary.append(r)
    write_csv(out/'summary.csv',summary)
    lines=['# KNN experiment results','',
        'Per-trial party max time/bytes, then group statistics. Both parties raw counts remain in CSV.',
        'Shared/checkpoint stage sums are composed; only knn_online in scope full is independently measured E2E.',
        'Only two round metrics: theoretical_rounds=dependency stages without chunk expansion; exchange_rounds=actual communication exchanges. transport_actions is an internal action counter, NOT rounds.','',
        '| Dataset | Part | k | Profile | Phase | Samples | Mean ms | SD ms | Mean bytes | Theoretical P0/P1 | Actual P0/P1 |',
        '|---|---|---:|---|---|---:|---:|---:|---:|---|---|']
    for r in summary:
        counters=[' / '.join(r[f'P{party}_{metric}_trials'] for party in [0,1]) for metric in ['theoretical_rounds','exchange_rounds']]
        lines.append('| '+' | '.join([str(r[k]) for k in ['dataset','part','k','network_profile','phase','samples','mean_ms','sample_sd_ms','mean_sent_bytes']]+counters)+' |')
    (out/'summary.md').write_text('\n'.join(lines)+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--scheme',required=True,choices=['constantpp','secknn','kona','hybrid','shao'])
    p.add_argument('--scope',choices=['experiment','full','distance','vote','topk-vote','topk'],default='experiment')
    p.add_argument('--ks',nargs='+',type=int,default=KS)
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--profiles',nargs='+',choices=['lan','wan','native'],default=None)
    p.add_argument('--dataset',default='arcene')
    p.add_argument('--datasets',nargs='+',help='Dataset sweep: supported full flows, distance-only for ConstantPP/SecKNN/KONA/Shao, KONA topk, Shao topk-vote')
    p.add_argument('--kona-backend',choices=['original','batched-dcf'],default='original')
    p.add_argument('--e2e-only',action='store_true',help='Headline only measured full KNN; retain raw verification metrics')
    p.add_argument('--monitor',action='store_true',help='Preflight bandwidth/RTT; sample processes/TCP/tc/memory on CPU4')
    p.add_argument('--container',default=os.environ.get('KONA_CONTAINER','kona-dev'))
    p.add_argument('--runtime-dir')
    p.add_argument('--timeout',type=int,default=3600)
    p.add_argument('--secmax-mode',choices=['paper-local-l2','legacy'],default='paper-local-l2')
    p.add_argument('--topk-mode',choices=['scalar'],default='scalar',help='Native SecKNN scan only; historical layered/tournament is disabled')
    p.add_argument('--lan-threshold',type=int,default=DEFAULTS['LAN_THRESHOLD'],help='Hybrid LAN cutoff; default from kona-bench-config.hpp')
    p.add_argument('--wan-threshold',type=int,default=DEFAULTS['WAN_THRESHOLD'],help='Hybrid WAN cutoff; default from kona-bench-config.hpp')
    p.add_argument('--comparator',choices=['hybrid','dcf','pcr'],default='hybrid',help='Hybrid selection backend; optimized vote stays DCF')
    p.add_argument('--matrix-backend',choices=['auto','dcf','pcr'],default='auto',help='Hybrid matrix ranking override only')
    p.add_argument('--output',type=Path)
    p.add_argument('--dry-run',action='store_true')
    a=p.parse_args();os.chdir(ROOT)
    if (a.comparator!='hybrid' or a.matrix_backend!='auto') and (a.scheme!='hybrid' or a.scope!='full'):
        p.error('Explicit selection backend overrides currently require --scheme hybrid --scope full')
    if not (0<a.lan_threshold<=2147483647 and 0<a.wan_threshold<=2147483647):p.error('hybrid thresholds must be positive 32-bit integers')
    if a.scope=='topk' and (a.scheme!='kona' or a.kona_backend!='batched-dcf'):p.error('--scope topk requires --scheme kona --kona-backend batched-dcf')
    if a.scope=='topk-vote' and a.scheme!='shao':p.error('--scope topk-vote currently supports shao only')
    if a.scope=='vote' and a.scheme not in ['hybrid','constantpp']:p.error('--scope vote supports hybrid/constantpp')
    if a.profiles is None:a.profiles=['wan'] if a.scheme=='shao' else ['lan','wan']
    if a.monitor and any(p not in ['lan','wan'] for p in a.profiles):p.error('--monitor requires lan/wan profiles')
    if a.datasets and not ((a.scheme in ['kona','secknn','hybrid'] and a.scope=='full') or (a.scope=='distance') or (a.scheme=='kona' and a.scope=='topk') or (a.scheme=='shao' and a.scope=='topk-vote')):p.error('--datasets requires supported full/distance, KONA topk or Shao topk-vote')
    if a.e2e_only and a.scope!='full':p.error('--e2e-only requires --scope full')
    def dataset_spec(name):
        name='mnist' if name=='minist' else name
        if '/' not in name:path=REMOTE+'/ConstantPP/data/'+name+('' if name.endswith('-data') else '-data')
        elif name.startswith(str(ROOT)):path=REMOTE+name[len(str(ROOT)):]
        elif not name.startswith('/'):path=REMOTE+'/'+name
        else:path=name
        local=ROOT/path.removeprefix(REMOTE+'/');n,features,domain=199,10000,2
        if (local/'Knn-meta').exists():
            meta=list(map(int,(local/'Knn-meta').read_text().split()))
            if len(meta)!=3 or meta[2]!=1:p.error('exactly one stored query is required')
            features,n,_=meta
            labels=list(map(int,(local/'P0-0-Y-Train').read_text().split()))
            if labels and min(labels)>=0:domain=max(labels)+1
        elif a.datasets:p.error('Missing local dataset metadata: '+str(local))
        return dict(dataset=path,N=n,features=features,label_domain=domain,dataset_name=Path(path).name)
    a.dataset_specs=[dataset_spec(name) for name in (a.datasets or [a.dataset])]
    if len({r['dataset'] for r in a.dataset_specs})!=len(a.dataset_specs):p.error('duplicate dataset')
    first=a.dataset_specs[0];a.dataset=first['dataset'];a.n=first['N'];a.features=first['features'];a.label_domain=first['label_domain']
    if min(a.repeats,a.timeout)<1 or any(k<1 or k>spec['N'] for spec in a.dataset_specs for k in a.ks):p.error('invalid repeats/timeout/k')
    if len(set(a.ks))!=len(a.ks) or len(set(a.profiles))!=len(a.profiles):p.error('duplicate k/profile')
    jobs=schedule(a)
    if a.scheme=='constantpp' and a.scope=='experiment' and a.n<2:p.error('pre-vote requires N>1')
    manifest=dict(scheme=a.scheme,scope=a.scope,dataset=a.dataset,N=a.n,features=a.features,query_count=1,
        datasets=a.dataset_specs,kona_backend=a.kona_backend if a.scheme=='kona' else 'NOT_APPLICABLE',headline_e2e_only=a.e2e_only,planned_pairs=len(jobs),schedule=jobs,cpu_affinity=[0,2],core_exclusivity='NOT_VERIFIED',warmup_trials=0,
        secmax_mode=a.secmax_mode,topk_mode=a.topk_mode,comparator=a.comparator,matrix_backend=a.matrix_backend,chunk_limits=dict(dcf=DEFAULTS['DCF_CHUNK'],mul=DEFAULTS['MUL_CHUNK']),rounds_schema='theoretical_without_chunk_vs_actual_exchange',hybrid_thresholds=dict(lan=a.lan_threshold,wan=a.wan_threshold),shao_network='rotated-msb-all-layers',shao_vote='batched-dcf',
        monitoring=dict(enabled=a.monitor,observer_cpu=4,sample_seconds=5,network_probe_mib=64),
        phase_semantics='full=measured E2E; checkpoints/shared/distance+topk-vote=staged/composed')
    if a.dry_run:print(json.dumps(manifest,indent=2));return
    lock=open('/tmp/kona-bench-experiment.lock','a+')
    try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError:raise RuntimeError('Another KNN experiment holds the lock; run one scheme at a time')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out=(a.output or ROOT/'KNN-experiment-res'/f'knn-{a.scheme}-{stamp}-{os.getpid()}').resolve()
    out.mkdir(parents=True,exist_ok=False);(out/'raw').mkdir()
    lock.seek(0);lock.truncate();lock.write(json.dumps(dict(pid=os.getpid(),scheme=a.scheme,output=str(out))));lock.flush()
    print(f'RESULT_DIRECTORY={out}',flush=True)
    baseline=source_hashes();manifest.update(source_sha256=baseline,command=sys.argv,started_utc=stamp)
    (out/'batch_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    rows=[];completed=0;failure=None;original=None;touched=False;started=time.monotonic()
    env=dict(os.environ,KONA_CONTAINER=a.container,KONA_NETWORK_PRIVILEGED='1')
    def stop(sig,frame):raise KeyboardInterrupt(f'signal {sig}')
    signal.signal(signal.SIGTERM,stop)
    def frozen():
        now=source_hashes();changed=[f for f,h in baseline.items() if now.get(f)!=h]
        if changed:raise RuntimeError('Sources changed during batch: '+', '.join(changed))
    try:
        with (out/'build.log').open('w') as log:
            subprocess.run(['bash','Scripts/codex-container-build.sh'],check=True,env=env,stdout=log,stderr=subprocess.STDOUT)
            subprocess.run(['docker','exec',a.container,'bash','-lc',f'cd {REMOTE} && make -j8 {TARGET}'],check=True,stdout=log,stderr=subprocess.STDOUT)
        probe=r'''
import pathlib,json,sys,subprocess,hashlib
root=pathlib.Path('/usr/src/Garnet');data=pathlib.Path(sys.argv[1]);dims=list(map(int,(data/'Knn-meta').read_text().split()))
if dims[2]!=1:raise RuntimeError('Expected one query')
candidates=[sys.argv[2]] if sys.argv[2] else [str(root),'/tmp/kona-accounting-regression','/tmp/kona-reference-regression']
keys=next((pathlib.Path(d)/folder for d in candidates for folder in ['Player-Data/2-fss','Player-Data'] if all((pathlib.Path(d)/folder/f).is_file() and (pathlib.Path(d)/folder/f).stat().st_size for f in ['k0','k1','r0','r1'])),None)
if keys is None:raise RuntimeError('Missing existing test material; no keys generated')
runtime=pathlib.Path(sys.argv[3]);target=runtime/'Player-Data/2-fss';target.mkdir(parents=True)
for f in ['k0','k1','r0','r1']:(target/f).symlink_to((keys/f).resolve())
qdisc=subprocess.check_output(['tc','qdisc','show','dev','lo'],text=True)
if any(line and not line.startswith('qdisc noqueue ') for line in qdisc.splitlines()):raise RuntimeError('Existing network shaping; refusing to replace it')
features=subprocess.check_output(['ethtool','-k','lo'],text=True)
flags={short:next(l.split(':',1)[1].strip().split()[0] for l in features.splitlines() if l.startswith(long+':')) for long,short in [('tcp-segmentation-offload','tso'),('generic-segmentation-offload','gso'),('generic-receive-offload','gro')]}
files={str(data/f):hashlib.sha256((data/f).read_bytes()).hexdigest() for f in ['Knn-meta','P0-0-X-Train','P0-0-Y-Train','P1-0-X-Test','P1-0-Y-Test']}
print(json.dumps(dict(runtime_dir=str(runtime),offload=flags,meta=dims,binary_sha256=hashlib.sha256((root/'knn-bench.x').read_bytes()).hexdigest(),dataset_files_sha256=files,key_source=str(keys))))
'''
        infos={}
        for spec in a.dataset_specs:
            info=json.loads(subprocess.check_output(['docker','exec',a.container,'python3','-c',probe,spec['dataset'],a.runtime_dir or '',
                '/tmp/knn-bench-'+uuid.uuid4().hex],text=True))
            if info['meta']!=[spec['features'],spec['N'],1]:raise RuntimeError('Host/container dataset dimensions disagree')
            infos[spec['dataset']]=info
        info=infos[a.dataset];original=info['offload'];manifest.update(info)
        manifest['dataset_environments']=infos
        (out/'batch_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
        probed_profiles=set()
        for sequence,item in enumerate(jobs,1):
            if 'dataset' in item:
                a.dataset=item['dataset'];a.n=item['N'];a.features=item['features'];a.label_domain=item['label_domain']
                info=infos[a.dataset]
            frozen();job=item['job'];print(f'START {sequence}/{len(jobs)} {job}',flush=True)
            with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(item,status='STARTED',sequence=sequence))+'\n')
            if item['network_profile']!='native':
                touched=True
                with (out/f'{job}-network.log').open('w') as log:
                    subprocess.run(['bash','Scripts/container-network.sh',item['network_profile'],'lo'],check=True,env=env,stdout=log,stderr=subprocess.STDOUT)
            if a.monitor and item['network_profile'] not in probed_profiles:
                network_probe(a,out,item['network_profile']);probed_profiles.add(item['network_profile'])
            ps=[];handles=[];paths=[];exits=None;job_start=time.monotonic()
            monitor=None
            try:
                if a.monitor:monitor=start_monitor(a,out,job,item['network_profile'])
                for party in [0,1]:
                    path=out/'raw'/f'{job}-P{party}.log';paths.append(path);handle=path.open('w');handles.append(handle)
                    cmd=['env',f'KONA_SOURCE_REVISION={revision}','KONA_SOURCE_DIRTY=True',
                        f'KONA_BENCH_SOURCE={REMOTE}/Machines/knn-bench.cpp',f'KONA_BENCH_BINARY={REMOTE}/{TARGET}',
                        f'KONA_DATASET_PATH={a.dataset}',f'KONA_RUN_ID={out.name}/{job}',
                        'bash',REMOTE+'/Scripts/kona-bench-log.sh','timeout','--kill-after=5',str(a.timeout),
                        REMOTE+'/'+TARGET,'-p',str(party),'-pn','33000',*command_arguments(a,item)]
                    ps.append(subprocess.Popen(['docker','exec',a.container,'bash','-lc',
                        f'cd {shlex.quote(info["runtime_dir"])} && {shlex.join(cmd)}'],stdout=handle,stderr=subprocess.STDOUT))
                exits=[proc.wait(timeout=a.timeout+30) for proc in ps]
            finally:
                # Docker wrappers can already have exited after Ctrl-C while
                # their container children continue offline setup. Clean by run
                # identity on EVERY interrupted/failed wait, not wrapper liveness.
                if exits is None or any(exits):
                    kill=r'''
import pathlib,os,signal,sys,time
marker=('KONA_RUN_ID='+sys.argv[1]).encode()
def matching():
 result=[]
 for p in pathlib.Path('/proc').glob('[0-9]*/environ'):
  try:
   if marker in p.read_bytes().split(b'\0'):result.append(int(p.parent.name))
  except OSError:pass
 return result
for pid in matching():
 try:os.kill(pid,signal.SIGTERM)
 except ProcessLookupError:pass
for _ in range(20):
 if not matching():break
 time.sleep(.1)
for pid in matching():
 try:os.kill(pid,signal.SIGKILL)
 except ProcessLookupError:pass
'''
                    subprocess.run(['docker','exec',a.container,'python3','-c',kill,f'{out.name}/{job}'],check=False)
                for proc in ps:
                    if proc.poll() is None:proc.terminate()
                for proc in ps:proc.wait()
                for h in handles:h.close()
                if monitor:stop_monitor(monitor)
            with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(item,status='COMPLETED' if not any(exits) else 'FAILED',
                sequence=sequence,exits=exits,wall_seconds=time.monotonic()-job_start))+'\n')
            if any(exits):raise RuntimeError(f'{job} failed {exits}; inspect raw logs, no auto retry')
            parsed=[parse(path,item) for path in paths]
            for party,(meta,phases) in enumerate(parsed):
                if set(phases)!=expected(a,item):raise RuntimeError(f'{job}: missing/extra phases: {set(phases)}')
                if meta.get('accounting_check')!='PASS' or meta.get('correctness')!='PASS':raise RuntimeError(f'{job}: accounting/correctness failed')
                if meta.get('benchmark_scheme')!=item['bench_scheme'] or meta.get('benchmark_scope')!=item['scope']:raise RuntimeError('Wrong scheme/scope')
                if item['scope']=='checkpoints':
                    actual_order=[int(v) for v in re.findall(r'^VOTE_EXECUTION k=(\d+) ',paths[party].read_text(),re.M)]
                    if actual_order!=list(map(int,item['ks'].split(','))):raise RuntimeError('Vote execution order differs from manifest')
                if item['bench_scheme']=='secknn' and (meta.get('topk_mode')!='scalar' or meta.get('algorithm_identity')!='secknn-native-serial-scan'):
                    raise RuntimeError('SecKNN must execute its original scalar scan; tournament substitution is forbidden')
                if item['bench_scheme']=='kona' and meta.get('kona_backend')!=a.kona_backend:raise RuntimeError('Wrong KONA original/batched-dcf backend')
                if item['bench_scheme']=='constantpp' and item['scope'] in ['full','vote'] and meta.get('secmax_mode')!=a.secmax_mode:
                    raise RuntimeError('ConstantPP SecMax mode mismatch: requested '+a.secmax_mode+', got '+str(meta.get('secmax_mode')))
                if a.scheme=='hybrid' and item['scope']=='full':
                    if meta.get('comparator_override')!=a.comparator or meta.get('matrix_backend_override')!=a.matrix_backend:
                        raise RuntimeError('Hybrid backend override was not applied')
                    if meta.get('vote_backend')!='DCF_OPTIMIZED' or meta.get('full_knn_measured')!='1':
                        raise RuntimeError('Hybrid must use optimized vote and measured full online flow')
                if (meta.get('party'),meta.get('train_count'),meta.get('features'))!=(str(party),str(a.n),str(a.features)):raise RuntimeError('Wrong party/scale')
                evidence=f'BENCH_AFFINITY party={party} cpu={0 if party==0 else 2} source=cpp_bench affinity_check=PASS'
                if evidence not in paths[party].read_text():raise RuntimeError('Missing C++ fixed-affinity evidence')
                if meta['binary_sha256'].split()[0]!=info['binary_sha256']:raise RuntimeError('Binary changed')
                actual={path:digest for digest,path in re.findall(r'^([0-9a-f]{64})  (.+)$',paths[party].read_text(),re.M)}
                if actual!=info['dataset_files_sha256']:raise RuntimeError('Dataset changed')
                if item['scope']=='full':check_closure(phases,'knn_online',['distance','topk','vote','online_other'],item['k'])
                if a.scheme=='hybrid' and item['scope']=='full':check_closure(phases,'topk',['block_cong','terminal','topk_other'],item['k'])
                if a.scheme=='shao' and item['scope']!='distance':
                    check_closure(phases,'vote',['vote_frequency','vote_max'],item['k'])
                    if item['scope']=='topk-vote':check_closure(phases,'topk_vote_online',['topk','vote','online_other'],item['k'])
                if item['scope']=='pre-vote':check_closure(phases,'pre_vote_online',['distance','shuffle','rank_matrix','threshold','indicator_select','online_other'],item['k'])
                if item['scope']=='checkpoints':
                    for k in a.ks:check_closure(phases,'vote',['vote_frequency','vote_max'],k)
                if item['scope']=='shared':
                    for k in a.ks:check_closure(phases,'topk',['block_cong','terminal'],k)
                    nodes=[l for l in paths[party].read_text().splitlines() if l.startswith('SHARED_NODE ')]
                    if len(nodes)!=int(meta['executed_unique_layer_count']) or any('execution_count=1 ' not in l for l in nodes):raise RuntimeError('Invalid shared execution trace')
            for phase,k in sorted(expected(a,item)):
                row=dict(scheme=a.scheme,dataset=a.dataset,kona_backend=a.kona_backend if a.scheme=='kona' else 'NOT_APPLICABLE',headline_e2e_only=a.e2e_only,part=item['part'],scope=item['scope'],k=k,trial=item['trial'],network_profile=item['network_profile'],
                    N=a.n,features=a.features,query_count=1,phase=phase,job=job,status='COMPLETED',correctness='PASS',
                    timing_kind='composed' if item['scope']=='shared' and phase in ['topk','block_cong'] else 'measured')
                for party,(_,phases) in enumerate(parsed):
                    r=phases[phase,k]
                    for metric in ['online_ms','sent_bytes','transport_actions','exchange_rounds','theoretical_rounds','dcf_evaluate_calls']:
                        row[f'P{party}_{metric}']=float(r[metric]) if metric=='online_ms' else int(r.get(metric,0))
                    if row[f'P{party}_transport_actions']!=2*row[f'P{party}_exchange_rounds']:raise RuntimeError('Round/action mismatch')
                    if not math.isfinite(row[f'P{party}_online_ms']) or row[f'P{party}_online_ms']<0:raise RuntimeError('Invalid time')
                row['online_ms']=max(row['P0_online_ms'],row['P1_online_ms']);row['sent_bytes']=max(row['P0_sent_bytes'],row['P1_sent_bytes'])
                rows.append(row)
            completed+=1;summarize(out,rows)
        frozen()
    except BaseException as e:
        failure=str(e) or type(e).__name__;raise
    finally:
        cleanup=[]
        if touched:
            with (out/'network-cleanup.log').open('w') as log:
                cleanup.append(subprocess.run(['bash','Scripts/container-network.sh','--clear','--dev','lo'],env=env,stdout=log,stderr=subprocess.STDOUT).returncode)
                flags=[v for key,value in original.items() for v in (key,value)]
                cleanup.append(subprocess.run(['docker','exec','--privileged',a.container,'ethtool','-K','lo',*flags],stdout=log,stderr=subprocess.STDOUT).returncode)
                cleanup.append(subprocess.run(['docker','exec',a.container,'bash','-lc','tc qdisc show dev lo; ethtool -k lo'],stdout=log,stderr=subprocess.STDOUT).returncode)
        summarize(out,rows)
        (out/'batch_status.json').write_text(json.dumps(dict(status='FAILED' if failure or any(cleanup) else 'COMPLETED',
            error=failure,planned_pairs=len(jobs),completed_pairs=completed,cleanup_exit_codes=cleanup,
            wall_seconds=time.monotonic()-started),indent=2)+'\n')
        fcntl.flock(lock,fcntl.LOCK_UN);lock.close()
        if not failure and any(cleanup):raise RuntimeError('Network cleanup failed')
    print(f'COMPLETED: {out}',flush=True)

if __name__=='__main__':main()
