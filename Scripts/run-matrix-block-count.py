#!/usr/bin/env python3
# Independent B sweep: this script + Machines/matrix-block-count-bench.cpp.
# Launch: python3 -u Scripts/run-matrix-block-count.py
# N32768,d32,k5,4 classes, one query, B2/4/8/16/32, LAN/WAN each3.
# Synthetic features/query (0..1023, splitmix64 seed20261105) live ONLY inside
# C++. Plaintext local distance/sort builds each equal block's sorted Top-k
# OUTSIDE timing. Both methods consume identical candidate values AND shares.
# No dataset files, no secure distance, no Cong, no automatic partition formula.
# FullMat uses the existing ALL candidate-pairs matrix, including same-block
# pairs; both rank/mask stages forced batch DCF. PCM uses the existing pruned
# matrix, ALSO fixed batch DCF for ranking and rank-mask (2026-11-06).
# Both use their existing vote_dcf_optimized secret-mask path (batch DCF).
# Block-local Top-k may retain hybrid selection, but is not executed here.
# Missing-point run: --blocks 32 --profiles wan --methods PCM --repeats 3.
# Outer repetition; rotate first B and FullMat/PCM order; LAN then WAN per B.
# C++ fixes P0/CPU0,P1/CPU2. Physical/system exclusivity is not claimed.
# Report rank_matrix/rank_mask/matrix_other/matrix/vote/online_other and the
# independent continuous matrix_vote_online total, ending at label shares.
# Online excludes synthetic fixture, plans, clone, barriers, validation/logs.
# Per-trial max party time/bytes then mean/median/sampleSD/CV/min/max. Both
# parties' actual exchanges, directional actions, logical rounds, DCF calls,
# DCF/PCR comparator counts are retained. Counter closure is checked by party.
# Existing L2 test material and cached DCF keys; no real offline claim/keygen.
# Existing kona-dev only; sync/build existing generic target, no Makefile edits.
# Probe BOTH rate lanes/RTT once per profile BEFORE protocols; never overlap
# probe traffic with timing. Poll tc/TCP/process/RSS every15s on observerCPU4.
# Freeze sources/binary/keys, preserve execution order and failures, no retry.
# Clear shaping and restore offloads on handled exit; SIGKILL/power loss may
# require manual cleanup. --smoke checks B2/B32, both policies, x1 UNSHAPED;
# smoke times are not formal LAN/WAN data. No extra README/launcher files.
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

ROOT=Path(__file__).resolve().parents[1];REMOTE='/usr/src/Garnet'
TARGET='matrix-block-count-bench.x';METHODS=['FullMat','PCM'];BLOCKS=[2,4,8,16,32]
PHASES=['rank_matrix','rank_mask','matrix_other','matrix','vote','online_other','matrix_vote_online']
METRICS=['online_ms','sent_bytes','transport_actions','exchange_rounds','theoretical_rounds','dcf_evaluate_calls','comparators','dcf_comparators','pcr_comparators']
def docker(*args):return sp.check_output(['docker','exec','kona-dev',*args],text=True)
def sources():
    files=['Machines/matrix-block-count-bench.cpp','Scripts/run-matrix-block-count.py','Makefile',
        'Scripts/codex-container-build.sh','Scripts/container-network.sh','Scripts/kona-bench-log.sh','Scripts/measure-container-profile-throughput.py',
        'Tools/octetStream.h','Tools/octetStream.cpp']
    files += [str(p.relative_to(ROOT)) for p in (ROOT/'Machines').glob('kona-*.hpp')]
    files += [str(p.relative_to(ROOT)) for pattern in ['*.h','*.cpp'] for p in (ROOT/'Networking').glob(pattern)]
    return {f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sorted(set(files))}
def csv_write(path,rows):
    if rows:
        with path.open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
def parse(text):
    meta=dict(re.findall(r'^(\w+)=([^\n]+)$',text,re.M));phases={};cases=[]
    for line in text.splitlines():
        if line.startswith(('MATRIX_PHASE ','MATRIX_CASE ')):
            r=dict(re.findall(r'(\w+)=([^\s]+)',line))
            if line.startswith('MATRIX_CASE '):cases.append(r)
            else:
                key=(r['method'],r['phase'])
                if key in phases:raise RuntimeError('duplicate phase')
                phases[key]=r
    return meta,phases,cases
