# Copyright (c) 2026 Aritra Saha. All rights reserved.
from pathlib import Path
import serial,time,re,json,hashlib
import argparse
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description='Verify a running Remote WSL Wokwi demo over its optional RFC2217 bridge.')
parser.add_argument('--url',default='rfc2217://127.0.0.1:4000')
parser.add_argument('--output-dir',type=Path,default=root/'.pio/wokwi-runtime')
args=parser.parse_args()
work=args.output_dir.resolve(); work.mkdir(parents=True,exist_ok=True)
results=[]; start=time.monotonic()
log=(work/'serial.log').open('w',buffering=1)
ser=serial.serial_for_url(args.url,baudrate=115200,timeout=.25)
def wait(predicate,label,timeout=55):
 deadline=time.monotonic()+timeout
 while time.monotonic()<deadline:
  line=ser.readline().decode('utf-8',errors='replace').strip()
  if not line:continue
  log.write(line+'\n')
  if predicate(line):
   results.append({'check':label,'passed':True,'observed':line,'wall_seconds':round(time.monotonic()-start,2)})
   (work/'wokwi-results.json').write_text(json.dumps(results,indent=2)+'\n')
   print('PASS',label,flush=True);return line
 raise RuntimeError('Timeout waiting for '+label)
def contains(text,label=None,timeout=55):return wait(lambda s:text in s,label or text,timeout)
def command(text):
 log.write('[TEST COMMAND] '+text+'\n');ser.write((text+'\n').encode())
def app(rpm=None,fresh=1,alert=None):
 def pred(line):
  if not line.startswith('APP '):return False
  fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
  return fields.get('fresh')==str(fresh) and (rpm is None or fields.get('rpm')==str(rpm)) and (alert is None or fields.get('alert')==str(alert))
 return pred
try:
 contains('WRONG_DLC','automatic wrong-DLC rejection')
 contains('OUT_OF_RANGE','automatic above-range rejection')
 wait(app(fresh=0),'automatic stale indication during silence')
 wait(app(fresh=1),'automatic recovery after silence')
 command('rpm nope');contains('[ERROR] RPM must be','reject malformed numeric RPM',12)
 command('brightness 20junk');contains('[ERROR] Brightness must be','reject trailing numeric junk',12)
 command('rpm 6000');contains('[SYNTHETIC MANUAL RPM] 6000',timeout=12)
 wait(app(6000,1,1),'manual full-frame RPM reaches validated shift alert',12)
 command('demo stop');contains('Synthetic reception stopped',timeout=12)
 wait(app(fresh=0),'manual replay stops and becomes stale',12)
 command('rpm 4200');contains('[SYNTHETIC MANUAL RPM] 4200',timeout=12)
 wait(app(4200,1,0),'valid manual frames resume after pause',12)
 command('rpm 6501');contains('[SYNTHETIC MANUAL RPM] 6501',timeout=12)
 contains('OUT_OF_RANGE','manual above-range frames reach decoder',12)
 wait(app(fresh=0),'above-range commands cannot keep RPM fresh',12)
 command('rpm 0');contains('[SYNTHETIC MANUAL RPM] 0 ',timeout=12)
 wait(app(0,1,0),'zero RPM remains valid',12)
 command('rpm 4200'+' '*90);contains('Command discarded','overlong command rejected without executing prefix',12)
 command('status');contains('RPM=0 ','parser recovers with unchanged RPM after overlong input',12)
 command('profile racing');contains('Style set to RACING','runtime style switch',12)
 command('brightness 100');contains('Brightness set to 100','runtime brightness change',12)
 command('rpm 4200');contains('[SYNTHETIC MANUAL RPM] 4200',timeout=12)
 wait(app(4200,1,0),'prepared stable progressive LED view',12)
 result={'status':'passed','checks':results,'elf_sha256':hashlib.sha256((root/'.pio/build/esp32dev_wokwi/firmware.elf').read_bytes()).hexdigest(),'transport':'Remote WSL Wokwi serial forwarding; full-frame replay, not TWAI','log':str(work/'serial.log')}
 (work/'wokwi-results.json').write_text(json.dumps(result,indent=2)+'\n')
 print('WOKWI RUNTIME PASS',len(results),'checks',flush=True)
except Exception as e:
 (work/'wokwi-results.json').write_text(json.dumps({'status':'failed','error':str(e),'checks':results},indent=2)+'\n')
 raise
finally:ser.close();log.close()
