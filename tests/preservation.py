"""Verify immutable originals/settings and exact moved training function bodies."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
root = Path(sys.argv[1]).resolve()
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
baseline = json.loads((root / 'docs/baseline/original_data.json').read_text(encoding='utf-8'))
actual = {str(p.relative_to(root)): digest(p) for p in (root / 'data/original').rglob('*') if p.is_file()}
assert actual == baseline['files'], 'Original file contents/names changed'
assert sorted(str(p.relative_to(root)) for p in (root / 'data/original').rglob('*') if p.is_dir()) == baseline['directories']
implementation = json.loads((root / 'docs/baseline/implementation.json').read_text(encoding='utf-8'))
for name, expected in implementation['sources'].items():
    if name.endswith('train.cpp') or name == 'tools/prepare_deepprint.py':
        continue
    path = root / name
    if not path.exists():
        path = root / name.replace('src/Algorithm/', 'src/Training/')
    if name.endswith('augment.cpp'):
        previous = subprocess.check_output(['git', 'show', implementation['commit'] + ':' + name], cwd=root, text=True, encoding='utf-8')
        current = path.read_text(encoding='utf-8').replace('"evaluationConfig.h"', '"../Engine/engineConfig.h"').replace('training::', 'engine::')
        assert current == previous, 'Augmentation behavior changed'
    else:
        assert digest(path) == expected, f'Configuration/algorithm changed: {path}'
for name, expected in implementation['weights'].items():
    assert digest(root / name) == expected, f'Weights changed: {name}'
old = subprocess.check_output(['git', 'show', implementation['commit'] + ':src/Algorithm/train.cpp'], cwd=root, text=True, encoding='utf-8')
new = (root / 'src/Training/train.cpp').read_text(encoding='utf-8').replace('training::', 'engine::')
validation = (root / 'src/Training/validation.cpp').read_text(encoding='utf-8').replace('training::', 'engine::')
for begin, end, target in [
    ('static bool Trainable(', 'namespace {\nstruct Pair', new),
    ('namespace {\nstruct Pair', 'int RunTraining(', validation),
    ('int RunTraining(', '} // namespace algorithm', new),
]:
    expected = old[old.index(begin):old.index(end, old.index(begin))].strip()
    assert expected in target, f'Moved training behavior changed: {begin}'
print(f'Preserved {len(actual)} original files, original directories, weights, settings, training and validation function bodies.')

old_engine = subprocess.check_output(['git', 'show', implementation['commit'] + ':src/Engine/Engine.cpp'], cwd=root, text=True, encoding='utf-8')
new_engine = (root / 'src/Engine/Engine.cpp').read_text(encoding='utf-8')
start = old_engine.index('bool Engine::PlanComparisons')
end = old_engine.index('std::vector<ModelState> Engine::SelectModels')
assert old_engine[start:end].strip() in new_engine, 'Comparison planning/randomization/replay changed'
