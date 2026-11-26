#!/usr/bin/env python3
"""Table11/Figure7: one C++ bench + this organizer; no other experiment wrappers.

Default: stored 16384-32, N16384,d32,k20,4 classes; five network profiles,
DCF/PPC/Hybrid each twice. Same Local+PCM graph, input shares, multiplication
and optimized DCF vote. No distance protocol, chunk, prewarm, common future
start or algorithm substitution. Existing L2 test keys only, no key generation.

Thresholds come ONLY from Machines/kona-bench-config.hpp. Network rates/delays
come ONLY from container-network.sh (serialized-v4); RTT is 2*one-way delay.
All SELECTION comparisons (block Cong, PCM ranking, rank-mask) obey the selected
mode; vote ALWAYS uses vote_dcf_optimized. Main table call counts are logical
comparison ITEMS in Hybrid selection, excluding vote, not batches/evaluate.
Raw logs also retain per-layer width/backend, batch counts, vote comparisons
and actual DCF evaluates. WAN26000 may yield no PPC at this N: report honestly.

Each condition is a fresh P0/P1 pair; fixed C++ CPU0/2. Rotate profile start and
backend order across trials, preserving exact execution order. All modes
measure selection+vote CONTINUOUSLY, not summed per-party phase maxima.
Input/distance fixture, cached keys, plans, cloning, readiness, validation,
printing and Python/probes stay outside online time. Stage counters/timers are
recorded together; total uses an independent outer timer. No stage barriers.

Use existing kona-dev/build sync; generic Makefile target only. Freeze sources,
binary, input and keys; fail without retry. Mandatory network checks before,
after and on 15s CPU4 diagnostic snapshots. Throughput/RTT preflight once per
profile, on separate connections before protocols. Cleanup tc and restore
original offload on handled failures. Affinity does not imply core exclusivity.

Prepare/review only: python3 Scripts/run-pcm-backend-ablation.py --dry-run
Future formal run: python3 -u Scripts/run-pcm-backend-ablation.py --repeats 2
"""
import argparse,csv,datetime,fcntl,hashlib,json,math,os,re,shlex,signal,statistics,subprocess as sp,time,uuid
from pathlib import Path
from kona_network_validation import network_contract,check_network,NETWORK_CONFIG
ROOT=Path(__file__).resolve().parents[1];REMOTE='/usr/src/Garnet';TARGET='pcm-backend-ablation-bench.x'
PROFILES=['super-dc','lan','mid-lan','mid-wan','wan'];MODES=['dcf','ppc','hybrid']
KEYS={'super-dc':'SUPER_DC_THRESHOLD','lan':'LAN_THRESHOLD','mid-lan':'MID_LAN_THRESHOLD','mid-wan':'MID_WAN_THRESHOLD','wan':'WAN_THRESHOLD'}
CONFIG=ROOT/'Machines/kona-bench-config.hpp'
DEFAULTS={k:int(v) for k,v in re.findall(r'constexpr std::size_t (\w+) = (\d+);',CONFIG.read_text())}
PHASES=['block_cong','pcm_terminal','selection_other','selection','vote','online_other','selection_vote_online']
METRICS=['online_ms','sent_bytes','transport_actions','theoretical_rounds','exchange_rounds','dcf_evaluate_calls']
def docker(*args):return sp.check_output(['docker','exec','kona-dev',*args],text=True,timeout=30)
def sources():
    names=['Machines/pcm-backend-ablation-bench.cpp','Scripts/run-pcm-backend-ablation.py',
        'Scripts/kona_network_validation.py','Scripts/container-network.sh','Scripts/codex-container-build.sh',
        'Scripts/kona-bench-log.sh','Scripts/measure-container-profile-throughput.py','Makefile',
        'ConstantPP/data-loader.cpp','ConstantPP/data-loader.hpp','Tools/octetStream.cpp','Tools/octetStream.h']
    names += [str(p.relative_to(ROOT)) for p in (ROOT/'Machines').glob('kona-*.hpp')]
    names += [str(p.relative_to(ROOT)) for pattern in ['*.h','*.cpp'] for p in (ROOT/'Networking').glob(pattern)]
    return {n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in sorted(set(names))}
def write_csv(path,rows):
    if rows:
        with path.open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
def parse(text):
    meta=dict(re.findall(r'^(\w+)=([^\n]+)$',text,re.M));phases={};layers=[]
    for line in text.splitlines():
        if line.startswith(('BACKEND_PHASE ','BACKEND_LAYER ')):
            r=dict(re.findall(r'(\w+)=([^\s]+)',line))
            if line.startswith('BACKEND_LAYER '):layers.append(r)
            else:
                if r['phase'] in phases:raise RuntimeError('Duplicate phase')
                phases[r['phase']]=r
    return meta,phases,layers
