#!/usr/bin/env python3
# Real-editor persistence test. All maps/settings written by the test stay under --out.
from pathlib import Path
import subprocess,os,json,shutil,re,hashlib
import argparse,sys
parser=argparse.ArgumentParser(description="Check per-source symmetry settings across editor restarts; requires a Twilight map and local game data")
for arg in ("exe","install","map","out"):parser.add_argument("--"+arg,required=True)
args=parser.parse_args()
out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
if any(out.iterdir()):parser.error("--out must be an empty directory")
source=Path(args.map).resolve()
a=out/'Source profile A.scx';b=out/'Source profile B.scx'
for p in [a,b]:
 if not p.exists():shutil.copyfile(source,p)
settings=out/'settings.json'
if not settings.exists():settings.write_text('{}',encoding='utf-8')
exe=Path(args.exe).resolve();game=str(Path(args.install).resolve());env=dict(os.environ,STARDRAFT_FONT='noto')
if sys.platform!='darwin':env.setdefault('SDL_VIDEODRIVER','offscreen')
initial={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [a,b]}
def run(name,mapfile,script,settingsfile=settings):
 path=out/(name+'.script');path.write_text(script,encoding='utf-8')
 cmd=[str(exe),str(mapfile),'--install',game,'--size','1600x1000','--scale','1','--settings',str(settingsfile),'--snapshot',str(out/(name+'.png')),'--script',str(path)]
 proc=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90);text=proc.stdout.decode('utf-8','replace');(out/(name+'.log')).write_text(text,encoding='utf-8')
 assert proc.returncode==0 and '(WRONG)' not in text,(name,proc.returncode,text[-1000:])
 print(name,flush=True)
 for line in text.splitlines():
  if 'symmetry-options: source=' in line:print(line,flush=True)
 return text
setup='history\ncommand mode.terrain\nkey Alt+0\nkey Alt+3\nkey Alt+4\nkey 2\ncommand mode.symmetry.options\nwait 200\nclick 433,145\nclick 135,384\nclick 433,145\nclick 225,340\nclick 200,713\nwait 200\nclick 135,200\nclick 200,668\nwait 600\npanel save\nhistory\n'
t=run('configure',a,setup)
assert re.search(r'source=3 saved=0 default=-1 choices=0',t), 'new source carried previous overrides'
assert re.search(r'source=2 saved=1 default=-1 choices=2',t), 'first source was not restored'
history=re.findall(r'history: (\d+) done, (\d+) to redo, map ([0-9a-f]+)',t);assert len(history)==2 and history[0]==history[1]
reopen='history\ncommand mode.terrain\nkey 2\ncommand mode.symmetry.options\nwait 200\nclick 200,713\nwait 200\nclick 200,668\nwait 200\nhistory\n'
t=run('reopen',a,reopen)
assert re.search(r'source=2 saved=1 default=-1 choices=2',t),'first source not restored from disk'
assert re.search(r'source=3 saved=1 default=1 choices=0',t),'second source not restored from disk'
t=run('separate-map',b,'command mode.terrain\nkey 2\ncommand mode.symmetry.options\nwait 200\n')
assert re.search(r'source=2 saved=0 default=-1 choices=0',t),'profiles leaked to another map'
mapfiles=list((out/'Maps').glob('*/map.json'));saved=next(p for p in mapfiles if 'Source profile A' in str(p))
loaded=json.loads(saved.read_text(encoding='utf-8'));side=loaded['SidePanels'];assert len([x for x in side if x.startswith('terrain-copy\t')])==2
legacy=out/'legacy';legacy.mkdir(exist_ok=True);legacysettings=legacy/'settings.json';legacysettings.write_text('{}')
oldfile=legacy/'Maps'/saved.parent.name/'map.json';oldfile.parent.mkdir(parents=True,exist_ok=True)
loaded['SidePanels']=[line for line in side if not line.startswith('terrain-copy\t')];oldfile.write_text(json.dumps(loaded),encoding='utf-8')
t=run('legacy-migration',a,reopen,legacysettings)
assert re.search(r'source=2 saved=1 default=-1 choices=2',t),'legacy setup not kept for last selected source'
assert re.search(r'source=3 saved=0 default=-1 choices=0',t),'legacy setup leaked to unconfigured source'
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==initial[p.name] for p in [a,b]),'map archives changed'
(out/'RESULT.json').write_text(json.dumps({'passed':True,'cases':['configure','reopen','separate-map','legacy-migration'],'mapArchivesUnchanged':True,'savedSettings':str(saved)},indent=2)+'\n')
print('PASS: source profiles persist across processes; maps isolated; legacy migrated; source maps unchanged.',flush=True)
