#!/usr/bin/env python3
# Figure 5 / Table 9: this file + Machines/topk-network-ablation-bench.cpp only.
# Run: python3 -u Scripts/run-topk-network-ablation.py
# Defaults: stored synthetic 16384-32, all N=16384 training rows, one query,
# d=32, 4 classes, k=3/5/10/20/50, WAN40Mbps/direction +20ms one-way, 3 trials.
# C++ binds P0/CPU0 and P1/CPU2. Affinity does not prove core exclusivity.
# All structures use the same prepared additive distance/label test shares,
# cached batch DCF, and existing arithmetic L2 multiplication. No distance is
# executed in the timed scope; no offline sharing is added to the WAN timing.
# DQ: original repeated Top-1/leftover graph; execute ONLY one max-k=50 chain
# per trial. Each checkpoint sums the actually measured prefix steps; copying
# snapshots, subsequent voting and diagnostics do not enter later prefixes.
# Merge: original full truncated Cong, optimized public-all-selected vote.
# FullMat/PCM: same existing block plan, execute blocks ONCE per k/trial;
# clone ready candidates outside timing, then run each terminal + its mask
# vote. FullMat includes same-block candidate comparisons, as user confirmed.
# DQ keeps its own pairwise frequency/max vote and native tie behavior; Merge
# and matrices use vote_dcf_optimized, preserving their all-selected/mask path.
# Thus the caption must not claim identical vote implementations/tie policies.
# C++ rotates k start, group order, and FullMat/PCM order across trials. The
# DQ prerequisite chain still advances in increasing k; cannot rotate that.
# Preserve selection, terminal, vote, online_other, and selection+vote metrics.
# DQ/matrix headline times are COMPOSED shared stages; Merge's total is a
# continuous outer measurement. Never label composed results continuous E2E.
# Time/bytes: per trial max(P0,P1), then mean/median/sample SD/CV/min/max.
# Compose on each party FIRST, never add separate party-max stage times.
# Retain P0/P1 actual exchanges, directional actions, theoretical logical
# rounds, DCF evals, comparator/swap counts and the shared execution trace.
# Table9 main round columns use THEORY; actual exchanges get a separate table.
# Benchmark copies/graphs/fixture setup/barriers/reveals/logging/launcher/probes
# are outside reported online intervals. Existing L2 security limits persist.
# Build/sync once using kona-dev, no new images/deps/Makefile edits/keygen.
# Throughput/RTT probe runs BEFORE protocols, never concurrent with timings.
# Read-only network/process sampling every 15s runs on observer CPU4. No auto
# retries or outlier removal. Stop on mismatch, retain partial logs; clear tc
# and restore original offload flags on handled exit. SIGKILL/power loss may
# require manual cleanup. --smoke is ONLY unshaped N64 x1 regression, not data.
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
import statistics as st
import subprocess as sp
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]
REMOTE='/usr/src/Garnet'
TARGET='topk-network-ablation-bench.x'
NAMES=['DQ','Merge','FullMat','PCM']
KS=[3,5,10,20,50]
METRICS=['online_ms','sent_bytes','transport_actions','exchange_rounds','theoretical_rounds','dcf_evaluate_calls','comparators','swaps']

def docker(*args,**kw):
    return sp.check_output(['docker','exec','kona-dev',*args],text=True,**kw)

def sources():
    files=['Machines/topk-network-ablation-bench.cpp','Scripts/run-topk-network-ablation.py',
        'Scripts/codex-container-build.sh','Scripts/container-network.sh','Scripts/kona-bench-log.sh',
        'Scripts/measure-container-profile-throughput.py','Makefile',
        'ConstantPP/data-loader.hpp','ConstantPP/data-loader.cpp']
    files += [str(p.relative_to(ROOT)) for p in (ROOT/'Machines').glob('kona-*.hpp')]
    files += [str(p.relative_to(ROOT)) for pattern in ['*.cpp','*.h'] for p in (ROOT/'Networking').glob(pattern)]
    files += ['Tools/octetStream.h','Tools/octetStream.cpp']
    return {f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sorted(set(files))}

