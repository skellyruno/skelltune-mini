#!/usr/bin/env python3
"""Create an offline, locally playable three-algorithm A/B page."""
from pathlib import Path
import html
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: generate_ab_index.py output_folder")
folder = Path(sys.argv[1])
modes = ("REGULAR", "TESTTUNE", "META")
speeds = (0, 10, 25)
for mode in modes:
    for speed in speeds:
        if not (folder / f"{mode}_{speed}ms.wav").is_file():
            raise SystemExit(f"missing {mode} {speed}ms real-singer render")
if not (folder / "DRY_real_voice.wav").is_file():
    raise SystemExit("missing original real-singer take")
parts = [
    "<!doctype html><html lang='en'><meta charset='utf-8'>",
    "<meta name='viewport' content='width=device-width,initial-scale=1'>",
    "<title>SkellyTune Mini - three-mode real-singer A/B</title>",
    "<style>body{background:#0c120f;color:#e7f3e9;font:15px system-ui;"
    "max-width:1000px;margin:0 auto;padding:22px}h1,h2{color:#9dff83}"
    "p,small{color:#adc2af}.cols{display:grid;grid-template-columns:"
    "repeat(auto-fit,minmax(230px,1fr));gap:12px}"
    ".tile{border:1px solid #304738;border-radius:12px;padding:15px;background:#131b15}"
    "audio{width:100%}strong{display:block;margin-bottom:10px}"
    "section{margin:24px 0}hr{border:0;border-top:1px solid #304738}</style>",
    "<h1>SkellyTune Mini — three-mode real-vocal listening test</h1>",
    "<p>All renders use the same eight-second repository singer take "
    "(benchmark/v33/raw_levelmatched_20-28.wav), G major, A4 440Hz, "
    "100% wet and correction. Compare at identical time ranges. "
    "This does not certify META latency or quality.</p>",
    "<section class='cols'>",
    "<div class='tile'><strong>Original dry recording</strong>"
    "<audio controls preload='metadata' src='DRY_real_voice.wav'></audio></div>",
    "<div class='tile'><strong>Existing Auto-Tune Pro reference</strong>"
    "<audio controls preload='metadata' src='REFERENCE_AutoTunePro.wav'></audio></div>",
    "</section>",
]
for speed in speeds:
    parts.append(f"<section><h2>{speed} ms retune — same input</h2><div class='cols'>")
    for mode in modes:
        filename=f"{mode}_{speed}ms.wav"
        parts.extend([
            "<div class='tile'>",
            f"<strong>{html.escape(mode)}</strong>",
            f"<audio controls preload='none' src='{html.escape(filename)}'></audio>",
            "</div>",
        ])
    parts.append("</div></section>")
parts.extend([
    "<hr><small>Float32 WAVs preserve output headroom. Different algorithms "
    "have different source ages and actual latency; play in a DAW for "
    "time-aligned comparisons, adjust listening loudness fairly and "
    "choose based on your own vocal recordings. META remains test-only.</small>",
    "</html>",
])
(folder / "index.html").write_text("\n".join(parts), encoding="utf-8")
(folder / "README.txt").write_text(
    "REAL singer fixture 20-28 seconds, G major, A4=440, amount/wet=100%.\n"
    "REGULAR, TESTTUNE and META each processed at 0, 10 and 25ms using "
    "the same 64-sample offline renderer. Research A/B only, not a release.\n"
    "META latency/PDC remains unverified. Compare playback loudness fairly.\n",
    encoding="utf-8",
)
print("PASS: real-singer three-mode A/B index and nine track references")
