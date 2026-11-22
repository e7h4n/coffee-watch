"""Check and describe the native Coffee Watch application for the ESP Hub StopWatch slot."""
import hashlib
import json
import sys
from pathlib import Path

PROFILE = b"ESPHUB1/micropython/stopwatch\0"


def describe(image, version):
    if not 320 <= len(image) <= 0x400000 or image[0] != 0xE9 or image[12:14] != b"\x09\x00":
        raise ValueError("Expected an ESP32-S3 application within the 4 MiB slot")
    if image[32:36] != bytes.fromhex("3254cdab") or not image[288:].startswith(PROFILE):
        raise ValueError("Missing ESP Hub StopWatch application descriptor")
    built_version = image[48:80].split(b"\0", 1)[0].decode()
    if built_version != version:
        raise ValueError(f"Image version {built_version!r} differs from {version!r}")
    return dict(board="stopwatch", kind="micropython", native=True, version=version,
                size=len(image), sha256=hashlib.sha256(image).hexdigest(),
                image_profile=PROFILE[:-1].decode(), app_offset=0x420000)


if __name__ == "__main__":
    binary, version, repository = sys.argv[1:]
    binary = Path(binary)
    manifest = describe(binary.read_bytes(), version)
    manifest.update(file=binary.name, url=f"https://github.com/{repository}/releases/download/{version}/{binary.name}")
    binary.with_name("esphub.json").write_text(json.dumps(manifest, indent=2) + "\n")
