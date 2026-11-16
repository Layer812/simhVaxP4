#!/usr/bin/env python3
# SIMHP4 REV9: convert ONLY the known boot command in the last built FIX11 app.
# Preserve the existing working DNS/Wi-Fi/DEQNA/bsd init firmware bytes.
from __future__ import annotations
import hashlib, json, re, struct, sys, tempfile, traceback, zipfile
from pathlib import Path
from datetime import datetime
import R80_FIX11_FLASH_CORE as flash

ROOT=Path(r'F:\build\tmp')
BASE=ROOT/'SIMHP4_R80_FIX11_APP.bin'
OUT=ROOT/'SIMHP4_R80_FIX11_REV9_MULTIUSER_APP.bin'
REPORT=ROOT/'SIMHP4_R80_REV9_SEND_THIS.zip'
MANIFEST='SIMHP4_R80_REV9_MANIFEST.json'
SINGLE=b'BOOT/R5:2 DUA0\r\x00'
MULTI=b'BOOT/R5:0 DUA0\r\x00'
OLDHEX=b'hex=42-4F-4F-54-2F-52-35-3A-32-20-44-55-41-30-0D'
NEWHEX=b'hex=42-4F-4F-54-2F-52-35-3A-30-20-44-55-41-30-0D'
OLD_BANNER_2=b'single-user BOOT/R5:2 DUA0 injector armed ok=%d trigger=5-4-3-8\n\x00'
OLD_BANNER_0=b'single-user BOOT/R5:0 DUA0 injector armed ok=%d trigger=5-4-3-8\n\x00'
NEW_BANNER=b'multi-user BOOT/R5:0 DUA0 injector armed ok=%d trigger=5-4-3-8\n\x00'

class Stop(RuntimeError):pass

def require(test,msg):
    if not test:raise Stop(msg)

def parse_image(raw:bytes):
    require(len(raw)>=192 and raw[0]==0xE9,'not an ESP application')
    count=raw[1]
    require(1<=count<=16,'invalid image segment count')
    require(raw[23]==1,'expected SHA-appended ESP-IDF format')
    pos=24; ranges=[];check=0xef
    for idx in range(count):
        require(pos+8<=len(raw)-33,'truncated segment header')
        addr,size=struct.unpack_from('<II',raw,pos);pos+=8
        require(size<=len(raw)-pos-33,'segment extends beyond image')
        ranges.append((pos,pos+size))
        for byte in raw[pos:pos+size]:check^=byte
        pos+=size
    checksum_at=((pos+16)&~15)-1
    require(checksum_at==len(raw)-33,'invalid image checksum position')
    require(raw[checksum_at]==check,'image XOR checksum mismatch')
    require(hashlib.sha256(raw[:-32]).digest()==raw[-32:], 'image appended SHA256 mismatch')
    return ranges,checksum_at

def patch_image(src:bytes, *, require_fix11=True):
    flash.candidate_identity(src)
    require(len(src)<=flash.APP_PARTITION_SIZE,'app partition overflow')
    ranges,cpos=parse_image(src)
    if require_fix11:
        require(b'CONNECT SAVED' in src,'the existing saved-WiFi UI is missing: wrong app')
        require(b'nameserver 1.1.1.1' in src,'the REV6 DNS setup is missing: wrong app')
        require(b'BSD INIT' in src and b'SIMHP4_R14M' in src,'FIX11/boot banner absent: wrong app')
    out=bytearray(src)
    patches=[]
    def edit(old,new,expected=1):
        require(len(old)==len(new),'patch changes image length')
        require(out.count(old)==expected,'expected %s occurrence(s) of %r; found %d'%(expected,old,out.count(old)))
        pos=out.index(old)
        require(any(start<=pos and pos+len(old)<=end for start,end in ranges),'patch outside data segment')
        out[pos:pos+len(old)]=new
        patches.append((pos,old,new))
    if out.count(SINGLE)==1:
        edit(SINGLE,MULTI)
    else:
        require(out.count(MULTI)==1,'cannot identify one active bootstrap command')
    if out.count(OLD_BANNER_2)==1:
        edit(OLD_BANNER_2,NEW_BANNER+b'\x00')
    elif out.count(OLD_BANNER_0)==1:
        edit(OLD_BANNER_0,NEW_BANNER+b'\x00')
    else:
        require(out.count(NEW_BANNER)==1,'cannot identify R14M mode banner')
    if out.count(OLDHEX)==1:
        edit(OLDHEX,NEWHEX)
    else:
        require(out.count(NEWHEX)==1,'cannot identify exact boot-byte trace')
    require(len(patches)>=1, 'no modification necessary: image is already multi-user')
    require(out.count(SINGLE)==0 and out.count(MULTI)==1,'boot command proof failed')
    require(out.count(NEW_BANNER)==1 and b'single-user BOOT/R5:' not in out, 'boot banner proof failed')
    require(out.count(OLDHEX)==0 and out.count(NEWHEX)==1,'hex trace proof failed')
    # Rebuild the ESP image's xor checksum and appended SHA digest, NOT the executable code.
    checksum=0xef
    for start,end in ranges:
        for byte in out[start:end]:checksum^=byte
    out[cpos]=checksum
    out[-32:]=hashlib.sha256(out[:-32]).digest()
    parse_image(bytes(out))
    # Any byte change outside these three known strings, checksum and SHA means STOP.
    allowed=set([cpos])|set(range(len(out)-32,len(out)))
    for pos,old,_ in patches:allowed.update(range(pos,pos+len(old)))
    require(all((src[i]==out[i] or i in allowed) for i in range(len(src))),'unapproved firmware mutation')
    require(len(out)==len(src),'image size changed')
    return bytes(out),[(hex(p),old.decode('ascii','replace'),new.decode('ascii','replace')) for p,old,new in patches]

