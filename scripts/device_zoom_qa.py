#!/usr/bin/env python3
"""Exercise local zoom on a connected QiScreen stream using the device's test driver."""
import json,re,subprocess
from pathlib import Path
from device_target import resolve_target
ROOT=Path(__file__).resolve().parents[1]
HDC='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc'
TARGET=resolve_target()
def shell(command):
 return subprocess.check_output([HDC,'-t',TARGET,'shell',command],text=True,timeout=15)
def nodes(node):
 if isinstance(node,dict):
  if 'attributes' in node:yield node['attributes']
  for k,v in node.items():
   if k!='attributes':yield from nodes(v)
 elif isinstance(node,list):
  for child in node:yield from nodes(child)
def layout():
 shell('uitest dumpLayout -p /data/local/tmp/qiscreen-zoom-qa.json')
 all_nodes=list(nodes(json.loads(shell('cat /data/local/tmp/qiscreen-zoom-qa.json'))))
 app=[n for n in all_nodes if n.get('bundleName')=='com.phoenix.qiscreen' and n.get('type')=='root']
 if not app:raise RuntimeError('QiScreen must be in the foreground')
 return all_nodes,tuple(map(int,re.findall(r'-?\d+',app[0]['bounds'])))
def button(identifier):
 all_nodes,bounds=layout()
 for n in all_nodes:
  if n.get('id')==identifier:
   a,b,c,d=map(int,re.findall(r'-?\d+',n['bounds']))
   return n,(a+c)//2,(b+d)//2
 return None,None,None
def click(identifier):
 n,x,y=button(identifier)
 if n is None:raise RuntimeError('Missing button '+identifier)
 shell(f'uitest uiInput click {x} {y}')
def show_controls():
 if button('zoomReset')[0] is None:
  _,(l,t,r,b)=layout();shell(f'uitest uiInput doubleClick {(l+r)//2} {int(t+(b-t)*.72)}')
 if button('zoomReset')[0] is None:raise RuntimeError('Stream controls unavailable')
def percent():
 show_controls();return int(button('zoomReset')[0]['text'].split('%')[0])
def codec_ids(pid):
 dump=shell('hidumper -s AVCodecService')
 return re.findall(r'InstanceId\s*-\s*(\d+)', '\n'.join(b for b in re.split(r'(?=    Instance_\d+_Info)',dump) if re.search(r'CallerPid\s*-\s*'+pid+r'\b',b)))
def main():
 show_controls();pid=shell('pidof com.phoenix.qiscreen').strip();before=codec_ids(pid)
 if not before:raise RuntimeError('No active decoder')
 _,(l,t,r,b)=layout();tap_x=(l+r)//2;tap_y=int(t+(b-t)*.60)
 shell(f'uitest uiInput click {tap_x} {tap_y}')
 assert button('zoomReset')[0] is not None,'Single tap hid the toolbar'
 shell(f'uitest uiInput doubleClick {tap_x} {tap_y}')
 assert button('zoomReset')[0] is None,'Double tap did not hide the toolbar'
 shell(f'uitest uiInput click {tap_x} {tap_y}')
 assert button('zoomReset')[0] is None,'Single tap opened the toolbar'
 show_controls()
 click('zoomReset');assert percent()==100
 click('zoomIn');assert percent()==125
 click('zoomOut');assert percent()==100
 _,(l,t,r,b)=layout();w=r-l;h=b-t;y=int(t+h*.72)
 x1=int(l+w*.36);x2=int(l+w*.64);end1=int(l+w*.25);end2=int(l+w*.75)
 shell(f'uinput -T -m {x1} {y} {end1} {y} {x2} {y} {end2} {y} 700')
 enlarged=percent();assert 170<=enlarged<=190, f'Pinch did not enlarge: {enlarged}'
 shell(f'uinput -T -m {end1} {y} {x1} {y} {end2} {y} {x2} {y} 700')
 reduced=percent();assert 100<=reduced<=102, f'Pinch did not shrink: {reduced}'
 shell(f'uinput -T -m {x1} {y} {end1} {y} {x2} {y} {end2} {y} 700')
 enlarged=percent()
 shell(f'uitest uiInput swipe {int(l+w*.5)} {y} {int(l+w*.65)} {y} 600')
 assert percent()==enlarged,'Pan changed zoom level'
 shell(f'uitest uiInput doubleClick {int(l+w*.5)} {y}')
 assert percent()==enlarged,'Double tap unexpectedly changed zoom'
 click('zoomReset');assert percent()==100,'Reset button did not restore full view'
 after=codec_ids(pid);assert before==after,'Zoom recreated the decoder'
 result={'status':'PASS','single_tap_ignored':True,'double_tap_toggles_toolbar':True,'button_zoom_percent':125,'pinch_zoom_percent':enlarged,'pinch_shrink_percent':reduced,'double_tap_preserves_zoom':True,'button_reset_percent':100,'same_decoder':True,'codec_instances':after}
 out=ROOT/'docs/validation/quality';out.mkdir(parents=True,exist_ok=True)
 (out/'zoom-qa.json').write_text(json.dumps(result,indent=2)+'\n')
 (out/'zoom-gestures.log').write_text(shell('hilog -x -P '+pid+' -T QiScreenView'))
 print(json.dumps(result))
if __name__=='__main__':main()
