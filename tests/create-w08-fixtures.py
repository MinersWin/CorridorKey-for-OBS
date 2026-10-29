# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Moritz Mantel <admin@moritz-mantel.de>
"""Write original SDR/alpha fixtures for obs-w08.lua; no external image library."""
import pathlib
import struct
import sys
import zlib


def png(path, pixel):
    width, height = 1920, 1080
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixel(x, y))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def calibration(x, y):
    if 140 < x < 540 and 180 < y < 820:
        return (255, 0, 255, 255)
    if ((x - 1000) / 300) ** 2 + ((y - 520) / 340) ** 2 < 1:
        return (128, 128, 128, 128)
    if 1400 < x < 1770 and 180 < y < 820:
        alpha = max(0, min(255, round((x - 1400) / 370 * 255)))
        return (210, 150, 100, alpha)
    if 100 < y < 970 and 580 < x < 660:
        return (255, 255, 255, 255 if x % 8 < 2 else 0)
    return (0, 255, 0, 255)


destination = pathlib.Path(sys.argv[1])
destination.mkdir(parents=True, exist_ok=True)
png(destination / "calibration.png", calibration)
png(destination / "checker.png", lambda x, y: (48, 48, 48, 255) if (x // 60 + y // 60) % 2 else (160, 160, 160, 255))
