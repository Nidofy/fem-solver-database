"""Check deterministic fixture bytes; set SDB_DBF12_PEER_SPECTRA for a cross-OS run."""
import hashlib
import os
from pathlib import Path
import sys

CANONICAL = {
    "modes.raw": "67b2f1634cbd83dee31c29e445a0bc7df91834145d08c4534aea8cc4ad38f34e",
    "frequency.rle": "84b4bf80b5aa0c4864a141c63647f1dbd0568ca1c378f0f48f9a303df8de00ea",
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
