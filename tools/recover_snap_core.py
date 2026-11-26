#!/usr/bin/env python3
"""Recover exactly the pinned Snap DSP header from its verified source overlay.
No UI assets, ZIP-installed plugins, or alternative/placeholder DSP are imported."""
import base64
import hashlib
import io
from pathlib import Path
import sys
import tarfile
import subprocess
import tempfile

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
    # The audition PR uses the same overlay plus a separately verified
    # DSP-only patch. Recover the exact user-tested engine BEFORE our change.
    patch = (root / "audition/skell-snap-dsp-only.patch").read_bytes().replace(b"\r\n", b"\n")
    if digest(patch) != "efa900f00f5f689b6f43f93e75e598d15917fc7f390201589e86ff6b006ed768":
        raise ValueError("Pinned Snap audition patch checksum mismatch")
    with tempfile.TemporaryDirectory(prefix="snap-audition-") as directory:
        work = Path(directory)
        core = work / "Source/dsp/SnapCore.h"
        core.parent.mkdir(parents=True)
        core.write_bytes(data)
        patchfile = work / "audition.patch"
        patchfile.write_bytes(patch)
        command = ["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf", "apply"]
        subprocess.run(command + ["--check", str(patchfile)], cwd=work, check=True)
        subprocess.run(command + [str(patchfile)], cwd=work, check=True)
        data = core.read_bytes()
    if digest(data) != "2be1e3ebf58a2569756de678e58470d78cd101c8a66324617388e69e306a7541":
        raise ValueError("User-tested audition header checksum mismatch")
    # Keep the original audition baseline intact next to the derived header.
    # Only the Playground copy receives the existing Mini stereo analysis
    # router, never the standalone Snap source or its program output lanes.
    target.parent.mkdir(parents=True, exist_ok=True)
    target.with_name("SnapCore.audition.h").write_bytes(data)
    source = data.decode("utf-8")
    replacements = [
        ('#include <vector>', '#include <vector>\n#include "RealtimeTuner.hpp" // Playground detector-only stereo rescue'),
        ('        det_.prepare(sr_);', '        stereoRouter_.prepare(sr_);\n        det_.prepare(sr_);'),
        ('        det_.reset(); detB_.reset();\n        std::fill(fmono_', '        stereoRouter_.reset();\n        det_.reset(); detB_.reset();\n        std::fill(fmono_'),
        ('            mono_[wi] = m * invN;', '            // Keep stereo output untouched; rescue only the shared analysis/splice reference.\n            mono_[wi] = nch == 2 ? stereoRouter_.push(ring_[0][wi], ring_[1][wi]) : m * invN;'),
        ('    std::vector<float> ring_[2], mono_;', '    skelly::StereoPitchRouter stereoRouter_;\n    std::vector<float> ring_[2], mono_;'),
    ]
    for before, after in replacements:
        if source.count(before) != 1:
            raise ValueError("Unexpected audition stereo patch anchor")
        source = source.replace(before, after, 1)
    updated = source.encode("utf-8")
    if digest(updated) != "ca46212c500efe397968a2ef9869050f26b0ea015b5f462fd8ac99ffb6b3672b":
        raise ValueError("Playground-derived Snap header checksum mismatch")
    target.write_bytes(updated)
    print("Recovered exact audition baseline and verified Playground stereo rescue:", target)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: recover_snap_core.py SNAP_CHECKOUT OUTPUT_HEADER")
    recover(Path(sys.argv[1]), Path(sys.argv[2]))
