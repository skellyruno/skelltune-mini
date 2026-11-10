#!/usr/bin/env python3
"""Zero-dependency real-vocal WAV regression checks (no perceptual-quality claim)."""
import array
import math
import struct
import sys
from pathlib import Path


def read_float_wav(path):
    with Path(path).open("rb") as fh:
        header = fh.read(12)
        if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
            raise ValueError(f"{path}: not RIFF WAVE")
        fmt = None
        data = None
        while True:
            chunk = fh.read(8)
            if not chunk:
                break
            if len(chunk) != 8:
                raise ValueError("partial WAV chunk header")
            tag, size = struct.unpack("<4sI", chunk)
            if size > 64 * 1024 * 1024:
                raise ValueError("oversized WAV chunk")
            payload = fh.read(size)
            if len(payload) != size:
                raise ValueError("partial WAV chunk data")
            if size & 1:
                fh.read(1)
            if tag == b"fmt ":
                if len(payload) < 16:
                    raise ValueError("short format chunk")
                codec, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", payload)
                fmt = (codec, channels, rate, bits)
            if tag == b"data":
                data = payload
        if fmt is None or data is None:
            raise ValueError("missing format/data chunk")
        codec, channels, rate, bits = fmt
        if codec != 3 or bits != 32 or channels not in (1, 2) or len(data) % (4 * channels):
            raise ValueError(f"expected mono/stereo IEEE float32 WAV: {fmt}")
        samples = array.array("f")
        samples.frombytes(data)
        if sys.byteorder != "little":
            samples.byteswap()
        return rate, channels, samples


def measure(samples, channels):
    energy = 0.0
    peak = 0.0
    step = 0.0
    over = 0
    previous = [0.0] * channels
    for i, sample in enumerate(samples):
        if not math.isfinite(sample):
            raise ValueError(f"nonfinite output at sample {i}")
        magnitude = abs(sample)
        energy += sample * sample
        peak = max(peak, magnitude)
        if magnitude >= 1.0:
            over += 1
        if i >= channels:
            step = max(step, abs(sample - previous[i % channels]))
        previous[i % channels] = sample
    return math.sqrt(energy / len(samples)), peak, step, over


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: validate_real_vocal.py original.wav rendered.wav")
    rate_a, channels_a, input_audio = read_float_wav(sys.argv[1])
    rate_b, channels_b, output_audio = read_float_wav(sys.argv[2])
    if rate_a != rate_b or channels_a != channels_b or len(input_audio) != len(output_audio):
        raise ValueError("renderer changed sample count, rate or channel count")
    if len(input_audio) < rate_a * channels_a * 5:
        raise ValueError("real-vocal fixture too short")
    dry = measure(input_audio, channels_a)
    wet = measure(output_audio, channels_a)
    ratio = wet[0] / max(dry[0], 1.0e-9)
    if not 0.25 < ratio < 4.0:
        raise ValueError(f"unreasonable real-vocal RMS gain change: {ratio:.3f}")
    if wet[1] > max(1.25, 1.8 * dry[1]):
        raise ValueError(f"unreasonable waveform output peak: {wet[1]:.4f}")
    if wet[2] > max(1.0, 2.5 * dry[2]):
        raise ValueError(f"possible gross waveform discontinuity: {wet[2]:.4f}")
    print(f"real vocal: rate={rate_a}, channels={channels_a}, "
          f"duration={len(input_audio)/(rate_a*channels_a):.2f}s")
    print(f"dry rms={dry[0]:.5f} peak={dry[1]:.5f} step={dry[2]:.5f}")
    print(f"wet rms={wet[0]:.5f} peak={wet[1]:.5f} step={wet[2]:.5f} "
          f"over-0dBFS-float={wet[3]} gain={ratio:.3f}x")
    print("PASS: finite, aligned, bounded real-vocal render; "
          "output may exceed 0 dBFS in floating point and must be gain-staged.")


if __name__ == "__main__":
    main()
