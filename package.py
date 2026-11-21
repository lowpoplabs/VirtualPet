"""Builds the release zip: releases/BaandwidthFarms-<version>.zip

    python package.py

Build the watch firmware first (firmware\\build.ps1 -Board watch). This builds
the site from it, checks that the firmware carries the version in
firmware/version.txt, and zips:

    README.md, CHANGELOG.md, LICENSE, INSTALL.txt
    installer/            the built site: pages, browser installer, manifest
    installer/firmware/   the four parts the installer (or esptool) writes

Entry names are written with forward slashes whatever the system.
"""
import re
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "firmware" / "build-watch"
OUT = ROOT / "site" / "out"


def main() -> None:
    version = (ROOT / "firmware" / "version.txt").read_text(encoding="utf-8").split()[0]

    app = BUILD / "virtualpet.bin"
    if not app.exists():
        sys.exit(r"no watch firmware: run firmware\build.ps1 -Board watch first")
    # The app description sits after the image and segment headers; its
    # version field is 32 bytes at offset 48.
    built = app.read_bytes()[48:80].split(b"\0")[0].decode("ascii", "replace")
    if built != version:
        sys.exit(f"the firmware in {BUILD.name} says version {built!r}, version.txt says "
                 f"{version!r}: rebuild it")

    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(ROOT / "site" / "build.ps1"), "-Version", version], check=True)

    # The esptool line, from the same list the build wrote.
    lines = (BUILD / "flash_args").read_text(encoding="utf-8").splitlines()
    options = lines[0].strip()
    parts = []
    for line in lines[1:]:
        m = re.match(r"^(0x[0-9a-fA-F]+)\s+(\S+)$", line.strip())
        if m:
            parts.append((int(m.group(1), 16), m.group(1), Path(m.group(2)).name))
    parts.sort()
    for _, _, name in parts:
        if not (OUT / "firmware" / name).exists():
            sys.exit(f"site build did not copy firmware/{name}")
    flash = " ".join(f"{addr} {name}" for _, addr, name in parts)

    install = f"""Baandwidth Farms {version}
For the Waveshare ESP32-S3-Touch-AMOLED-2.06 watch.

Either way: plug the watch into a computer with a USB-C cable that carries
data. Erase the watch the first time only. When updating a watch that already
has a pet, do not erase, or the pet and its farm go too.

1. From the browser (Chrome or Edge)

   In this folder, run:

       python -m http.server --directory installer

   Open http://localhost:8000 and press the Install button. The page also has
   the manual.

2. With esptool

   In the installer/firmware folder, with COM5 changed to the watch's port:

       esptool --chip esp32s3 -p COM5 write_flash {options} {flash}

   Write the four parts separately, as above. The pet's save sits in flash
   between two of them, and one joined file would overwrite it.

Afterwards: swipe up, open Settings and set the clock.
"""

    releases = ROOT / "releases"
    releases.mkdir(exist_ok=True)
    target = releases / f"BaandwidthFarms-{version}.zip"
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("README.md", "CHANGELOG.md", "LICENSE"):
            z.write(ROOT / name, name)
        z.writestr("INSTALL.txt", install)
        for p in sorted(OUT.rglob("*")):
            if p.is_file():
                z.write(p, "installer/" + p.relative_to(OUT).as_posix())
        count = len(z.namelist())
    print(f"{target.relative_to(ROOT)}: {count} entries, {target.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
