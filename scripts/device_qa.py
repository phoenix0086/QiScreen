#!/usr/bin/env python3
"""Bounded QA of this application's UI, logs, and decoder on an attached device."""
import argparse,json,re,subprocess
from pathlib import Path
from device_target import resolve_target
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/validation'
HDC='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc'
BUNDLE='com.phoenix.qiscreen'
TARGET=resolve_target()
def run(*args):
 r=subprocess.run([HDC,'-t',TARGET,*args],capture_output=True,text=True,timeout=12)
 return r.stdout+r.stderr
def shell(cmd):return run('shell',cmd)
def attrs(node):
 if isinstance(node,dict):
  if 'attributes' in node:yield node['attributes']
  for k,v in node.items():
   if k!='attributes':yield from attrs(v)
 elif isinstance(node,list):
  for v in node:yield from attrs(v)
def layout():
 shell('uitest dumpLayout -p /data/local/tmp/qiscreen-qa.json')
 raw=shell('cat /data/local/tmp/qiscreen-qa.json');j=json.loads(raw)
 nodes=list(attrs(j))
 if not any(n.get('bundleName')==BUNDLE for n in nodes):raise RuntimeError('QiScreen is not in the foreground')
 return raw,nodes
p=argparse.ArgumentParser();p.add_argument('action',choices=['start','toggle','inspect','snapshot']);a=p.parse_args()
OUT.mkdir(exist_ok=True)
if a.action=='start':print(shell('aa start -a EntryAbility -b '+BUNDLE))
if a.action in ['start','toggle']:
 for attempt in range(4):
  try:
   raw,nodes=layout();buttons=[n for n in nodes if n.get('id')=='receiverToggle' and n.get('enabled')=='true']
   if not buttons:continue
   b=buttons[0]
   if a.action=='start' and b.get('text')=='停止接收':print('Already listening');break
   nums=list(map(int,re.findall(r'\d+',b['bounds'])))
   print(shell(f'uitest uiInput click {(nums[0]+nums[2])//2} {(nums[1]+nums[3])//2}'));break
  except (ValueError,RuntimeError):
   if attempt==3:raise
 else:raise RuntimeError('Receiver button is not ready')
else:
 raw,nodes=layout();(OUT/'device-ui-latest.json').write_text(raw)
 print('UI:',[(n.get('text'),n.get('bounds')) for n in nodes if n.get('text')][:20])
 pid=shell('pidof '+BUNDLE).strip();print('PID:',pid)
 if not pid.isdigit():raise RuntimeError('Application is not running')
 logs=shell('hilog -x -P '+pid+' -T QiScreen,QiScreenProtocol,JSAPP')
 (OUT/'device-runtime.log').write_text(logs);print(logs)
 dump=shell('hidumper -s AVCodecService')
 blocks=re.split(r'(?=    Instance_\d+_Info)',dump)
 selected='\n'.join(b for b in blocks if re.search(r'CallerPid\s*-\s*'+pid+r'\b',b))
 (OUT/'device-codec.txt').write_text(selected);print(selected[:2400])
 if a.action=='snapshot':
  print(shell('snapshot_display -f /data/local/tmp/qiscreen-qa.jpeg'))
  print(run('file','recv','/data/local/tmp/qiscreen-qa.jpeg',str(OUT/'device-screen.jpeg')))
