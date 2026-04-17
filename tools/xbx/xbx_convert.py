#!/usr/bin/env python3
"""
xbx_convert.py — Xbox XPR0 (.xbx) texture <-> standard image converter.

Decodes and encodes the Xbox XPR0 texture format used by the original Xbox
dashboard and skins (UIX, Theseus, User.Interface.X). Output is PNG by default
on the decode side; encode side accepts any format Pillow can read.

Format support:
  decode: DXT1, DXT3, DXT5, A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, X1R5G5B5,
          A4R4G4B4, L8, AL8, P8 (as grayscale), and their LIN_* (non-swizzled) variants
  encode: DXT1 (default, lossy), A8R8G8B8 (lossless, larger)

Usage:
  xbx_convert.py decode <input.xbx> [output.png]
  xbx_convert.py encode <input.png> [output.xbx] [--format dxt1|argb8888]
  xbx_convert.py info <input.xbx>

Reference: ported from UIX-Desktop platform/xbx_texture.h
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from typing import Optional, Tuple

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("error: Pillow is required (pip install Pillow)\n")
    sys.exit(2)


# ---------------------------------------------------------------------------
# XPR header constants
# ---------------------------------------------------------------------------
XPR_MAGIC = 0x30525058  # 'XPR0' little-endian
XPR_HEADER_SIZE = 12    # dwMagic + dwTotalSize + dwHeaderSize

D3DCOMMON_TYPE_TEXTURE = 0x00040000
D3DCOMMON_TYPE_MASK    = 0x00070000

D3DFORMAT_FORMAT_MASK   = 0x0000FF00
D3DFORMAT_FORMAT_SHIFT  = 8
D3DFORMAT_USIZE_MASK    = 0x00F00000
D3DFORMAT_USIZE_SHIFT   = 20
D3DFORMAT_VSIZE_MASK    = 0x0F000000
D3DFORMAT_VSIZE_SHIFT   = 24

D3DSIZE_WIDTH_MASK   = 0x00000FFF
D3DSIZE_HEIGHT_MASK  = 0x00FFF000
D3DSIZE_HEIGHT_SHIFT = 12

# Xbox D3D format IDs
FMT_L8           = 0x00
FMT_AL8          = 0x01
FMT_A1R5G5B5     = 0x02
FMT_X1R5G5B5     = 0x03
FMT_A4R4G4B4     = 0x04
FMT_R5G6B5       = 0x05
FMT_A8R8G8B8     = 0x06
FMT_X8R8G8B8     = 0x07
FMT_P8           = 0x0B
FMT_DXT1         = 0x0C
FMT_DXT3         = 0x0E
FMT_DXT5         = 0x0F
FMT_LIN_R5G6B5   = 0x11
FMT_LIN_A8R8G8B8 = 0x12
FMT_LIN_L8       = 0x13
FMT_LIN_R8B8     = 0x16
FMT_LIN_G8B8     = 0x17
FMT_LIN_A8       = 0x19
FMT_LIN_A8L8     = 0x1A
FMT_LIN_AL8      = 0x1B
FMT_LIN_X1R5G5B5 = 0x1C
FMT_LIN_A4R4G4B4 = 0x1D
FMT_LIN_X8R8G8B8 = 0x1E

FORMAT_NAMES = {
    FMT_L8: "L8", FMT_AL8: "AL8", FMT_A1R5G5B5: "A1R5G5B5",
    FMT_X1R5G5B5: "X1R5G5B5", FMT_A4R4G4B4: "A4R4G4B4",
    FMT_R5G6B5: "R5G6B5", FMT_A8R8G8B8: "A8R8G8B8",
    FMT_X8R8G8B8: "X8R8G8B8", FMT_P8: "P8",
    FMT_DXT1: "DXT1", FMT_DXT3: "DXT3", FMT_DXT5: "DXT5",
    FMT_LIN_R5G6B5: "LIN_R5G6B5", FMT_LIN_A8R8G8B8: "LIN_A8R8G8B8",
    FMT_LIN_L8: "LIN_L8", FMT_LIN_R8B8: "LIN_R8B8",
    FMT_LIN_G8B8: "LIN_G8B8", FMT_LIN_A8: "LIN_A8",
    FMT_LIN_A8L8: "LIN_A8L8", FMT_LIN_AL8: "LIN_AL8",
    FMT_LIN_X1R5G5B5: "LIN_X1R5G5B5", FMT_LIN_A4R4G4B4: "LIN_A4R4G4B4",
    FMT_LIN_X8R8G8B8: "LIN_X8R8G8B8",
}

EXP_TABLE = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096]


def _is_dxt(fmt: int) -> bool:
    return fmt in (FMT_DXT1, FMT_DXT3, FMT_DXT5)


def _is_linear(fmt: int) -> bool:
    return fmt >= 0x10


def _bytes_per_pixel(fmt: int) -> int:
    if fmt in (FMT_A8R8G8B8, FMT_X8R8G8B8, FMT_LIN_A8R8G8B8, FMT_LIN_X8R8G8B8):
        return 4
    if fmt in (FMT_R5G6B5, FMT_A1R5G5B5, FMT_X1R5G5B5, FMT_A4R4G4B4,
               FMT_LIN_R5G6B5, FMT_LIN_A4R4G4B4, FMT_LIN_X1R5G5B5,
               FMT_LIN_A8L8, FMT_LIN_R8B8, FMT_LIN_G8B8):
        return 2
    if fmt in (FMT_L8, FMT_AL8, FMT_P8, FMT_LIN_L8, FMT_LIN_A8, FMT_LIN_AL8):
        return 1
    return 4


# ---------------------------------------------------------------------------
# Xbox swizzler (Morton / Z-order)
# Ported from XDK XGraphics.h Swizzler class
# ---------------------------------------------------------------------------
class Swizzler:
    def __init__(self, width: int, height: int) -> None:
        self.mask_u = 0
        self.mask_v = 0
        i, j = 1, 1
        while True:
            k = 0
            if i < width:
                self.mask_u |= j
                j <<= 1
                k = j
            if i < height:
                self.mask_v |= j
                j <<= 1
                k = j
            i <<= 1
            if k == 0:
                break

    def linear_to_swizzled(self, x: int, y: int) -> int:
        u, v = x, y
        su = sv = 0
        bit = 1
        limit = self.mask_u | self.mask_v
        while bit <= limit:
            if self.mask_u & bit:
                if u & 1:
                    su |= bit
                u >>= 1
            if self.mask_v & bit:
                if v & 1:
                    sv |= bit
                v >>= 1
            bit <<= 1
        return su | sv


# ---------------------------------------------------------------------------
# DXT decoders (S3TC)
# ---------------------------------------------------------------------------
def _decode_color_block(block: bytes, has_alpha: bool):
    c0, c1, bits = struct.unpack_from("<HHI", block, 0)
    colors = [[0, 0, 0, 255] for _ in range(4)]

    colors[0][0] = ((c0 >> 11) & 0x1F) * 255 // 31
    colors[0][1] = ((c0 >> 5) & 0x3F) * 255 // 63
    colors[0][2] = (c0 & 0x1F) * 255 // 31
    colors[1][0] = ((c1 >> 11) & 0x1F) * 255 // 31
    colors[1][1] = ((c1 >> 5) & 0x3F) * 255 // 63
    colors[1][2] = (c1 & 0x1F) * 255 // 31

    if c0 > c1 or has_alpha:
        for ch in range(3):
            colors[2][ch] = (2 * colors[0][ch] + colors[1][ch] + 1) // 3
            colors[3][ch] = (colors[0][ch] + 2 * colors[1][ch] + 1) // 3
    else:
        for ch in range(3):
            colors[2][ch] = (colors[0][ch] + colors[1][ch]) // 2
        colors[3] = [0, 0, 0, 0]

    pixels = bytearray(4 * 4 * 4)
    for y in range(4):
        for x in range(4):
            idx = (bits >> (2 * (y * 4 + x))) & 3
            o = (y * 4 + x) * 4
            pixels[o + 0] = colors[idx][0]
            pixels[o + 1] = colors[idx][1]
            pixels[o + 2] = colors[idx][2]
            pixels[o + 3] = colors[idx][3]
    return pixels


def _decode_dxt1(src: bytes, w: int, h: int) -> bytearray:
    out = bytearray(w * h * 4)
    off = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            if off + 8 > len(src):
                return out
            block = _decode_color_block(src[off:off + 8], has_alpha=False)
            off += 8
            for y in range(4):
                if by + y >= h:
                    break
                for x in range(4):
                    if bx + x >= w:
                        break
                    si = (y * 4 + x) * 4
                    di = ((by + y) * w + (bx + x)) * 4
                    out[di:di + 4] = block[si:si + 4]
    return out


def _decode_dxt3(src: bytes, w: int, h: int) -> bytearray:
    out = bytearray(w * h * 4)
    off = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            if off + 16 > len(src):
                return out
            alpha_block = src[off:off + 8]
            off += 8
            block = _decode_color_block(src[off:off + 8], has_alpha=True)
            off += 8
            for y in range(4):
                arow = struct.unpack_from("<H", alpha_block, y * 2)[0]
                for x in range(4):
                    a4 = (arow >> (x * 4)) & 0xF
                    block[(y * 4 + x) * 4 + 3] = a4 * 255 // 15
            for y in range(4):
                if by + y >= h:
                    break
                for x in range(4):
                    if bx + x >= w:
                        break
                    si = (y * 4 + x) * 4
                    di = ((by + y) * w + (bx + x)) * 4
                    out[di:di + 4] = block[si:si + 4]
    return out


def _decode_dxt5(src: bytes, w: int, h: int) -> bytearray:
    out = bytearray(w * h * 4)
    off = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            if off + 16 > len(src):
                return out
            a0 = src[off]
            a1 = src[off + 1]
            alphas = [a0, a1, 0, 0, 0, 0, 0, 0]
            if a0 > a1:
                for i in range(6):
                    alphas[i + 2] = ((6 - i) * a0 + (i + 1) * a1) // 7
            else:
                for i in range(4):
                    alphas[i + 2] = ((4 - i) * a0 + (i + 1) * a1) // 5
                alphas[6] = 0
                alphas[7] = 255
            bits = 0
            for i in range(6):
                bits |= src[off + 2 + i] << (8 * i)
            alpha_idx = [(bits >> (3 * i)) & 7 for i in range(16)]
            off += 8

            block = _decode_color_block(src[off:off + 8], has_alpha=True)
            off += 8
            for i in range(16):
                block[i * 4 + 3] = alphas[alpha_idx[i]]
            for y in range(4):
                if by + y >= h:
                    break
                for x in range(4):
                    if bx + x >= w:
                        break
                    si = (y * 4 + x) * 4
                    di = ((by + y) * w + (bx + x)) * 4
                    out[di:di + 4] = block[si:si + 4]
    return out


# ---------------------------------------------------------------------------
# Linear / swizzled pixel format conversions
# ---------------------------------------------------------------------------
def _pixel_to_rgba(fmt: int, src: bytes, off: int) -> Tuple[int, int, int, int]:
    if fmt in (FMT_A8R8G8B8, FMT_LIN_A8R8G8B8):
        return (src[off + 2], src[off + 1], src[off + 0], src[off + 3])
    if fmt in (FMT_X8R8G8B8, FMT_LIN_X8R8G8B8):
        return (src[off + 2], src[off + 1], src[off + 0], 255)
    if fmt in (FMT_R5G6B5, FMT_LIN_R5G6B5):
        px = src[off] | (src[off + 1] << 8)
        r = ((px >> 11) & 0x1F) * 255 // 31
        g = ((px >> 5) & 0x3F) * 255 // 63
        b = (px & 0x1F) * 255 // 31
        return (r, g, b, 255)
    if fmt == FMT_A1R5G5B5:
        px = src[off] | (src[off + 1] << 8)
        r = ((px >> 10) & 0x1F) * 255 // 31
        g = ((px >> 5) & 0x1F) * 255 // 31
        b = (px & 0x1F) * 255 // 31
        a = 255 if (px & 0x8000) else 0
        return (r, g, b, a)
    if fmt in (FMT_X1R5G5B5, FMT_LIN_X1R5G5B5):
        px = src[off] | (src[off + 1] << 8)
        r = ((px >> 10) & 0x1F) * 255 // 31
        g = ((px >> 5) & 0x1F) * 255 // 31
        b = (px & 0x1F) * 255 // 31
        return (r, g, b, 255)
    if fmt in (FMT_A4R4G4B4, FMT_LIN_A4R4G4B4):
        px = src[off] | (src[off + 1] << 8)
        a = ((px >> 12) & 0xF) * 255 // 15
        r = ((px >> 8) & 0xF) * 255 // 15
        g = ((px >> 4) & 0xF) * 255 // 15
        b = (px & 0xF) * 255 // 15
        return (r, g, b, a)
    if fmt in (FMT_L8, FMT_LIN_L8, FMT_P8):
        v = src[off]
        return (v, v, v, 255)
    if fmt == FMT_LIN_A8:
        return (255, 255, 255, src[off])
    if fmt in (FMT_AL8, FMT_LIN_AL8):
        v = src[off]
        return (v, v, v, v)
    if fmt == FMT_LIN_A8L8:
        l = src[off]
        a = src[off + 1]
        return (l, l, l, a)
    return (128, 128, 128, 255)


# ---------------------------------------------------------------------------
# Decode entry point
# ---------------------------------------------------------------------------
def _detect_renamed_image(data: bytes) -> str | None:
    """If `data` looks like a standard image format that's been renamed to .xbx,
    return a friendly name for it. Otherwise None."""
    if len(data) < 8:
        return None
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return "PNG"
    if data[:3] == b"\xff\xd8\xff":
        return "JPEG"
    if data[:6] in (b"GIF87a", b"GIF89a"):
        return "GIF"
    if data[:2] == b"BM":
        return "BMP"
    if data[:4] == b"DDS ":
        return "DDS (DirectDraw Surface)"
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "WebP"
    if data[:4] == b"\x00\x00\x01\x00":
        return "ICO"
    # TGA has no header magic but a footer signature
    if data[-18:-2] == b"TRUEVISION-XFILE":
        return "TGA"
    return None


def parse_xbx_header(data: bytes):
    if len(data) < XPR_HEADER_SIZE:
        raise ValueError("file too small to contain XPR_HEADER")
    magic, total, header = struct.unpack_from("<III", data, 0)
    if magic != XPR_MAGIC:
        renamed = _detect_renamed_image(data)
        if renamed:
            raise ValueError(
                f"this file is a {renamed}, not an XPR0 .xbx — looks like it "
                f"was renamed. To turn it into a real .xbx, run: "
                f"`xbx_convert.py encode <file> <output.xbx>`"
            )
        raise ValueError(f"bad magic 0x{magic:08X} (expected 0x{XPR_MAGIC:08X} 'XPR0')")
    if header < XPR_HEADER_SIZE + 20:
        raise ValueError(f"header too small ({header})")

    # D3DPixelContainer descriptor at offset 12
    common, dat, lock, fmt_dw, size_dw = struct.unpack_from("<IIIII", data, XPR_HEADER_SIZE)
    if (common & D3DCOMMON_TYPE_MASK) != D3DCOMMON_TYPE_TEXTURE:
        raise ValueError(f"resource is not a texture (Common=0x{common:08X})")
    xbox_fmt = (fmt_dw & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT

    if size_dw != 0 and size_dw != 0xFFFFFFFF:
        width = (size_dw & D3DSIZE_WIDTH_MASK) + 1
        height = ((size_dw & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1
    else:
        u_idx = (fmt_dw & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT
        v_idx = (fmt_dw & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT
        width = EXP_TABLE[u_idx] if u_idx < len(EXP_TABLE) else 1
        height = EXP_TABLE[v_idx] if v_idx < len(EXP_TABLE) else 1

    return {
        "magic": magic,
        "total_size": total,
        "header_size": header,
        "common": common,
        "format_dw": fmt_dw,
        "size_dw": size_dw,
        "xbox_fmt": xbox_fmt,
        "width": width,
        "height": height,
    }


def decode_xbx(data: bytes) -> Image.Image:
    h = parse_xbx_header(data)
    width, height, fmt = h["width"], h["height"], h["xbox_fmt"]
    pixel_data = data[h["header_size"]:]

    if _is_dxt(fmt):
        if fmt == FMT_DXT1:
            rgba = _decode_dxt1(pixel_data, width, height)
        elif fmt == FMT_DXT3:
            rgba = _decode_dxt3(pixel_data, width, height)
        else:
            rgba = _decode_dxt5(pixel_data, width, height)
    elif _is_linear(fmt):
        bpp = _bytes_per_pixel(fmt)
        # NV2A linear pitch is 64-byte aligned
        src_pitch = (width * bpp + 63) & ~63
        rgba = bytearray(width * height * 4)
        for y in range(height):
            for x in range(width):
                src_off = y * src_pitch + x * bpp
                if src_off + bpp > len(pixel_data):
                    continue
                r, g, b, a = _pixel_to_rgba(fmt, pixel_data, src_off)
                di = (y * width + x) * 4
                rgba[di + 0] = r
                rgba[di + 1] = g
                rgba[di + 2] = b
                rgba[di + 3] = a
    else:
        # Swizzled
        bpp = _bytes_per_pixel(fmt)
        sw = Swizzler(width, height)
        rgba = bytearray(width * height * 4)
        for y in range(height):
            for x in range(width):
                swiz_idx = sw.linear_to_swizzled(x, y)
                src_off = swiz_idx * bpp
                if src_off + bpp > len(pixel_data):
                    continue
                r, g, b, a = _pixel_to_rgba(fmt, pixel_data, src_off)
                di = (y * width + x) * 4
                rgba[di + 0] = r
                rgba[di + 1] = g
                rgba[di + 2] = b
                rgba[di + 3] = a

    return Image.frombytes("RGBA", (width, height), bytes(rgba))


# ---------------------------------------------------------------------------
# DXT1 encoder (simple endpoint-pick + nearest quantization)
# ---------------------------------------------------------------------------
def _rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _565_to_rgb(c: int):
    r = ((c >> 11) & 0x1F) * 255 // 31
    g = ((c >> 5) & 0x3F) * 255 // 63
    b = (c & 0x1F) * 255 // 31
    return r, g, b


def _encode_dxt1_block(pixels) -> bytes:
    """pixels: list of 16 (r,g,b,a) tuples in row-major 4x4 order."""
    rs = [p[0] for p in pixels]
    gs = [p[1] for p in pixels]
    bs = [p[2] for p in pixels]
    # Pick endpoints as min/max along the (loose) diagonal
    min_r, max_r = min(rs), max(rs)
    min_g, max_g = min(gs), max(gs)
    min_b, max_b = min(bs), max(bs)

    c0_565 = _rgb_to_565(max_r, max_g, max_b)
    c1_565 = _rgb_to_565(min_r, min_g, min_b)

    if c0_565 == c1_565:
        # Solid color: just emit two identical endpoints + zero indices
        return struct.pack("<HHI", c0_565, c1_565, 0)

    # Ensure c0 > c1 (4-color mode, no alpha)
    if c0_565 < c1_565:
        c0_565, c1_565 = c1_565, c0_565

    p0 = _565_to_rgb(c0_565)
    p1 = _565_to_rgb(c1_565)
    p2 = (
        (2 * p0[0] + p1[0] + 1) // 3,
        (2 * p0[1] + p1[1] + 1) // 3,
        (2 * p0[2] + p1[2] + 1) // 3,
    )
    p3 = (
        (p0[0] + 2 * p1[0] + 1) // 3,
        (p0[1] + 2 * p1[1] + 1) // 3,
        (p0[2] + 2 * p1[2] + 1) // 3,
    )
    palette = (p0, p1, p2, p3)

    bits = 0
    for i, (r, g, b, _a) in enumerate(pixels):
        best = 0
        best_d = 1 << 30
        for k in range(4):
            pr, pg, pb = palette[k]
            d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
            if d < best_d:
                best_d = d
                best = k
        bits |= best << (2 * i)

    return struct.pack("<HHI", c0_565, c1_565, bits)


def _encode_dxt1(img: Image.Image) -> bytes:
    img = img.convert("RGBA")
    w, h = img.size
    if w % 4 != 0 or h % 4 != 0:
        # Pad to 4-pixel boundary
        new_w = (w + 3) & ~3
        new_h = (h + 3) & ~3
        padded = Image.new("RGBA", (new_w, new_h), (0, 0, 0, 0))
        padded.paste(img, (0, 0))
        img = padded
        w, h = new_w, new_h
    px = img.load()
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            block = []
            for y in range(4):
                for x in range(4):
                    block.append(px[bx + x, by + y])
            out += _encode_dxt1_block(block)
    return bytes(out)


def _encode_argb8888_linear(img: Image.Image) -> bytes:
    img = img.convert("RGBA")
    w, h = img.size
    bpp = 4
    pitch = (w * bpp + 63) & ~63
    out = bytearray(pitch * h)
    px = img.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            o = y * pitch + x * 4
            out[o + 0] = b
            out[o + 1] = g
            out[o + 2] = r
            out[o + 3] = a
    return bytes(out)


def _log2_or_none(n: int) -> Optional[int]:
    if n <= 0:
        return None
    if n & (n - 1) != 0:
        return None
    bits = 0
    v = n
    while v > 1:
        v >>= 1
        bits += 1
    return bits


def encode_xbx(img: Image.Image, format_name: str = "dxt1") -> bytes:
    format_name = format_name.lower()
    img = img.convert("RGBA")
    w_orig, h_orig = img.size

    if format_name == "dxt1":
        # Pad to 4-aligned (encoder does this internally)
        w = (w_orig + 3) & ~3
        h = (h_orig + 3) & ~3
        pixel_data = _encode_dxt1(img)
        xbox_fmt = FMT_DXT1
        is_linear = False
    elif format_name in ("argb8888", "a8r8g8b8", "lin_a8r8g8b8"):
        w = w_orig
        h = h_orig
        pixel_data = _encode_argb8888_linear(img)
        xbox_fmt = FMT_LIN_A8R8G8B8
        is_linear = True
    else:
        raise ValueError(f"unsupported encode format: {format_name}")

    # Build the Format DWORD and Size DWORD
    u_log = _log2_or_none(w)
    v_log = _log2_or_none(h)
    use_size_field = (u_log is None or v_log is None or is_linear)

    fmt_dw = (xbox_fmt << D3DFORMAT_FORMAT_SHIFT)
    if not use_size_field:
        fmt_dw |= (u_log << D3DFORMAT_USIZE_SHIFT)
        fmt_dw |= (v_log << D3DFORMAT_VSIZE_SHIFT)
        size_dw = 0
    else:
        size_dw = ((w - 1) & D3DSIZE_WIDTH_MASK) | (((h - 1) << D3DSIZE_HEIGHT_SHIFT) & D3DSIZE_HEIGHT_MASK)

    # Build D3DPixelContainer (5 DWORDs after XPR_HEADER, then padding to header_size)
    common = D3DCOMMON_TYPE_TEXTURE | 0x00000001  # +refcount=1 for safety
    descriptor = struct.pack("<IIIII", common, 0, 0, fmt_dw, size_dw)

    # Header is XPR(12) + descriptor(20) + padding to align pixel data to 2048
    # Real Xbox XPR0 files use 2048-byte (D3DTEXTURE_ALIGNMENT) header alignment
    base_header = XPR_HEADER_SIZE + len(descriptor)
    header_size = ((base_header + 2047) // 2048) * 2048
    if header_size < base_header:
        header_size = base_header

    total_size = header_size + len(pixel_data)
    xpr_header = struct.pack("<III", XPR_MAGIC, total_size, header_size)

    out = bytearray()
    out += xpr_header
    out += descriptor
    out += b"\xAD" * (header_size - len(out))
    out += pixel_data
    return bytes(out)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def cmd_decode(args):
    with open(args.input, "rb") as f:
        data = f.read()
    img = decode_xbx(data)
    out = args.output or os.path.splitext(args.input)[0] + ".png"
    img.save(out)
    print(f"decoded {args.input} -> {out} ({img.width}x{img.height})")


def cmd_encode(args):
    img = Image.open(args.input)
    blob = encode_xbx(img, args.format)
    out = args.output or os.path.splitext(args.input)[0] + ".xbx"
    with open(out, "wb") as f:
        f.write(blob)
    print(f"encoded {args.input} -> {out} ({img.width}x{img.height}, {args.format})")


def cmd_info(args):
    with open(args.input, "rb") as f:
        data = f.read()
    h = parse_xbx_header(data)
    print(f"file:        {args.input}")
    print(f"size:        {len(data)} bytes")
    print(f"total_size:  {h['total_size']}")
    print(f"header_size: {h['header_size']}")
    fmt = h["xbox_fmt"]
    name = FORMAT_NAMES.get(fmt, f"UNKNOWN(0x{fmt:02X})")
    print(f"format:      {name} (0x{fmt:02X})")
    print(f"dimensions:  {h['width']}x{h['height']}")
    print(f"common:      0x{h['common']:08X}")
    print(f"format_dw:   0x{h['format_dw']:08X}")
    print(f"size_dw:     0x{h['size_dw']:08X}")


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    sub = p.add_subparsers(dest="cmd", required=True)

    pd = sub.add_parser("decode", help="decode .xbx to image")
    pd.add_argument("input")
    pd.add_argument("output", nargs="?")
    pd.set_defaults(func=cmd_decode)

    pe = sub.add_parser("encode", help="encode image to .xbx")
    pe.add_argument("input")
    pe.add_argument("output", nargs="?")
    pe.add_argument("--format", "-f", default="dxt1",
                    help="output format: dxt1 (default) or argb8888")
    pe.set_defaults(func=cmd_encode)

    pi = sub.add_parser("info", help="print .xbx header info")
    pi.add_argument("input")
    pi.set_defaults(func=cmd_info)

    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
