"""Check deterministic fixture bytes; set SDB_DBF12_PEER_SPECTRA for a cross-OS run."""
import hashlib
import os
from pathlib import Path
import sys

CANONICAL = {
    "modes.raw": "ee7cf861aeddc550c3d0a06c706b4d1b0777185b7fc2c9e9b326520688179bd9",
    "frequency.rle": "f727e7cef7a3ae5c28fe5c4bdcfe12a33cd0248596884ec10844d4124dc41bbb",
}


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as source:
        for data in iter(lambda: source.read(65536), b""):
            result.update(data)
    return result.hexdigest()


def main():
    root = Path(sys.argv[1])
    zstd = sys.argv[2] == "1"
    peer_dir = os.environ.get("SDB_DBF12_PEER_SPECTRA")
    for name in [*CANONICAL, *(["frequency.zst"] if zstd else [])]:
        path = root / name
        actual = digest(path)
        if name in CANONICAL and CANONICAL[name] != actual:
            raise ValueError(f"{name}: canonical wire digest mismatch")
        if peer_dir:
            peer = Path(peer_dir) / name
            if path.samefile(peer):
                raise ValueError("cross-OS fixtures must be independent files")
            with path.open("rb") as a, peer.open("rb") as b:
                while True:
                    left, right = a.read(65536), b.read(65536)
                    if left != right:
                        raise ValueError(f"{name}: cross-system archive bytes differ")
                    if not left:
                        break
        print(f"{name} bytes={path.stat().st_size} sha256={actual} peer_compared={bool(peer_dir)}")


if __name__ == "__main__":
    main()
