#!/usr/bin/env python3
"""SIMHP4 R80 FIX28 source-only candidate. Exact donor / unchanged UI and Wi-Fi;
NO FLASH; never accesses COM9 or writes any SD data.
"""
from __future__ import annotations
import datetime,hashlib,json,os,re,shutil,subprocess,sys,tempfile,traceback,zipfile
from pathlib import Path
from collections import deque
import R80_FIX11_FLASH_CORE as flash
import R80_REV9_BINARY_PATCH as boot

ROOT=Path(r'F:\build\tmp')
SELF=Path(__file__).resolve().parent
IDF=Path(r'G:\esp-idf-5.5.4')
APPNAME='simhp4_vax630_visual_media_probe_v12'
RELATIVE=[f'project/main/{n}' for n in ('v13_main.cpp','simhp4_wifi_settings.c',
          'simhp4_wifi_settings.h','simhp4_a164_keyboard.c','simhp4_vax_terminal_r14f.cpp',
          'simhp4_sim_console_esp_v14c.c','simhp4_wifi_overlay.cpp')]
REPORT=ROOT/'SIMHP4_R80_RC14_FIX28_SCB_PROVENANCE_BUILD_ONLY_RESULT.zip'
APP=ROOT/'SIMHP4_R80_RC14_FIX28_SCB_PROVENANCE_BUILD_ONLY_APP.bin'
SNAPSHOT_MARKERS=(b'RESTORE disabled; single-user cold boot',
 b'VT100 cursor/scroll/erase and A164 DEL active',
 b'GUEST_INPUT_CTRL_ENQUEUED',
 b'MACRO',b'SIMHP4_FIX27_QBA_UNIT_RESET_NO_RETRY',
 b'SIMHP4_FIX28_SCB_CAPTURE_LINKED', b'SIMHP4_FIX28_SCB_INVALID',
 b'SIMHP4_FIX24_QBA_FLUSH_GUARD',b'SIMHP4_FIX24_QBMAP_COMPILED_GUARD',
 b'SIMHP4_FIX24_VAX_EXIT_BEFORE_FLUSH',b'SIMHP4_FIX25_QBA_SEEK_GUARD',b'SIMHP4_FIX27_QBA_UNIT_SANITIZE',b'SIMHP4_FIX27_QBA_FULL_RESET',b'macro.txt',b'quasdisk.dsk',b'Preparing BSD disk (first boot)',b'S19SAVE.BIN',
 b'SIMHP4_R80_REV17_RQ_GATE_256',b'SIMHP4_RC5_VI_FULLWINDOW',
 b'SIMHP4_RC6_TOUCH_LIVE',
 b'RC13 UI framebuffer PSRAM=',b'FIX22_PRESENTER_STACK',b'FIX22 snapshot PSRAM allocation failed',
 b'FIX22 partial refresh unavailable',b'HOST_RESET_REASON=',b'SIMHP4_RC13_A164_STABLE',
 b'SIMHP4_RC14_TERMINAL_CPU1',b'SIMHP4_RC14_TTO_LOG_RATE_65536',b'SIMHP4_RC14_FIX1_SHADOW_ALLOC')

def require(x,msg):
 if not x:raise RuntimeError(msg)
def sha(data):return hashlib.sha256(data).hexdigest()

def guard_package():
 require(SELF.is_dir() and not SELF.is_symlink(),'Missing SIMHP4_RELEASE_UI directory')
 require(Path(__file__).resolve().parent==SELF.resolve(),
         'Extract complete ZIP to F:\\build\\tmp; do not move files independently')
 pkg=json.loads((SELF/'PACKAGE_SHA256.json').read_text('utf8'))
 for rel,exp in pkg.items():
  f=SELF/rel
  require(f.is_file() and not f.is_symlink() and sha(f.read_bytes())==exp,
          'Package damaged: '+rel)

