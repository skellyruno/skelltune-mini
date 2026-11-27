#!/usr/bin/env python3
"""Independent same-take metrics at the 2.5ms host profile. Not a listening score."""
import csv, pathlib, sys, hashlib
import numpy as np
from scipy.io import wavfile
from scipy import signal
from numpy.lib.stride_tricks import sliding_window_view

def read(path):
    fs,x=wavfile.read(path)
    if np.issubdtype(x.dtype,np.integer):
        x=x.astype(np.float64)/(2.**(8*x.dtype.itemsize-1))
    else:x=x.astype(np.float64)
    if x.ndim==1:x=x[:,None]
    assert np.all(np.isfinite(x)),path
    return fs,x

def analyze(x,fs):
    # RMS and smooth log-spaced band energies on the same frame grid.
    mono=x[:,np.argmax(np.mean(x*x,axis=0))]
    win=round(.040*fs);hop=round(.010*fs)
    frame=sliding_window_view(mono,win)[::hop].copy()
    rms=np.sqrt(np.mean(frame**2,axis=1))
    spec=np.abs(np.fft.rfft(frame*np.hanning(win),axis=1))**2
    hz=np.fft.rfftfreq(win,1/fs)
    edges=[100,250,450,700,1050,1600,2400,3500,5000,7500]
    band=np.stack([spec[:,(hz>=a)&(hz<b)].sum(axis=1) for a,b in zip(edges[:-1],edges[1:])],axis=1)
    envelope=10*np.log10(np.maximum(band,1.e-10)/np.maximum(band.sum(axis=1,keepdims=True),1.e-10))
    # Pitch measurement via normalized autocorrelation at 12k or lower,
    # restricted to robust voiced source frames; report missing coverage.
    rate=min(fs,12000)
    y=signal.resample_poly(mono,rate,fs)
    n=round(.040*rate);h=round(.010*rate)
    fr=sliding_window_view(y,n)[::h].copy()
    fr-=fr.mean(axis=1,keepdims=True)
    fft=np.fft.rfft(fr,n=2*n,axis=1)
    ac=np.fft.irfft(np.abs(fft)**2,n=2*n,axis=1)[:,:n]
    lo=max(2,int(rate/850));hi=min(n-2,int(rate/65))
    sq=np.concatenate([np.zeros((len(fr),1)),np.cumsum(fr*fr,axis=1)],axis=1)
    lag=np.arange(lo,hi+1)
    norm=np.sqrt(np.maximum(sq[:,n-lag]*(sq[:,-1,None]-sq[:,lag]),1.e-16))
    corr=ac[:,lag]/norm
    # earliest local peak near global peak avoids subharmonic preference.
    pk=(corr[:,1:-1]>=corr[:,:-2])&(corr[:,1:-1]>=corr[:,2:])
    mx=corr.max(axis=1)
    good=pk&(corr[:,1:-1]>=np.maximum(.72,.92*mx[:,None]))
    idx=np.argmax(good,axis=1)+1
    valid=good.any(axis=1)
    q=np.arange(len(fr));a,b,c=corr[q,idx-1],corr[q,idx],corr[q,idx+1]
    delta=np.clip(.5*(a-c)/np.minimum(-1.e-9,a-2*b+c),-.5,.5)
    f0=np.where(valid,rate/(lo+idx+delta),np.nan)
    m=min(len(rms),len(f0));return rms[:m],envelope[:m],f0[:m]