def collect(texts,b,trial,profile,info):
    parsed=[parse(t) for t in texts];rows=[];cases=[]
    for party,(meta,phases,case) in enumerate(parsed):
        assert set(phases)=={(m,p) for m in METHODS for p in PHASES}
        assert len(case)==len(METHODS) and {r['method'] for r in case}==set(METHODS)
        assert all(r['correctness']==r['accounting_check']=='PASS' for r in case)
        assert f'BENCH_AFFINITY party={party} cpu={0 if party==0 else 2} source=cpp_bench affinity_check=PASS' in texts[party]
        for key,value in [('party',party),('train_count',32768),('features',32),('k',5),('public_label_domain',4),('block_count',b),('candidate_count',b*5),('distance_execution_count',0),('cong_execution_count',0)]:assert meta[key]==str(value),(key,meta.get(key))
        assert meta['vote_backend']=='DCF_OPTIMIZED' and meta['fullmat_backend']=='DCF' and meta['pcm_backend']=='DCF'
        assert meta['test_status']=='COMPLETED' and info['binary_sha256'] in texts[party]
        assert meta['network_profile']==profile
        for r in case:
            assert (int(r['B']),int(r['trial']),r['profile'],int(r['party']))==(b,trial,profile,party)
            rank_pairs=b*5*(b*5-1)//2 if r['method']=='FullMat' else b*(b-1)//2*15
            rank_backend=mask_backend='DCF'
            assert r['rank_backend']==rank_backend and r['mask_backend']==mask_backend
            assert int(r['rank_pairs'])==rank_pairs and int(r['mask_pairs'])==b*5
            for phase,pairs,backend in [('rank_matrix',rank_pairs,rank_backend),('rank_mask',b*5,mask_backend)]:
                p=phases[r['method'],phase];assert int(p['sent_bytes'])==(8 if backend=='DCF' else 70)*pairs
                assert int(p['exchange_rounds'])==(1 if backend=='DCF' else 8)
                assert int(p['theoretical_rounds'])==(1 if backend=='DCF' else 8)
                assert int(p['dcf_evaluate_calls'])==(2*pairs if backend=='DCF' else 0)
            p=phases[r['method'],'vote'];assert int(p['sent_bytes'])==72*(5*b)+72 and int(p['exchange_rounds'])==6
            for total,leaves in [('matrix',['rank_matrix','rank_mask','matrix_other']),('matrix_vote_online',['matrix','vote','online_other'])]:
                for metric in METRICS:
                    cast=float if metric=='online_ms' else int
                    actual=cast(phases[r['method'],total][metric]);expected=sum(cast(phases[r['method'],p][metric]) for p in leaves)
                    assert abs(actual-expected)<=(1e-5 if metric=='online_ms' else 0),(total,metric)
            cases.append(dict(**r,fixture_checksum=meta['fixture_checksum'],fixture_and_setup_ms=meta['fixture_and_setup_ms']))
        assert all(int(r['transport_actions'])==2*int(r['exchange_rounds']) for r in phases.values())
        assert all(math.isfinite(float(r['online_ms'])) and float(r['online_ms'])>=0 for r in phases.values())
    assert parsed[0][0]['fixture_checksum']==parsed[1][0]['fixture_checksum']
    assert parsed[0][0]['candidate_checksum']==parsed[1][0]['candidate_checksum']
    assert [r['method'] for r in parsed[0][2]]==[r['method'] for r in parsed[1][2]]
    for method,phase in sorted(parsed[0][1]):
        r=dict(B=b,trial=trial,profile=profile,method=method,phase=phase,timing_kind='measured')
        for party,(_,phases,_) in enumerate(parsed):
            for metric in METRICS:r[f'P{party}_{metric}']=(float if metric=='online_ms' else int)(phases[method,phase][metric])
        r['online_ms']=max(r['P0_online_ms'],r['P1_online_ms']);r['sent_bytes']=max(r['P0_sent_bytes'],r['P1_sent_bytes']);rows.append(r)
    return rows,cases