def donor_stage():
 parent=ROOT/'s94'
 require(parent.is_dir(),'No completed FIX4 source stage in F:\\build\\tmp\\s94 (NO FLASH)')
 expected=json.loads((SELF/'BASE_SOURCE_SHA256.json').read_text('utf8'))
 candidates=[]
 for st in parent.iterdir():
  if not st.is_dir() or st.is_symlink():continue
  try:
   for part in ('project','components','third_party'):
    require((st/part).is_dir(),'Missing required source part '+part)
   for name,hashval in expected.items():
    file=st/'project/main'/name
    require(file.is_file() and not file.is_symlink() and sha(file.read_bytes())==hashval,
            'Different donor source: '+name)
   app=st/'project/build'/(APPNAME+'.bin')
   log=st/'idf_build.log'
   require(app.is_file() and log.is_file(),'Incomplete native source stage')
   require('Project build complete.' in log.read_text('utf8',errors='replace'),'Native build not finished')
   raw=app.read_bytes();flash.candidate_identity(raw);boot.parse_image(raw)
   require(b'SIMHP4_R80_REV19_SUSPEND_ONESHOT' in raw,'Donor not R80 FIX4')
   candidates.append((app.stat().st_mtime,st))
  except Exception:continue
 require(candidates,'No FIX4 stage matches all 5 uploaded source files and completed BIN; NO FLASH')
 return max(candidates)[1]

def isolated_copy(src,dst):
 require(not dst.exists(),'Generated destination already exists')
 def ignore(parent,names):
  return {n for n in names if n in {'build','.git','__pycache__','.pio','dist'} or n.lower().endswith(
   ('.zip','.rar','.xdf','.dsk','.img','.adf','.hdf','.vhd','.qcow2','.bak'))}
 dst.mkdir(parents=True)
 for part in ('project','components','third_party'):
  shutil.copytree(src/part,dst/part,ignore=ignore,symlinks=False)


def patch_sources(stage):
 expected=json.loads((SELF/'BASE_SOURCE_SHA256.json').read_text('utf8'))
 result={}
 for name,sha0 in expected.items():
  dst=stage/'project/main'/name
  require(dst.is_file() and sha(dst.read_bytes())==sha0,'Staged source mismatch '+name)
  new=(SELF/'source'/name).read_bytes()
  dst.write_bytes(new)
  require(dst.read_bytes()==new,'Writing staged file failed '+name)
  result[name]={'old_sha256':sha0,'new_sha256':sha(new)}
 return result