def main():
    source,folder=map(pathlib.Path,sys.argv[1:])
    fs,dry=read(source);delay=round(.0025*fs)
    dry=dry[:-delay]
    dr,de,df=analyze(dry,fs)
    active=dr>max(.002,np.max(dr)*.05)
    pcs=np.array([i for i in range(12) if 2773&(1<<i)])
    rows=[]
    for p in sorted(folder.glob("*.wav")):
        if not p.name.startswith(("adaptive_","envelope_","next_")):continue
        rate,x=read(p);assert rate==fs and len(x)==len(dry)+delay,p
        rms,env,f0=analyze(x[delay:],fs);n=min(len(dr),len(rms))
        voiced=active[:n]&np.isfinite(df[:n]);valid=voiced&np.isfinite(f0[:n])
        midi=69+12*np.log2(f0[:n][valid]/440)
        dist=midi[:,None]-pcs[None,:]
        cents=np.min(np.abs(dist-12*np.round(dist/12)),axis=1)*100 if len(midi) else np.array([np.nan])
        level=20*np.log10(np.maximum(rms[:n],1e-8)/np.maximum(dr[:n],1e-8))
        # Dry envelope is a timbre anchor, not an ideal pitch-corrected target.
        spec=np.mean(np.abs(env[:n]-de[:n]),axis=1)
        row=dict(file=p.name,voiced_coverage=round(float(valid.sum()/max(1,voiced.sum())),4),
                 median_cents=round(float(np.nanmedian(cents)),3),
                 pitch_p90_cents=round(float(np.nanpercentile(cents,90)),3),
                 within_10c_coverage=round(float(np.sum(cents<=10)/max(1,voiced.sum())),4),
                 within_25c_coverage=round(float(np.sum(cents<=25)/max(1,voiced.sum())),4),
                 envelope_mae_db=round(float(np.median(spec[voiced])),3),
                 level_p10_db=round(float(np.percentile(level[voiced],10)),3),
                 level_mae_db=round(float(np.median(np.abs(level[voiced]))),3),
                 peak=round(float(np.max(np.abs(x))),6),over_full_scale=int(np.count_nonzero(np.abs(x)>1)))
        rows.append(row);print("VOCAL_SCORE",row,flush=True)
        if p.name.startswith("next_"):assert row["over_full_scale"]==0,("targeted candidate clipped",p)
        # All protected-reference candidate WAVs must match same-profile base.
        bits=p.stem.split("_")
        if bits[1] in ("3","4"):
            base=folder/f"envelope_{bits[1]}_base_{bits[-1]}.wav"
            if base.exists():assert hashlib.sha256(p.read_bytes()).digest()==hashlib.sha256(base.read_bytes()).digest(),p
    assert rows,"missing vocal scores"
    # Verify the real plugin policy exactly matches previously measured trial
    # settings (including the retained TESTTUNE 25ms motion and frozen engines).
    for path in folder.glob("next_*_promoted_*ms.wav"):
        _,mode,_,speed=path.stem.split("_")
        if mode in ("1","2"):
            variant="dynamics" if mode=="1" and speed=="25ms" else "combined"
            expected=folder/f"next_{mode}_{variant}_{speed}.wav"
        elif mode=="6" and speed=="25ms":expected=folder/f"next_{mode}_combined_{speed}.wav"
        else:expected=folder/f"envelope_{mode}_base_{speed}.wav"
        assert expected.exists() and hashlib.sha256(path.read_bytes()).digest()==hashlib.sha256(expected.read_bytes()).digest(),("promoted audio differs",path,expected)
    for path in folder.glob("next_*_listening_*ms.wav"):
        _,mode,_,speed=path.stem.split("_")
        variant="synthesis" if mode=="2" and speed in ("0ms","100ms") else "promoted"
        expected=folder/f"next_{mode}_{variant}_{speed}.wav"
        assert expected.exists() and hashlib.sha256(path.read_bytes()).digest()==hashlib.sha256(expected.read_bytes()).digest(),("listening promotion differs",path,expected)
    lookup={r["file"]:r for r in rows}
    for r in rows:
        bits=pathlib.Path(r["file"]).stem.split("_")
        if bits[0]!="next" or bits[1] in ("3","4"):continue
        baseline=lookup[f"next_{bits[1]}_promoted_{bits[-1]}.wav"] if bits[2] in ("phase","synthesis") else lookup[f"envelope_{bits[1]}_base_{bits[-1]}.wav"]
        refs=[lookup[f"envelope_{m}_base_{bits[-1]}.wav"] for m in ("3","4")]
        print("CHALLENGER_DELTA",r["file"],{k:round(r[k]-baseline[k],3) for k in ("median_cents","envelope_mae_db","level_mae_db","voiced_coverage")}, "beats_both_pitch",r["median_cents"]<min(x["median_cents"] for x in refs),flush=True)
    with (folder/"vocal_scores.csv").open("w",newline="") as out:
        writer=csv.DictWriter(out,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)

if __name__=="__main__":main()
