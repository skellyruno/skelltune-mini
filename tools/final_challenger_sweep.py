#!/usr/bin/env python3
"""Bounded real-vocal multi-objective audition calibration and exact-build QA.

No candidate is selected by pitch alone. Four retune speeds, two disjoint
temporal folds (same singer/take, NOT independent recordings), minimum coverage,
tail level, timing, overload and frozen-reference hash guards. Selection is
one stable flag set per engine, no sudden per-speed engine switching.
"""
import argparse, csv, hashlib, json, os, pathlib, re, subprocess, sys
import numpy as np
from score_playground import read, analyze

SPEEDS=(0,10,25,100)
MODES=(1,2,3,4,6)
NAMES={1:"TESTTUNE",2:"META",3:"CLASSIC",4:"CRYSTAL",6:"METATUNE 2.5"}
# Native phase, compact grains, calibrated motion, relative vowel control.
TRIALS=(0,1,2,3,4,5,6,7,8,9,12,13)
METRICS=("median_cents","pitch_p90_cents","envelope_db","level_db")
FLOOR=np.array([.5,2.,.025,.012])
WEIGHTS=np.array([.28,.22,.30,.20])

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save_json(p,x):p.write_text(json.dumps(x,indent=2,allow_nan=False)+"\n",encoding="utf-8")
def source_context(path):
    fs,d=read(path);delay=round(fs*.0025)
    dr,de,df=analyze(d[:-delay],fs)
    voiced=(dr>max(.002,np.max(dr)*.05))&np.isfinite(df)
    # Non-overlapping 0.8-second blocks, not randomly interleaved frames.
    fold=(np.arange(len(dr))//80)%2
    return fs,delay,len(d),dr,de,voiced,fold
def pitch_error_cents(hz):
    pcs=np.array([i for i in range(12) if 2773&(1<<i)])
    midi=69+12*np.log2(np.maximum(hz,1)/440.)
    delta=midi[:,None]-pcs[None,:]
    return np.min(np.abs(delta-12*np.round(delta/12)),axis=1)*100

def measure(path,ctx):
    fs,delay,length,dr,de,voiced,fold=ctx
    sr,x=read(path)
    assert sr==fs and len(x)==length,path
    rms,env,f0=analyze(x[delay:],fs)
    n=min(len(dr),len(rms));mask=voiced[:n]
    valid=np.isfinite(f0[:n])
    cents=pitch_error_cents(f0[:n])
    level=20*np.log10(np.maximum(rms[:n],1e-8)/np.maximum(dr[:n],1e-8))
    spec=np.mean(np.abs(env[:n]-de[:n]),axis=1)
    def metrics(sel):
        ok=sel&valid
        if not ok.any() or not sel.any():raise RuntimeError("empty evaluation fold")
        return dict(median_cents=float(np.median(cents[ok])),pitch_p90_cents=float(np.percentile(cents[ok],90)),
                    envelope_db=float(np.median(spec[sel])),level_db=float(np.median(np.abs(level[sel]))),
                    level_p10_db=float(np.percentile(level[sel],10)),coverage=float(ok.sum()/sel.sum()),
                    within25=float(np.sum(ok&(cents<=25))/sel.sum()))
    return dict(over_full_scale=int(np.count_nonzero(np.abs(x)>1)),peak=float(np.max(np.abs(x))),
                all=metrics(mask),folds=[metrics(mask&(fold[:n]==i)) for i in (0,1)])
def render(exe,source,out,mode,speed,flags,ctx,final=False):
    env=os.environ.copy()
    for k in list(env):
        if k.startswith("SKELLY_PLAYGROUND_"):env.pop(k)
    env.update(SKELLY_PLAYGROUND_MODE=str(mode),SKELLY_PLAYGROUND_LATENCY_PROFILE="3",
               SKELLY_PLAYGROUND_LISTENING_PROMOTION="1",SKELLY_PLAYGROUND_MEASURED_GRAIN="1")
    if final:env["SKELLY_PLAYGROUND_FINAL_POLICY"]="1"
    else:env["SKELLY_PLAYGROUND_FINAL_TRIAL"]=str(flags)
    log=subprocess.check_output([str(exe),str(source),str(out),str(speed),"100","100","0","0","2773"],env=env,text=True)
    def field(name):
        match=re.search(r"\b"+name+r"=([0-9.eE+-]+)",log)
        if not match:raise RuntimeError(f"missing {name}: {log}")
        return float(match[1])
    result=measure(out,ctx)
    result.update(p99_ms=field("p99_ms"),pdc_samples=int(field("common_samples")),
                  native_samples=int(field("native_samples")),sha256=digest(out))
    print("FINAL_TRIAL",NAMES[mode],speed,flags,result["all"],"p99_ms",result["p99_ms"],flush=True)
    assert result["pdc_samples"]==ctx[1],"transport changed"
    return result
def ratio(candidate,baseline):
    a=np.array([candidate[k] for k in METRICS]);b=np.array([baseline[k] for k in METRICS])
    return (a-b)/np.maximum(b,FLOOR)
def assess(rows,mode,flags,ctx):
    scores=[];reasons=[]
    for speed in SPEEDS:
        a=rows[f"{mode}:{speed}:{flags}"];b=rows[f"{mode}:{speed}:0"]
        if a["over_full_scale"]>0:reasons.append(f"{speed}: overload")
        if a["pdc_samples"]!=b["pdc_samples"]:reasons.append(f"{speed}: PDC")
        # Timing is noisy; reject a material increase, not individual jitter.
        if a["p99_ms"]>max(.05+b["p99_ms"],1.4*b["p99_ms"]):reasons.append(f"{speed}: CPU")
        if a["p99_ms"]>=.8*128/ctx[0]*1000:reasons.append(f"{speed}: deadline")
        for i,(x,y) in enumerate(zip(a["folds"],b["folds"])):
            r=ratio(x,y)
            # Hard per-fold no-material-regression guardrails, absolute plus
            # relative tolerances. Preserve source coverage and deep level dips.
            limits=np.array([max(1.0,.09*y["median_cents"]),max(3.,.09*y["pitch_p90_cents"]),max(.035,.08*y["envelope_db"]),max(.025,.13*y["level_db"])])
            if np.any(np.array([x[k]-y[k] for k in METRICS])>limits):reasons.append(f"{speed}/{i}: metric regression")
            if x["coverage"]<y["coverage"]-.015 or x["within25"]<y["within25"]-.025:reasons.append(f"{speed}/{i}: coverage")
            if x["level_p10_db"]<y["level_p10_db"]-.15:reasons.append(f"{speed}/{i}: level tail")
            scores.append(float(np.dot(r,WEIGHTS)))
    # Both folds contribute equally across speeds, preventing one favourable
    # retune mode from dominating. Minimum 3% aggregate gain to replace base.
    return float(np.mean(scores)),reasons
def run(args):
    source=args.source.resolve();exe=args.exe.resolve();folder=args.out.resolve();folder.mkdir(parents=True,exist_ok=True)
    # Independent sanity anchors: A440 is zero error, A+25c is 25 cents,
    # A#4 is not in the G-major mask, and G4 belongs to it.
    known=pitch_error_cents(np.array([440.,440.*2**(25/1200),440.*2**(1/12),440.*2**(-2/12)]))
    assert np.allclose(known,[0.,25.,100.,0.],atol=1.e-7),known
    ctx=source_context(source)
    if args.verify:
        old=json.loads((folder/"selection.json").read_text(encoding="utf-8"))
        rows=old["trials"];chosen=old["chosen"];final=[]
        for mode in MODES:
            flags=int(chosen[str(mode)])
            for speed in SPEEDS:
                out=folder/f"final_{mode}_{speed}ms.wav"
                x=render(exe,source,out,mode,speed,flags,ctx,True)
                expected=rows[f"{mode}:{speed}:{flags}"]
                assert x["sha256"]==expected["sha256"],("exact selected policy mismatch",mode,speed)
                assert x["over_full_scale"]==0,"final overload"
                final.append(dict(engine=NAMES[mode],mode=mode,retune_ms=speed,flags=flags,**x["all"],peak=x["peak"],over_full_scale=x["over_full_scale"],p99_ms=x["p99_ms"],pdc_samples=x["pdc_samples"],native_samples=x["native_samples"],sha256=x["sha256"]))
        with (folder/"FINAL-NUMBERS.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=list(final[0]));w.writeheader();w.writerows(final)
        save_json(folder/"FINAL-NUMBERS.json",dict(source_sha256=digest(source),sample_rate=ctx[0],rows=final))
        print("FINAL_POLICY_VERIFIED",chosen,flush=True)
        return
    rows={};chosen={};decisions={}
    for mode in MODES:
        for speed in SPEEDS:
            for flags in (TRIALS if mode not in (3,4) else (0,15)):
                out=folder/f"trial_{mode}_{speed}_{flags}.wav"
                result=render(exe,source,out,mode,speed,flags,ctx)
                rows[f"{mode}:{speed}:{flags}"]=result
                if mode in (3,4) and flags==15:assert result["sha256"]==rows[f"{mode}:{speed}:0"]["sha256"],"frozen reference changed"
                # Keep only final listening WAVs; trial audio is reproducible.
                out.unlink()
        chosen[str(mode)]=0;decisions[str(mode)]=[]
        if mode in (3,4):continue
        for flags in TRIALS[1:]:
            score,reasons=assess(rows,mode,flags,ctx)
            decisions[str(mode)].append(dict(flags=flags,score=score,rejected=reasons))
        eligible=[x for x in decisions[str(mode)] if not x["rejected"] and x["score"]<=-.03]
        if eligible:chosen[str(mode)]=min(eligible,key=lambda x:x["score"])["flags"]
    save_json(folder/"selection.json",dict(source_sha256=digest(source),sample_rate=ctx[0],chosen=chosen,decisions=decisions,trials=rows))
    flags=",".join(str(chosen[str(mode)]) for mode in MODES)
    args.header.write_text("#pragma once\n#include <array>\nnamespace skelly::playground {\ninline constexpr std::array<unsigned,5> finalListeningFlags{{"+flags+"}};\nstatic_assert(finalListeningFlags[2]==0 && finalListeningFlags[3]==0);\n}\n",encoding="utf-8")
    print("FINAL_POLICY_SELECTED",chosen,flush=True)
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe",type=pathlib.Path,required=True)
    p.add_argument("--source",type=pathlib.Path,required=True)
    p.add_argument("--out",type=pathlib.Path,required=True)
    p.add_argument("--header",type=pathlib.Path,default=pathlib.Path("dsp/FinalListeningPolicy.hpp"))
    p.add_argument("--verify",action="store_true")
    run(p.parse_args())
