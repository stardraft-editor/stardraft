# Find and run stardraft-cli: STARDRAFT_CLI, else this checkout's build (build/Release), else the PATH.
import json, os, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))


def find_cli():
    if os.environ.get('STARDRAFT_CLI'): return os.environ['STARDRAFT_CLI']
    repo = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..'))
    for p in (os.path.join(repo, 'build', 'Release', 'stardraft-cli.exe'), os.path.join(repo, 'build', 'stardraft-cli')):
        if os.path.exists(p): return p
    return shutil.which('stardraft-cli') or 'stardraft-cli'


CLI = find_cli()


def cli(*args, ok=True):
    """Run stardraft-cli; its JSON answer. With ok, an answer with "ok": false stops the script."""
    r = subprocess.run([CLI, *args], capture_output=True, text=True, encoding='utf-8')
    if not r.stdout.strip(): raise SystemExit(f'{args[0]}: no answer (exit code {r.returncode}) {r.stderr.strip()[:300]}')
    out = json.loads(r.stdout)
    if ok and not out.get('ok'): raise SystemExit(f'{args[0]}: {out.get("error")}')
    return out
