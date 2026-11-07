#!/usr/bin/env python3
"""One calibration bench, four workloads; no KNN/Top-k/vote.
Default: LAN/WAN interleaved for each width, three independent repetitions.
PCR/DCF order alternates by width/trial; workload order rotates in C++.
compare3 = three sequential compare calls on unchanged inputs; swap3 = three
preplanned disjoint-pair layers, each consuming preceding swapped records.
Preparation/sync/correctness opening are untimed; default protocol chunks stay.
Only theoretical_rounds (unchunked stages) and exchange_rounds (actual exchanges)
are round metrics. Raw directional actions are accounting checks only.
Uses existing kona-dev, existing test keys and fixed CPU0/2; no key generation.
The five network profiles remain in container-network.sh. --smoke is unshaped.
Failure stops without retries; finally kills this job's processes, clears shaping
and restores prior offload flags. Archives source/binary identity, execution
order, network snapshots, both raw logs, phase CSV and per-party round arrays.
Old three-profile compare-only script remains available as historical evidence.

Launch: python3 -u Scripts/run-comparator-crossover.py
Focused widths: --widths 128 160 180 200 256
Other profiles: --profiles super-dc mid-lan mid-wan
Small regression: --smoke --widths 3 8193 16385 --repeats 1
"""
import argparse,csv,datetime,fcntl,hashlib,json,os,re,shlex,signal,statistics,subprocess as sp,time,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REMOTE='/usr/src/Garnet';TARGET='kona-comparator-crossover-bench.x'
NETWORK_CONFIG={name:dict(rate=rate,delay=delay) for name,rate,delay in re.findall(r'([a-z-]+)\) PARENT_RATE=\S+; RATE=(\S+); DELAY=([^ ;]+)',(ROOT/'Scripts/container-network.sh').read_text())}
WIDTHS={'lan':[32,64,128,160,180,200,256,512,1024],
        'wan':[1000,2000,4000,6000,8192,12000,16384,20000,32768],
        'super-dc':[4,8,16,32,64,128,256],
        'mid-lan':[128,256,512,768,1024,2048],
        'mid-wan':[1000,2000,3000,4000,6000,8192]}
def docker(*args):return sp.check_output(['docker','exec','kona-dev',*args],text=True,timeout=30)
def hashes():
 files=[ROOT/'Machines/kona-comparator-crossover-bench.cpp',Path(__file__),ROOT/'Makefile',ROOT/'Scripts/container-network.sh',ROOT/'Scripts/codex-container-build.sh']
 files+=list((ROOT/'Machines').glob('kona-*.hpp'))+list((ROOT/'Networking').glob('*.h'))+list((ROOT/'Networking').glob('*.cpp'))+[ROOT/'Tools/octetStream.cpp',ROOT/'Tools/octetStream.h']
 return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
def csv_write(path,rows):
 if rows:
  with path.open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
def summarize(out,rows):
 csv_write(out/'phase_trials.csv',rows);summary=[]
 for key in sorted({(r['profile'],r['width'],r['test'],r['backend']) for r in rows if r['phase']=='total'}):
  group=[r for r in rows if r['phase']=='total' and (r['profile'],r['width'],r['test'],r['backend'])==key];v=[r['online_ms'] for r in group];mean=statistics.mean(v)
  r=dict(zip(['profile','width','test','backend'],key));r.update(samples=len(v),mean_ms=mean,median_ms=statistics.median(v),sd_ms=statistics.stdev(v) if len(v)>1 else None,min_ms=min(v),max_ms=max(v),cv=statistics.stdev(v)/mean if len(v)>1 and mean else None,mean_sent_bytes=statistics.mean(x['sent_bytes'] for x in group))
  for party in [0,1]:
   for metric in ['theoretical_rounds','exchange_rounds']:r[f'P{party}_{metric}_trials']=json.dumps([x[f'P{party}_{metric}'] for x in group])
  summary.append(r)
 csv_write(out/'summary.csv',summary)
 lines=['# Comparator crossover calibration','','Time/bytes: max(P0,P1) per trial. Round arrays retain P0/P1 separately.','Theoretical rounds exclude chunks; actual rounds are exchange_rounds. Compare/swap layer details are in phase_trials.csv.','','| Profile | Width | Test | Backend | Trials | Mean ms | Median ms | SD ms | Bytes/party | Theory P0/P1 | Actual P0/P1 |','|---|---:|---|---|---:|---:|---:|---:|---:|---|---|']
 for r in summary:lines.append('| '+' | '.join(str(r[k]) for k in ['profile','width','test','backend','samples','mean_ms','median_ms','sd_ms','mean_sent_bytes'])+' | '+r['P0_theoretical_rounds_trials']+'/'+r['P1_theoretical_rounds_trials']+' | '+r['P0_exchange_rounds_trials']+'/'+r['P1_exchange_rounds_trials']+' |')
 (out/'summary.md').write_text('\n'.join(lines)+'\n')
