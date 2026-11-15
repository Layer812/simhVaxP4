import hashlib, importlib.util, json, tempfile
from pathlib import Path

spec=importlib.util.spec_from_file_location('gate',str(Path(__file__).with_name('source_completion_gate.py')))
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)

def sha(x):return hashlib.sha256(x).hexdigest()
def die_check(fn, needle):
    try:fn()
    except RuntimeError as e:
        assert needle in str(e), (str(e),needle)
    else:raise AssertionError('expected safe stop: '+needle)

with tempfile.TemporaryDirectory() as d:
    root=Path(d);here=root/'firmware'/'r80_fix28';full=here/'full_project';full.mkdir(parents=True);(here/'legacy_staging').mkdir()
    known={f'file{x}.cpp':sha(f'sample{x}'.encode()) for x in range(7)}
    (here/'legacy_staging'/'BASE_SOURCE_SHA256.json').write_text(json.dumps(known))
    original={'status':'IMPORTED_FROM_SHA_PINNED_LOCAL_BASE','donor_source_sha256':known,'included':[],'excluded':[{'path':gate.REQUIRED,'reason':'unrecognized extension'}]}
    paths=set(gate.MUST_EXIST)
    paths.update(f'project/main/verified_{i}.c' for i in range(1000))
    paths.update('project/main/'+name for name in known)
    for rel in sorted(paths):
        f=full/rel;f.parent.mkdir(parents=True,exist_ok=True)
        content=(f'#include "BMI270_config.inl"\nint x = 1;\n' if rel.endswith('BMI270_Class.cpp') else 'int x=1;\n').encode()
        f.write_bytes(content)
        original['included'].append({'path':rel,'sha256':sha(content),'bytes':len(content)})
    (full/'SOURCE_IMPORT_MANIFEST.json').write_text(json.dumps(original))
    gate.HERE=here;gate.FULL=full;gate.ORIGINAL=full/'SOURCE_IMPORT_MANIFEST.json';gate.REPAIRED=full/'SOURCE_COMPLETION_MANIFEST.json';gate.BASE_SHA=here/'legacy_staging'/'BASE_SOURCE_SHA256.json'
    data=gate.audit_original();assert len(data['included'])>1000
    assert gate.verify_completion() is False
    die_check(lambda:gate.main(), 'required') if False else None
    donor=root/'donor'; p=donor/gate.REQUIRED;p.parent.mkdir(parents=True);p.write_bytes(b'const unsigned char sensor_config[] = {1,2,3};\n')
    gate.strict_repo=lambda:root
    gate.find_donor=lambda original:donor
    gate.run=lambda *a,**kw:'MOCK_GIT_OK'
    gate.publish_once(data)
    assert gate.verify_completion() is True
    assert p.read_bytes()==(full/gate.REQUIRED).read_bytes()
    print('PASS: source SHA-256 audit, exact donor source publication, no overwrite')
    q=full/gate.REQUIRED
    q.write_bytes(b'bad\n')
    die_check(gate.verify_completion, 'differ')
    q.write_bytes(p.read_bytes())
    (full/'project/main/verified_2.c').write_bytes(b'tamper\n')
    die_check(gate.audit_original, 'changed or incomplete')
    print('PASS: changed original + repaired sources are rejected')
    (full/'project/main/verified_2.c').write_bytes(b'int x=1;\n')
    original['excluded'].append({'path':'components/M5Unified/src/utility/another_config.ipp','reason':'unrecognized extension'})
    gate.ORIGINAL.write_text(json.dumps(original))
    die_check(gate.audit_original,'unexpected excluded compile inputs')
    print('PASS: a second missing build-source extension blocks compilation')
