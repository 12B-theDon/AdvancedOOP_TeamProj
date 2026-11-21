"""Verify the complete file set and SHA-256 values of a packaged algorithm."""
import hashlib
from pathlib import Path
import re
import sys

def verify(folder):
    folder = Path(folder).resolve()
    expected = {}
    for line in (folder / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if not match:
            raise ValueError('Malformed SHA-256 manifest line')
        digest, name = match.groups()
        file = folder / name
        if name in expected or name == 'SHA256SUMS.txt' or file.is_symlink() or not file.resolve().is_relative_to(folder):
            raise ValueError(f'Unsafe/duplicate manifest path: {name}')
        expected[name] = digest
    actual = {p.relative_to(folder).as_posix() for p in folder.rglob('*') if p.is_file() and p != folder / 'SHA256SUMS.txt'}
    if actual != set(expected):
        raise ValueError(f'File set differs: missing={set(expected) - actual}, extra={actual - set(expected)}')
    for name, digest in expected.items():
        with (folder / name).open('rb') as stream:
            actual_digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual_digest != digest:
            raise ValueError(f'SHA-256 mismatch: {name}')
    return len(expected)

if __name__ == '__main__':
    try:
        print(f'Verified {verify(sys.argv[1])} packaged files.')
    except (OSError, ValueError, IndexError) as error:
        sys.exit(str(error))
