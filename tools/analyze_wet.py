#!/usr/bin/env python3
"""Offline diagnostics. Do not treat source-age as plugin transport latency."""
import sys, numpy as np
from scipy import signal, ndimage
from scipy.io import wavfile
from pathlib import Path
base=Path(sys.argv[1]) if len(sys.argv)>1 else Path('results/v19')
for prefix in sorted({p.name[:-11] for p in base.glob('*_source.f32')}):
    files={v:np.fromfile(base/f'{prefix}_{v}.f32',np.float32).astype(np.float64) for v in ('source','old','new')}
    fs=48000
    # Analysis uses stationary segments, and smooths across harmonic gaps.
    means={}; envs={}
    for v,x in files.items():
        f,p=signal.welch(x[fs//2:-fs//4],fs,nperseg=8192,noverlap=4096,scaling='spectrum')
        log=10*np.log10(np.maximum(p,1e-14))
        envelope=ndimage.gaussian_filter1d(log,85/(f[1]-f[0]))
        envs[v]=envelope
        peaks=[]
        for low,high in ((350,900),(900,1700),(1900,3000)):
            inds=np.flatnonzero((f>=low)&(f<=high))
            peaks.append(float(f[inds[np.argmax(envelope[inds])]]))
        means[v]=peaks
    print('\n',prefix,'formant proxy peak centers in Hz:')
    for v in ('source','old','new'): print(v,means[v])
    ref=envs['source']
    domain=(f>250)&(f<3400)
    for v in ('old','new'):
        d=envs[v][domain]-ref[domain]
        d=d-np.mean(d)
        err=np.sqrt(np.mean(d**2))
        x=files[v][fs//2:-fs//4]
        sp=np.sqrt(np.mean(x*x))
        maxjump=np.max(np.abs(np.diff(x)))/(sp+1e-9)
        print(v, 'spectral_envelope_log_error_db=',round(float(err),3),'max_adjacent_delta_over_rms=',round(float(maxjump),3))
    src,old,new=files['source'],files['old'],files['new']
    for nm,s in [('source',src),('v18',old),('v19',new)]:
        wavfile.write(base/f'{prefix}_{nm}.wav',fs, np.int16(np.clip(s,-1,1)*32767))
