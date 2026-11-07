#!/usr/bin/env python3
"""Verify the extracted engine text, allowing Windows checkout line endings."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
manifest = json.loads((root / "SOURCE.json").read_text(encoding="utf-8"))
failed = []
for name, expected in manifest["files"].items():
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise SystemExit("Source manifest contains a path outside the engine")
    data = path.read_bytes().replace(b"\r\n", b"\n")
    if hashlib.sha256(data).hexdigest() != expected:
        failed.append(name)
if failed:
    raise SystemExit("Engine source differs: " + ", ".join(failed))
print(f"Verified {len(manifest['files'])} engine files (LF-normalized SHA-256)")
