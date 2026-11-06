#!/usr/bin/env python3
"""Reproduce the G-major note-centering measure on the supplied vocal.
Requires numpy, scipy, librosa and soundfile. Usage:
 python benchmark/v31/evaluate.py raw.wav xpitch.wav waves.wav autotune.wav skelly_baseline.wav skelly_candidate.wav
Frame selection comes from the original raw vocal and is stored in evaluation_frame_mask.npz.
The original benchmark includes 959/960 frames for SkellyTune due to the independent pitch estimator's F0 validity.
"""
import pathlib,sys,numpy as np,soundfile as sf,scipy.signal as signal,librosa
if len(sys.argv)!=7: raise SystemExit(__doc__)
z=np.load(pathlib.Path(__file__).with_name('evaluation_frame_mask.npz'))
t=z['times'];mask=z['raw_mask'];pcs=np.array([0,2,4,6,7,9,11])
for name,path in zip(['Raw','XPitch','Waves','Auto-Tune','Skelly baseline','Skelly candidate'],sys.argv[1:]):
    y,sr=sf.read(path,always_2d=True,dtype='float32')
    d=signal.resample_poly(y.mean(axis=1),1,4)
    hz=librosa.yin(d,sr=sr/4,frame_length=2048,hop_length=256,fmin=65,fmax=850,trough_threshold=.12)
    hz=np.interp(t,librosa.times_like(hz,sr=sr/4,hop_length=256),hz)
    midi=69+12*np.log2(np.maximum(1,hz)/440)
    diff=midi[:,None]-pcs[None,:]
    cents=np.min(np.abs(diff-12*np.round(diff/12)),axis=1)*100
    ok=mask&(hz>65)&(hz<850)
    print(name,'frames',int(ok.sum()),'median cents',round(float(np.median(cents[ok])),3),
          'within25 %',round(100*float(np.mean(cents[ok]<25)),3))
