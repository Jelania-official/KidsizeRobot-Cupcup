#!/usr/bin/env python3
"""Download the pinned official model and verify it before conversion."""
import hashlib
import json
from pathlib import Path
import urllib.request

folder = Path(__file__).resolve().parents[1]/'models/bitbots-2026'
info = json.loads((folder/'provenance.json').read_text())
target = folder/'yoeo.onnx'
if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == info['sha256']:
    print('Verified existing model:', target)
else:
    with urllib.request.urlopen(info['source'], timeout=120) as response:
        content = response.read()
    if hashlib.sha256(content).hexdigest() != info['sha256']:
        raise RuntimeError('Downloaded model hash differs from the verified version')
    target.write_bytes(content)
    print('Downloaded and verified:', target)
