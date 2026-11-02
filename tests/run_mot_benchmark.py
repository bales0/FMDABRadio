"""Historical/current real-driver replay. Synthetic clock, no RF/COM/network."""
from pathlib import Path
import json
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
BASE = '28097fdd1ed8dbc6414f21884738896c6db230d5'
HEAD = '1f196446e96488f6188840b5fb3134b55c892ee9'
def source(revision, path):
    if revision == 'worktree':
        return (ROOT / path).read_text(encoding='utf-8')
    return subprocess.run(['git', 'show', f'{revision}:{path}'], cwd=ROOT,
                          capture_output=True, check=True).stdout.decode('utf-8')

results = {}
with tempfile.TemporaryDirectory(prefix='fmdab_mot_bench_') as temp:
    for label, rev, enabled in [('old', BASE, 0), ('head-off', HEAD, 0),
                                ('head-on', HEAD, 1), ('current-off', 'worktree', 0),
                                ('current-on', 'worktree', 1)]:
        folder = Path(temp) / label
        folder.mkdir()
        header = source(rev, 'src/si4684.h').replace('class DAB {', 'class DAB {\n  friend struct DabHostAccess;', 1)
        (folder / 'si4684.h').write_text(header, encoding='utf-8')
        (folder / 'mot_assembly_policy.h').write_text(source(rev, 'src/mot_assembly_policy.h'), encoding='utf-8')
        driver = folder / 'si4684.cpp'
        driver.write_text(source(rev, 'src/si4684.cpp'), encoding='utf-8')
        exe = folder / 'bench.exe'
        flags = ['-DFMDAB_HOST_TEST', '-Wno-error=switch', f'-DEPG_DISCOVERY_ENABLE={enabled}',
                 f'-DFMDAB_BENCH_L1={0 if label == "old" else 1}']
        if rev == 'worktree':
            flags += ['-DFMDAB_BENCH_CURRENT']
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic', *flags,
                        '-I' + str(folder), '-I' + str(ROOT / 'tests/radio_shims'),
                        '-I' + str(ROOT / 'tests/host_shims'), '-I' + str(ROOT / 'src'),
                        str(ROOT / 'tests/test_mot_drain_bench.cpp'), str(driver), '-o', str(exe)], check=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError(f'{label}: {run.stdout}\n{run.stderr}')
        if run.stderr:
            print(label, run.stderr, flush=True)
        rows = [json.loads(line) for line in run.stdout.splitlines()]
        results[label] = {row['case']: row for row in rows}
        print(label, json.dumps(rows, separators=(',', ':')), flush=True)

for name in results['current-on']:
    off, on = results['current-off'][name], results['current-on'][name]
    assert (off['complete'], off['hash'], off['reads']) == (on['complete'], on['hash'], on['reads'])
    assert abs(on['firstToCompleteMs'] - off['firstToCompleteMs']) <= 3
    assert on['UA'] >= 17 and on['UA'] % 17 == 0 and off['UA'] == 0
    assert on['retained'] and off['retained']
    for label in ['head-off', 'head-on']:
        assert results[label][name]['complete'] == on['complete']
        assert results[label][name]['hash'] == on['hash']
for name in ['gap45', 'gap90', 'gap180']:
    assert not results['old'][name]['retained'] and results['old'][name]['complete'] == 0
    assert results['current-on'][name]['complete'] == 1
assert results['current-on']['two-TID']['complete'] == 2
assert results['current-on']['two-TID']['firstToCompleteMs'] < results['head-on']['two-TID']['firstToCompleteMs']
print('MOT synthetic A/B PASS: exact bytes/completion, ON/OFF bounded command latency, 17 UA, historical idle-loss reproduced', flush=True)
