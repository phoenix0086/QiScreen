#!/usr/bin/env python3
"""Measure the receiver's own startup and RTSP readiness, excluding the macOS picker."""
import json,re,socket,statistics,subprocess,sys,time,os,hashlib
from pathlib import Path
from device_target import resolve_target
ROOT=Path(__file__).resolve().parents[1]
HDC='/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/toolchains/hdc'
OUT=ROOT/'docs/validation/optimization'
TARGET=resolve_target()
ENV=os.environ.copy();ENV['QISCREEN_DEVICE_TARGET']=TARGET
def sh(cmd):return subprocess.check_output([HDC,'-t',TARGET,'shell',cmd],text=True,timeout=12)
def action(name):
 subprocess.run([sys.executable,str(ROOT/'scripts/device_qa.py'),name],check=True,capture_output=True,text=True,timeout=35,env=ENV)
def info(ip,port):
 begin=time.perf_counter();s=socket.create_connection((ip,port),timeout=3);s.settimeout(3)
 try:
  s.sendall(b'GET /info RTSP/1.0\r\nCSeq: 1\r\nContent-Length: 0\r\n\r\n');data=b''
  while len(data)<16384:
   part=s.recv(16384)
   if not part:break
   data+=part
   if b'\r\n\r\n' in data:
    header,body=data.split(b'\r\n\r\n',1)
    lengths=[int(x.split(b':',1)[1]) for x in header.split(b'\r\n') if x.lower().startswith(b'content-length:')]
    if lengths and len(body)>=lengths[0]:break
  assert data.startswith(b'RTSP/1.0 200 OK') and b'bplist00' in data
  return round((time.perf_counter()-begin)*1000,2)
 finally:s.close()
ip=re.search(r'inet addr:([0-9.]+)',sh('ifconfig wlan0')).group(1)
rows=[]
for cycle in range(4):
 action('start')
 pid=sh('pidof com.phoenix.qiscreen').strip()
 logs=sh('hilog -x -P '+pid+' -T QiScreen,JSAPP')
 matches=re.findall(r'Receiver ready: .*port (\d+), startup (\d+) ms',logs)
 assert matches,'No startup timing for current process'
 port,ms=map(int,matches[-1])
 times=[info(ip,port) for _ in range(3)]
 row={'cycle':cycle+1,'process':pid,'kind':'after_process_restart' if cycle==3 else 'start_or_warm_restart','port':port,'startup_ms':ms,'rtsp_roundtrip_ms':times}
 rows.append(row);print(json.dumps(row),flush=True)
 if cycle<3:
  action('toggle')
  if cycle==2:sh('aa force-stop com.phoenix.qiscreen')
result={'scope':'Receiver startup and local-network RTSP capability query. Excludes mDNS discovery, pairing, media transfer and macOS user selection.','device_fingerprint':hashlib.sha256(TARGET.encode()).hexdigest()[:12],'samples':rows,'all_ports_equal':len({x['port'] for x in rows})==1,'startup_median_ms':statistics.median(x['startup_ms'] for x in rows),'rtsp_median_ms':statistics.median(t for x in rows for t in x['rtsp_roundtrip_ms'])}
OUT.mkdir(parents=True,exist_ok=True);(OUT/'device-startup-benchmark.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
assert result['all_ports_equal'],'Cached endpoint changed across restart'
print(json.dumps({k:v for k,v in result.items() if k!='samples'},ensure_ascii=False),flush=True)
