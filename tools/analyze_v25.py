#!/usr/bin/env python3
"""Reproducible level-invariant spectral and temporal metrics for A/B audio.
Not objective perceptual quality, and the formant score is a spectral proxy."""
from pathlib import Path
from scipy.ndimage import gaussian_filter1d
from scipy.signal import welch, butter, sosfilt
import numpy as np, csv
root=Path(__file__).resolve().parents[1]/'results/v25'
fs=48000
rows=[]
for source in sorted(root.glob('*_input.f32')):
    name=source.name[:-10]
    dry=np.fromfile(source,np.float32).astype(float)
    old=np.fromfile(root/(name+'_old.f32'),np.float32).astype(float)
    new=np.fromfile(root/(name+'_new.f32'),np.float32).astype(float)
    if min(len(dry),len(old),len(new))<fs*.5:continue
    i0=int(fs*.45); i1=min(len(dry)-int(fs*.17),int(fs*1.75))
    if i1<=i0:continue
    freq,ref=welch(dry[i0:i1],fs,nperseg=4096,noverlap=2048)
    df=freq[1]-freq[0]
    smooth=lambda p:gaussian_filter1d(10*np.log10(np.maximum(p,1e-12)),125/df)
    refEnv=smooth(ref)
    domain=(freq>300)&(freq<3200)
    refEnv=refEnv[domain]-np.mean(refEnv[domain])
    vals={}
    for version,x in [('old',old),('new',new)]:
        f,p=welch(x[i0:i1],fs,nperseg=4096,noverlap=2048)
        spec=smooth(p)[domain]
        spec=spec-np.mean(spec)
        err=float(np.sqrt(np.mean((spec-refEnv)**2)))
        # The 3-10 ms envelope curvature responds to metallic tremolo and
        # grain-boundary level fluctuations, but also legitimate vibrato.
        frame=240
        x0=x[i0:i1]
        rms=np.sqrt(np.mean(x0[:len(x0)//frame*frame].reshape(-1,frame)**2,axis=1)+1e-12)
        ripple=np.std(np.diff(np.log(np.maximum(rms,1e-5)),2))
        relRms=np.sqrt(np.mean(x0*x0))
        clipped=np.mean(np.abs(x0)>.99)
        vals[version]=(err,float(ripple),float(relRms),float(clipped))
    rows.append({'case':name,'old_envelope_error_db':vals['old'][0], 'new_envelope_error_db':vals['new'][0],
                 'old_ripple':vals['old'][1], 'new_ripple':vals['new'][1],
                 'old_rms':vals['old'][2], 'new_rms':vals['new'][2],
                 'relative_waveform_change':float(np.sqrt(np.mean((new[i0:i1]-old[i0:i1])**2))/max(1e-8,vals['old'][2]))})
with open(root/'phase_formant_ab.csv','w',newline='') as handle:
    out=csv.DictWriter(handle,fieldnames=rows[0].keys());out.writeheader();out.writerows(rows)
for key in ('envelope_error_db','ripple'):
    a=np.array([r['old_'+key] for r in rows]);b=np.array([r['new_'+key] for r in rows]);
    print(key,'old=',round(float(np.mean(a)),4),'new=',round(float(np.mean(b)),4),
          'improved',int(np.sum(b<a)),'/',len(rows),'worsened',int(np.sum(b>a)))
print('cases top changes')
for row in rows:
    if row['case'].startswith('vowel') and ('_75' in row['case'] or '_110' in row['case']):
        print(row['case'],round(row['old_envelope_error_db'],2),round(row['new_envelope_error_db'],2),
              round(row['old_ripple'],3),round(row['new_ripple'],3))
