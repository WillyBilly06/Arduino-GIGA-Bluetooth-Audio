"""Reproduce pinned fixtures with an existing, hash-verified AOSP source copy.

Does not download source. Cargo may fetch the locked crate dependencies.
Use an empty output directory outside this library. No hardware is accessed.
"""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,time
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-source',type=Path,required=True,help='root containing the pinned AOSP LHDC Cargo.toml and src')
    p.add_argument('--output',type=Path,required=True);p.add_argument('--cargo',default='cargo');p.add_argument('--rustc',default='rustc')
    a=p.parse_args();out=a.output.resolve();source=a.reference_source.resolve();fixture=ROOT/'tests/lhdc_aosp'
    if out==ROOT or ROOT in out.parents or out==source or source in out.parents:p.error('output must be outside library and reference source')
    manifest=json.loads((fixture/'manifest.json').read_text());pins=manifest['upstream_source_blobs']['files']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for n,pin in pins.items():
        data=(source/n).read_bytes();assert len(data)==pin['bytes']and hashlib.sha256(data).hexdigest()==pin['sha256'],n
        assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==pin['git_blob'],n
    out.mkdir(parents=True,exist_ok=False);project=out/'project';project.mkdir();steps=[]
    for n in pins:
        dest=project/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/n,dest)
    shutil.copyfile(fixture/'encoder-Cargo.lock',project/'Cargo.lock')
    (project/'examples').mkdir()
    for n in ('encode_reference.rs','window_reference.rs'):shutil.copyfile(fixture/n,project/'examples'/n)
    def run(label,cmd):
        cmd=list(map(str,cmd));r=subprocess.run(cmd,capture_output=True,text=True,timeout=180)
        (out/(label+'.stdout.log')).write_text(r.stdout);(out/(label+'.stderr.log')).write_text(r.stderr)
        steps.append(dict(label=label,command=cmd,returncode=r.returncode));assert r.returncode==0,(label,r.stderr[-2000:]);return r.stdout
    versions=dict(rustc=run('rustc',[a.rustc,'-Vv']),cargo=run('cargo',[a.cargo,'-V']))
    run('build',[a.cargo,'build','--locked','--manifest-path',project/'Cargo.toml','--example','encode_reference','--example','window_reference'])
    suffix='.exe'if __import__('os').name=='nt'else'';exe=project/'target/debug/examples'
    run('windows',[exe/('window_reference'+suffix),out])
    for n in (96,192,384):assert (out/f'window-{n}.q31').read_bytes()==(fixture/f'window-{n}.q31').read_bytes(),n
    for c in manifest['cases']:
        encoded=out/(c['name']+'.bin')
        run(c['name'],[exe/('encode_reference'+suffix),fixture/c['name']/'input.pcm',encoded,c['rate'],c['bits'],'8','1023'])
        assert encoded.read_bytes()==(fixture/c['name']/'encoded.bin').read_bytes(),c['name']
    assert all(sha(project/n)==pin['sha256']for n,pin in pins.items())
    result=dict(status='PASS_UNMODIFIED_PINNED_AOSP_FIXTURE_REPRODUCTION',upstream_commit=manifest['upstream_commit'],source_files=28,
        encoded_configurations=8,encoded_frames=256,windows=3,versions=versions,steps=steps,
        fixture_manifest_sha256=sha(fixture/'manifest.json'),scope='Host encoder fixture reproduction only; no decoder or hardware qualification.')
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n');print(result['status'])

if __name__=='__main__':main()