def summarize(out,rows,calls,contracts):
    write_csv(out/'phase_trials.csv',rows);summary=[]
    for profile,mode,phase in sorted({(r['profile'],r['mode'],r['phase']) for r in rows}):
        group=[r for r in rows if (r['profile'],r['mode'],r['phase'])==(profile,mode,phase)]
        values=[r['online_ms'] for r in group];mean=statistics.mean(values);sd=statistics.stdev(values) if len(values)>1 else None
        r=dict(profile=profile,mode=mode,phase=phase,samples=len(values),mean_ms=mean,median_ms=statistics.median(values),sample_sd_ms=sd,
            cv=sd/mean if sd is not None and mean else None,min_ms=min(values),max_ms=max(values))
        for party in [0,1]:
            for metric in METRICS:r[f'P{party}_{metric}_trials']=json.dumps([v[f'P{party}_{metric}'] for v in group])
        summary.append(r)
    write_csv(out/'phase_summary.csv',summary)
    def rate_mbps(s):
        m=re.fullmatch(r'([0-9.]+)([kmg]?)bit',s.lower());return float(m[1])*{'':1e-6,'k':.001,'m':1,'g':1000}[m[2]]
    def delay_ms(s):
        m=re.fullmatch(r'([0-9.]+)(us|ms|s)',s);return float(m[1])*{'us':.001,'ms':1,'s':1000}[m[2]]
    table=[];lines=['# Table11: fixed Local+PCM, comparator-backend ablation','',
        'Time is independently measured selection+fixed-DCF-vote, seconds. Hybrid call counts are selection comparison items (vote excluded).',
        'No distance, chunk, prewarm or common-future start. Both parties retained; total time/bytes=max(P0,P1) before averaging.','',
        '| Network | Mbps/direction | RTT ms | DCF s | PPC s | Hybrid s | tau | Hybrid DCF items | Hybrid PPC items |',
        '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for profile in PROFILES:
        values={r['mode']:r for r in summary if r['profile']==profile and r['phase']=='selection_vote_online'}
        if set(values)!=set(MODES):continue
        counts=[r for r in calls if r['profile']==profile and r['mode']=='hybrid'];pairs={(r['dcf_items'],r['ppc_items']) for r in counts}
        if len(pairs)!=1:raise RuntimeError('Hybrid split changed between trials')
        dcf_items,ppc_items=pairs.pop();c=contracts[profile]
        r=dict(profile=profile,mbps=rate_mbps(c['rate_per_direction']),rtt_ms=2*delay_ms(c['one_way_delay']),tau=DEFAULTS[KEYS[profile]],
            dcf_mean_s=values['dcf']['mean_ms']/1000,ppc_mean_s=values['ppc']['mean_ms']/1000,hybrid_mean_s=values['hybrid']['mean_ms']/1000,
            hybrid_dcf_items=dcf_items,hybrid_ppc_items=ppc_items,samples=values['hybrid']['samples'])
        table.append(r);lines.append('| '+' | '.join([profile,str(r['mbps']),str(r['rtt_ms'])]+[f"{r[n+'_mean_s']:.6f}" for n in MODES]+[str(r['tau']),str(dcf_items),str(ppc_items)])+' |')
    write_csv(out/'table11.csv',table);(out/'table11.md').write_text('\n'.join(lines)+'\n');write_csv(out/'backend_counts.csv',calls)
def main():
    p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--profiles',nargs='+',choices=PROFILES,default=PROFILES);p.add_argument('--repeats',type=int,default=2)
    p.add_argument('--timeout',type=int,default=600);p.add_argument('--dry-run',action='store_true')
    a=p.parse_args();os.chdir(ROOT)
    if a.repeats<1 or a.timeout<1 or len(a.profiles)!=len(set(a.profiles)):p.error('Invalid repeats/timeout/duplicate profile')
    contracts={profile:network_contract(profile,'serialized') for profile in a.profiles}
    if any(not c['version'].endswith('-serialized-v4') for c in contracts.values()):p.error('Requires serialized-v4')
    jobs=[]
    for trial in range(1,a.repeats+1):
        shift=(trial-1)%len(a.profiles)
        for profile in a.profiles[shift:]+a.profiles[:shift]:
            fixed=PROFILES.index(profile);s=(trial-1+fixed)%len(MODES)
            for mode in MODES[s:]+MODES[:s]:jobs.append(dict(profile=profile,mode=mode,trial=trial,threshold=DEFAULTS[KEYS[profile]],job=f'{profile}-{mode}-t{trial}'))
    config=dict(N=16384,d=32,k=20,classes=4,chunking='off',cpu=[0,2],core_exclusivity='NOT_VERIFIED',warmup_trials=0,
        common_future_start=False,network_contracts=contracts,threshold_source=str(CONFIG),thresholds={n:DEFAULTS[KEYS[n]] for n in PROFILES},
        comparator_scope='block-Cong + PCM-ranking + rank-mask; vote fixed DCF',jobs=jobs,
        expected_pairs=len(jobs),output_scope='winning_label_secret_share',distance_execution_count=0)
    if a.dry_run:print(json.dumps(config,indent=2));return
    lock=open('/tmp/kona-bench-experiment.lock','a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out=ROOT/'KNN-experiment-res'/('pcm-backend-ablation-'+stamp+'-'+str(os.getpid()));out.mkdir();(out/'raw').mkdir()
    print('RESULT_DIRECTORY='+str(out),flush=True)
    baseline=sources();revision=sp.check_output(['git','rev-parse','HEAD'],text=True).strip();config.update(source_sha256=baseline,source_revision=revision,command=os.sys.argv);env=dict(os.environ,KONA_NETWORK_PRIVILEGED='1')
    (out/'manifest.json').write_text(json.dumps(config,indent=2)+'\n')
    rows=[];calls=[];layer_rows=[];checks=[];done=0;failure=None;original=None;touched=False;handles=[];procs=[];marker=out.name;start=time.monotonic()
    def frozen():
        if sources()!=baseline:raise RuntimeError('Measured source changed; no mixed builds')
    def snapshot(job,boundary):
        text=docker('taskset','-c','4','bash','-lc','set -e; tc -s qdisc show dev lo; tc -s class show dev lo; ethtool -k lo; ss -tin; ps -eo pid,psr,comm,rss,stat,args; free -k')
        (out/(job['job']+'-'+boundary+'.log')).write_text(text)
        try:result=check_network(text,job['profile'],'serialized',(out/(job['job']+'-network.log')).read_text(),contracts[job['profile']])
        except RuntimeError as error:
            checks.append(dict(job=job['job'],boundary=boundary,status='FAILED',error=str(error)));(out/'network-validation.json').write_text(json.dumps(checks,indent=2));raise
        checks.append(dict(job=job['job'],boundary=boundary,status='PASS',**result));(out/'network-validation.json').write_text(json.dumps(checks,indent=2))
    def stop(sig,frame):raise KeyboardInterrupt(str(sig))
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:
        with (out/'build.log').open('w') as log:
            sp.run(['bash','Scripts/codex-container-build.sh'],check=True,stdout=log,stderr=sp.STDOUT)
            sp.run(['docker','exec','kona-dev','bash','-lc',f'cd {REMOTE} && make -j4 {TARGET}'],check=True,stdout=log,stderr=sp.STDOUT)
        setup=r'''
import pathlib,subprocess,hashlib,json,sys
root=pathlib.Path('/usr/src/Garnet');runtime=pathlib.Path(sys.argv[1]);runtime.mkdir()
q=subprocess.check_output(['tc','qdisc','show','dev','lo'],text=True)
if any(l and not l.startswith('qdisc noqueue ') for l in q.splitlines()):raise RuntimeError('Existing network shaping')
keys=next(pathlib.Path(b)/d for b in [str(root),'/tmp/kona-accounting-regression','/tmp/kona-reference-regression'] for d in ['Player-Data/2-fss','Player-Data'] if all((pathlib.Path(b)/d/f).is_file() and (pathlib.Path(b)/d/f).stat().st_size for f in ['k0','k1','r0','r1']))
(runtime/'Player-Data/2-fss').mkdir(parents=True)
for f in ['k0','k1','r0','r1']:(runtime/'Player-Data/2-fss'/f).symlink_to((keys/f).resolve())
flags=subprocess.check_output(['ethtool','-k','lo'],text=True)
offload={s:next(l.split(':',1)[1].split()[0] for l in flags.splitlines() if l.startswith(k+':')) for k,s in [('tcp-segmentation-offload','tso'),('generic-segmentation-offload','gso'),('generic-receive-offload','gro')]}
if list(map(int,(root/'ConstantPP/data/16384-32/Knn-meta').read_text().split()))!=[32,16384,1]:raise RuntimeError('Unexpected input dimensions/query count')
files=[root/sys.argv[2]]+list(keys/f for f in ['k0','k1','r0','r1'])+[root/'ConstantPP/data/16384-32'/f for f in ['Knn-meta','P0-0-X-Train','P0-0-Y-Train','P1-0-X-Test','P1-0-Y-Test']]
print(json.dumps(dict(runtime=str(runtime),offload=offload,identities={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files})))
'''
        info=json.loads(docker('python3','-c',setup,'/tmp/pcm-backend-'+uuid.uuid4().hex,TARGET));original=info['offload'];config.update(info)
        (out/'manifest.json').write_text(json.dumps(config,indent=2)+'\n');checksum=None;graph_count=None;probed=set()
        for sequence,job in enumerate(jobs,1):
            frozen();marker=out.name+'/'+job['job'];print(f"START {sequence}/{len(jobs)} {job['job']}",flush=True)
            with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(job,sequence=sequence,status='STARTED'))+'\n')
            touched=True
            with (out/(job['job']+'-network.log')).open('w') as log:sp.run(['bash','Scripts/container-network.sh',job['profile'],'lo','--pacing','serialized'],env=env,check=True,stdout=log,stderr=sp.STDOUT)
            snapshot(job,'before')
            if job['profile'] not in probed:
                remote=info['runtime']+'/probe-'+job['profile']+'.json'
                with (out/('probe-'+job['profile']+'.log')).open('w') as log:sp.run(['docker','exec','kona-dev','taskset','-c','4','python3',REMOTE+'/Scripts/measure-container-profile-throughput.py','--profile',job['profile'],'--base','42000','--mib','64','--samples','10','--json-output',remote],check=True,stdout=log,stderr=sp.STDOUT,timeout=180)
                probe=json.loads(docker('cat',remote));(out/('probe-'+job['profile']+'.json')).write_text(json.dumps(probe,indent=2));probed.add(job['profile'])
                # Retain actual throughput/RTT; configured link rate is validated
                # separately, not inferred from protocol time or peak throughput.
            procs=[];handles=[]
            for party in [0,1]:
                h=(out/'raw'/f"{job['job']}-P{party}.log").open('w');handles.append(h)
                args=['env','KONA_RUN_ID='+marker,'KONA_SOURCE_REVISION='+revision,'KONA_SOURCE_DIRTY=True','KONA_BENCH_SOURCE='+REMOTE+'/Machines/pcm-backend-ablation-bench.cpp','KONA_BENCH_BINARY='+REMOTE+'/'+TARGET,'KONA_DATASET_PATH='+REMOTE+'/ConstantPP/data/16384-32',
                    'bash',REMOTE+'/Scripts/kona-bench-log.sh','timeout','--kill-after=5',str(a.timeout),REMOTE+'/'+TARGET,'-p',str(party),'-pn','37000',
                    '--profile',job['profile'],'--mode',job['mode'],'--trial',str(job['trial']),'--threshold',str(job['threshold'])]
                procs.append(sp.Popen(['docker','exec','kona-dev','bash','-lc','cd '+shlex.quote(info['runtime'])+' && '+shlex.join(args)],stdout=h,stderr=sp.STDOUT))
            deadline=time.monotonic()+a.timeout+20;next_sample=time.monotonic()+15
            while any(p.poll() is None for p in procs):
                if time.monotonic()>deadline:raise TimeoutError(job['job'])
                if any(p.poll() not in [None,0] for p in procs):raise RuntimeError('Party failed; no retry')
                if time.monotonic()>=next_sample:frozen();snapshot(job,'monitor-'+str(int(time.monotonic()-start)));next_sample=time.monotonic()+15
                time.sleep(.25)
            codes=[p.wait() for p in procs]
            for h in handles:h.close()
            snapshot(job,'after');frozen()
            if any(codes):raise RuntimeError('Party exits '+str(codes))
            parsed=[parse((out/'raw'/f"{job['job']}-P{i}.log").read_text()) for i in [0,1]]
            for party,(meta,phases,layers) in enumerate(parsed):
                if set(phases)!=set(PHASES) or meta.get('correctness')!='PASS' or meta.get('accounting_check')!='PASS':raise RuntimeError('Missing/invalid phases')
                for r in phases.values():
                    if (r['profile'],r['mode'],r['trial'],r['party'])!=(job['profile'],job['mode'],str(job['trial']),str(party)):
                        raise RuntimeError('Phase identity differs from job')
                text=(out/'raw'/f"{job['job']}-P{party}.log").read_text()
                if f'BENCH_AFFINITY party={party} cpu={0 if party==0 else 2} source=cpp_bench affinity_check=PASS' not in text:raise RuntimeError('Affinity evidence missing')
                for key,value in [('threshold',job['threshold']),('mode',job['mode']),('profile',job['profile']),('chunking','off'),('train_count',16384),('features',32),('k',20),('public_label_domain',4),('distance_execution_count',0),('vote_backend','DCF_OPTIMIZED')]:
                    if meta.get(key)!=str(value):raise RuntimeError('Effective setting mismatch '+key)
                if checksum is None:checksum=meta['fixture_checksum'];graph_count=meta['selection_comparators']
                if meta['fixture_checksum']!=checksum or meta['selection_comparators']!=graph_count:raise RuntimeError('Fixture/graph mismatch')
                if meta['selection_dcf_comparators']!=str(sum(int(l['width']) for l in layers if l['backend']=='dcf')) or meta['selection_ppc_comparators']!=str(sum(int(l['width']) for l in layers if l['backend']=='ppc')):raise RuntimeError('Layer split mismatch')
                if job['mode']=='dcf' and int(meta['selection_ppc_comparators']):raise RuntimeError('Unexpected PPC')
                if job['mode']=='ppc' and int(meta['selection_dcf_comparators']):raise RuntimeError('Unexpected DCF in selection')
                if job['mode']=='hybrid' and any(l['backend']!=('dcf' if int(l['width'])<=job['threshold'] else 'ppc') for l in layers):raise RuntimeError('Hybrid switch mismatch')
                for total,leaves in [('selection',['block_cong','pcm_terminal','selection_other']),('selection_vote_online',['selection','vote','online_other'])]:
                    for metric in METRICS:
                        cast=float if metric=='online_ms' else int
                        if abs(cast(phases[total][metric])-sum(cast(phases[leaf][metric]) for leaf in leaves))>(1e-5 if metric=='online_ms' else 0):raise RuntimeError('Counter closure failure')
                calls.append(dict(profile=job['profile'],mode=job['mode'],trial=job['trial'],party=party,dcf_items=int(meta['selection_dcf_comparators']),ppc_items=int(meta['selection_ppc_comparators']),dcf_batches=int(meta['selection_dcf_batches']),ppc_batches=int(meta['selection_ppc_batches']),vote_dcf_items=int(meta['vote_dcf_comparators'])))
                layer_rows.extend(dict(profile=job['profile'],mode=job['mode'],trial=job['trial'],party=party,**l) for l in layers)
            for phase in PHASES:
                row=dict(profile=job['profile'],mode=job['mode'],trial=job['trial'],phase=phase)
                for party,(_,phases,_) in enumerate(parsed):
                    for metric in METRICS:row[f'P{party}_{metric}']=(float if metric=='online_ms' else int)(phases[phase][metric])
                    if row[f'P{party}_transport_actions']!=2*row[f'P{party}_exchange_rounds'] or row[f'P{party}_theoretical_rounds']!=row[f'P{party}_exchange_rounds']:raise RuntimeError('Unchunked round mismatch')
                    if not math.isfinite(row[f'P{party}_online_ms']) or row[f'P{party}_online_ms']<0:raise RuntimeError('Invalid timing')
                row['online_ms']=max(row['P0_online_ms'],row['P1_online_ms']);row['sent_bytes']=max(row['P0_sent_bytes'],row['P1_sent_bytes']);rows.append(row)
            hashes=json.loads(docker('python3','-c','import pathlib,hashlib,json,sys; paths=json.loads(sys.argv[1]); print(json.dumps({f:hashlib.sha256(pathlib.Path(f).read_bytes()).hexdigest() for f in paths}))',json.dumps(list(info['identities']))))
            if hashes!=info['identities']:raise RuntimeError('Binary/input/material identity changed')
            done+=1;summarize(out,rows,calls,contracts);write_csv(out/'layer_trials.csv',layer_rows)
            with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(job,sequence=sequence,status='COMPLETED',exits=codes))+'\n')
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
                flags=[v for k,val in original.items() for v in [k,val]];cleanup.append(sp.run(['docker','exec','--privileged','kona-dev','ethtool','-K','lo',*flags],stdout=log,stderr=sp.STDOUT).returncode)
        (out/'status.json').write_text(json.dumps(dict(status='FAILED' if failure or any(cleanup) else 'COMPLETED',error=failure,completed_pairs=done,planned_pairs=len(jobs),cleanup=cleanup,wall_seconds=time.monotonic()-start),indent=2))
        fcntl.flock(lock,fcntl.LOCK_UN);lock.close()
        if not failure and any(cleanup):raise RuntimeError('Cleanup failed')
    print('COMPLETED: '+str(out),flush=True)
if __name__=='__main__':main()
