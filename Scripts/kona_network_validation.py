"""Shared network contract/checks; no experiment launching or protocol timing.
Source of truth: container-network.sh. Used by KNN and comparator organizers.
"""
import os
import re
import subprocess as sp
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
NETWORK_SCRIPT=ROOT/'Scripts/container-network.sh'
NETWORK_SOURCE=NETWORK_SCRIPT.read_text()
DEFAULT_PACING=re.search(r'^PACING=([a-z]+)$',NETWORK_SOURCE,re.M).group(1)
NETWORK_CONFIG={name:dict(rate=rate,delay=delay) for name,rate,delay in re.findall(r'([a-z-]+)\) PARENT_RATE=\S+; RATE=(\S+); DELAY=([^ ;]+)',NETWORK_SOURCE)}
def profile_metadata(text):
 lines=[line for line in text.splitlines() if line.startswith('NETWORK_PROFILE ')]
 if len(lines)!=1:raise RuntimeError('Missing/ambiguous NETWORK_PROFILE evidence')
 return dict(item.split('=',1) for item in lines[0].split()[1:])
def network_contract(profile,pacing,container=None):
 # Read the exact mode/version from the single shell source of truth, no
 # duplicate v3/v4 version literals in this organizer. Dry-run never uses Docker.
 output=sp.check_output(['bash',str(NETWORK_SCRIPT),profile,'lo','--pacing',pacing,'--dry-run'],text=True,env=dict(os.environ,**({'KONA_CONTAINER':container} if container else {})))
 contract=profile_metadata(output)
 if contract.get('pacing')!=pacing or contract.get('profile')!=profile or contract.get('action')!='apply' or not contract.get('version'):
  raise RuntimeError('Invalid network source-of-truth contract')
 return contract
def check_network(text,profile,pacing=DEFAULT_PACING,profile_log='',contract=None):
 config=NETWORK_CONFIG[profile]
 expected=contract if contract is not None else network_contract(profile,pacing)
 metadata=profile_metadata(profile_log)
 for key in ['version','pacing','action','profile','container','dev','rate_per_direction','one_way_delay','parent_rate']:
  if metadata.get(key)!=expected.get(key):raise RuntimeError('Network profile metadata mismatch: '+key)
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
  lines=re.findall(r'^qdisc netem '+lane+r':[^\n]*',text,re.M)
  if len(lines)!=1:raise RuntimeError('Missing/ambiguous netem lane '+lane)
  match=re.search(r'\bdelay (\S+)',lines[0])
  if not match or abs(delay(match[1])-delay(config['delay']))>2:raise RuntimeError('Lane delay mismatch')
  match=re.search(r'\brate (\S+)',lines[0])
  if pacing=='serialized':
   if not match or rate(match[1])!=rate(config['rate']):raise RuntimeError('Netem lane '+lane+' rate mismatch/missing')
  elif pacing=='burst':
   if match:raise RuntimeError('Unexpected netem rate in historical burst mode')
  else:raise RuntimeError('Unsupported network pacing')
 for feature in ['tcp-segmentation-offload','generic-segmentation-offload','generic-receive-offload']:
  if not re.search(r'^'+feature+r': off',text,re.M):raise RuntimeError('Offload configuration mismatch')
 return dict(profile=profile,pacing=pacing,version=metadata['version'],netem_rate=config['rate'] if pacing=='serialized' else None)
