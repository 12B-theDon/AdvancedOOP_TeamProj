import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
root = Path(sys.argv[1]).resolve()
spec = importlib.util.spec_from_file_location('manifest', root / 'tools/verify_manifest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    (folder / 'a.dll').write_bytes(b'fixture')
    manifest = folder / 'SHA256SUMS.txt'
    manifest.write_text(hashlib.sha256(b'fixture').hexdigest() + '  a.dll\n')
    assert module.verify(folder) == 1
    (folder / 'a.dll').write_bytes(b'changed')
    try:
        module.verify(folder)
        raise AssertionError('Mutation was accepted')
    except ValueError:
        pass
    manifest.write_text('0' * 64 + '  ../outside\n')
    try:
        module.verify(folder)
        raise AssertionError('Traversal was accepted')
    except ValueError:
        pass
    result = subprocess.run([sys.argv[2], '-DMODELS=' + temp, '-DDESTINATION=' + str(folder / 'dist'), '-P', str(root / 'cmake/PackageAlgorithm.cmake')], capture_output=True, text=True, encoding='utf-8', errors='replace')
    assert result.returncode != 0 and 'Missing production weight' in result.stderr
    assert not (folder / 'dist').exists()
print('Manifest validation/mutation/path handling and missing-weight packaging rejection passed.')

# Common destination validation is portable; never replace source/originals.
with tempfile.TemporaryDirectory() as temp:
    script = Path(temp) / 'check.cmake'
    script.write_text('include("' + (root / 'cmake/PackageCommon.cmake').as_posix() + '")\ncheck_destination()\n')
    for destination in [root, root.parent, root / 'data/original', root / 'data/original/subfolder']:
        result = subprocess.run([sys.argv[2], '-DSOURCE=' + str(root), '-DDESTINATION=' + str(destination), '-P', str(script)], capture_output=True, text=True, encoding='utf-8', errors='replace')
        assert result.returncode != 0, destination
    result = subprocess.run([sys.argv[2], '-DSOURCE=' + str(root), '-DDESTINATION=' + str(Path(temp) / 'valid'), '-P', str(script)], capture_output=True, text=True, encoding='utf-8', errors='replace')
    assert result.returncode == 0, result.stderr
