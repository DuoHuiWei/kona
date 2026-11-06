#!/usr/bin/env python3
"""Dependency-free dataset preparation; never runs protocols."""
import argparse, gzip, hashlib, json, random, shutil, struct
from decimal import Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / 'Player-Data/Knn-Data/raw-20261103'
OUT = ROOT / 'Player-Data/Knn-Data/knn-1'
SEED = 20261103


def digest(p):
    h = hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda: f.read(1048576), b''): h.update(b)
    return h.hexdigest()


def emit(name, rows, labels, train, test, d, source):
    p = OUT / (name + '-data')
    if p.exists(): raise RuntimeError(f'Refusing overwrite: {p}')
    p.mkdir(parents=True)
    (p/'Knn-meta').write_text(f'{d} {len(train)} {len(test)}\n')
    for split, ids, prefix in [('train', train, 'P0-0'), ('test', test, 'P1-0')]:
        with (p/f'{prefix}-X-{split.title()}').open('w') as fx, (p/f'{prefix}-Y-{split.title()}').open('w') as fy:
            for i in ids:
                fx.write(' '.join(map(str, rows[i][:d]))+'\n')
                fy.write(str(labels[i])+'\n')
        (p/f'{split}-indices.txt').write_text(''.join(f'{i}\n' for i in ids))
    info = dict(seed=SEED, dataset=name, features=d, train=len(train), test=len(test), source=source,
                indices='zero-based source row IDs; MNIST: official train followed by official test')
    info['sha256'] = {f.name:digest(f) for f in p.iterdir() if f.is_file()}
    (p/'manifest.json').write_text(json.dumps(info, indent=2)+'\n')
    mirror = ROOT/'ConstantPP/data'/p.name
    if mirror.exists(): raise RuntimeError(f'Refusing overwrite: {mirror}')
    shutil.copytree(p, mirror)
    print(name, d, len(train), len(test), flush=True)


def split(n, ntest):
    ids=list(range(n)); random.Random(SEED).shuffle(ids)
    return ids[ntest:], ids[:ntest]


def prepare():
    iris = [s.split(',') for s in (RAW/'iris.data').read_text().splitlines() if s.strip()]
    assert len(iris)==150
    names=['Iris-setosa','Iris-versicolor','Iris-virginica']
    x=[[int(Decimal(v)*10) for v in row[:4]] for row in iris]
    y=[names.index(row[4]) for row in iris]
    for name, nt in [('iris',1),('iris-120-30',30)]:
        a,b=split(150,nt); emit(name,x,y,a,b,4,'UCI iris.data; labels 0=setosa, 1=versicolor, 2=virginica; features x10')
    x=[]; y=[]
    expected={'train-images-idx3-ubyte.gz':'f68b3c2dcbeaaa9fbdd348bbdeb94873','train-labels-idx1-ubyte.gz':'d53e105ee54ea40749a09fcbcd1e9432','t10k-images-idx3-ubyte.gz':'9fb629c4189551a2d022fa330f9573f3','t10k-labels-idx1-ubyte.gz':'ec29112dd5afa0611ce80d1b7f02629c'}
    for filename, md5 in expected.items():
        assert hashlib.md5((RAW/filename).read_bytes()).hexdigest()==md5, filename
    for prefix,n in [('train',60000),('t10k',10000)]:
        with gzip.open(RAW/f'{prefix}-images-idx3-ubyte.gz','rb') as f:
            assert struct.unpack('>IIII',f.read(16))==(2051,n,28,28)
            buf=f.read(); assert len(buf)==n*784
            x.extend(buf[i:i+784] for i in range(0,len(buf),784))
        with gzip.open(RAW/f'{prefix}-labels-idx1-ubyte.gz','rb') as f:
            assert struct.unpack('>II',f.read(8))==(2049,n)
            buf=f.read(); assert len(buf)==n and max(buf)<=9; y.extend(buf)
    emit('mnist-original',x,y,list(range(60000)),list(range(60000,70000)),784,'MNIST official 60000/10000; raw pixels 0..255, row-major')
    for name,nt in [('mnist',1),('mnist-56000-14000',14000)]:
        a,b=split(70000,nt); emit(name,x,y,a,b,784,'MNIST all 70000; seeded shuffled split; raw pixels 0..255, row-major')

def synthetic():
    # Independent streams keep labels and query independent of generated database size.
    rng=random.Random(SEED); x=[tuple(rng.randrange(1024) for _ in range(512)) for _ in range(65536)]
    lr=random.Random(SEED+1); y=[lr.randrange(4) for _ in range(65536)]
    qr=random.Random(SEED+2); x.append(tuple(qr.randrange(1024) for _ in range(512))); y.append(qr.randrange(4))
    for n,d in [(n,32) for n in (1024,4096,16384,32768,65536)]+[(4096,d) for d in (8,128,512)]:
        emit(f'synthetic-n{n}-d{d}',x,y,list(range(n)),[65536],d,'master 65536x512; row/column prefixes; independent query ID 65536; RNG seeds 20261103/20261104/20261105 (features/labels/query)')

def checksums():
    (RAW/'checksums.json').write_text(json.dumps({p.name:digest(p) for p in RAW.iterdir() if p.is_file() and p.name!='checksums.json'},indent=2)+'\n')


def check():
    results=[]
    for p in sorted(OUT.glob('*-data')):
        if not (p/'manifest.json').exists(): continue
        d,n,t=map(int,(p/'Knn-meta').read_text().split())
        maxv=1023 if p.name.startswith('synthetic') else 255 if p.name.startswith('mnist') else 100
        maxlabel=3 if p.name.startswith('synthetic') else 9 if p.name.startswith('mnist') else 2
        for prefix,count in [('P0-0',n),('P1-0',t)]:
            suffix='Train' if prefix=='P0-0' else 'Test'
            for kind,width,limit in [('X',d,maxv),('Y',1,maxlabel)]:
                f=p/f'{prefix}-{kind}-{suffix}'; lines=0
                with f.open() as src:
                    for line in src:
                        vals=list(map(int,line.split())); assert len(vals)==width and all(0<=v<=limit for v in vals), f
                        lines+=1
                assert lines==count,(f,lines,count)
        a=list(map(int,(p/'train-indices.txt').read_text().split())); b=list(map(int,(p/'test-indices.txt').read_text().split()))
        assert len(a)==n and len(b)==t and not set(a)&set(b)
        for filename,sha in json.loads((p/'manifest.json').read_text())['sha256'].items():
            assert digest(p/filename)==sha
            assert digest(ROOT/'ConstantPP/data'/p.name/filename)==sha
        results.append(dict(dataset=p.name,features=d,train=n,test=t,status='PASS'))
        print('PASS',p.name,flush=True)
    assert len(results)==13,len(results)
    (RAW/'validation.json').write_text(json.dumps(results,indent=2)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--check',action='store_true'); args=parser.parse_args()
    if args.check: check()
    else:
        prepare(); synthetic(); checksums()
