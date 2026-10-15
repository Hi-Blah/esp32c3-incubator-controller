"""Validate catalogued source ZIPs, checksums, paths, source syntax and generated assets."""
from pathlib import Path, PurePosixPath
import ast,gzip,hashlib,json,re,zipfile

PATTERNS=(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',r'gh[pousr]_[A-Za-z0-9]{30,}',r'github_pat_[A-Za-z0-9_]{30,}',r'AKIA[0-9A-Z]{16}',r'[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s]+',r'/vol[0-9]+/[0-9]+/')
FORBIDDEN={'.bin','.elf','.map','.db','.sqlite3','.log','.zip'}

def inspect_zip(path,version=None):
    errors=[]
    with zipfile.ZipFile(path) as z:
        infos=z.infolist();names=[i.filename for i in infos]
        if len(names)!=len(set(names)):errors.append('duplicate member')
        if len(infos)>10000 or sum(i.file_size for i in infos)>40*1024*1024: return ['excessive archive size']
        if z.testzip():errors.append('CRC mismatch')
        payload={}
        for info in infos:
            if info.is_dir():continue
            p=PurePosixPath(info.filename)
            if p.is_absolute() or '..' in p.parts or '\\' in info.filename:errors.append('unsafe path');continue
            if p.suffix.lower() in FORBIDDEN or p.name in ('.env','smtp.json') or p.name.endswith('_password.txt'):errors.append('runtime/build file: '+info.filename);continue
            b=z.read(info);payload[info.filename]=b
            try:t=b.decode('utf-8-sig')
            except UnicodeDecodeError:errors.append('non-text member: '+info.filename);continue
            if any(re.search(pattern,t) for pattern in PATTERNS):errors.append('credential/path pattern: '+info.filename)
            if p.suffix=='.py':
                try:ast.parse(t)
                except SyntaxError:errors.append('Python syntax: '+info.filename)
            if p.suffix=='.json':
                try:json.loads(t)
                except ValueError:errors.append('JSON syntax: '+info.filename)
        if version:
            for required in ('README.md','README.zh-CN.md','LICENSE-STATUS.md','manifest.json'):
                if required not in payload:errors.append('missing '+required)
            if 'manifest.json' in payload:
                m=json.loads(payload['manifest.json'])
                if m.get('version')!=version:errors.append('version mismatch')
                expected=m.get('files',{})
                if set(expected)!=set(payload)-{'manifest.json'}:errors.append('manifest members differ')
                for name,wanted in expected.items():
                    if name not in payload or hashlib.sha256(payload[name]).hexdigest()!=wanted:errors.append('member checksum: '+name)
        for name,b in payload.items():
            if not name.endswith('/web_ui_gzip.h'):continue
            raw=payload.get(name.replace('web_ui_gzip.h','web_ui.h'),b'').decode()
            m=re.search(r'R"HTML\((.*?)\)HTML"',raw,re.S)
            packed=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',b.decode()))
            try:body=gzip.decompress(packed)
            except (ValueError,OSError):errors.append('invalid compressed HTML: '+name);continue
            if not m or body!=m[1].encode():errors.append('compressed HTML differs: '+name)
    return errors

def audit(root):
    folder=Path(root)/'archive';catalog=folder/'catalog.json'
    if not catalog.exists():return []
    data=json.loads(catalog.read_text(encoding='utf8'));errors=[];known=set()
    for entry in data['entries']+data.get('dependencies',[]):
        name=entry['path'];p=PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts:errors.append('catalog unsafe path');continue
        if name in known:errors.append('duplicate catalog path')
        known.add(name);target=folder/name
        if not target.is_file():errors.append('missing '+name);continue
        if hashlib.sha256(target.read_bytes()).hexdigest()!=entry['sha256']:errors.append('archive checksum: '+name)
        errors += [name+': '+e for e in inspect_zip(target,entry.get('version'))]
    actual={p.relative_to(folder).as_posix() for p in folder.rglob('*.zip')}
    if known!=actual:errors.append('uncatalogued or missing ZIP')
    return errors

if __name__=='__main__':
    results=audit(Path(__file__).resolve().parents[1])
    print('\n'.join(results) if results else 'PASS version archive integrity, source syntax and hygiene')
    raise SystemExit(bool(results))