def summarize(out,rows,cases):
    csv_write(out/'phase_trials.csv',rows);csv_write(out/'cases.csv',cases);summary=[]
    for key in sorted({(r['profile'],r['B'],r['method'],r['phase']) for r in rows}):
        profile,b,method,phase=key;group=[r for r in rows if (r['profile'],r['B'],r['method'],r['phase'])==key]
        vals=[r['online_ms'] for r in group];mean=st.mean(vals);sd=st.stdev(vals) if len(vals)>1 else None
        r=dict(profile=profile,B=b,method=method,phase=phase,samples=len(vals),mean_ms=mean,median_ms=st.median(vals),sample_sd_ms=sd,
            cv=sd/mean if sd is not None and mean else None,min_ms=min(vals),max_ms=max(vals))
        for party in [0,1]:
            for metric in METRICS:r[f'P{party}_{metric}_trials']=json.dumps([g[f'P{party}_{metric}'] for g in group])
        summary.append(r)
    csv_write(out/'phase_summary.csv',summary);lines=[]
    for profile in ['lan','wan']:
        lines += ['**'+profile.upper()+'**','','| B | 方法 | 矩阵ms | 投票ms | 连续合计ms | 单方发送B | 实际交换P0/P1 | 逻辑轮次P0/P1 |','|---|---|---:|---:|---:|---:|---|---|']
        for b in sorted({r['B'] for r in rows if r['profile']==profile}):
            for method in METHODS:
                group=[r for r in summary if r['profile']==profile and r['B']==b and r['method']==method]
                get=lambda p:next(r for r in group if r['phase']==p)
                times=[get(p)['mean_ms'] for p in ['matrix','vote','matrix_vote_online']];total=get('matrix_vote_online')
                def counter(metric,party):
                    values=json.loads(total[f'P{party}_{metric}_trials']);assert len(set(values))==1;return values[0]
                lines.append('| '+' | '.join([str(b),method]+[f'{v:.6f}' for v in times]+[str(max(counter('sent_bytes',0),counter('sent_bytes',1)))]+[
                    '/'.join(str(counter(metric,p)) for p in [0,1]) for metric in ['exchange_rounds','theoretical_rounds']])+' |')
        lines.append('')
    (out/'tables.md').write_text('\n'.join(lines)+'\n')
