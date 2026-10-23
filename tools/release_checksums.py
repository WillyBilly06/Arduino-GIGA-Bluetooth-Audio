"""Write portable GNU SHA-256 sums with literal LF bytes and basename-only entries."""
from pathlib import Path
import argparse
import hashlib
import re

def checksum_bytes(paths):
    entries=[];seen=set()
    for path in map(Path,paths):
        name=path.name
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*',name) or name.casefold() in seen:
            raise ValueError('Unsafe or duplicate checksum basename: '+name)
        seen.add(name.casefold())
        entries.append(hashlib.sha256(path.read_bytes()).hexdigest()+'  '+name+'\n')
    if not entries:raise ValueError('At least one checksum input is required')
    return ''.join(entries).encode('ascii')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('files',nargs='+',type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();data=checksum_bytes(a.files)
    # Refuse replacement of an existing versioned release checksum artifact.
    with a.output.open('xb') as stream:stream.write(data)
    print('CHECKSUMS_WRITTEN entries='+str(len(a.files))+' bytes='+str(len(data)))

if __name__=='__main__':main()
