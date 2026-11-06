#!/usr/bin/env python3
"""Score matched G-major note centering on the saved 960-frame raw-voice mask.
Usage: python benchmark/v32/evaluate.py file1.wav [file2.wav ...]
Requires numpy, scipy, librosa, soundfile. Different recordings must be aligned
to the original 30.57-second user-supplied take."""
import numpy as np,sys,pathlib,scipy.signal as ss,librosa,soundfile as sf
if len(sys.argv)<2: raise SystemExit(__doc__)
maskpath=pathlib.Path(__file__).parent.parent/'v31'/'evaluation_frame_mask.npz'
z=np.load(maskpath);times=z['times'];active=z['raw_mask'];pcs=np.array([0,2,4,6,7,9,11])
for file in sys.argv[1:]:
 audio,sr=sf.read(file,always_2d=True,dtype='float32')
 wave=ss.resample_poly(audio.mean(axis=1),1,4)
 f0=librosa.yin(wave,sr=sr/4,frame_length=2048,hop_length=256,fmin=65,fmax=850,trough_threshold=.12)
 f0=np.interp(times,librosa.times_like(f0,sr=sr/4,hop_length=256),f0)
 midi=69+12*np.log2(np.maximum(1,f0)/440)
 delta=midi[:,None]-pcs[None,:]
 cents=np.min(abs(delta-12*np.round(delta/12)),axis=1)*100
 valid=active&(f0>65)&(f0<850)
 n=int(valid.sum());hits25=int((valid&(cents<25)).sum());hits50=int((valid&(cents<50)).sum())
 print(pathlib.Path(file).name,'valid',n,'of',int(active.sum()),'median cents',round(float(np.median(cents[valid])),3),'within25',round(100*hits25/n,3),'within50',round(100*hits50/n,3),'hits25 of 960',hits25)
