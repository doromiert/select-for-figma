#!/usr/bin/env python3
"""Fetch a pinned stable Linux x86_64 CEF SDK; verify it before extraction."""
import argparse
import hashlib
import pathlib
import tarfile
import tempfile
import urllib.request

NAME = "cef_binary_150.0.22+gc1a097e+chromium-150.0.7871.256_linux64_minimal.tar.bz2"
SHA256 = "dd1b5cc3ac6efe659ec7e8e9b0677ba4e43216a2fefcafd5abc9340837e32d96"
ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=pathlib.Path, default=ROOT / ".deps")
    args = parser.parse_args()
    destination = args.destination.resolve()
    if (destination / "include/cef_version.h").exists():
        version = (destination / "include/cef_version.h").read_text()
        if "150.0.22+gc1a097e+chromium-150.0.7871.256" in version:
            print(f"Pinned CEF SDK already present: {destination}")
            return
        raise SystemExit("Another SDK is already present. Choose an empty destination.")
    if destination.exists() and any(destination.iterdir()):
        raise SystemExit("Destination must be empty.")
    with tempfile.TemporaryDirectory(prefix="select-for-figma-cef-") as temporary:
        archive = pathlib.Path(temporary) / NAME
        print("Downloading stable CEF (approximately 300 MB)…", flush=True)
        digest = hashlib.sha256()
        with urllib.request.urlopen(f"https://cef-builds.spotifycdn.com/{NAME}", timeout=60) as response, archive.open("wb") as output:
            while block := response.read(1024 * 1024):
                digest.update(block)
                output.write(block)
        if digest.hexdigest() != SHA256:
            raise SystemExit("SDK checksum mismatch; refusing to extract.")
        print("Checksum verified. Extracting…", flush=True)
        extracted = pathlib.Path(temporary) / "extracted"
        with tarfile.open(archive, "r:bz2") as sdk:
            sdk.extractall(extracted, filter="data")
        folder, = extracted.iterdir()
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.exists():
            destination.rmdir()
        import shutil
        shutil.move(str(folder), str(destination))
    print(f"CEF ready: {destination}")


if __name__ == "__main__":
    main()
