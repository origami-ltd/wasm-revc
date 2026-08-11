"""
Pull a Vice City install out of a CD image.

The PC release is two discs: disc 1 installs models/, data/, anim/ and TEXT/, and disc 2 ("Play")
carries audio/. Both are plain ISO9660 inside a raw MODE1/2352 .bin, which no macOS tool mounts,
hence this.

    python3 scripts/extract-disc.py disc.bin                 # list what is on it
    python3 scripts/extract-disc.py disc.bin --to ~/GTAVC    # extract into an install tree

Paths are lowercased on the way out: reVC opens them with mixed case and its casepath() fallback
resolves the rest.
"""
import struct, sys

RAW, USER, OFF = 2352, 2048, 16

class Disc:
    def __init__(self, path):
        self.f = open(path, "rb")

    def sector(self, lba):
        self.f.seek(lba * RAW + OFF)
        return self.f.read(USER)

    def read(self, lba, length):
        out = bytearray()
        while len(out) < length:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:length])


def records(data):
    i = 0
    while i < len(data):
        n = data[i]
        if n == 0:
            i = (i // USER + 1) * USER          # records never straddle a sector
            if i >= len(data):
                break
            continue
        r = data[i:i + n]
        lba, size, flags = struct.unpack("<I", r[2:6])[0], struct.unpack("<I", r[10:14])[0], r[25]
        name = r[33:33 + r[32]].split(b";")[0].decode("latin-1")
        if name not in ("\x00", "\x01"):
            yield name, lba, size, bool(flags & 2)
        i += n


def walk(disc, lba, size, prefix=""):
    for name, child_lba, child_size, is_dir in records(disc.read(lba, size)):
        path = f"{prefix}/{name}" if prefix else name
        if is_dir:
            yield from walk(disc, child_lba, child_size, path)
        else:
            yield path, child_lba, child_size


def open_disc(path):
    disc = Disc(path)
    pvd = disc.sector(16)
    assert pvd[1:6] == b"CD001", "not ISO9660"
    root = pvd[156:156 + 34]
    return disc, struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0]


if __name__ == "__main__":
    disc, root_lba, root_size = open_disc(sys.argv[1])
    if len(sys.argv) > 3 and sys.argv[2] == "--to":
        import os, pathlib
        out = pathlib.Path(os.path.expanduser(sys.argv[3]))
        written = 0
        for path, lba, size in walk(disc, root_lba, root_size):
            target = out / path.lower()
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(disc.read(lba, size))
            written += size
        print(f"extracted {written / 2**20:.0f} MB into {out}")
    else:
        files = sorted(walk(disc, root_lba, root_size), key=lambda e: -e[2])
        print(f"{len(files)} files, {sum(e[2] for e in files) / 2**20:.0f} MB total\n")
        for path, _, size in files[:40]:
            print(f"{size / 2**20:10.2f} MB  {path}")