def check_manifest():
    home=Path(__file__).resolve().parent
    require(home.resolve()==ROOT.resolve(),'extract ZIP to F:\\build\\tmp')
    mf=json.loads((home/MANIFEST).read_text('utf-8'))
    for fn,sha in mf['files'].items():
        require((home/fn).is_file() and hashlib.sha256((home/fn).read_bytes()).hexdigest()==sha,'package mismatch '+fn)

def report(state,logs):
    tmp=REPORT.with_suffix('.tmp')
    with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('RESULT.json',json.dumps(state,indent=2,ensure_ascii=False)+'\n')
        z.writestr('ESPTOOL_LOG.txt','\n\n'.join(logs))
    tmp.replace(REPORT)

def main():
    state={'revision':'REV9','phase':'START','flash':'NOT_ATTEMPTED','backup':'NEVER',
           'mode':'DIRECT_VERIFIED_APP_IMAGE_PATCH','source':str(BASE),'report':str(REPORT),
           'start_time':datetime.now().isoformat()}
    logs=[]
    try:
        require(sys.platform=='win32','Windows-only')
        check_manifest()
        require(BASE.is_file(),'FIX11 last-built app.bin missing: '+str(BASE))
        src=BASE.read_bytes()
        patched,changes=patch_image(src)
        state.update({'source_sha256':hashlib.sha256(src).hexdigest(),
                      'app_sha256':hashlib.sha256(patched).hexdigest(),'changed_strings':changes,'app_size':len(patched)})
        OUT.write_bytes(patched)
        require(OUT.read_bytes()==patched,'REV9 candidate write failed')
        flash.APP_SIZE=len(patched)
        flash.APP_SHA256=hashlib.sha256(patched).hexdigest()
        print('PASS: FIX11 DNS/saved-WiFi preserved, R5:0 binary and checksum/SHA valid',flush=True)
        print('REV9 app bytes:',len(patched),'changed strings:',len(changes),flush=True)
        with tempfile.TemporaryDirectory(prefix='simhp4_r9_pt_',dir=str(ROOT)) as tmp:
            tool=flash.Esptool(sys.executable,'COM9',logs)
            state['phase']='PARTITION_READ'
            table=tool.read_partition_table(Path(tmp)/'table.bin')
            state['partitions']=flash.parse_partition_table(table)
            state['phase']='APP_ONLY_WRITE'
            state['flash']='WRITE_ATTEMPTED'
            tool.write_app(OUT)
            state['phase']='APP_ONLY_VERIFY'
            tool.verify_app(OUT)
            state['flash']='APP_ONLY_VERIFIED'
        state['phase']='COMPLETE'
        state['pass']=True
        print('PASS: verified APP-only write; actual BSD multi-user boot not yet observed',flush=True)
    except Exception as e:
        state['pass']=False
        state['error']=repr(e)
        state['traceback']=traceback.format_exc()[-4000:]
        state['phase']+='_STOP'
        print('STOP:',e,flush=True)
    finally:
        report(state,logs)
        print('ONE REPORT ZIP:',REPORT,flush=True)
    return 0 if state.get('pass') else 2

if __name__=='__main__':sys.exit(main())