def check_network(text,profile):
 config=NETWORK_CONFIG[profile]
 def rate(value):
  m=re.fullmatch(r'([0-9.]+)([kmg]?)bit',value.lower())
  if not m:raise RuntimeError('Unrecognized rate '+value)
  return float(m[1])*{'':1,'k':1000,'m':1000000,'g':1000000000}[m[2]]
 def delay(value):
  m=re.fullmatch(r'([0-9.]+)(us|ms|s)',value)
  if not m:raise RuntimeError('Unrecognized delay '+value)
  return float(m[1])*{'us':1,'ms':1000,'s':1000000}[m[2]]
 for lane in ['10','20']:
  match=re.search(r'^class htb 1:'+lane+r' .*?rate (\S+)',text,re.M)
  if not match or rate(match[1])!=rate(config['rate']):raise RuntimeError('Lane rate mismatch')
  match=re.search(r'^qdisc netem '+lane+r':.*?delay (\S+)',text,re.M)
  if not match or abs(delay(match[1])-delay(config['delay']))>2:raise RuntimeError('Lane delay mismatch')
 for feature in ['tcp-segmentation-offload','generic-segmentation-offload','generic-receive-offload']:
  if not re.search(r'^'+feature+r': off',text,re.M):raise RuntimeError('Offload configuration mismatch')
