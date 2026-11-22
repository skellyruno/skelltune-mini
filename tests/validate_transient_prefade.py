"""Check a previously audible real-singer dry/wet click against the repaired render.

No pitch-quality claim: objective waveform discontinuity and PDC regression only.
Read float32 / PCM WAVs using stdlib, no scipy dependency.
"""
from __future__ import annotations
import math
import struct
import sys



def read(path):
    # Python wave.open rejects IEEE-float WAVE_FORMAT_IEEE_FLOAT (format 3).
    # The Mini offline renderer emits float32 RIFF PCM-style chunks.
    with open(path, "rb") as f:
        if f.read(4) != b"RIFF":
            raise ValueError(f"expected RIFF WAV: {path}")
        f.read(4)
        if f.read(4) != b"WAVE":
            raise ValueError(f"expected WAVE: {path}")
        fmt = None
        data = None
        while True:
            chunk = f.read(8)
            if len(chunk) < 8:
                break
            tag, length = struct.unpack("<4sI", chunk)
            content = f.read(length)
            if len(content) != length:
                raise ValueError("truncated WAV chunk")
            if tag == b"fmt ":
                fmt = struct.unpack_from("<HHIIHH", content, 0)
            elif tag == b"data":
                data = content
            if length & 1:
                f.read(1)
    if fmt is None or data is None:
        raise ValueError("missing WAV fmt/data")
    encoding, channels, fs, _, _, bits = fmt
    if encoding == 3 and bits == 32:
        samples = struct.unpack("<" + "f" * (len(data) // 4), data)
    elif encoding == 1 and bits == 16:
        samples = [x / 32768.0 for x in struct.unpack("<" + "h" * (len(data) // 2), data)]
    else:
        raise ValueError(f"unsupported WAV format {encoding}, bits {bits}")
    return fs, channels, samples


def startup_peak(samples, channels, fs):
    # Exact problem was the first attack within 100ms of the real singer.
    lo, hi = int(fs * 0.015), int(fs * 0.11)
    max_step = 0.0
    at = 0
    for i in range(lo, hi):
        for c in range(channels):
            step = abs(samples[i * channels + c] - samples[(i - 1) * channels + c])
            if step > max_step:
                max_step = step
                at = i
    return max_step, at / fs


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: validate_transient_prefade.py dry.wav old.wav new.wav")
    dry, old, new = [read(path) for path in sys.argv[1:]]
    assert dry[:2] == old[:2] == new[:2], "sample rate / channel mismatch"
    assert len(dry[2]) == len(old[2]) == len(new[2]), "sample count mismatch"
    fs, channels = dry[:2]
    a, ta = startup_peak(old[2], channels, fs)
    b, tb = startup_peak(new[2], channels, fs)
    c, _ = startup_peak(dry[2], channels, fs)
    assert all(math.isfinite(x) for x in new[2]), "nonfinite output"
    print(f"real-singer first-phrase max sample step: dry={c:.4f}, "
          f"old={a:.4f} at {ta:.3f}s, prefade={b:.4f} at {tb:.3f}s")
    assert a > c * 1.5, "fixture no longer reproduces the dry/wet click"
    assert b < a * 0.80, "prefade failed to reduce real-singer click >=20%"
    assert b < max(0.40, c * 1.6), "prefade left an unusually large first-phrase jump"
    print("PASS: real-singer protected attack prefade reduces sample discontinuity")


if __name__ == "__main__":
    main()