def patch_actual_qba(stage):
 # Patch the compiled ESP32-P4 Qbus implementation, not upstream vax630_io.c.
 scp=stage/'third_party'/'simh_vax630_87eb'/'scp.c'
 io=stage/'project'/'main'/'simhp4_vax630_io_esp_r14e.c'
 require(scp.is_file() and io.is_file(),
         'FIX24 requires vendor scp.c and compiled ESP io_r14e.c; NO FLASH')
 a=scp.read_text('utf8')
 b=io.read_text('utf8')
 needle='for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* flush attached files */\n'
 require(a.count(needle)==1,'SIMH flush anchor changed; NO FLASH')
 a=a.replace(needle,'''for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* flush attached files */
    /* FIX24: QBA is not a file-backed device; never call its corrupt io_flush. */
    if (dptr->name && (strcmp(dptr->name, "QBA") == 0)) {
        static t_bool qba_reported = FALSE;
        if (!qba_reported && dptr->units && dptr->numunits &&
            ((dptr->units[0].flags & UNIT_ATT) || dptr->units[0].io_flush)) {
            qba_reported = TRUE;
            sim_printf ("SIMHP4_FIX24_QBA_FLUSH_GUARD unexpected_flags=0x%X unexpected_callback=%d\\n",
                        (unsigned)dptr->units[0].flags,
                        dptr->units[0].io_flush != NULL);
            }
        continue;
        }
''',1)
 needle='''signal (SIGTERM, sigterm_received ? SIG_IGN : SIG_DFL); /* cancel WRU */
sim_flush_buffered_files();'''
 require(a.count(needle)==1,'SIMH exit anchor mismatch; NO FLASH')
 a=a.replace(needle,'''signal (SIGTERM, sigterm_received ? SIG_IGN : SIG_DFL); /* cancel WRU */
if (r != SCPE_OK)
    sim_printf ("SIMHP4_FIX24_VAX_EXIT_BEFORE_FLUSH reason=%d\\n", (int)r);
sim_flush_buffered_files();''',1)
 # Retained identification lives IN the compiled custom Qbus translation unit,
 # not merely an unused upstream file. GCC 14 (IDF 5.5) supports retain.
 marker='''
/* FIX24: compiled source identity. GNU retain prevents --gc-sections from
 * deleting this marker; the source and object are also checked below. */
static const char simhp4_fix24_qbmap_compiled_guard_marker[]
    __attribute__((used, retain)) = "SIMHP4_FIX24_QBMAP_COMPILED_GUARD";
'''
 require('SIMHP4_FIX24_QBMAP_COMPILED_GUARD' not in b,
         'Compiled Qbus source unexpectedly patched; NO FLASH')
 require('QBMAPBASE' in b and 'QBNMAPR' in b and 'MSER_NXM' in b,
         'Compiled Qbus file missing expected KA630 range definitions; NO FLASH')
 b=marker+'\n'+b
 # Patch the entry of the two top-level functions so an invalid address
 # cannot compute an out-of-bounds index before range validation.
 def entry_guard(src, func, statement):
  pattern=re.compile(r'(?m)^[ \t]*(?:void|int32|uint32|t_stat)\s+'+re.escape(func)+r'\s*\([^;{}]*\)\s*\{')
  matches=list(pattern.finditer(src))
  require(len(matches)==1, f'Compiled {func} definition count {len(matches)}; NO FLASH')
  header=matches[0].group(0)
  require(re.search(r'\b(?:int32|uint32)\s+pa\b',header),
          f'Compiled {func} first argument name/type changed; NO FLASH')
  # Ensure we never silently accept an unrelated function implementation.
  funcstart=matches[0].end()
  endhint=src.find('\n}',funcstart)
  require(endhint>funcstart and ('qb_map[' in src[funcstart:endhint] or 'qb_map [' in src[funcstart:endhint]),
          f'Compiled {func} has no qb_map access; NO FLASH')
  return src[:funcstart]+'\n'+statement+'\n'+src[funcstart:]
 check='((uint32)pa < (uint32)QBMAPBASE || (uint32)pa >= ((uint32)QBMAPBASE + ((uint32)QBNMAPR * 4u)))'
 b=entry_guard(b,'qbmap_wr',f'''/* FIX24: reject invalid Qbus mapping *before* signed idx arithmetic. */
if {check} {{
    static unsigned invalid_qbmap_writes = 0;
    if (invalid_qbmap_writes++ < 4)
        sim_printf ("FIX24 QBA invalid map write PA=%08X\\n", (unsigned)pa);
    ka_mser |= MSER_NXM;
    return;
    }}''')
 b=entry_guard(b,'qbmap_rd',f'''/* FIX24: never index qb_map with an invalid signed address. */
if {check} {{
    ka_mser |= MSER_NXM;
    return 0;
    }}''')
 scp.write_text(a,'utf8');io.write_text(b,'utf8')
 require('SIMHP4_FIX24_QBA_FLUSH_GUARD' in scp.read_text('utf8') and
         'SIMHP4_FIX24_QBMAP_COMPILED_GUARD' in io.read_text('utf8') and
         'FIX24 QBA invalid map write' in io.read_text('utf8'),
         'QBA patched source verification failed; NO FLASH')
 return {'vendor_scp':{'path':str(scp),'sha256':sha(scp.read_bytes())},
         'compiled_io':{'path':str(io),'sha256':sha(io.read_bytes())}}

