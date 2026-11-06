#!/usr/bin/env python3
"""Causal AM-envelope time delay: a measurable behavior, not exact pitch-shifted
waveform group delay. Reports local cross-correlation peak lag and window spread."""
from scipy.ndimage import gaussian_filter1d
from pathlib import Path
import numpy as np,csv
root=Path(__file__).resolve().parents[1]/'results/v25'
fs=48000
rows=[]
def load(path):return np.fromfile(path,dtype=np.float32).astype(np.float64)
def envelope(data):
    # 20-ms RMS smooth suppresses most pitch-cycle carrier fluctuations.
    smooth=gaussian_filter1d(data*data,fs*.012)
    return np.sqrt(np.maximum(0,smooth))[::48]
def estimate_lag(dry,wet,start,stop):
    dry=dry[start:stop]
    # Normalize locally, for level-invariant comparisons between wet modes.
    best=(-np.inf,0)
    for lag in range(0,81):
        segment=wet[start+lag:stop+lag]
        if len(segment)!=len(dry):continue
        x=dry-np.mean(dry);y=segment-np.mean(segment)
        denom=np.linalg.norm(x)*np.linalg.norm(y)
        corr=float(np.dot(x,y)/denom) if denom>1e-10 else 0.0
        if corr>best[0]:best=(corr,lag)
    return best
for f0 in (95,225):
    dry=envelope(load(root/f'envelope_{f0}_dry.f32'))
    for pipeline in ('isolated','integrated'):
      for nominal in (0,40,50):
        suffix=f'mode{nominal}' if pipeline=='isolated' else f'integrated{nominal}'
        wet=envelope(load(root/f'envelope_{f0}_{suffix}.f32'))
        metrics=[estimate_lag(dry,wet,start,start+850) for start in (400,1250,2100,2950)]
        rows.append({'pipeline':pipeline,'f0_Hz':f0,'mode':'Unclocked' if nominal==0 else f'{nominal} ms Studio',
                     'declared_timeline_ms':nominal,'median_envelope_lag_ms':np.median([lag for _,lag in metrics]),
                     'min_envelope_lag_ms':min(lag for _,lag in metrics),
                     'max_envelope_lag_ms':max(lag for _,lag in metrics),
                     'mean_cross_correlation':round(float(np.mean([c for c,_ in metrics])),4)})
with open(root/'voiced_envelope_timing.csv','w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
for row in rows:print(row)
