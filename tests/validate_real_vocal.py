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



def write_float_wav(path, rate, channels, samples):
    import array
    pcm = array.array("f", samples)
    if sys.byteorder != "little":
        pcm.byteswap()
    payload = pcm.tobytes()
    with Path(path).open("wb") as fh:
        fh.write(struct.pack("<4sI4s4sIHHIIHH4sI",
              b"RIFF", 36 + len(payload), b"WAVE", b"fmt ", 16,
              3, channels, rate, rate * channels * 4, channels * 4, 32,
              b"data", len(payload)))
        fh.write(payload)


def run_gain_scale(source, destination, factor):
    fs, channels, samples = read_float_wav(source)
    if not 0.5 < factor < 1.5:
        raise ValueError("test gain out of safe range")
    peak = max(abs(s) for s in samples)
    if peak * factor >= 0.98:
        raise ValueError("scaled test would approach full-scale input")
    write_float_wav(destination, fs, channels, (x * factor for x in samples))
    print(f"Scaled real-singer input by {factor:.3f}x; peak={peak*factor:.5f}")


def compare_gain_normalized(baseline, louder, factor, report_only=False):
    fs, channels, low = read_float_wav(baseline)
    new_fs, new_channels, high = read_float_wav(louder)
    if (fs, channels, len(low)) != (new_fs, new_channels, len(high)):
        raise ValueError("real-vocal comparison has different timing/channels")
    start = int(fs * 0.3) * channels
    signal = 1.e-12
    difference = 0.0
    maxstep = 0.0
    for i in range(start, len(low)):
        target = low[i]
        actual = high[i] / factor
        signal += target * target
        difference += (target - actual) ** 2
        maxstep = max(maxstep, abs(target - actual))
    relative = math.sqrt(difference / signal)
    # Pitch-synchronous read heads may choose a neighboring source cycle:
    # a sub-millisecond phase difference inflates samplewise RMS error even
    # when the rendered vocal has nearly identical audible level.
    frame = max(channels, int(fs * 0.02) * channels)
    envelope_error = 0.0
    envelope_energy = 1.0e-12
    for pos in range(start, len(low), frame):
        lo = low[pos:pos + frame]
        hi = high[pos:pos + frame]
        if not lo:
            continue
        e_lo = math.sqrt(sum(float(x) ** 2 for x in lo) / len(lo))
        e_hi = math.sqrt(sum((float(x) / factor) ** 2 for x in hi) / len(hi))
        envelope_error += (e_lo - e_hi) ** 2 * len(lo)
        envelope_energy += e_lo ** 2 * len(lo)
    envelope_mismatch = math.sqrt(envelope_error / envelope_energy)
    print(f"Real-vocal 20-ms envelope mismatch={100*envelope_mismatch:.3f}%")
    print(f"Real-singer loudness A/B: normalized RMS mismatch={100*relative:.3f}% "
          f"maximum per-sample difference={maxstep:.5f} "
          f"gain={factor:.3f}x")
    # Cycle-resynthesis can select a neighboring source period, shifting
    # waveform phase without changing the audible level or note target.
    # Test real-singer short-time envelopes here, and target decisions via
    # a separate pitch-trace comparison instead of a raw sample RMSE gate.
    if envelope_mismatch > 0.05 and not report_only:
        raise ValueError("real-vocal gain scaling changed short-time loudness")

def compare_traces(low_file, high_file):
    import csv
    with Path(low_file).open() as f:
        low=list(csv.DictReader(f))
    with Path(high_file).open() as f:
        high=list(csv.DictReader(f))
    if len(low) != len(high) or not low:
        raise ValueError("trace lengths differ")
    pitches=0
    target_changes=0
    pending_changes=0
    mixed_wet_difference=0.0
    pitch_divergence=0.0
    for left,right in zip(low,high):
        if int(left["target"]) != int(right["target"]):
            target_changes += 1
        if int(left["pending"]) != int(right["pending"]):
            pending_changes += 1
        mixed_wet_difference += abs(float(left["mix"]) - float(right["mix"]))
        if float(left["f0"]) > 65 and float(right["f0"]) > 65:
            pitches+=1
            pitch_divergence += abs(1200*math.log2(float(left["f0"])/float(right["f0"])))
    average_wet = mixed_wet_difference / len(low)
    average_pitch_cents = pitch_divergence / max(pitches, 1)
    print(f"Detector A/B: target mismatched frames={target_changes}/{len(low)} "
          f"pending mismatch={pending_changes} "
          f"average wet difference={average_wet:.4f} "
          f"average pitch drift={average_pitch_cents:.3f} cents")
    if (target_changes > max(4, len(low) * 0.02)
            or pending_changes > max(4, len(low) * 0.04)
            or average_wet > 0.04 or average_pitch_cents > 6.0):
        raise ValueError("higher vocal level caused an unintended note/gate change")

def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--traces":
        compare_traces(sys.argv[2],sys.argv[3])
        return
    if len(sys.argv) == 5 and sys.argv[1] == "--scale":
        run_gain_scale(sys.argv[2], sys.argv[3], float(sys.argv[4]))
        return
    if len(sys.argv) == 5 and sys.argv[1] in ("--compare", "--report"):
        compare_gain_normalized(sys.argv[2], sys.argv[3], float(sys.argv[4]),
                                report_only=sys.argv[1] == "--report")
        return
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
