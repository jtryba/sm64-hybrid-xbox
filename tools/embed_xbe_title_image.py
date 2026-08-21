#!/usr/bin/env python3
"""Embed SM64's ROM-extracted Mario-head title icon into an Xbox XBE.

This targets the actual dashboard/launcher title-image mechanism:
an XPR0 TitleImage.xbx stored in an inserted-file XBE section named
"$$XTIMAGE".

It intentionally does NOT modify the XBE header's 100x17 boot-logo bitmap.

The generated title XPR is 128x128 DXT1:
  2048-byte XPR0 header + 8192-byte BC1/DXT1 payload = 0x2800 bytes.

The script appends the inserted-file section without shifting any existing
raw section. It relocates only the section-header table into unused header
slack, preserving all existing section data offsets and the rounded header
page used by cxbe.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

SOURCE_W = 16
SOURCE_H = 16
SOURCE_BYTES = SOURCE_W * SOURCE_H * 2

TITLE_W = 128
TITLE_H = 128

XPR_MAGIC = 0x30525058
XPR_HEADER_SIZE = 0x800
XPR_TITLE_TOTAL_SIZE = 0x2800
XPR_COMMON = 0x00040001
XPR_FORMAT_DXT1_128 = 0x07710C29
XPR_SIZE = 0
XPR_EOH = 0xFFFFFFFF

XBE_MAGIC = 0x48454258
XBE_SECTION_HEADER_SIZE = 0x38
XBE_INSERTED_READONLY_FLAGS = 0x38
XBE_TITLE_SECTION_NAME = "$$XTIMAGE"

OFF_BASE = 0x104
OFF_HEADERS_SIZE = 0x108
OFF_IMAGE_SIZE = 0x10C
OFF_SECTION_COUNT = 0x11C
OFF_SECTION_HEADERS_ADDR = 0x120

CRITICAL_HEADER_U32_OFFSETS = [
    0x104,  # base
    0x110,  # image header size
    0x114,  # timestamp
    0x118,  # certificate
    0x124,  # init flags
    0x128,  # entry
    0x12C,  # TLS
    0x130, 0x134, 0x138,  # PE stack/heap
    0x13C, 0x140, 0x144, 0x148, 0x14C, 0x150, 0x154,
    0x158,  # kernel thunk
    0x15C,  # import dir
    0x160, 0x164, 0x168, 0x16C,  # library fields
    0x170, 0x174,  # 100x17 boot-logo addr/size
]


def align_up(v: int, a: int) -> int:
    return (v + a - 1) & ~(a - 1)


def u32(data: bytes | bytearray, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def put_u32(data: bytearray, off: int, value: int) -> None:
    struct.pack_into("<I", data, off, value)


def expand5(v: int) -> int:
    return (v << 3) | (v >> 2)


def decode_rgba16(raw: bytes):
    if len(raw) != SOURCE_BYTES:
        raise ValueError(f"expected {SOURCE_BYTES} source bytes, got {len(raw)}")
    out = []
    for i in range(0, len(raw), 2):
        v = (raw[i] << 8) | raw[i + 1]
        r = expand5((v >> 11) & 31)
        g = expand5((v >> 6) & 31)
        b = expand5((v >> 1) & 31)
        a = 255 if (v & 1) else 0
        out.append((r, g, b, a))
    return out


def upscale_nearest(src, width: int, height: int):
    sx = width // SOURCE_W
    sy = height // SOURCE_H
    if sx * SOURCE_W != width or sy * SOURCE_H != height:
        raise ValueError("destination dimensions must be integer multiples of source")
    out = []
    for y in range(height):
        row = (y // sy) * SOURCE_W
        for x in range(width):
            out.append(src[row + x // sx])
    return out


def rgb888_to_565(rgb):
    r, g, b = rgb
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def rgb565_to_888(c: int):
    return (
        (((c >> 11) & 31) * 255 + 15) // 31,
        (((c >> 5) & 63) * 255 + 31) // 63,
        ((c & 31) * 255 + 15) // 31,
    )


def dist2(a, b):
    return sum((int(a[i]) - int(b[i])) ** 2 for i in range(3))


def choose_endpoints(colors):
    if not colors:
        return 0, 0
    unique = list(dict.fromkeys(colors))
    if len(unique) == 1:
        c = rgb888_to_565(unique[0])
        return c, c
    best_i, best_j, best_d = 0, 1, -1
    for i in range(len(unique)):
        for j in range(i + 1, len(unique)):
            d = dist2(unique[i], unique[j])
            if d > best_d:
                best_i, best_j, best_d = i, j, d
    return rgb888_to_565(unique[best_i]), rgb888_to_565(unique[best_j])


def palette(c0: int, c1: int):
    p0 = rgb565_to_888(c0)
    p1 = rgb565_to_888(c1)
    if c0 > c1:
        p2 = tuple((2 * p0[i] + p1[i]) // 3 for i in range(3))
        p3 = tuple((p0[i] + 2 * p1[i]) // 3 for i in range(3))
        return [(*p0, 255), (*p1, 255), (*p2, 255), (*p3, 255)]
    p2 = tuple((p0[i] + p1[i]) // 2 for i in range(3))
    return [(*p0, 255), (*p1, 255), (*p2, 255), (0, 0, 0, 0)]


def encode_dxt1_block(block):
    transparent = any(p[3] < 128 for p in block)
    opaque = [(p[0], p[1], p[2]) for p in block if p[3] >= 128]

    if not opaque:
        return struct.pack("<HHI", 0, 0, 0xFFFFFFFF)

    a, b = choose_endpoints(opaque)

    if transparent:
        c0, c1 = sorted((a, b))
        if c0 == c1:
            c1 = c1 + 1 if c1 < 0xFFFF else c1
            c0 = c0 - 1 if c0 == c1 and c0 > 0 else c0
        if c0 > c1:
            c0, c1 = c1, c0
    else:
        c0, c1 = sorted((a, b), reverse=True)
        if c0 == c1:
            if c0 < 0xFFFF:
                c0 += 1
            elif c1 > 0:
                c1 -= 1
        if c0 <= c1:
            c0, c1 = max(c0, c1), min(c0, c1)

    pal = palette(c0, c1)
    usable = 3 if transparent else 4

    bits = 0
    for i, p in enumerate(block):
        if transparent and p[3] < 128:
            code = 3
        else:
            rgb = p[:3]
            code = min(range(usable), key=lambda k: dist2(rgb, pal[k][:3]))
        bits |= (code & 3) << (2 * i)

    return struct.pack("<HHI", c0, c1, bits)


def encode_dxt1(pixels, width: int, height: int) -> bytes:
    if width % 4 or height % 4:
        raise ValueError("DXT1 dimensions must be multiples of 4")
    out = bytearray()
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = []
            for py in range(4):
                row = (by + py) * width
                for px in range(4):
                    block.append(pixels[row + bx + px])
            out += encode_dxt1_block(block)
    expected = width * height // 2
    if len(out) != expected:
        raise AssertionError((len(out), expected))
    return bytes(out)


def make_title_xpr(raw: bytes) -> bytes:
    src = decode_rgba16(raw)
    pixels = upscale_nearest(src, TITLE_W, TITLE_H)
    dxt = encode_dxt1(pixels, TITLE_W, TITLE_H)
    if len(dxt) != 0x2000:
        raise AssertionError("128x128 DXT1 payload must be 0x2000 bytes")

    out = bytearray()
    out += struct.pack("<III", XPR_MAGIC, XPR_TITLE_TOTAL_SIZE, XPR_HEADER_SIZE)
    out += struct.pack(
        "<IIIIII",
        XPR_COMMON,
        0,
        0,
        XPR_FORMAT_DXT1_128,
        XPR_SIZE,
        XPR_EOH,
    )
    out += b"\xAD" * (XPR_HEADER_SIZE - len(out))
    out += dxt
    if len(out) != XPR_TITLE_TOTAL_SIZE:
        raise AssertionError("Title XPR size mismatch")
    return bytes(out)


def read_c_string(data: bytes | bytearray, off: int, max_len: int = 128) -> str:
    if off < 0 or off >= len(data):
        raise ValueError(f"string offset out of range: 0x{off:X}")
    end = off
    limit = min(len(data), off + max_len)
    while end < limit and data[end] != 0:
        end += 1
    if end == limit:
        raise ValueError("unterminated XBE section name")
    return bytes(data[off:end]).decode("ascii", "strict")


def parse_sections(data: bytes | bytearray):
    if len(data) < 0x178 or u32(data, 0) != XBE_MAGIC:
        raise ValueError("invalid XBE header")
    base = u32(data, OFF_BASE)
    count = u32(data, OFF_SECTION_COUNT)
    table_va = u32(data, OFF_SECTION_HEADERS_ADDR)
    table_off = table_va - base
    if count <= 0 or table_off < 0:
        raise ValueError("invalid XBE section table")
    if table_off + count * XBE_SECTION_HEADER_SIZE > len(data):
        raise ValueError("section table extends beyond file")

    result = []
    for i in range(count):
        off = table_off + i * XBE_SECTION_HEADER_SIZE
        raw = bytes(data[off:off + XBE_SECTION_HEADER_SIZE])
        fields = struct.unpack("<IIIIIIIII20s", raw)
        name_off = fields[5] - base
        name = read_c_string(data, name_off)
        result.append({
            "index": i,
            "header_off": off,
            "header_bytes": raw,
            "flags": fields[0],
            "virtual_addr": fields[1],
            "virtual_size": fields[2],
            "raw_addr": fields[3],
            "raw_size": fields[4],
            "name_addr": fields[5],
            "section_ref_count": fields[6],
            "head_ref_addr": fields[7],
            "tail_ref_addr": fields[8],
            "digest": fields[9],
            "name": name,
        })
    return result


def section_snapshot(section):
    return {
        "name": section["name"],
        "flags": section["flags"],
        "virtual_addr": section["virtual_addr"],
        "virtual_size": section["virtual_size"],
        "raw_addr": section["raw_addr"],
        "raw_size": section["raw_size"],
        "name_addr": section["name_addr"],
        "section_ref_count": section["section_ref_count"],
        "head_ref_addr": section["head_ref_addr"],
        "tail_ref_addr": section["tail_ref_addr"],
        "digest_hex": section["digest"].hex().upper(),
    }


def inject_title_image(xbe: bytes, title_xpr: bytes):
    if len(xbe) < 0x178 or u32(xbe, 0) != XBE_MAGIC:
        raise ValueError("input is not an XBE")

    base = u32(xbe, OFF_BASE)
    old_headers_size = u32(xbe, OFF_HEADERS_SIZE)
    old_image_size = u32(xbe, OFF_IMAGE_SIZE)
    old_count = u32(xbe, OFF_SECTION_COUNT)
    old_table_addr = u32(xbe, OFF_SECTION_HEADERS_ADDR)
    old_sections = parse_sections(xbe)

    if len(old_sections) != old_count:
        raise AssertionError("section count mismatch")
    if any(s["name"] == XBE_TITLE_SECTION_NAME for s in old_sections):
        raise ValueError("input XBE already contains $$XTIMAGE")

    first_raw = min(s["raw_addr"] for s in old_sections if s["raw_addr"] > 0)

    new_count = old_count + 1
    new_table_off = align_up(old_headers_size, 0x10)
    new_names_off = new_table_off + new_count * XBE_SECTION_HEADER_SIZE
    name_bytes = XBE_TITLE_SECTION_NAME.encode("ascii") + b"\x00"
    zero_ref_off = new_names_off + len(name_bytes)
    new_headers_end = align_up(zero_ref_off + 2, 0x10)

    old_header_page = align_up(old_headers_size, 0x1000)
    new_header_page = align_up(new_headers_end, 0x1000)

    if new_headers_end > first_raw:
        raise ValueError(
            f"insufficient XBE header slack: need 0x{new_headers_end:X}, "
            f"first section starts 0x{first_raw:X}"
        )
    if new_header_page != old_header_page:
        raise ValueError(
            "embedding would change cxbe's rounded header page / PE relocation base"
        )
    if new_header_page > first_raw:
        raise ValueError("new rounded header overlaps first raw section")

    last_virtual_end = max(s["virtual_addr"] + s["virtual_size"] for s in old_sections)
    new_virtual_addr = align_up(last_virtual_end, 0x20)

    output = bytearray(xbe)
    new_raw_addr = align_up(len(output), 0x1000)
    if len(output) < new_raw_addr:
        output += b"\x00" * (new_raw_addr - len(output))

    output += title_xpr

    # Relocate the whole section-header table to unused header slack.
    table_blob = b"".join(s["header_bytes"] for s in old_sections)

    new_name_addr = base + new_names_off
    zero_ref_addr = base + zero_ref_off
    new_header = struct.pack(
        "<IIIIIIIII20s",
        XBE_INSERTED_READONLY_FLAGS,
        new_virtual_addr,
        len(title_xpr),
        new_raw_addr,
        len(title_xpr),
        new_name_addr,
        0,
        zero_ref_addr,
        zero_ref_addr,
        b"\x00" * 20,  # nxdk cxbe uses zero section digests for homebrew XBEs
    )

    output[new_table_off:new_table_off + len(table_blob) + len(new_header)] = (
        table_blob + new_header
    )
    output[new_names_off:new_names_off + len(name_bytes)] = name_bytes
    output[zero_ref_off:zero_ref_off + 2] = b"\x00\x00"

    put_u32(output, OFF_HEADERS_SIZE, new_headers_end)
    put_u32(output, OFF_IMAGE_SIZE, new_virtual_addr + len(title_xpr) - base)
    put_u32(output, OFF_SECTION_COUNT, new_count)
    put_u32(output, OFF_SECTION_HEADERS_ADDR, base + new_table_off)

    new_sections = parse_sections(output)
    embedded = new_sections[-1]
    if embedded["name"] != XBE_TITLE_SECTION_NAME:
        raise AssertionError("new title-image section not found at end")
    if embedded["raw_size"] != len(title_xpr):
        raise AssertionError("new section raw size mismatch")
    if bytes(output[embedded["raw_addr"]:embedded["raw_addr"] + embedded["raw_size"]]) != title_xpr:
        raise AssertionError("new section payload mismatch")

    # Prove every existing raw section remains byte-identical at the same offset.
    existing_raw_unchanged = True
    existing_headers_logically_unchanged = True
    for before, after in zip(old_sections, new_sections[:old_count]):
        before_raw = xbe[before["raw_addr"]:before["raw_addr"] + before["raw_size"]]
        after_raw = output[after["raw_addr"]:after["raw_addr"] + after["raw_size"]]
        if before_raw != after_raw:
            existing_raw_unchanged = False

        # New section-header table location changes, but each existing section
        # header's logical contents remain exact.
        if before["header_bytes"] != after["header_bytes"]:
            existing_headers_logically_unchanged = False

    critical_unchanged = all(
        u32(xbe, off) == u32(output, off) for off in CRITICAL_HEADER_U32_OFFSETS
    )

    facts = {
        "old_headers_size": old_headers_size,
        "new_headers_size": new_headers_end,
        "old_header_page": old_header_page,
        "new_header_page": new_header_page,
        "old_image_size": old_image_size,
        "new_image_size": u32(output, OFF_IMAGE_SIZE),
        "old_section_count": old_count,
        "new_section_count": new_count,
        "old_section_table_addr": f"0x{old_table_addr:08X}",
        "new_section_table_addr": f"0x{u32(output, OFF_SECTION_HEADERS_ADDR):08X}",
        "first_existing_raw_addr": first_raw,
        "new_title_section": section_snapshot(embedded),
        "existing_raw_sections_unchanged": existing_raw_unchanged,
        "existing_section_headers_logically_unchanged": existing_headers_logically_unchanged,
        "critical_header_fields_unchanged": critical_unchanged,
    }
    return bytes(output), facts


def inspect_title_xpr(xpr: bytes):
    if len(xpr) != 0x2800:
        raise ValueError("embedded title XPR is not 0x2800 bytes")
    magic, total, header = struct.unpack_from("<III", xpr, 0)
    common, data, lock, fmt, size, eoh = struct.unpack_from("<IIIIII", xpr, 12)
    return {
        "magic": f"0x{magic:08X}",
        "total_size": total,
        "header_size": header,
        "common": f"0x{common:08X}",
        "data": data,
        "lock": lock,
        "format": f"0x{fmt:08X}",
        "size": f"0x{size:08X}",
        "end_of_header": f"0x{eoh:08X}",
        "decoded_width": 1 << ((fmt >> 20) & 0xF),
        "decoded_height": 1 << ((fmt >> 24) & 0xF),
        "decoded_format": (fmt >> 8) & 0xFF,
        "decoded_dimensions": (fmt >> 4) & 0xF,
    }


def make_synthetic_xbe() -> bytes:
    base = 0x10000
    data = bytearray(0x1100)
    put_u32(data, 0, XBE_MAGIC)
    put_u32(data, OFF_BASE, base)
    put_u32(data, OFF_HEADERS_SIZE, 0x400)
    put_u32(data, OFF_IMAGE_SIZE, 0x1100)
    put_u32(data, 0x110, 0x178)
    put_u32(data, OFF_SECTION_COUNT, 1)
    table_off = 0x300
    name_off = 0x380
    put_u32(data, OFF_SECTION_HEADERS_ADDR, base + table_off)

    data[name_off:name_off + 6] = b".text\x00"
    hdr = struct.pack(
        "<IIIIIIIII20s",
        0x06,
        0x11000,
        0x100,
        0x1000,
        0x100,
        base + name_off,
        0,
        base + 0x390,
        base + 0x390,
        b"\x00" * 20,
    )
    data[table_off:table_off + 0x38] = hdr
    data[0x1000:0x1100] = bytes((i & 0xFF) for i in range(0x100))
    return bytes(data)


def self_test() -> None:
    raw = bytearray()
    for y in range(SOURCE_H):
        for x in range(SOURCE_W):
            r = x & 31
            g = y & 31
            b = (x + y) & 31
            a = 1 if 2 <= x <= 13 and 2 <= y <= 13 else 0
            v = (r << 11) | (g << 6) | (b << 1) | a
            raw += bytes(((v >> 8) & 0xFF, v & 0xFF))

    xpr = make_title_xpr(bytes(raw))
    xi = inspect_title_xpr(xpr)
    assert xi["magic"] == "0x30525058"
    assert xi["total_size"] == 0x2800
    assert xi["header_size"] == 0x800
    assert xi["common"] == "0x00040001"
    assert xi["format"] == "0x07710C29"
    assert xi["end_of_header"] == "0xFFFFFFFF"
    assert xi["decoded_width"] == 128
    assert xi["decoded_height"] == 128
    assert xi["decoded_format"] == 0x0C
    assert xi["decoded_dimensions"] == 2

    fake = make_synthetic_xbe()
    patched, facts = inject_title_image(fake, xpr)
    secs = parse_sections(patched)
    assert len(secs) == 2
    assert secs[-1]["name"] == "$$XTIMAGE"
    assert secs[-1]["flags"] == 0x38
    assert secs[-1]["raw_size"] == 0x2800
    assert facts["existing_raw_sections_unchanged"] is True
    assert facts["existing_section_headers_logically_unchanged"] is True
    assert facts["critical_header_fields_unchanged"] is True

    print("SM64_XBOX_XBE_EMBEDDED_TITLE_ICON_SELF_TEST_PASS")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input-xbe", type=Path)
    ap.add_argument("--source", type=Path)
    ap.add_argument("--output-xbe", type=Path)
    ap.add_argument("--report", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0

    if not args.input_xbe or not args.source or not args.output_xbe:
        ap.error("--input-xbe, --source and --output-xbe are required")

    raw = args.source.read_bytes()
    if len(raw) != SOURCE_BYTES:
        raise SystemExit(f"unexpected source size: {len(raw)}")

    input_xbe = args.input_xbe.read_bytes()
    source_sha = hashlib.sha256(raw).hexdigest().upper()
    input_sha = hashlib.sha256(input_xbe).hexdigest().upper()

    title_xpr = make_title_xpr(raw)
    xpr_sha = hashlib.sha256(title_xpr).hexdigest().upper()
    xpr_info = inspect_title_xpr(title_xpr)

    output_xbe, inject = inject_title_image(input_xbe, title_xpr)
    output_sha = hashlib.sha256(output_xbe).hexdigest().upper()

    args.output_xbe.parent.mkdir(parents=True, exist_ok=True)
    args.output_xbe.write_bytes(output_xbe)

    final_sections = parse_sections(output_xbe)
    title_section = next(s for s in final_sections if s["name"] == XBE_TITLE_SECTION_NAME)
    embedded = output_xbe[
        title_section["raw_addr"]:
        title_section["raw_addr"] + title_section["raw_size"]
    ]

    report = {
        "schema": "sm64-xbox-xbe-embedded-title-icon-v2",
        "mechanism": "XBE_INSERTED_FILE_SECTION_$$XTIMAGE",
        "source": str(args.source),
        "source_role": "HUD_MARIO_HEAD",
        "source_bytes": len(raw),
        "source_sha256": source_sha,
        "title_xpr_bytes": len(title_xpr),
        "title_xpr_sha256": xpr_sha,
        "title_xpr": xpr_info,
        "input_xbe_sha256": input_sha,
        "input_xbe_bytes": len(input_xbe),
        "output_xbe_sha256": output_sha,
        "output_xbe_bytes": len(output_xbe),
        "embedded_title_section_sha256": hashlib.sha256(embedded).hexdigest().upper(),
        "embedded_title_section_bytes": len(embedded),
        "embedded_title_section_matches_generated_xpr": embedded == title_xpr,
        "injection": inject,
        "boot_logo_field_modified": False,
        "generated_nintendo_bytes_committed": False,
    }

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_bytes((json.dumps(report, indent=2) + "\n").encode("utf-8"))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
