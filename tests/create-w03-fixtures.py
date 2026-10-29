# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Write authored image/browser fixtures for obs-w03.lua. Uses the standard library."""
import argparse
import pathlib
import struct
import zlib


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def calibration(width):
    height = width * 9 // 16
    rows = bytearray()
    for y in range(height):
        row = bytearray(bytes((0, 255, 0, 255)) * width)
        if height // 4 <= y <= height * 3 // 4:
            for x in range(width // 8, width * 3 // 8 + 1):
                row[x * 4:x * 4 + 4] = bytes((255, 0, 0, 255))
            cy, ry = height / 2, height / 4
            cx, rx = width * 11 / 16, width * 3 / 16
            for x in range(width // 2, width * 7 // 8 + 1):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1:
                    row[x * 4:x * 4 + 4] = bytes((255, 255, 255, 128))
        rows.append(0)  # PNG filter: none.
        rows.extend(row)
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    for width in (1920, 3840):
        (args.directory / f"input-{width}.png").write_bytes(calibration(width))
    (args.directory / "browser.html").write_text(
        '<!doctype html><body style="margin:0;background:#00ff00">'
        '<div style="position:absolute;left:10%;top:20%;width:30%;height:60%;'
        'background:#ff0080;animation:move 2s alternate infinite"></div>'
        '<div style="position:absolute;left:60%;top:30%;color:white;'
        'font:100px sans-serif">W03<br>Browser</div>'
        '<style>@keyframes move {to{transform:translateX(80px)}}</style></body>',
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
