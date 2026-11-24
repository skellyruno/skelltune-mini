#!/usr/bin/env python3
"""Recover exactly the pinned Snap DSP header from its verified source overlay.
No UI assets, ZIP-installed plugins, or alternative/placeholder DSP are imported."""
import base64
import hashlib
import io
from pathlib import Path
import sys
import tarfile

def digest(data):
    return hashlib.sha256(data).hexdigest()

def recover(root: Path, target: Path):
    parts = []
    for i in range(1, 9):
        if i in (6, 8):
            raw = (root / "source-overlay/parts-b64" / f"part-{i:02d}.b64").read_bytes()
            expected = {6: "4f990f29c3c90fa4a3cf642d2b66df6bf8d124218cee92bfe73568e7a532a4c3",
                        8: "cdd519f9ef0955a06cd939e161006f3d76b2c392f5d808387c00ec80714a72dc"}[i]
            if digest(raw) != expected:
                raise ValueError("Snap base64 fragment checksum mismatch")
            part = base64.b64decode(raw)
        else:
            part = (root / "source-overlay/parts" / f"part-{i:02d}.bin").read_bytes()
        parts.append(part)
    archive = b"".join(parts)
    if len(archive) != 39943 or digest(archive) != "ce1f4c42e89d82d10241a97f0ccb002abc8cd6e88a9907fd8f6012c67a9d5a9f":
        raise ValueError("Pinned Snap overlay checksum mismatch")
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        matches = [m for m in tar.getmembers() if m.isfile() and m.name.lstrip("./") == "Source/dsp/SnapCore.h"]
        if len(matches) != 1:
            raise ValueError("Missing unique SnapCore header")
        data = tar.extractfile(matches[0]).read()
    if digest(data) != "9bef538b0df377df49720bcd0fe8ae7c98f9d0a166e952ddc0023e430c3d9736":
        raise ValueError("Pinned native SnapCore source checksum mismatch")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    print("Recovered exact verified Classic/Crystal DSP:", target)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: recover_snap_core.py SNAP_CHECKOUT OUTPUT_HEADER")
    recover(Path(sys.argv[1]), Path(sys.argv[2]))
