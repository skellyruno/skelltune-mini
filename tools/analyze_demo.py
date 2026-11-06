"""Optional (numpy/scipy/matplotlib) frequency-domain check for generated test WAVs."""
import csv
import os
import sys
import wave
import numpy as np
from scipy.signal import periodogram

def wav16(path):
    with wave.open(path, 'rb') as wf:
        assert wf.getsampwidth() == 2 and wf.getnchannels() == 1
        sr = wf.getframerate()
        x = np.frombuffer(wf.readframes(wf.getnframes()), dtype='<i2').astype(float)/32768
    return x,sr

if __name__ == '__main__':
    prefix=sys.argv[1] if len(sys.argv)>1 else 'results/demo'
    raw,sr=wav16(prefix+'_raw.wav')
    tuned,_=wav16(prefix+'_tuned.wav')
    targets=[220.0,261.625565,195.997718]
    rows=[]
    for j in range(3):
        start=int((j+.3)*sr);end=int((j+.85)*sr)
        def peak(x):
            f,p=periodogram(x[start:end],sr,nfft=2**18)
            valid=(f>=160)&(f<=400)
            return float(f[valid][np.argmax(p[valid])])
        fr,ft=peak(raw),peak(tuned)
        cents=1200*np.log2(ft/targets[j])
        rows.append(dict(segment=j+1,input_hz=round(fr,3),
                    output_hz=round(ft,3),target_hz=round(targets[j],3),
                    output_error_cents=round(cents,3)))
        print(rows[-1])
    with open(os.path.join(os.path.dirname(prefix),'frequency_results.csv'),'w',newline='') as f:
        out=csv.DictWriter(f,fieldnames=rows[0].keys());out.writeheader();out.writerows(rows)
    try:
        import matplotlib.pyplot as plt
        fig, ax=plt.subplots(figsize=(8.0,3.5))
        k=np.arange(3)
        ax.plot(k+1,[r['input_hz'] for r in rows],marker='o',label='Input')
        ax.plot(k+1,[r['output_hz'] for r in rows],marker='o',label='Corrected')
        ax.plot(k+1,targets,marker='x',linestyle='--',label='Musical target')
        ax.set_xticks([1,2,3]);ax.set_xlabel('Synthetic note segment')
        ax.set_ylabel('Fundamental frequency (Hz)');ax.grid(alpha=.25);ax.legend()
        ax.set_title('SkellyTune working engine — sustained-note frequency')
        fig.tight_layout();fig.savefig(os.path.join(os.path.dirname(prefix),'measured_pitch.png'),dpi=150)
        plt.close(fig)
    except ImportError:
        pass