def patch_qba_file_metadata(stage):
 # Confirmed by FIX24R1 ELF: s5=0x4ff82198 is qba_unit, qba_unit.fileref
 # 0xf951f400 was given to sim_can_seek in run_cmd. QBA is never file-backed.
 scp=stage/'third_party'/'simh_vax630_87eb'/'scp.c'
 require(scp.is_file(), 'Missing SIMH scp.c; NO FLASH')
 a=scp.read_text('utf8')
 needle='for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* reposition all */\n'
 require(a.count(needle)==1,'SIMH run_cmd sequential-file anchor changed; NO FLASH')
 require('SIMHP4_FIX24_QBA_FLUSH_GUARD' in a,
         'FIX24 QBA flush protection missing; NO FLASH')
 insertion='''for (i = 1; (dptr = sim_devices[i]) != NULL; i++) {     /* reposition all */
    /* FIX25: qba_unit is UDATA(NULL,0,0), never a sequential file.
     * It is adjacent to qb_map in the ESP32-P4 ELF, and its file fields
     * can be corrupted by writes before the Qbus address guard.  Restore
     * only non-file metadata, and never feed its fileref to fileno(). */
    if (dptr->name && strcmp(dptr->name, "QBA") == 0) {
        if (dptr->units && dptr->numunits > 0) {
            UNIT *q = &dptr->units[0];
            if (q->flags || q->fileref || q->io_flush || q->filename || q->action) {
                sim_printf ("SIMHP4_FIX25_QBA_SEEK_GUARD flags=%08X fileref=%p\\n",
                            (unsigned)q->flags, (void *)q->fileref);
                /* FIX27: original QBA is exactly UDATA(NULL,0,0), and has
                 * no service action or scheduled events. Reset every UNIT
                 * field, not just the four file fields; partial repairs
                 * leave corrupt event links and userdata behind. */
                memset(q, 0, sizeof(*q));
                sim_printf ("SIMHP4_FIX27_QBA_FULL_RESET done\\n");
                }
            }
        continue;
        }
'''
 a=a.replace(needle,insertion,1)
 scp.write_text(a,'utf8')
 require(a.count('SIMHP4_FIX25_QBA_SEEK_GUARD')==1 and
         a.count('SIMHP4_FIX27_QBA_FULL_RESET')==1 and
         a.count('SIMHP4_FIX24_QBA_FLUSH_GUARD')==1 and
         a.count('/* reposition all */')==1,
         'FIX25 compiled source guard verification failed; NO FLASH')
 return {'path':str(scp),'sha256':sha(scp.read_bytes())}


def patch_scb_fault_provenance(stage):
  """Record an invalid SCB exception on the already-existing error path only."""
  f=stage/'third_party'/'simh_vax630_87eb'/'VAX'/'vax_cpu1.c'
  require(f.is_file(), 'Missing compiled VAX/vax_cpu1.c; NO FLASH')
  s=f.read_text('utf8')
  require('simhp4_fix28_bad_scb_seen' not in s, 'FIX28 SCB patch previously applied; NO FLASH')
  start='int32 intexc (int32 vec, int32 cc, int32 ipl, int ei)'
  require(s.count(start)==1, 'VAX intexc signature differs; NO FLASH')
  signature='if (newpc & 2)                                          /* bad flags? */\n    ABORT (STOP_ILLVEC);'
  require(s.count(signature)==1, 'VAX invalid SCB anchor differs; NO FLASH')
  global_block='''/* FIX28: one-shot VAX invalid-SCB evidence, no change to normal execution. */
volatile uint32 simhp4_fix28_bad_scb_seen = 0;
volatile uint32 simhp4_fix28_bad_scb_scbb = 0;
volatile uint32 simhp4_fix28_bad_scb_vec = 0;
volatile uint32 simhp4_fix28_bad_scb_newpc = 0;
volatile uint32 simhp4_fix28_bad_scb_guest_pc = 0;
volatile uint32 simhp4_fix28_bad_scb_psl = 0;
volatile int32 simhp4_fix28_bad_scb_ipl = 0;
volatile int32 simhp4_fix28_bad_scb_ei = 0;
/* Marker is retained in the APP image, proving this TU is linked. */
static const char fix28_scb_linked_marker[]
  __attribute__((used, retain)) = "SIMHP4_FIX28_SCB_CAPTURE_LINKED";

'''
  s=s.replace(start, global_block+start,1)
  replacement='''if (newpc & 2) {                                         /* bad SCB flags */
    simhp4_fix28_bad_scb_scbb = (uint32)SCBB;
    simhp4_fix28_bad_scb_vec = (uint32)vec;
    simhp4_fix28_bad_scb_newpc = (uint32)newpc;
    simhp4_fix28_bad_scb_guest_pc = (uint32)R[15];
    simhp4_fix28_bad_scb_psl = (uint32)PSL;
    simhp4_fix28_bad_scb_ipl = (int32)ipl;
    simhp4_fix28_bad_scb_ei = (int32)ei;
    simhp4_fix28_bad_scb_seen = 1;
    ABORT (STOP_ILLVEC);                                /* preserve behavior */
    }'''
  s=s.replace(signature,replacement,1)
  require(s.count('SIMHP4_FIX28_SCB_CAPTURE_LINKED')==1 and
          s.count('ABORT (STOP_ILLVEC)')==1,
          'FIX28 SCB replacement invalid; NO FLASH')
  f.write_text(s,'utf8')
  return {'path':str(f), 'sha256':sha(s.encode('utf8'))}

