#!/usr/bin/env python3
"""Owned Playground comparison across distinct, fingerprinted dry excerpts.
Keep input manifests, renders and individual reports private. No vendor binary execution.
"""
import argparse, hashlib, json, pathlib, tempfile
from final_challenger_sweep import source_context, render, SPEEDS

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=pathlib.Path,required=True)
    p.add_argument('--baseline',type=pathlib.Path,required=True)
    p.add_argument('--candidate',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--portable',action='store_true',help='Four-engine build; omit MetaTune 2.5, never substitute')
    p.add_argument('--compare-final-policy',action='store_true',help='Compare baseline trial zero against candidate final policy; otherwise final versus final')
    a=p.parse_args();rows=[];seen=set();modes=(1,2,3,4) if a.portable else (1,2,3,4,6)
    with tempfile.TemporaryDirectory(prefix='skell-pitch-private-') as temp:
        folder=pathlib.Path(temp)
        for item in json.loads(a.manifest.read_text())['inputs']:
            if item['stage']!='dry_archive_labeled':continue
            source=pathlib.Path(item['path']).resolve();digest=hashlib.sha256(source.read_bytes()).hexdigest()
            if digest!=item['sha256'] or digest in seen:raise ValueError('Fingerprint mismatch or duplicate')
            seen.add(digest);ctx=source_context(source)
            for mode in modes:
                for speed in SPEEDS:
                    base=render(a.baseline.resolve(),source,folder/'base.wav',mode,speed,0,ctx,not a.compare_final_policy)
                    final=render(a.candidate.resolve(),source,folder/'candidate.wav',mode,speed,0,ctx,True)
                    if mode in (3,4) and base['sha256']!=final['sha256']:raise RuntimeError('Protected reference changed')
                    rows.append({'id':item['id'],'mode':mode,'speed':speed,'base':base,'final':final})
    if not seen:raise ValueError('No explicitly labeled dry input')
    a.output.write_text(json.dumps({'schema':1,'portable':a.portable,'baseline_trial_zero':a.compare_final_policy,'rows':rows,'limits':'Fixed existing test scale (not verified song key), same-take metrics, not listening preferences or production readiness.'},indent=2,allow_nan=False)+'\n')
if __name__=='__main__':main()