def main():
    global METHODS
    parser=argparse.ArgumentParser(description='Matrix-only B sweep; instructions in source comments')
    parser.add_argument('--repeats',type=int,default=3);parser.add_argument('--smoke',action='store_true');parser.add_argument('--dry-run',action='store_true')
    parser.add_argument('--blocks',nargs='+',type=int,choices=BLOCKS,default=BLOCKS)
    parser.add_argument('--profiles',nargs='+',choices=['lan','wan'],default=['lan','wan'])
    parser.add_argument('--methods',nargs='+',choices=['FullMat','PCM'],default=['FullMat','PCM'])
    a=parser.parse_args();os.chdir(ROOT)
    if a.repeats<1:parser.error('positive repeats required')
    if any(len(x)!=len(set(x)) for x in [a.blocks,a.profiles,a.methods]):parser.error('duplicate block/profile/method')
    METHODS=a.methods
    if a.smoke:a.repeats=1
    blocks=[2,32] if a.smoke else a.blocks
    jobs=[]
    for trial in range(1,a.repeats+1):
        shift=(trial-1)%len(blocks)
        for i,b in enumerate(blocks[shift:]+blocks[:shift]):
            for profile in a.profiles:jobs.append(dict(B=b,trial=trial,profile=profile,order=','.join(METHODS if (trial+i)%2 else list(reversed(METHODS)))))
    if a.dry_run:print(json.dumps(dict(N=32768,d=32,k=5,classes=4,pairs=len(jobs),cases=len(METHODS)*len(jobs),smoke=a.smoke,schedule=jobs,comparison_policy='fixed DCF rank/mask/vote'),indent=2));return
    lock=open('/tmp/kona-bench-experiment.lock','a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');out=ROOT/'KNN-experiment-res'/('matrix-block-count-'+('smoke-' if a.smoke else '')+stamp+'-'+str(os.getpid()))
    out.mkdir();(out/'raw').mkdir();print('RESULT_DIRECTORY='+str(out),flush=True)
    baseline=sources();manifest=dict(N=32768,d=32,k=5,classes=4,seed=20261105,smoke=a.smoke,schedule=jobs,source_sha256=baseline,
        comparison_policy=dict(FullMat='DCF',PCM='DCF',rank_mask='DCF'),methods=METHODS,vote='DCF_OPTIMIZED_MASK',cpu=[0,2],core_exclusivity='NOT_VERIFIED')
    env=dict(os.environ,KONA_NETWORK_PRIVILEGED='1');rows=[];cases=[];original=None;touched=False;failure=None;done=0;procs=[];handles=[];marker=out.name;started=time.monotonic()
    def save(): (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    save()
    def stop(sig,frame):raise KeyboardInterrupt('signal '+str(sig))
    signal.signal(signal.SIGINT,stop);signal.signal(signal.SIGTERM,stop)
    def frozen():
        if sources()!=baseline:raise RuntimeError('Measured source changed')
    def snapshot(name,profile):
        text=docker('taskset','-c','4','bash','-lc','tc qdisc show dev lo; tc class show dev lo; tc -s class show dev lo; ethtool -k lo; ss -tin; ps -eo pid,psr,comm,rss,stat,args; free -k')
        (out/(name+'.log')).write_text(text)
        if not a.smoke:
            rate='1Gbit' if profile=='lan' else '40Mbit'
            for leaf in ['10','20']:assert re.search(r'class htb 1:'+leaf+r' .*?rate '+rate,text)
            if profile=='wan':assert len(re.findall(r'qdisc netem (?:10:|20:).*?delay 20ms',text))==2
            else:
                delays=re.findall(r'^qdisc netem (?:10:|20:)[^\n]*?delay ([0-9.]+)(us|ms|s)',text,re.M)
                assert len(delays)==2 and all(abs(float(v)*{'us':1,'ms':1000,'s':1000000}[unit]-500)<=2 for v,unit in delays)
            for name in ['tcp-segmentation-offload','generic-segmentation-offload','generic-receive-offload']:assert re.search(r'^'+name+r': off',text,re.M)
    try:
        with (out/'build.log').open('w') as log:
            sp.run(['bash','Scripts/codex-container-build.sh'],check=True,stdout=log,stderr=sp.STDOUT)
            sp.run(['docker','exec','kona-dev','bash','-lc',f'cd {REMOTE} && make -j8 {TARGET}'],check=True,stdout=log,stderr=sp.STDOUT)
        setup=r'''
import pathlib,json,subprocess,hashlib,sys
root=pathlib.Path('/usr/src/Garnet');runtime=pathlib.Path(sys.argv[1]);runtime.mkdir()
q=subprocess.check_output(['tc','qdisc','show','dev','lo'],text=True)
if any(l and not l.startswith('qdisc noqueue ') for l in q.splitlines()):raise RuntimeError('Network already configured; refuse overwrite')
keys=next((pathlib.Path(base)/p for base in [str(root),'/tmp/kona-accounting-regression','/tmp/kona-reference-regression'] for p in ['Player-Data/2-fss','Player-Data'] if all((pathlib.Path(base)/p/f).is_file() and (pathlib.Path(base)/p/f).stat().st_size for f in ['k0','k1','r0','r1'])),None)
if keys is None:raise RuntimeError('Missing existing DCF material; no generation')
target=runtime/'Player-Data/2-fss';target.mkdir(parents=True)
for f in ['k0','k1','r0','r1']:(target/f).symlink_to((keys/f).resolve())
flags=subprocess.check_output(['ethtool','-k','lo'],text=True)
offload={s:next(l.split(':',1)[1].strip().split()[0] for l in flags.splitlines() if l.startswith(k+':')) for k,s in [('tcp-segmentation-offload','tso'),('generic-segmentation-offload','gso'),('generic-receive-offload','gro')]}
print(json.dumps(dict(runtime=str(runtime),key_source=str(keys),key_sha256={f:hashlib.sha256((keys/f).read_bytes()).hexdigest() for f in ['k0','k1','r0','r1']},offload=offload,binary_sha256=hashlib.sha256((root/'matrix-block-count-bench.x').read_bytes()).hexdigest())))
'''
        info=json.loads(docker('python3','-c',setup,'/tmp/matrix-b-'+uuid.uuid4().hex));original=info['offload'];manifest.update(info);save();probed=set();fixture=None;candidate_hashes={}
        for sequence,job in enumerate(jobs,1):
            b,trial,profile=job['B'],job['trial'],job['profile'];label=f'B{b}-t{trial}-{profile}';marker=out.name+'/'+label
            frozen();print(f'START {sequence}/{len(jobs)} {label}',flush=True)
            if not a.smoke:
                touched=True
                with (out/(label+'-network.log')).open('w') as log:sp.run(['bash','Scripts/container-network.sh',profile,'lo'],env=env,check=True,stdout=log,stderr=sp.STDOUT)
                if profile not in probed:
                    remote=info['runtime']+'/probe-'+profile+'.json'
                    with (out/('probe-'+profile+'.log')).open('w') as log:sp.run(['docker','exec','kona-dev','taskset','-c','4','python3',REMOTE+'/Scripts/measure-container-profile-throughput.py','--profile',profile,'--base','42000','--mib','64','--samples','10','--json-output',remote],check=True,stdout=log,stderr=sp.STDOUT,timeout=180)
                    probe=json.loads(docker('cat',remote));(out/('probe-'+profile+'.json')).write_text(json.dumps(probe,indent=2)+'\n');target=1000 if profile=='lan' else 40
                    assert all(.65*target<v['receiver_mbps']<1.3*target for part in probe['transfers'].values() for v in part['lanes'])
                    assert all((32<probe[k]['median_ms']<60) if profile=='wan' else (0<=probe[k]['median_ms']<5) for k in ['rtt_odd','rtt_even']);probed.add(profile)
            snapshot(label+'-before',profile);procs=[];handles=[]
            for party in [0,1]:
                h=(out/'raw'/f'{label}-P{party}.log').open('w');handles.append(h)
                args=['env',f'KONA_RUN_ID={marker}',f'KONA_BENCH_SOURCE={REMOTE}/Machines/matrix-block-count-bench.cpp',f'KONA_BENCH_BINARY={REMOTE}/{TARGET}',
                    'bash',REMOTE+'/Scripts/kona-bench-log.sh','timeout','--kill-after=5','120',REMOTE+'/'+TARGET,
                    '-p',str(party),'-pn','36000','--blocks',str(b),'--trial',str(trial),'--network-profile',profile,'--order',job['order']]
                procs.append(sp.Popen(['docker','exec','kona-dev','bash','-lc','cd '+shlex.quote(info['runtime'])+' && '+shlex.join(args)],stdout=h,stderr=sp.STDOUT))
            deadline=time.monotonic()+150;sample=0;last_monitor=time.monotonic()
            while any(p.poll() is None for p in procs):
                if time.monotonic()>deadline:raise TimeoutError(label)
                if any(p.poll() not in [None,0] for p in procs):raise RuntimeError(label+' party failed; no retry')
                if time.monotonic()-last_monitor>=15:
                    frozen();snapshot(label+f'-monitor-{sample}',profile);last_monitor=time.monotonic()
                sample+=1;time.sleep(.25)
            codes=[p.wait() for p in procs]
            for h in handles:h.close()
            if any(codes):raise RuntimeError(label+' exits '+str(codes))
            snapshot(label+'-after',profile);frozen()
            texts=[(out/'raw'/f'{label}-P{p}.log').read_text() for p in [0,1]]
            new_rows,new_cases=collect(texts,b,trial,profile,info)
            meta=parse(texts[0])[0]
            if fixture is None:fixture=meta['fixture_checksum']
            assert meta['fixture_checksum']==fixture
            if b not in candidate_hashes:candidate_hashes[b]=meta['candidate_checksum']
            assert meta['candidate_checksum']==candidate_hashes[b]
            rows+=new_rows;cases+=new_cases;done+=1;summarize(out,rows,cases)
            with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(job,sequence=sequence,exits=codes,status='COMPLETED'))+'\n')
        key_hashes=json.loads(docker('python3','-c','import pathlib,hashlib,json,sys; p=pathlib.Path(sys.argv[1]); print(json.dumps({f:hashlib.sha256((p/f).read_bytes()).hexdigest() for f in ["k0","k1","r0","r1"]}))',info['key_source']))
        assert key_hashes==info['key_sha256'];frozen()
    except BaseException as e:failure=str(e) or type(e).__name__;raise
    finally:
        if failure:
            kill=r'''
import pathlib,os,signal,sys
needle=('KONA_RUN_ID='+sys.argv[1]).encode()
for f in pathlib.Path('/proc').glob('[0-9]*/environ'):
 try:
  if needle in f.read_bytes().split(b'\0'):os.kill(int(f.parent.name),signal.SIGKILL)
 except OSError:pass
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
        (out/'status.json').write_text(json.dumps(dict(status='FAILED' if failure or any(cleanup) else 'COMPLETED',error=failure,completed_pairs=done,planned_pairs=len(jobs),cleanup_exit_codes=cleanup,wall_seconds=time.monotonic()-started,smoke=a.smoke),indent=2)+'\n')
        fcntl.flock(lock,fcntl.LOCK_UN);lock.close()
        if not failure and any(cleanup):raise RuntimeError('Network cleanup failed')
    print('COMPLETED: '+str(out),flush=True)
if __name__=='__main__':main()
