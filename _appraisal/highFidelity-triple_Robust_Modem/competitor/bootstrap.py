#!/usr/bin/env python3
"""Fetch checksum-pinned optional native comparator binaries, without installing.

Debian 13 amd64 runtime libraries are required. These packages are a disposable
cache, not vendored modem implementations. No codec or channel DSP runs here.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".build" / "competitors"
PACKAGES = (
    ("https://deb.debian.org/debian/pool/main/w/wsjtx/"
     "wsjtx_3.0.2+dfsg-2~bpo13+1_amd64.deb",
     "b02197aad6b4a8610f50458e80d9a5de4d9c59711f6540e3d3f6ea79e855f177"),
    ("https://deb.debian.org/debian/pool/main/w/wsjtx/"
     "wsjtx-data_3.0.2+dfsg-2~bpo13+1_all.deb",
     "e9469f477da8961387ec97d574239dcb5dda1e2a81e758f6501dd1663812e418"),
    ("https://security.debian.org/debian-security/pool/updates/main/x/xorg-server/"
     "xvfb_21.1.16-1.3+deb13u4_amd64.deb",
     "893d87bf159b6de077a929fc796a73c39f837d45310b2076e3eeebebb80552b6"),
)


def main():
    packages = CACHE / "packages"
    packages.mkdir(parents=True, exist_ok=True)
    for url, expected in PACKAGES:
        archive = packages / url.rsplit("/", 1)[-1]
        if not archive.exists():
            temporary = archive.with_suffix(".partial")
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read(12 * 1024 * 1024)
                if response.read(1):
                    raise RuntimeError("package exceeds predeclared 12 MiB bound")
            temporary.write_bytes(data)
            if hashlib.sha256(data).hexdigest() != expected:
                temporary.unlink()
                raise RuntimeError("download checksum mismatch")
            temporary.rename(archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"cache checksum mismatch: {archive}")
        subprocess.run(["dpkg-deb", "-x", str(archive), str(CACHE / "root")], check=True)
        print(json.dumps({"url": url, "sha256": expected, "bytes": archive.stat().st_size}))
    print(f"Native comparator tools: {CACHE / 'root/usr/bin'}")


if __name__ == "__main__":
    main()
