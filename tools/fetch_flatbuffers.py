#!/usr/bin/env python3
"""Download the pinned FlatBuffers source tree and Linux flatc binary.

The C# NuGet package for this release was never published, so the client
compiles the C# runtime from the same tag that flatc and the C++ headers
come from. Checksums are pinned to that GitHub release.
"""

from __future__ import annotations

import hashlib
import io
import shutil
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "25.12.19"
SOURCE_URL = f"https://github.com/google/flatbuffers/archive/refs/tags/v{VERSION}.tar.gz"
FLATC_URL = (
    f"https://github.com/google/flatbuffers/releases/download/v{VERSION}/"
    "Linux.flatc.binary.g++-13.zip"
)
SOURCE_SHA256 = "f81c3162b1046fe8b84b9a0dbdd383e24fdbcf88583b9cb6028f90d04d90696a"
FLATC_SHA256 = "9f87066dc5dfa7fe02090b55bab5f3e55df03e32c9b0cdf229004ade7d091039"

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / ".deps" / "flatbuffers"
MARKER = DEST / "VERSION"


def _download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "antor-engine-fetch"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def _check(label: str, data: bytes, expected: str) -> None:
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected:
        raise SystemExit(f"{label} checksum mismatch: {digest} != {expected}")


def _ready() -> bool:
    header = DEST / "src" / "include" / "flatbuffers" / "flatbuffers.h"
    runtime = DEST / "src" / "net" / "FlatBuffers" / "FlatBufferBuilder.cs"
    flatc = DEST / "flatc"
    if not (MARKER.is_file() and header.is_file() and runtime.is_file() and flatc.is_file()):
        return False
    return MARKER.read_text(encoding="utf-8").strip() == VERSION


def main() -> int:
    if _ready():
        return 0

    print(f"fetching FlatBuffers {VERSION}", file=sys.stderr)
    source = _download(SOURCE_URL)
    flatc_zip = _download(FLATC_URL)
    _check("flatbuffers source", source, SOURCE_SHA256)
    _check("flatc", flatc_zip, FLATC_SHA256)

    if DEST.exists():
        shutil.rmtree(DEST)
    DEST.mkdir(parents=True)

    with tarfile.open(fileobj=io.BytesIO(source), mode="r:gz") as archive:
        prefix = f"flatbuffers-{VERSION}/"
        for member in archive.getmembers():
            if not member.name.startswith(prefix) or member.issym() or member.islnk():
                continue
            relative = member.name[len(prefix) :]
            if not relative or ".." in Path(relative).parts:
                continue
            target = DEST / "src" / relative
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            extracted = archive.extractfile(member)
            if extracted is None:
                continue
            with extracted, target.open("wb") as out:
                shutil.copyfileobj(extracted, out)

    with zipfile.ZipFile(io.BytesIO(flatc_zip)) as archive:
        names = [name for name in archive.namelist() if Path(name).name == "flatc"]
        if len(names) != 1:
            raise SystemExit(f"unexpected flatc archive contents: {archive.namelist()}")
        flatc_path = DEST / "flatc"
        with archive.open(names[0]) as src, flatc_path.open("wb") as out:
            shutil.copyfileobj(src, out)
        flatc_path.chmod(0o755)

    header = DEST / "src" / "include" / "flatbuffers" / "flatbuffers.h"
    if not header.is_file():
        raise SystemExit("flatbuffers headers missing after extract")
    MARKER.write_text(VERSION + "\n", encoding="utf-8")
    print(f"flatbuffers {VERSION} ready", file=sys.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # noqa: BLE001 - top-level fetch failure should be one message
        print(f"fetch_flatbuffers: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
