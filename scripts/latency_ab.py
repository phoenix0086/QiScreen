#!/usr/bin/env python3
"""Compare live presentation policies without restarting the AirPlay session."""
import json,re,subprocess,time,statistics
from pathlib import Path
from device_target import HDC,resolve_target
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'docs/validation/latency';OUT.mkdir(exist_ok=True)
H=[HDC,'-t',resolve_target()]
def sh(c):return subprocess.check_output(H+['shell',c],text=True,timeout=12)
def walk(x):
 if isinstance(x,dict):
  if 'attributes' in x:yield x['attributes']
  for k,v in x.items():
   if k!='attributes':yield from walk(v)
 elif isinstance(x,list):
  for y in x:yield from walk(y)
def ui():
 sh('uitest dumpLayout -p /data/local/tmp/qi-ab-ui.json')
 nodes=list(walk(json.loads(sh('cat /data/local/tmp/qi-ab-ui.json'))))
 assert any(x.get('bundleName')=='com.phoenix.qiscreen' for x in nodes),'QiScreen is not foreground'
 return nodes
def click(n,double=False):
 b=list(map(int,re.findall(r'\d+',n['bounds'])));sh(f"uitest uiInput {'doubleClick' if double else 'click'} {(b[0]+b[2])//2} {(b[1]+b[3])//2}")
def mode(low):
 nodes=ui();buttons=[x for x in nodes if x.get('id')=='latencyModeToggle']
 surface=next((x for x in nodes if x.get('type')=='XComponent'),None)
 if not buttons and not any(x.get('id')=='diagnosticsToggle' for x in nodes):
  assert surface,'No streaming surface';click(surface,True);nodes=ui();buttons=[x for x in nodes if x.get('id')=='latencyModeToggle']
 if not buttons:
  diagnostic=next((x for x in nodes if x.get('id')=='diagnosticsToggle'),None)
  if diagnostic:
   click(diagnostic);nodes=ui();buttons=[x for x in nodes if x.get('id')=='latencyModeToggle']
 assert buttons,'No connected display controls'
 if ('低延迟' in buttons[0].get('text',''))!=low:
  click(buttons[0]);nodes=ui();buttons=[x for x in nodes if x.get('id')=='latencyModeToggle']
  assert buttons and (('低延迟' in buttons[0].get('text',''))==low),'Mode did not change'
 if surface:click(surface,True)
def rows(text):
 out=[]
 for line in text.splitlines():
  if 'QiScreenPerf:' not in line:continue
  d={k:float(v) for k,v in re.findall(r'([a-z][a-z0-9_]*)=(-?\d+(?:\.\d+)?)',line.split('QiScreenPerf:',1)[1])}
  d['time']=line[:18];out.append(d)
 return out
pid=sh('pidof com.phoenix.qiscreen').strip();assert pid.isdigit()
results=[]
try:
 for low in [False,True]:
  mode(low);before=sh('hilog -x -P '+pid+' -T QiScreenPerf');old=set(before.splitlines())
  print('Sampling', 'low_latency' if low else 'full_playback',flush=True)
  time.sleep(8)
  after=sh('hilog -x -P '+pid+' -T QiScreenPerf')
  new='\n'.join(l for l in after.splitlines() if l not in old)
  data=[x for x in rows(new) if x.get('mode')==int(low)]
  results.append({'mode':'low_latency' if low else 'full_playback','samples':data})
  (OUT/('low.log' if low else 'full.log')).write_text(new+'\n')
  print(json.dumps(results[-1],ensure_ascii=False),flush=True)
finally:
 try:mode(True)
 except Exception as e:print('Could not restore mode:',str(e),flush=True)
(OUT/'ab-samples.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
