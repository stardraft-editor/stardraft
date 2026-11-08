#!/usr/bin/env python3
"""Check saved overlay preferences in isolated, read-only editor snapshots."""
from pathlib import Path
import argparse,subprocess,os,hashlib,re,json
parser=argparse.ArgumentParser(description=__doc__)
for key in ('exe','install','map','out'):parser.add_argument('--'+key,required=True)
a=parser.parse_args();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
if any(out.iterdir()):parser.error('--out must be empty')
settings=out/'settings.json'
source=Path(a.map).resolve();before=hashlib.sha256(source.read_bytes()).hexdigest()
def run(name,script,dialog=False):
    path=out/(name+'.script');path.write_text(script,encoding='utf-8')
    command=[str(Path(a.exe).resolve()),str(source),'--install',str(Path(a.install).resolve()),'--settings',str(settings),
             '--snapshot',str(out/(name+'.png')),'--script',str(path)]
    if dialog:command+=['--dialog','region-options','--size','320x112']
    p=subprocess.run(command,env=dict(os.environ,SDL_VIDEODRIVER='offscreen',STARDRAFT_FONT='noto'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
    text=p.stdout.decode('utf-8','replace');(out/(name+'.log')).write_text(text,encoding='utf-8')
    assert p.returncode==0 and '(WRONG)' not in text,(name,text[-1500:])
    return re.findall(r'region-overlay: visible=(\d) areas=(\d) opacity=(\d+) ready=(\d)',text)
cases=[('defaults',{},('0','0','35','0')),
       ('visible',{'Settings/ShowPathing':1,'Settings/RegionOpacity':20},('1','0','20','1')),
       ('hidden',{'Settings/ShowPathing':0,'Settings/RegionOpacity':20},('0','0','20','0')),
       ('negative',{'Settings/ShowPathing':1,'Settings/RegionOpacity':-20},('1','0','0','1')),
       ('large',{'Settings/ShowPathing':1,'Settings/RegionOpacity':120},('1','0','100','1')),
       ('areas',{'Settings/ShowPathing':1,'Settings/PathingAreas':1,'Settings/RegionOpacity':20},('1','1','20','0'))]
for name,values,expected in cases:
    settings.write_text(json.dumps(values),encoding='utf-8');saved=settings.read_bytes()
    records=run(name,'regions\n');assert records==[expected],(name,records)
    assert settings.read_bytes()==saved,'snapshot changed saved preferences'
    print(name,'PASS',flush=True)
assert before==hashlib.sha256(source.read_bytes()).hexdigest(),'source archive changed'
(out/'RESULT.json').write_text(json.dumps({'passed':True,'cases':[c[0] for c in cases],'mapUnchanged':True,'preferencesUnchanged':True},indent=2)+'\n')
print('PASS: saved visibility/mode/opacity load in new processes; range clamped; snapshots and source archive unchanged.')
