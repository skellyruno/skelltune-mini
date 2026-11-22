"""Check a previously audible real-singer dry/wet click against the repaired render.

No pitch-quality claim: objective waveform discontinuity and PDC regression only.
Read float32 / PCM WAVs using stdlib, no scipy dependency.
"""
from __future__ import annotations
import math
import struct
import sys
import wave


def read(path):
    with wave.open(path, "rb") as w:
        fs, channels, size, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        data = w.readframes(n)
    if size == 4:
        samples = struct.unpack("<" + "f" * (len(data) // 4), data)
    elif size == 2:
        samples = [x / 32768.0 for x in struct.unpack("<" + "h" * (len(data) // 2), data)]
    else:
        raise ValueError(f"unsupported sample format {size} bytes")
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