def csv_write(path,rows):
    if not rows:return
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def parse(text):
    phases={};cases=[];shared=[]
    for line in text.splitlines():
        if line.startswith(('ABLATION_PHASE ','ABLATION_CASE ','SHARED_EXECUTION ')):
            r=dict(re.findall(r'(\w+)=([^\s]+)',line))
            if line.startswith('ABLATION_PHASE '):
                key=(int(r['trial']),int(r['k']),r['structure'],r['phase'])
                if key in phases:raise RuntimeError('duplicate phase '+str(key))
                phases[key]=r
            elif line.startswith('ABLATION_CASE '):cases.append(r)
            else:shared.append(r)
    return phases,cases,shared

def summarize(out,texts,repeats,n):
    parsed=[parse(t) for t in texts];rows=[];case_rows=[]
    expected={(trial,k,name) for trial in range(1,repeats+1) for k in KS for name in NAMES}
    for party,(phases,cases,shared) in enumerate(parsed):
        if {(int(r['trial']),int(r['k']),r['structure']) for r in cases}!=expected or len(cases)!=len(expected):
            raise RuntimeError('missing/duplicate completed cases')
        assert all(r['accounting_check']==r['correctness']=='PASS' for r in cases)
        for trial in range(1,repeats+1):
            dq=[r for r in shared if r['group']=='DQ' and int(r['trial'])==trial]
            matrices=[r for r in shared if r['group']=='Matrix' and int(r['trial'])==trial]
            assert len(dq)==1 and dq[0]['max_k']=='50' and dq[0]['execution_count']=='1'
            assert len(matrices)==5 and {int(r['k']) for r in matrices}==set(KS)
            assert all(r['execution_count']=='1' for r in matrices)
        case_rows += [dict(party=party,**{k:v for k,v in r.items() if k!='party'}) for r in cases]
        for trial,k,name in expected:
            get=lambda ph:phases[trial,k,name,ph]
            a,b,total,other=[get(ph) for ph in ['selection','vote','selection_vote_online','online_other']]
            for metric in METRICS:
                if metric in ['comparators','swaps']:continue
                values=[float(r[metric]) if metric=='online_ms' else int(r[metric]) for r in [a,b,other,total]]
                if abs(sum(values[:3])-values[3])>(1e-5 if metric=='online_ms' else 0):
                    raise RuntimeError('phase closure failed '+str((trial,k,name,metric)))
            if name=='DQ':assert int(a['sent_bytes'])==72*(k*n-k*(k+1)//2)
            if name in ['FullMat','PCM']:
                block=phases[trial,k,'MatrixShared','block_cong'];tail=get('terminal')
                for metric in METRICS:
                    if metric=='online_ms':assert abs(float(a[metric])-float(block[metric])-float(tail[metric]))<1e-5
                    else:assert int(a[metric])==int(block[metric])+int(tail[metric])
            assert total['timing_kind']==('measured' if name=='Merge' else 'composed_shared_stages')
        assert all(int(r['transport_actions'])==2*int(r['exchange_rounds']) for r in phases.values())
        for r in phases.values():assert math.isfinite(float(r['online_ms'])) and float(r['online_ms'])>=0
    assert [(r['trial'],r['k'],r['structure']) for r in parsed[0][1]]==[(r['trial'],r['k'],r['structure']) for r in parsed[1][1]]
    assert set(parsed[0][0])==set(parsed[1][0])
    for key in sorted(parsed[0][0]):
        trial,k,name,phase=key
        a,b=[v[0][key] for v in parsed]
        r=dict(trial=trial,k=k,structure=name,phase=phase,timing_kind=a['timing_kind'])
        for party,v in enumerate([a,b]):
            for metric in METRICS:r[f'P{party}_{metric}']=float(v[metric]) if metric=='online_ms' else int(v[metric])
        r['online_ms']=max(r['P0_online_ms'],r['P1_online_ms']);r['sent_bytes']=max(r['P0_sent_bytes'],r['P1_sent_bytes'])
        rows.append(r)
    csv_write(out/'phase_trials.csv',rows);csv_write(out/'execution_order.csv',case_rows)
    summary=[]
    for key in sorted({(r['k'],r['structure'],r['phase']) for r in rows}):
        k,name,phase=key;group=[r for r in rows if (r['k'],r['structure'],r['phase'])==key];values=[r['online_ms'] for r in group]
        assert len(group)==repeats
        mean=st.mean(values);sd=st.stdev(values) if len(values)>1 else None
        r=dict(k=k,structure=name,phase=phase,samples=len(values),mean_ms=mean,median_ms=st.median(values),sample_sd_ms=sd,
            cv=sd/mean if sd is not None and mean else None,min_ms=min(values),max_ms=max(values),timing_kind=group[0]['timing_kind'])
        for party in [0,1]:
            for metric in METRICS:
                r[f'P{party}_{metric}_trials']=json.dumps([v[f'P{party}_{metric}'] for v in group])
        summary.append(r)
    csv_write(out/'phase_summary.csv',summary)
    table=[];lines=['| k | DQ时间s | Merge时间s | FullMat时间s | PCM时间s | DQ逻辑轮次 | Merge逻辑轮次 | FullMat逻辑轮次 | PCM逻辑轮次 |',
        '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for k in KS:
        r=dict(k=k);times=[];rounds=[]
        for name in NAMES:
            group=[x for x in rows if x['k']==k and x['structure']==name and x['phase']=='selection_vote_online']
            values=[x['online_ms']/1000 for x in group];r[name+'_mean_s']=st.mean(values);r[name+'_sample_sd_s']=st.stdev(values) if len(values)>1 else None
            times.append(f"{r[name+'_mean_s']:.6f}")
            for metric in ['sent_bytes','exchange_rounds','theoretical_rounds']:
                for party in [0,1]:
                    numbers={x[f'P{party}_{metric}'] for x in group};assert len(numbers)==1
                    r[f'{name}_P{party}_{metric}']=numbers.pop()
            assert r[name+'_P0_theoretical_rounds']==r[name+'_P1_theoretical_rounds']
            rounds.append(str(r[name+'_P0_theoretical_rounds']))
        table.append(r);lines.append('| '+' | '.join([str(k)]+times+rounds)+' |')
    csv_write(out/'table9.csv',table)
    lines += ['', '时间每次先在同方合成，再取双方较大值；三次均值。DQ及矩阵方案为复用阶段合成，Merge为连续总段实测。未计距离。',
        '主表轮次为理论逻辑轮次；table9.csv及phase_trials.csv另保留P0/P1实际交换次数、方向动作、字节与阶段时间。',
        'DQ保留自身投票和平局规则；Merge为全入选优化投票，FullMat/PCM共用mask投票。不是相同投票实现的纯选择网络计时。']
    (out/'table9.md').write_text('\n'.join(lines)+'\n')

def main():
    parser=argparse.ArgumentParser(description='Independent WAN Top-k network ablation; instructions in script comments')
    parser.add_argument('--repeats',type=int,default=3);parser.add_argument('--timeout',type=int,default=1800)
    parser.add_argument('--smoke',action='store_true');parser.add_argument('--dry-run',action='store_true')
    parser.add_argument('--chunking',choices=['on','off'],default=('on' if int(re.search(r'MATRIX_CHUNKING = (\d+);',(ROOT/'Machines/kona-bench-config.hpp').read_text()).group(1)) else 'off'),help='Communication chunking; default off for matrix experiments')
    a=parser.parse_args();os.chdir(ROOT)
    if a.repeats<1 or a.timeout<1:parser.error('positive repeats/timeout required')
    if a.smoke:a.repeats=1
    if a.dry_run:
        print(json.dumps(dict(chunking=a.chunking,N=64 if a.smoke else 16384,d=32,classes=4,ks=KS,repeats=a.repeats,
            wan_rate_per_direction_mbps=None if a.smoke else 40,one_way_delay_ms=None if a.smoke else 20,
            cases=20*a.repeats,DQ_max_k_chains=a.repeats,matrix_block_executions=5*a.repeats,cpu=[0,2],
            headline='DQ/matrices composed shared stages; Merge continuous'),indent=2));return
    lock=open('/tmp/kona-bench-experiment.lock','a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out=ROOT/'KNN-experiment-res'/('topk-network-ablation-'+('smoke-' if a.smoke else '')+stamp+'-'+str(os.getpid()))
    out.mkdir();print('RESULT_DIRECTORY='+str(out),flush=True)
    baseline=sources();env=dict(os.environ,KONA_NETWORK_PRIVILEGED='1');marker=out.name
    manifest=dict(chunking=a.chunking,command=os.sys.argv,source_sha256=baseline,ks=KS,repeats=a.repeats,smoke=a.smoke,N=64 if a.smoke else 16384,
        cpu_affinity=[0,2],core_exclusivity='NOT_VERIFIED',network='unshaped-smoke' if a.smoke else 'WAN40Mbps/direction20ms',
        reuse=dict(DQ='one max50 chain/repetition',matrices='one block execution/k/repetition'),expected_cases=20*a.repeats)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    procs=[];handles=[];original=None;touched=False;failure=None;codes=None;started=time.monotonic()
    def stop(sig,frame):raise KeyboardInterrupt('signal '+str(sig))
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    def frozen():
        if sources()!=baseline:raise RuntimeError('Measured source changed; refusing mixed versions')
    def snapshot(name):
        text=docker('taskset','-c','4','bash','-lc','tc qdisc show dev lo; tc class show dev lo; tc -s class show dev lo; ethtool -k lo; ss -tin; ps -eo pid,psr,comm,rss,stat,args; free -k')
        (out/(name+'.log')).write_text(text)
        if not a.smoke:
            assert re.search(r'class htb 1:10 .*?rate 40Mbit',text) and re.search(r'class htb 1:20 .*?rate 40Mbit',text)
            assert len(re.findall(r'qdisc netem (?:10:|20:).*?delay 20ms',text))==2
            for name in ['tcp-segmentation-offload','generic-segmentation-offload','generic-receive-offload']:
                assert re.search(r'^'+name+r': off',text,re.M)
        return text
    try:
        with (out/'build.log').open('w') as log:
            sp.run(['bash','Scripts/codex-container-build.sh'],check=True,stdout=log,stderr=sp.STDOUT)
            sp.run(['docker','exec','kona-dev','bash','-lc',f'cd {REMOTE} && make -j8 {TARGET}'],check=True,stdout=log,stderr=sp.STDOUT)
        setup=r'''
import pathlib,json,subprocess,hashlib,sys
root=pathlib.Path('/usr/src/Garnet');runtime=pathlib.Path(sys.argv[1]);runtime.mkdir()
q=subprocess.check_output(['tc','qdisc','show','dev','lo'],text=True)
if any(l and not l.startswith('qdisc noqueue ') for l in q.splitlines()):raise RuntimeError('Network already configured; do not overwrite')
keys=next((pathlib.Path(base)/p for base in [str(root),'/tmp/kona-accounting-regression','/tmp/kona-reference-regression'] for p in ['Player-Data/2-fss','Player-Data'] if all((pathlib.Path(base)/p/f).is_file() and (pathlib.Path(base)/p/f).stat().st_size for f in ['k0','k1','r0','r1'])),None)
if keys is None:raise RuntimeError('Missing existing DCF material; no generation')
target=runtime/'Player-Data/2-fss';target.mkdir(parents=True)
for f in ['k0','k1','r0','r1']:(target/f).symlink_to((keys/f).resolve())
flags=subprocess.check_output(['ethtool','-k','lo'],text=True)
offload={s:next(l.split(':',1)[1].strip().split()[0] for l in flags.splitlines() if l.startswith(k+':')) for k,s in [('tcp-segmentation-offload','tso'),('generic-segmentation-offload','gso'),('generic-receive-offload','gro')]}
data=root/'ConstantPP/data/16384-32';assert list(map(int,(data/'Knn-meta').read_text().split()))==[32,16384,1]
files={str(data/f):hashlib.sha256((data/f).read_bytes()).hexdigest() for f in ['Knn-meta','P0-0-X-Train','P0-0-Y-Train','P1-0-X-Test','P1-0-Y-Test']}
print(json.dumps(dict(runtime=str(runtime),key_source=str(keys),key_sha256={f:hashlib.sha256((keys/f).read_bytes()).hexdigest() for f in ['k0','k1','r0','r1']},offload=offload,files=files,binary_sha256=hashlib.sha256((root/'topk-network-ablation-bench.x').read_bytes()).hexdigest())))
'''
        info=json.loads(docker('python3','-c',setup,'/tmp/topk-ablation-'+uuid.uuid4().hex));original=info['offload'];manifest.update(info)
        for remote,digest in info['files'].items():assert hashlib.sha256((ROOT/remote.removeprefix(REMOTE+'/')).read_bytes()).hexdigest()==digest
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        if not a.smoke:
            touched=True
            with (out/'network-apply.log').open('w') as log:sp.run(['bash','Scripts/container-network.sh','wan','lo'],env=env,check=True,stdout=log,stderr=sp.STDOUT)
            with (out/'network-probe.log').open('w') as log:
                probe_path=info['runtime']+'/network-probe.json'
                sp.run(['docker','exec','kona-dev','taskset','-c','4','python3',REMOTE+'/Scripts/measure-container-profile-throughput.py','--profile','wan','--base','42000','--mib','64','--samples','10','--json-output',probe_path],check=True,stdout=log,stderr=sp.STDOUT,timeout=180)
            probe=json.loads(docker('cat',probe_path));(out/'network-probe.json').write_text(json.dumps(probe,indent=2)+'\n')
            assert all(26<v['receiver_mbps']<52 for part in probe['transfers'].values() for v in part['lanes'])
            assert all(32<probe[k]['median_ms']<60 for k in ['rtt_odd','rtt_even'])
        snapshot('network-process-before');frozen()
        for party in [0,1]:
            path=out/f'P{party}.log';handle=path.open('w');handles.append(handle)
            args=['env',f'KONA_RUN_ID={marker}',f'KONA_BENCH_SOURCE={REMOTE}/Machines/topk-network-ablation-bench.cpp',
                f'KONA_BENCH_BINARY={REMOTE}/{TARGET}',f'KONA_DATASET_PATH={REMOTE}/ConstantPP/data/16384-32',
                'bash',REMOTE+'/Scripts/kona-bench-log.sh','timeout','--kill-after=5',str(a.timeout),REMOTE+'/'+TARGET,
                '-p',str(party),'-pn','35000','--dataset',REMOTE+'/ConstantPP/data/16384-32',
                '--ks',','.join(map(str,KS)),'--repeats',str(a.repeats),'--network-profile','wan','--reuse-common','--chunking',a.chunking]
            if a.smoke:args+=['--limit','64']
            procs.append(sp.Popen(['docker','exec','kona-dev','bash','-lc','cd '+shlex.quote(info['runtime'])+' && '+shlex.join(args)],stdout=handle,stderr=sp.STDOUT))
        deadline=time.monotonic()+a.timeout+30;sample=0
        while any(p.poll() is None for p in procs):
            if time.monotonic()>deadline:raise TimeoutError('Benchmark deadline exceeded')
            if any(p.poll() not in [None,0] for p in procs):raise RuntimeError('Party failed; stop without retry')
            if sample%15==0:
                frozen();snapshot(f'monitor-{sample:05d}')
                done=(out/'P0.log').read_text().count('ABLATION_CASE ')
                print(f'PROGRESS completed_cases={done}/{20*a.repeats}',flush=True)
            sample+=1;time.sleep(1)
        codes=[p.wait() for p in procs]
        for h in handles:h.close()
        if any(codes):raise RuntimeError('Party exits '+str(codes))
        snapshot('network-process-after');frozen();texts=[(out/f'P{p}.log').read_text() for p in [0,1]]
        key_hashes=json.loads(docker('python3','-c',
            'import pathlib,hashlib,json,sys; p=pathlib.Path(sys.argv[1]); print(json.dumps({f:hashlib.sha256((p/f).read_bytes()).hexdigest() for f in ["k0","k1","r0","r1"]}))',info['key_source']))
        assert key_hashes==info['key_sha256'],'DCF material changed during run'
        for party,text in enumerate(texts):
            assert f'BENCH_AFFINITY party={party} cpu={0 if party==0 else 2} source=cpp_bench affinity_check=PASS' in text
            assert 'reuse_common=1' in text and 'test_status=COMPLETED' in text and 'backend=DCF_ONLY' in text
            for field,value in [('train_count',64 if a.smoke else 16384),('features',32),('public_label_domain',4)]:
                assert re.search(r'^'+field+'='+str(value)+r'$',text,re.M),'Wrong '+field
            assert info['binary_sha256'] in text
            for digest in info['files'].values():assert digest in text
        assert re.search(r'^fixture_checksum=(\d+)$',texts[0],re.M)[1]==re.search(r'^fixture_checksum=(\d+)$',texts[1],re.M)[1]
        summarize(out,texts,a.repeats,64 if a.smoke else 16384)
    except BaseException as e:
        failure=str(e) or type(e).__name__;raise
    finally:
        # Docker wrapper termination alone does not kill container children.
        if failure:
            kill=r'''
import pathlib,os,signal,sys
needle=('KONA_RUN_ID='+sys.argv[1]).encode()
for f in pathlib.Path('/proc').glob('[0-9]*/environ'):
 try:
  if needle in f.read_bytes().split(b'\0'):os.kill(int(f.parent.name),signal.SIGKILL)
 except (OSError,ProcessLookupError):pass
'''
            sp.run(['docker','exec','kona-dev','python3','-c',kill,marker],check=False)
        for p in procs:
            if p.poll() is None:p.terminate()
            p.wait()
        for h in handles:
            if not h.closed:h.close()
        cleanup=[]
        if touched:
            with (out/'network-cleanup.log').open('w') as log:
                cleanup.append(sp.run(['bash','Scripts/container-network.sh','--clear','--dev','lo'],env=env,stdout=log,stderr=sp.STDOUT).returncode)
                flags=[v for k,val in original.items() for v in [k,val]]
                cleanup.append(sp.run(['docker','exec','--privileged','kona-dev','ethtool','-K','lo',*flags],stdout=log,stderr=sp.STDOUT).returncode)
        (out/'status.json').write_text(json.dumps(dict(status='FAILED' if failure or any(cleanup) else 'COMPLETED',error=failure,
            party_exit_codes=codes,completed_cases=(out/'P0.log').read_text().count('ABLATION_CASE ') if (out/'P0.log').exists() else 0,
            cleanup_exit_codes=cleanup,wall_seconds=time.monotonic()-started,smoke=a.smoke),indent=2)+'\n')
        fcntl.flock(lock,fcntl.LOCK_UN);lock.close()
        if not failure and any(cleanup):raise RuntimeError('Network cleanup failed')
    print('COMPLETED: '+str(out),flush=True)

if __name__=='__main__':main()
