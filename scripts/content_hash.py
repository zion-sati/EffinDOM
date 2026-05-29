#!/usr/bin/env python3
"""
content_hash.py

Shared helper for EffinDom artifact naming.

Computes SHA-256 content hashes for EffinDom artifacts.

- Base64URL (unpadded) is used for filenames and manifest hashes.
- Standard Base64 is used for browser integrity strings (W3C SRI).
"""

from __future__ import annotations

import base64
import hashlib
import sys
from pathlib import Path

DEFAULT_HASH_LENGTH = 43


def sha256_digest(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def short_content_hash(data: bytes, length: int = DEFAULT_HASH_LENGTH) -> str:
    digest = base64.urlsafe_b64encode(sha256_digest(data)).decode("ascii").rstrip("=")
    return digest[:length]


def standard_content_hash(data: bytes) -> str:
    return base64.b64encode(sha256_digest(data)).decode("ascii")


def file_content_hash(path: str | Path, length: int = DEFAULT_HASH_LENGTH) -> str:
    return short_content_hash(Path(path).read_bytes(), length)


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(f"Usage: {Path(sys.argv[0]).name} <file> [hash-length]", file=sys.stderr)
        return 1

    file_path = Path(sys.argv[1])
    hash_length = int(sys.argv[2]) if len(sys.argv) == 3 else DEFAULT_HASH_LENGTH
    print(file_content_hash(file_path, hash_length))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