def main():
 p=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
 p.add_argument('--profiles',nargs='+',choices=list(WIDTHS),default=['lan','wan']);p.add_argument('--widths',nargs='+',type=int)
 p.add_argument('--repeats',type=int,default=3);p.add_argument('--timeout',type=int,default=120);p.add_argument('--smoke',action='store_true');p.add_argument('--dry-run',action='store_true');p.add_argument('--output',type=Path)
 a=p.parse_args();os.chdir(ROOT)
 if a.repeats<1 or a.timeout<1 or (a.widths and any(w<1 or w>1000000 for w in a.widths)):p.error('invalid repeats/widths/timeout')
 if len(set(a.profiles))!=len(a.profiles) or (a.widths and len(set(a.widths))!=len(a.widths)):p.error('duplicate profiles/widths')
 if a.smoke and not a.widths:p.error('--smoke requires explicit small --widths')
 profiles=['native'] if a.smoke else a.profiles
 jobs=[]
 for trial in range(1,a.repeats+1):
  count=max(len(a.widths or WIDTHS[profile]) for profile in profiles) if not a.smoke else len(a.widths)
  for i in range(count):
   for profile in profiles:
    widths=a.widths or WIDTHS[profile]
    if i<len(widths):jobs.append(dict(trial=trial,profile=profile,width=widths[(i+trial-1)%len(widths)],order='dcf,pcr' if (i+trial)%2 else 'pcr,dcf'))
 if a.dry_run:print(json.dumps(jobs,indent=2));return
 lock=open('/tmp/kona-bench-experiment.lock','a+');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
 out=(a.output or ROOT/'KNN-experiment-res'/('comparator-crossover-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))).resolve();out.mkdir(exist_ok=False);(out/'raw').mkdir();print('RESULT_DIRECTORY='+str(out),flush=True)
 for sig in [signal.SIGINT,signal.SIGTERM]:signal.signal(sig,lambda s,f:(_ for _ in ()).throw(KeyboardInterrupt(str(s))))
 baseline=hashes();env=dict(os.environ,KONA_NETWORK_PRIVILEGED='1');original=None;touched=False;active=None;procs=[];rows=[];completed=0;failure=None;runtime='/tmp/crossover-'+uuid.uuid4().hex
 manifest=dict(jobs=jobs,source_sha256=baseline,cpu_affinity=[0,2],core_exclusivity='NOT_VERIFIED',smoke=a.smoke,configuration_source='Machines/kona-bench-config.hpp and Scripts/container-network.sh',network_profiles=NETWORK_CONFIG)
 try:
  with (out/'build.log').open('w') as f:
   sp.run(['bash','Scripts/codex-container-build.sh'],stdout=f,stderr=sp.STDOUT,check=True)
   sp.run(['docker','exec','kona-dev','bash','-lc',f'cd {REMOTE} && make -j4 {TARGET}'],stdout=f,stderr=sp.STDOUT,check=True)
  setup='''import pathlib,subprocess,json,sys,hashlib
r=pathlib.Path(sys.argv[1]);q=subprocess.check_output(['tc','qdisc','show','dev','lo'],text=True)
if any(l and not l.startswith('qdisc noqueue ') for l in q.splitlines()):raise RuntimeError('Existing shaping; refusing overwrite')
if subprocess.run(['pgrep','-x','knn-bench.x'],stdout=subprocess.DEVNULL).returncode==0:raise RuntimeError('Active protocol')
k=next(pathlib.Path(base)/folder for base in ['/usr/src/Garnet','/tmp/kona-accounting-regression','/tmp/kona-reference-regression'] for folder in ['Player-Data/2-fss','Player-Data'] if all((pathlib.Path(base)/folder/n).is_file() for n in ['k0','k1','r0','r1']))
(r/'Player-Data/2-fss').mkdir(parents=True)
for n in ['k0','k1','r0','r1']:(r/'Player-Data/2-fss'/n).symlink_to((k/n).resolve())
s=subprocess.check_output(['ethtool','-k','lo'],text=True)
flags={short:next(l.split(':',1)[1].strip().split()[0] for l in s.splitlines() if l.startswith(long+':')) for long,short in [('tcp-segmentation-offload','tso'),('generic-segmentation-offload','gso'),('generic-receive-offload','gro')]}
print(json.dumps(dict(offload=flags,key_source=str(k),binary_sha256=hashlib.sha256(pathlib.Path(sys.argv[2]).read_bytes()).hexdigest())))'''
  info=json.loads(docker('python3','-c',setup,runtime,REMOTE+'/'+TARGET));original=info['offload'];manifest.update(info);(out/'manifest.json').write_text(json.dumps(manifest,indent=2))
  for seq,job in enumerate(jobs,1):
   if hashes()!=baseline:raise RuntimeError('Source changed during batch')
   profile=job['profile'];label=f"t{job['trial']}-m{job['width']}-{profile}";active=out.name+'/'+label
   print(f'START {seq}/{len(jobs)} {label}',flush=True)
   with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(job,sequence=seq,status='STARTED'))+'\n')
   if not a.smoke:
    touched=True
    with (out/(label+'-network.log')).open('w') as f:sp.run(['bash','Scripts/container-network.sh',profile,'lo'],env=env,stdout=f,stderr=sp.STDOUT,check=True)
   network_text=docker('bash','-lc','tc -s qdisc show dev lo; tc -s class show dev lo; ethtool -k lo')
   (out/(label+'-before.log')).write_text(network_text)
   if not a.smoke:check_network(network_text,profile)
   current=docker('sha256sum',REMOTE+'/'+TARGET).split()[0]
   if current!=info['binary_sha256']:raise RuntimeError('Binary changed')
   handles=[];procs=[]
   try:
    for party in [0,1]:
     f=(out/'raw'/f'{label}-P{party}.log').open('w');handles.append(f)
     args=['env','KONA_RUN_ID='+active,'timeout','--kill-after=5',str(a.timeout),REMOTE+'/'+TARGET,'-p',str(party),'-pn','39000','--width',str(job['width']),'--trial',str(job['trial']),'--profile',profile,'--order',job['order']]
     procs.append(sp.Popen(['docker','exec','kona-dev','bash','-lc','cd '+shlex.quote(runtime)+' && '+shlex.join(args)],stdout=f,stderr=sp.STDOUT))
    exits=[proc.wait(timeout=a.timeout+10) for proc in procs]
    if any(exits):raise RuntimeError(f'{label} failed {exits}; no retry')
   finally:
    for f in handles:f.close()
   parsed=[]
   for party in [0,1]:
    text=(out/'raw'/f'{label}-P{party}.log').read_text();checks=[l for l in text.splitlines() if l.startswith('CHECK ')]
    if len(checks)!=8 or any('correctness=PASS accounting=PASS' not in l for l in checks):raise RuntimeError('Missing validation')
    phase={}
    for line in text.splitlines():
     if line.startswith('CROSSOVER '):
      r=dict(x.split('=',1) for x in line.split()[1:]);key=(r['test'],r['backend'],r['phase'],int(r['layer']))
      if key in phase or int(r['transport_actions'])!=2*int(r['exchange_rounds']):raise RuntimeError('Invalid phase counters')
      if r['profile']!=profile or int(r['width'])!=job['width'] or int(r['trial'])!=job['trial']:raise RuntimeError('Wrong parameters')
      phase[key]=r
    if f'BENCH_AFFINITY party={party} cpu={0 if party==0 else 2}' not in text:raise RuntimeError('Missing affinity')
    for test in ['compare1','compare3','swap1','swap3']:
     for backend in ['dcf','pcr']:
      total=phase[test,backend,'total',-1];parts=[r for (t,b,ph,l),r in phase.items() if t==test and b==backend and ph!='total']
      for metric in ['sent_bytes','theoretical_rounds','exchange_rounds']:
       if sum(int(r[metric]) for r in parts)!=int(total[metric]):raise RuntimeError('Phase counter closure failed')
      if abs(sum(float(r['online_ms']) for r in parts)-float(total['online_ms']))>1e-5:raise RuntimeError('Timer closure failed')
    parsed.append(phase)
   if set(parsed[0])!=set(parsed[1]):raise RuntimeError('Party phase mismatch')
   for key in parsed[0]:
    r=dict(job,test=key[0],backend=key[1],phase=key[2],layer=key[3],correctness='PASS',accounting='PASS')
    for party in [0,1]:
     for metric in ['online_ms','sent_bytes','theoretical_rounds','exchange_rounds']:
      r[f'P{party}_{metric}']=float(parsed[party][key][metric]) if metric=='online_ms' else int(parsed[party][key][metric])
    for metric in ['sent_bytes','theoretical_rounds','exchange_rounds']:
     if r['P0_'+metric]!=r['P1_'+metric]:raise RuntimeError('Party accounting mismatch')
    r.update(online_ms=max(r['P0_online_ms'],r['P1_online_ms']),sent_bytes=max(r['P0_sent_bytes'],r['P1_sent_bytes']));rows.append(r)
   if hashes()!=baseline:raise RuntimeError('Source changed during job')
   (out/(label+'-after.log')).write_text(docker('bash','-lc','tc -s qdisc show dev lo; tc -s class show dev lo'))
   completed+=1;active=None;summarize(out,rows)
   with (out/'order.jsonl').open('a') as f:f.write(json.dumps(dict(job,sequence=seq,status='COMPLETED'))+'\n')
 except BaseException as e:failure=str(e) or type(e).__name__;raise
 finally:
  cleanup=[]
  if active:
   kill="""import pathlib,os,signal,sys
marker=('KONA_RUN_ID='+sys.argv[1]).encode()
for p in pathlib.Path('/proc').glob('[0-9]*/environ'):
 try:
  if marker in p.read_bytes().split(bytes([0])):os.kill(int(p.parent.name),signal.SIGKILL)
 except (OSError,ProcessLookupError):pass
"""
   cleanup.append(sp.run(['docker','exec','kona-dev','python3','-c',kill,active],stdout=sp.DEVNULL,stderr=sp.DEVNULL).returncode)
  for proc in procs:
   if proc.poll() is None:proc.terminate()
  if touched:
   with (out/'network-cleanup.log').open('w') as f:
    cleanup.append(sp.run(['bash','Scripts/container-network.sh','--clear','--dev','lo'],env=env,stdout=f,stderr=sp.STDOUT).returncode)
    if original:cleanup.append(sp.run(['docker','exec','--privileged','kona-dev','ethtool','-K','lo',*[v for kv in original.items() for v in kv]],stdout=f,stderr=sp.STDOUT).returncode)
  summarize(out,rows);(out/'status.json').write_text(json.dumps(dict(status='FAILED' if failure or any(cleanup) else 'COMPLETED',error=failure,planned=len(jobs),completed=completed,cleanup=cleanup),indent=2));lock.close()
  if not failure and any(cleanup):raise RuntimeError('Cleanup failed')
 print('COMPLETED',out,flush=True)
if __name__=='__main__':main()
