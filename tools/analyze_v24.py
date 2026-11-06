#!/usr/bin/env python3
"""Proxy envelope comparison on source and v2.3/v2.4 renders; level-invariant metrics.
Not an objective perceptual sound-quality metric and not ground-truth formants."""
from scipy.io import wavfile
from scipy.signal import welch
from scipy.ndimage import gaussian_filter1d
import numpy as np, csv
from pathlib import Path
base=Path(__file__).resolve().parents[1]/'results/v24'
rows=[]
for name in ['recorded','synthetic']:
    wav={}
    for suffix in ['source','formant_off','formant_on']:
        fs,x=wavfile.read(base/f'{name}_{suffix}.wav')
        wav[suffix]=x.astype('float64')/(2**(31 if x.dtype==np.int32 else 15))
    dry=np.pad(wav['source'],(144,0))[:len(wav['source'])]
    nwin=24000 # 0.5 seconds
    count=0;improved=0
    values={'old':[],'new':[],'old_norm':[],'new_norm':[]}
    for start in range(24000,len(dry)-nwin,12000):
        x=dry[start:start+nwin]
        o=wav['formant_off'][start:start+nwin]
        n=wav['formant_on'][start:start+nwin]
        if np.sqrt(np.mean(x*x))<0.012:continue
        # Require enough output energy to avoid using silence as an envelope.
        if min(np.sqrt(np.mean(o*o)),np.sqrt(np.mean(n*n)))<0.008:continue
        f,s=welch(x,fs=fs,nperseg=8192,noverlap=4096,scaling='spectrum')
        def env(z):
            _,p=welch(z,fs=fs,nperseg=8192,noverlap=4096,scaling='spectrum')
            y=10*np.log10(np.maximum(1e-12,p))
            return gaussian_filter1d(y,90/(f[1]-f[0]))
        reference=env(x)
        domain=(f>=350)&(f<=3100)
        baseline=reference[domain]
        baseline-=np.mean(baseline)
        for label,z in [('old',o),('new',n)]:
            test=env(z)[domain]
            test-=np.mean(test)
            error=float(np.sqrt(np.mean((test-baseline)**2)))
            values[label].append(error)
            values[label+'_norm'].append(float(np.sqrt(np.mean(z*z))))
        improved+=values['new'][-1]<values['old'][-1]
        count+=1
    print(name,'valid windows',count,'improved windows',improved,
          'spectral RMS old/new',np.mean(values['old']),np.mean(values['new']),
          'mean output RMS old/new',np.mean(values['old_norm']),np.mean(values['new_norm']))
    rows.append({'source':name,'windows':count,'improved_windows':improved,'mean_proxy_error_formant_off_db':round(float(np.mean(values['old'])),3),
        'mean_proxy_error_formant_on_db':round(float(np.mean(values['new'])),3),
        'output_rms_formant_off':round(float(np.mean(values['old_norm'])),5),'output_rms_formant_on':round(float(np.mean(values['new_norm'])),5)})
with (base/'FORMANT_AB.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