def native_build(stage,logfile):
 env=os.environ.copy();env['IDF_PATH']=str(IDF)
 env['IDF_COMPONENT_CACHE_PATH']=str(ROOT/'s85cache')
 cmd=[sys.executable,str(IDF/'tools/idf.py'),'-B',str(stage/'project/build'),'build']
 err=deque(maxlen=30)
 with logfile.open('w',encoding='utf-8') as f:
  f.write('simhP4 R80 FIX28 native build only; NO FLASH; no COM9 access\n'+repr(cmd)+'\n')
  f.flush()
  p=subprocess.Popen(cmd,cwd=stage/'project',env=env,stdout=subprocess.PIPE,
     stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',bufsize=1)
  for ln in p.stdout:
   f.write(ln)
   if any(q in ln.lower() for q in ('error:','undefined reference','fatal error:','ld.exe:','collect2.exe:')):
    err.append(ln.rstrip());print(ln.rstrip(),flush=True)
  code=p.wait()
 if code:
  if err:
   for line in err:print(line,flush=True)
  else:
   print('Build exited without a compiler error line; last 60 lines:',flush=True)
   try:
    for line in logfile.read_text('utf8',errors='replace').splitlines()[-60:]:
     print(line,flush=True)
   except OSError:pass
 require(code==0,'ESP-IDF native compile failed, no flash. rc='+str(code))
 require('Project build complete.' in logfile.read_text('utf8',errors='replace'),
         'Missing native build success marker')


def single_image(raw):
 flash.candidate_identity(raw)
 ranges,checksum_pos=boot.parse_image(raw)
 missing=[tok.decode('ascii','replace') for tok in SNAPSHOT_MARKERS if tok not in raw]
 if missing:
  print('STOP: missing compiled-app marker(s): '+', '.join(repr(m) for m in missing),flush=True)
 require(not missing,'Not all release UI features are present in compiled app; NO FLASH')
 out=bytearray(raw);changes=[]
 def edit(a,b):
  require(len(a)==len(b),'Incorrect boot patch length')
  require(out.count(a)==1,'Expected exactly one boot string '+repr(a))
  pos=out.index(a)
  require(any(left<=pos and pos+len(a)<=right for left,right in ranges),
          'Boot string not inside ESP app segment')
  out[pos:pos+len(a)]=b;changes.append((hex(pos),a.decode('ascii','replace'),b.decode('ascii','replace')))
 if out.count(boot.MULTI)==1:
  edit(boot.MULTI,boot.SINGLE)
  if out.count(boot.NEW_BANNER+b'\x00')==1:edit(boot.NEW_BANNER+b'\x00',boot.OLD_BANNER_2)
  elif out.count(boot.OLD_BANNER_2)!=1:raise RuntimeError('Unknown boot banner, NO FLASH')
  if out.count(boot.NEWHEX)==1:edit(boot.NEWHEX,boot.OLDHEX)
  elif out.count(boot.OLDHEX)!=1:raise RuntimeError('Unknown boot-byte trace, NO FLASH')
 else:
  require(out.count(boot.SINGLE)==1 and out.count(boot.OLD_BANNER_2)==1 and out.count(boot.OLDHEX)==1,
    'Single-user boot command not verified, NO FLASH')
 if changes:
  checksum=0xef
  for a,b in ranges:
   for value in out[a:b]:checksum ^= value
  out[checksum_pos]=checksum
  out[-32:]=hashlib.sha256(out[:-32]).digest()
 result=bytes(out);boot.parse_image(result)
 require(result.count(boot.SINGLE)==1 and boot.MULTI not in result,
         'Final release boot is not single-user')
 require(len(result)<=flash.APP_PARTITION_SIZE,'APP exceeds factory partition')
 return result,changes


def report(data,log,build_log,bin_file,partition_table=None,otadata_blob=None,stage=None):
 tmp=REPORT.with_suffix('.tmp')
 with zipfile.ZipFile(tmp,'w',zipfile.ZIP_DEFLATED) as z:
  z.writestr('RESULT.json',json.dumps(data,ensure_ascii=False,indent=2)+'\n')
  z.writestr('ESPTOOL_LOG.txt','\n\n'.join(log))
  if build_log is not None and build_log.is_file():z.write(build_log,'idf_build.log')
  if bin_file is not None and bin_file.is_file():z.write(bin_file,'app.bin')
  if stage is not None and stage.is_dir():
   for name,rel in ((APPNAME+'.elf','project/build/'+APPNAME+'.elf'),
                    ('v13_main.cpp','project/main/v13_main.cpp'),
                    ('scp.c','third_party/simh_vax630_87eb/scp.c'),
                    ('vax_cpu1.c','third_party/simh_vax630_87eb/VAX/vax_cpu1.c'),
                    ('simhp4_vax630_io_esp_r14e.c','project/main/simhp4_vax630_io_esp_r14e.c')):
    item=stage/rel
    if item.is_file():z.write(item,'compiled/'+name)
  if partition_table is not None:z.writestr('PARTITION_TABLE_READONLY.bin',partition_table)
  if otadata_blob is not None:z.writestr('OTADATA_READONLY.bin',otadata_blob)
 tmp.replace(REPORT)

def main():
 state={'version':'SIMHP4_R80_RC14_FIX28_SCB_PROVENANCE_BUILD_ONLY','phase':'INIT','pass':False,
  'native_build':'NOT_ATTEMPTED','flash':'NOT_ATTEMPTED','backup':'NONE',
  'mode':'SINGLE_USER_ONLY','restore':'DISABLED','suspend_button':'REMOVED',
  'target':'BUILD_ONLY_NO_COM_PORT','report':str(REPORT), 'flash_blocked':True}
 logs=[];build_log=None;app=None;partition_table=None;otadata_blob=None;stage=None
 try:
  require(sys.platform=='win32','Windows ESP-IDF environment required')
  guard_package()
  src=donor_stage();state['donor']=str(src)
  stage=ROOT/'s114'/datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')
  stage.parent.mkdir(exist_ok=True)
  state['phase']='ISOLATED_STAGE'
  isolated_copy(src,stage)
  state['source_changes']=patch_sources(stage)
  console_file=stage/'project/main/simhp4_sim_console_esp_v14c.c'
  console_text=console_file.read_text('utf8')
  require(console_text.count('#if !defined(ESP_PLATFORM)') >= 1 and
          console_text.count('GUEST_INPUT_CTRL_ENQUEUED') == 1,
          'Guest SEND isolation not present in staged console; NO FLASH')
  state['guest_send_policy']='DISABLED_FOR_ESP32'
  state['qba_guard_patch']=patch_actual_qba(stage)
  state['qba_file_metadata_patch']=patch_qba_file_metadata(stage)
  state['vax_scb_patch']=patch_scb_fault_provenance(stage)
  v13_built_source=(stage/'project/main/v13_main.cpp').read_text('utf8')
  require('SIMHP4_FIX27_QBA_UNIT_SANITIZE' in v13_built_source and
          'SIMHP4_FIX27_QBA_UNIT_RESET_NO_RETRY' in v13_built_source and
          'SIMHP4_FIX26_ONE_SHOT_CONTINUE' not in v13_built_source,
          'FIX27 source identity / full QBA sanitize / no retry guard failed; NO FLASH')
  overlay_text=(stage/'project/main/simhp4_wifi_overlay.cpp').read_text('utf8')
  require('present_canvas()' in overlay_text and 'getBuffer()' in overlay_text and
          'M5.Display.setClipRect(' in overlay_text and
          'heap_caps_calloc(1,sizeof(simhp4_wifi_snapshot_t)' in overlay_text and
          'uxTaskGetStackHighWaterMark(nullptr)' in overlay_text and
          'if(scan_requested)' in overlay_text,
          'FIX22 Wi-Fi / presenter stack safety not staged; NO FLASH')
  state['wifi_ui']='PSRAM snapshot + tile-clipped sprite + first-paint-before-startup'
  state['presenter_stack']='8192 bytes unchanged; HWM reported at runtime'


  build_log=stage/'idf_build.log'
  state['phase']='NATIVE_BUILD'
  print('R80 FIX28 source-only: FIX27 QBA guards retained; capture VAX invalid SCB; NO FLASH',flush=True)
  native_build(stage,build_log);state['native_build']='PASS'
  require('vax_cpu1.c.obj' in build_log.read_text('utf8',errors='replace'),
          'Compiled VAX/vax_cpu1.c not found in Ninja build log; NO FLASH')
  require('scp.c.obj' in build_log.read_text('utf8',errors='replace'),
          'SIMH scp.c compile not evidenced; NO FLASH')
  require('simhp4_vax630_io_esp_r14e.c.obj' in build_log.read_text('utf8',errors='replace'),
          'Compiled ESP32-P4 Qbus source not found in Ninja build log; NO FLASH')
  raw_path=stage/'project/build'/(APPNAME+'.bin')
  require(raw_path.is_file(),'Compiled BIN missing after build')
  new,changes=single_image(raw_path.read_bytes())
  APP.write_bytes(new);require(APP.read_bytes()==new,'Prepared APP image mismatch')
  app=APP
  state.update({'phase':'APP_PREPARED','app_size':len(new),'app_sha256':sha(new),
   'boot_patches':changes})
  # FIX28: never inspect COM9, never write or verify the real flash.
  # Match the built APP to its ELF, then distribute source and both binaries.
  elf=stage/'project/build'/(APPNAME+'.elf')
  require(elf.is_file(),'Exact build ELF absent; stop before preparing release')
  elf_sha=sha(elf.read_bytes())
  require(bytes.fromhex(elf_sha) in new[:512],
           'APP does not embed the exact generated ELF SHA256; build not accepted')
  state.update({'phase':'BUILD_ONLY_COMPLETE','pass':True,'flash':'NOT_ATTEMPTED',
       'elf_sha256':elf_sha,'elf_path':str(elf),
       'verified':'SOURCE_COMPILE_AND_ELF_APP_MATCH_ONLY',
       'hardware_validation':'NOT_ATTEMPTED'})
  print('PASS: native source build and APP/ELF identity. NO FLASH, no COM9 access, physical validation pending.',flush=True)

 except Exception as e:
  state.update({'phase':state['phase']+'_STOP','error':repr(e),
                'traceback':traceback.format_exc()[-5000:]})
  print('STOP:',e,flush=True)
 finally:
  try:report(state,logs,build_log,app,partition_table,otadata_blob,stage)
  except Exception as e:print('REPORT ERROR:',e,flush=True)
  print('ONE REPORT:',REPORT,flush=True)
 return 0 if state['pass'] else 2
if __name__=='__main__':sys.exit(main())
