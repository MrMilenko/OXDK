// xbx-convert: Xbox XPR0 (.xbx) texture <-> PNG converter.
//
// Decodes and encodes the Xbox XPR0 texture format used by the original Xbox
// dashboard and skins (UIX, Theseus, User.Interface.X). Pure Go, no external
// dependencies — produces a single static binary for easy distribution.
//
// Format support:
//   decode: DXT1, DXT3, DXT5, A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, X1R5G5B5,
//           A4R4G4B4, L8, AL8, P8 (as grayscale), and their LIN_* variants
//   encode: DXT1 (default, lossy), A8R8G8B8 (lossless, larger)
//
// Usage:
//   xbx-convert decode <input.xbx> [output.png]
//   xbx-convert encode <input.png> [output.xbx] [--format dxt1|argb8888]
//   xbx-convert info   <input.xbx>
//
// Ported from xbx_convert.py / UIX-Desktop platform/xbx_texture.h.
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"image"
	"image/draw"
	_ "image/gif"
	_ "image/jpeg"
	"image/png"
	"os"
	"path/filepath"
	"strings"
)

// ---------------------------------------------------------------------------
// XPR header constants
// ---------------------------------------------------------------------------

const (
	xprMagic      uint32 = 0x30525058 // 'XPR0'
	xprHeaderSize        = 12

	d3dCommonTypeTexture uint32 = 0x00040000
	d3dCommonTypeMask    uint32 = 0x00070000

	d3dFormatFormatMask  uint32 = 0x0000FF00
	d3dFormatFormatShift uint32 = 8
	d3dFormatUSizeMask   uint32 = 0x00F00000
	d3dFormatUSizeShift  uint32 = 20
	d3dFormatVSizeMask   uint32 = 0x0F000000
	d3dFormatVSizeShift  uint32 = 24

	d3dSizeWidthMask   uint32 = 0x00000FFF
	d3dSizeHeightMask  uint32 = 0x00FFF000
	d3dSizeHeightShift uint32 = 12

	d3dTextureAlignment = 2048
)

// Xbox D3D format IDs.
const (
	fmtL8           = 0x00
	fmtAL8          = 0x01
	fmtA1R5G5B5     = 0x02
	fmtX1R5G5B5     = 0x03
	fmtA4R4G4B4     = 0x04
	fmtR5G6B5       = 0x05
	fmtA8R8G8B8     = 0x06
	fmtX8R8G8B8     = 0x07
	fmtP8           = 0x0B
	fmtDXT1         = 0x0C
	fmtDXT3         = 0x0E
	fmtDXT5         = 0x0F
	fmtLinR5G6B5    = 0x11
	fmtLinA8R8G8B8  = 0x12
	fmtLinL8        = 0x13
	fmtLinR8B8      = 0x16
	fmtLinG8B8      = 0x17
	fmtLinA8        = 0x19
	fmtLinA8L8      = 0x1A
	fmtLinAL8       = 0x1B
	fmtLinX1R5G5B5  = 0x1C
	fmtLinA4R4G4B4  = 0x1D
	fmtLinX8R8G8B8  = 0x1E
)

var formatNames = map[int]string{
	fmtL8: "L8", fmtAL8: "AL8", fmtA1R5G5B5: "A1R5G5B5",
	fmtX1R5G5B5: "X1R5G5B5", fmtA4R4G4B4: "A4R4G4B4",
	fmtR5G6B5: "R5G6B5", fmtA8R8G8B8: "A8R8G8B8",
	fmtX8R8G8B8: "X8R8G8B8", fmtP8: "P8",
	fmtDXT1: "DXT1", fmtDXT3: "DXT3", fmtDXT5: "DXT5",
	fmtLinR5G6B5: "LIN_R5G6B5", fmtLinA8R8G8B8: "LIN_A8R8G8B8",
	fmtLinL8: "LIN_L8", fmtLinR8B8: "LIN_R8B8",
	fmtLinG8B8: "LIN_G8B8", fmtLinA8: "LIN_A8",
	fmtLinA8L8: "LIN_A8L8", fmtLinAL8: "LIN_AL8",
	fmtLinX1R5G5B5: "LIN_X1R5G5B5", fmtLinA4R4G4B4: "LIN_A4R4G4B4",
	fmtLinX8R8G8B8: "LIN_X8R8G8B8",
}

var expTable = []int{1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096}

func isDXT(fmt int) bool {
	return fmt == fmtDXT1 || fmt == fmtDXT3 || fmt == fmtDXT5
}

func isLinear(fmt int) bool {
	return fmt >= 0x10
}

func bytesPerPixel(fmt int) int {
	switch fmt {
	case fmtA8R8G8B8, fmtX8R8G8B8, fmtLinA8R8G8B8, fmtLinX8R8G8B8:
		return 4
	case fmtR5G6B5, fmtA1R5G5B5, fmtX1R5G5B5, fmtA4R4G4B4,
		fmtLinR5G6B5, fmtLinA4R4G4B4, fmtLinX1R5G5B5,
		fmtLinA8L8, fmtLinR8B8, fmtLinG8B8:
		return 2
	case fmtL8, fmtAL8, fmtP8, fmtLinL8, fmtLinA8, fmtLinAL8:
		return 1
	}
	return 4
}

// ---------------------------------------------------------------------------
// Xbox swizzler (Morton / Z-order) — ported from XDK XGraphics.h
// ---------------------------------------------------------------------------

type swizzler struct {
	maskU, maskV uint32
}

func newSwizzler(width, height int) *swizzler {
	s := &swizzler{}
	i, j := uint32(1), uint32(1)
	for {
		k := uint32(0)
		if i < uint32(width) {
			s.maskU |= j
			j <<= 1
			k = j
		}
		if i < uint32(height) {
			s.maskV |= j
			j <<= 1
			k = j
		}
		i <<= 1
		if k == 0 {
			break
		}
	}
	return s
}

func (s *swizzler) linearToSwizzled(x, y int) int {
	u, v := uint32(x), uint32(y)
	var su, sv uint32
	bit := uint32(1)
	limit := s.maskU | s.maskV
	for bit <= limit {
		if s.maskU&bit != 0 {
			if u&1 != 0 {
				su |= bit
			}
			u >>= 1
		}
		if s.maskV&bit != 0 {
			if v&1 != 0 {
				sv |= bit
			}
			v >>= 1
		}
		bit <<= 1
	}
	return int(su | sv)
}

// ---------------------------------------------------------------------------
// DXT decoders (S3TC)
// ---------------------------------------------------------------------------

// decodeColorBlock fills `out` (64 bytes, 4x4 RGBA row-major) from an 8-byte
// DXT color block. has_alpha forces 4-color mode (no 1-bit alpha).
func decodeColorBlock(block []byte, hasAlpha bool, out []byte) {
	c0 := uint16(block[0]) | uint16(block[1])<<8
	c1 := uint16(block[2]) | uint16(block[3])<<8
	bits := uint32(block[4]) | uint32(block[5])<<8 | uint32(block[6])<<16 | uint32(block[7])<<24

	var colors [4][4]int
	for i := range colors {
		colors[i][3] = 255
	}
	colors[0][0] = int((c0>>11)&0x1F) * 255 / 31
	colors[0][1] = int((c0>>5)&0x3F) * 255 / 63
	colors[0][2] = int(c0&0x1F) * 255 / 31
	colors[1][0] = int((c1>>11)&0x1F) * 255 / 31
	colors[1][1] = int((c1>>5)&0x3F) * 255 / 63
	colors[1][2] = int(c1&0x1F) * 255 / 31

	if c0 > c1 || hasAlpha {
		for ch := 0; ch < 3; ch++ {
			colors[2][ch] = (2*colors[0][ch] + colors[1][ch] + 1) / 3
			colors[3][ch] = (colors[0][ch] + 2*colors[1][ch] + 1) / 3
		}
	} else {
		for ch := 0; ch < 3; ch++ {
			colors[2][ch] = (colors[0][ch] + colors[1][ch]) / 2
		}
		colors[3] = [4]int{0, 0, 0, 0}
	}

	for i := 0; i < 16; i++ {
		idx := (bits >> (2 * uint(i))) & 3
		o := i * 4
		out[o+0] = byte(colors[idx][0])
		out[o+1] = byte(colors[idx][1])
		out[o+2] = byte(colors[idx][2])
		out[o+3] = byte(colors[idx][3])
	}
}

func decodeDXT1(src []byte, w, h int) []byte {
	out := make([]byte, w*h*4)
	var block [64]byte
	off := 0
	for by := 0; by < h; by += 4 {
		for bx := 0; bx < w; bx += 4 {
			if off+8 > len(src) {
				return out
			}
			decodeColorBlock(src[off:off+8], false, block[:])
			off += 8
			copyBlock(out, block[:], bx, by, w, h)
		}
	}
	return out
}

func decodeDXT3(src []byte, w, h int) []byte {
	out := make([]byte, w*h*4)
	var block [64]byte
	off := 0
	for by := 0; by < h; by += 4 {
		for bx := 0; bx < w; bx += 4 {
			if off+16 > len(src) {
				return out
			}
			alphaBlock := src[off : off+8]
			off += 8
			decodeColorBlock(src[off:off+8], true, block[:])
			off += 8
			for y := 0; y < 4; y++ {
				arow := uint16(alphaBlock[y*2]) | uint16(alphaBlock[y*2+1])<<8
				for x := 0; x < 4; x++ {
					a4 := (arow >> (uint(x) * 4)) & 0xF
					block[(y*4+x)*4+3] = byte(int(a4) * 255 / 15)
				}
			}
			copyBlock(out, block[:], bx, by, w, h)
		}
	}
	return out
}

func decodeDXT5(src []byte, w, h int) []byte {
	out := make([]byte, w*h*4)
	var block [64]byte
	off := 0
	for by := 0; by < h; by += 4 {
		for bx := 0; bx < w; bx += 4 {
			if off+16 > len(src) {
				return out
			}
			a0 := int(src[off])
			a1 := int(src[off+1])
			alphas := [8]int{a0, a1}
			if a0 > a1 {
				for i := 0; i < 6; i++ {
					alphas[i+2] = ((6-i)*a0 + (i+1)*a1) / 7
				}
			} else {
				for i := 0; i < 4; i++ {
					alphas[i+2] = ((4-i)*a0 + (i+1)*a1) / 5
				}
				alphas[6] = 0
				alphas[7] = 255
			}
			var bits uint64
			for i := 0; i < 6; i++ {
				bits |= uint64(src[off+2+i]) << (8 * uint(i))
			}
			off += 8

			decodeColorBlock(src[off:off+8], true, block[:])
			off += 8
			for i := 0; i < 16; i++ {
				idx := (bits >> (3 * uint(i))) & 7
				block[i*4+3] = byte(alphas[idx])
			}
			copyBlock(out, block[:], bx, by, w, h)
		}
	}
	return out
}

// copyBlock blits a decoded 4x4 RGBA tile into the output buffer, clipping
// at the right and bottom edges.
func copyBlock(dst, block []byte, bx, by, w, h int) {
	for y := 0; y < 4; y++ {
		if by+y >= h {
			break
		}
		for x := 0; x < 4; x++ {
			if bx+x >= w {
				break
			}
			si := (y*4 + x) * 4
			di := ((by+y)*w + (bx + x)) * 4
			dst[di+0] = block[si+0]
			dst[di+1] = block[si+1]
			dst[di+2] = block[si+2]
			dst[di+3] = block[si+3]
		}
	}
}

// ---------------------------------------------------------------------------
// Linear / swizzled pixel format conversions
// ---------------------------------------------------------------------------

func pixelToRGBA(fmt int, src []byte, off int) (r, g, b, a byte) {
	switch fmt {
	case fmtA8R8G8B8, fmtLinA8R8G8B8:
		return src[off+2], src[off+1], src[off+0], src[off+3]
	case fmtX8R8G8B8, fmtLinX8R8G8B8:
		return src[off+2], src[off+1], src[off+0], 255
	case fmtR5G6B5, fmtLinR5G6B5:
		px := uint16(src[off]) | uint16(src[off+1])<<8
		return byte(int((px>>11)&0x1F) * 255 / 31),
			byte(int((px>>5)&0x3F) * 255 / 63),
			byte(int(px&0x1F) * 255 / 31),
			255
	case fmtA1R5G5B5:
		px := uint16(src[off]) | uint16(src[off+1])<<8
		alpha := byte(0)
		if px&0x8000 != 0 {
			alpha = 255
		}
		return byte(int((px>>10)&0x1F) * 255 / 31),
			byte(int((px>>5)&0x1F) * 255 / 31),
			byte(int(px&0x1F) * 255 / 31),
			alpha
	case fmtX1R5G5B5, fmtLinX1R5G5B5:
		px := uint16(src[off]) | uint16(src[off+1])<<8
		return byte(int((px>>10)&0x1F) * 255 / 31),
			byte(int((px>>5)&0x1F) * 255 / 31),
			byte(int(px&0x1F) * 255 / 31),
			255
	case fmtA4R4G4B4, fmtLinA4R4G4B4:
		px := uint16(src[off]) | uint16(src[off+1])<<8
		return byte(int((px>>8)&0xF) * 255 / 15),
			byte(int((px>>4)&0xF) * 255 / 15),
			byte(int(px&0xF) * 255 / 15),
			byte(int((px>>12)&0xF) * 255 / 15)
	case fmtL8, fmtLinL8, fmtP8:
		v := src[off]
		return v, v, v, 255
	case fmtLinA8:
		return 255, 255, 255, src[off]
	case fmtAL8, fmtLinAL8:
		v := src[off]
		return v, v, v, v
	case fmtLinA8L8:
		l := src[off]
		a := src[off+1]
		return l, l, l, a
	}
	return 128, 128, 128, 255
}

// ---------------------------------------------------------------------------
// Header parsing
// ---------------------------------------------------------------------------

type xbxHeader struct {
	Magic      uint32
	TotalSize  uint32
	HeaderSize uint32
	Common     uint32
	FormatDW   uint32
	SizeDW     uint32
	XboxFmt    int
	Width      int
	Height     int
}

// detectRenamedImage returns a friendly name for common image formats that
// the user may have renamed to .xbx without converting.
func detectRenamedImage(data []byte) string {
	if len(data) < 8 {
		return ""
	}
	if string(data[:8]) == "\x89PNG\r\n\x1a\n" {
		return "PNG"
	}
	if string(data[:3]) == "\xff\xd8\xff" {
		return "JPEG"
	}
	if string(data[:6]) == "GIF87a" || string(data[:6]) == "GIF89a" {
		return "GIF"
	}
	if string(data[:2]) == "BM" {
		return "BMP"
	}
	if string(data[:4]) == "DDS " {
		return "DDS (DirectDraw Surface)"
	}
	if len(data) >= 12 && string(data[:4]) == "RIFF" && string(data[8:12]) == "WEBP" {
		return "WebP"
	}
	if string(data[:4]) == "\x00\x00\x01\x00" {
		return "ICO"
	}
	if len(data) >= 18 && string(data[len(data)-18:len(data)-2]) == "TRUEVISION-XFILE" {
		return "TGA"
	}
	return ""
}

func parseHeader(data []byte) (*xbxHeader, error) {
	if len(data) < xprHeaderSize {
		return nil, errors.New("file too small to contain XPR_HEADER")
	}
	h := &xbxHeader{
		Magic:      binary.LittleEndian.Uint32(data[0:4]),
		TotalSize:  binary.LittleEndian.Uint32(data[4:8]),
		HeaderSize: binary.LittleEndian.Uint32(data[8:12]),
	}
	if h.Magic != xprMagic {
		if name := detectRenamedImage(data); name != "" {
			return nil, fmt.Errorf("this file is a %s, not an XPR0 .xbx — looks like it was renamed. To turn it into a real .xbx, run: xbx-convert encode <file> <output.xbx>", name)
		}
		return nil, fmt.Errorf("bad magic 0x%08X (expected 0x%08X 'XPR0')", h.Magic, xprMagic)
	}
	if h.HeaderSize < xprHeaderSize+20 {
		return nil, fmt.Errorf("header too small (%d)", h.HeaderSize)
	}
	if int(h.HeaderSize) > len(data) {
		return nil, fmt.Errorf("header_size %d exceeds file size %d", h.HeaderSize, len(data))
	}

	h.Common = binary.LittleEndian.Uint32(data[12:16])
	// data[16:20] = data ptr, data[20:24] = lock — both ignored
	h.FormatDW = binary.LittleEndian.Uint32(data[24:28])
	h.SizeDW = binary.LittleEndian.Uint32(data[28:32])

	if h.Common&d3dCommonTypeMask != d3dCommonTypeTexture {
		return nil, fmt.Errorf("resource is not a texture (Common=0x%08X)", h.Common)
	}
	h.XboxFmt = int((h.FormatDW & d3dFormatFormatMask) >> d3dFormatFormatShift)

	if h.SizeDW != 0 && h.SizeDW != 0xFFFFFFFF {
		h.Width = int(h.SizeDW&d3dSizeWidthMask) + 1
		h.Height = int((h.SizeDW&d3dSizeHeightMask)>>d3dSizeHeightShift) + 1
	} else {
		uIdx := int((h.FormatDW & d3dFormatUSizeMask) >> d3dFormatUSizeShift)
		vIdx := int((h.FormatDW & d3dFormatVSizeMask) >> d3dFormatVSizeShift)
		h.Width = 1
		h.Height = 1
		if uIdx < len(expTable) {
			h.Width = expTable[uIdx]
		}
		if vIdx < len(expTable) {
			h.Height = expTable[vIdx]
		}
	}
	return h, nil
}

// ---------------------------------------------------------------------------
// Decode entry point
// ---------------------------------------------------------------------------

func decodeXBX(data []byte) (*image.NRGBA, error) {
	h, err := parseHeader(data)
	if err != nil {
		return nil, err
	}
	width, height, fmt := h.Width, h.Height, h.XboxFmt
	pixels := data[h.HeaderSize:]

	var rgba []byte
	switch {
	case isDXT(fmt):
		switch fmt {
		case fmtDXT1:
			rgba = decodeDXT1(pixels, width, height)
		case fmtDXT3:
			rgba = decodeDXT3(pixels, width, height)
		default:
			rgba = decodeDXT5(pixels, width, height)
		}
	case isLinear(fmt):
		bpp := bytesPerPixel(fmt)
		// NV2A linear pitch is 64-byte aligned
		srcPitch := (width*bpp + 63) &^ 63
		rgba = make([]byte, width*height*4)
		for y := 0; y < height; y++ {
			for x := 0; x < width; x++ {
				srcOff := y*srcPitch + x*bpp
				if srcOff+bpp > len(pixels) {
					continue
				}
				r, g, b, a := pixelToRGBA(fmt, pixels, srcOff)
				di := (y*width + x) * 4
				rgba[di+0] = r
				rgba[di+1] = g
				rgba[di+2] = b
				rgba[di+3] = a
			}
		}
	default:
		bpp := bytesPerPixel(fmt)
		sw := newSwizzler(width, height)
		rgba = make([]byte, width*height*4)
		for y := 0; y < height; y++ {
			for x := 0; x < width; x++ {
				swizIdx := sw.linearToSwizzled(x, y)
				srcOff := swizIdx * bpp
				if srcOff+bpp > len(pixels) {
					continue
				}
				r, g, b, a := pixelToRGBA(fmt, pixels, srcOff)
				di := (y*width + x) * 4
				rgba[di+0] = r
				rgba[di+1] = g
				rgba[di+2] = b
				rgba[di+3] = a
			}
		}
	}

	img := image.NewNRGBA(image.Rect(0, 0, width, height))
	copy(img.Pix, rgba)
	return img, nil
}

// ---------------------------------------------------------------------------
// DXT1 encoder (simple endpoint-pick + nearest quantization)
// ---------------------------------------------------------------------------

func rgbTo565(r, g, b int) uint16 {
	return uint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
}

func from565(c uint16) (r, g, b int) {
	r = int((c>>11)&0x1F) * 255 / 31
	g = int((c>>5)&0x3F) * 255 / 63
	b = int(c&0x1F) * 255 / 31
	return
}

// encodeDXT1Block compresses a 4x4 RGBA block (16 pixels, each as 4 bytes
// in row-major order) into 8 bytes of DXT1.
func encodeDXT1Block(block []byte, out []byte) {
	minR, minG, minB := 255, 255, 255
	maxR, maxG, maxB := 0, 0, 0
	for i := 0; i < 16; i++ {
		r, g, b := int(block[i*4]), int(block[i*4+1]), int(block[i*4+2])
		if r < minR {
			minR = r
		}
		if r > maxR {
			maxR = r
		}
		if g < minG {
			minG = g
		}
		if g > maxG {
			maxG = g
		}
		if b < minB {
			minB = b
		}
		if b > maxB {
			maxB = b
		}
	}

	c0 := rgbTo565(maxR, maxG, maxB)
	c1 := rgbTo565(minR, minG, minB)

	if c0 == c1 {
		binary.LittleEndian.PutUint16(out[0:2], c0)
		binary.LittleEndian.PutUint16(out[2:4], c1)
		binary.LittleEndian.PutUint32(out[4:8], 0)
		return
	}
	if c0 < c1 {
		c0, c1 = c1, c0
	}

	p0r, p0g, p0b := from565(c0)
	p1r, p1g, p1b := from565(c1)
	p2r := (2*p0r + p1r + 1) / 3
	p2g := (2*p0g + p1g + 1) / 3
	p2b := (2*p0b + p1b + 1) / 3
	p3r := (p0r + 2*p1r + 1) / 3
	p3g := (p0g + 2*p1g + 1) / 3
	p3b := (p0b + 2*p1b + 1) / 3
	pal := [4][3]int{{p0r, p0g, p0b}, {p1r, p1g, p1b}, {p2r, p2g, p2b}, {p3r, p3g, p3b}}

	var bits uint32
	for i := 0; i < 16; i++ {
		r := int(block[i*4])
		g := int(block[i*4+1])
		b := int(block[i*4+2])
		best := 0
		bestD := 1 << 30
		for k := 0; k < 4; k++ {
			dr := r - pal[k][0]
			dg := g - pal[k][1]
			db := b - pal[k][2]
			d := dr*dr + dg*dg + db*db
			if d < bestD {
				bestD = d
				best = k
			}
		}
		bits |= uint32(best) << (2 * uint(i))
	}
	binary.LittleEndian.PutUint16(out[0:2], c0)
	binary.LittleEndian.PutUint16(out[2:4], c1)
	binary.LittleEndian.PutUint32(out[4:8], bits)
}

// toNRGBA returns img as an *image.NRGBA, copying if necessary.
func toNRGBA(img image.Image) *image.NRGBA {
	if n, ok := img.(*image.NRGBA); ok {
		return n
	}
	n := image.NewNRGBA(img.Bounds())
	draw.Draw(n, n.Bounds(), img, img.Bounds().Min, draw.Src)
	return n
}

func encodeDXT1(img image.Image) ([]byte, int, int) {
	src := toNRGBA(img)
	w, h := src.Bounds().Dx(), src.Bounds().Dy()
	if w%4 != 0 || h%4 != 0 {
		newW := (w + 3) &^ 3
		newH := (h + 3) &^ 3
		padded := image.NewNRGBA(image.Rect(0, 0, newW, newH))
		draw.Draw(padded, image.Rect(0, 0, w, h), src, image.Point{}, draw.Src)
		src = padded
		w, h = newW, newH
	}

	out := make([]byte, (w/4)*(h/4)*8)
	var block [64]byte
	pos := 0
	for by := 0; by < h; by += 4 {
		for bx := 0; bx < w; bx += 4 {
			for y := 0; y < 4; y++ {
				row := src.PixOffset(bx, by+y)
				copy(block[y*16:y*16+16], src.Pix[row:row+16])
			}
			encodeDXT1Block(block[:], out[pos:pos+8])
			pos += 8
		}
	}
	return out, w, h
}

func encodeARGB8888Linear(img image.Image) ([]byte, int, int) {
	src := toNRGBA(img)
	w, h := src.Bounds().Dx(), src.Bounds().Dy()
	pitch := (w*4 + 63) &^ 63
	out := make([]byte, pitch*h)
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			si := src.PixOffset(x, y)
			r := src.Pix[si+0]
			g := src.Pix[si+1]
			b := src.Pix[si+2]
			a := src.Pix[si+3]
			o := y*pitch + x*4
			out[o+0] = b
			out[o+1] = g
			out[o+2] = r
			out[o+3] = a
		}
	}
	return out, w, h
}

func log2OrNeg(n int) int {
	if n <= 0 || (n&(n-1)) != 0 {
		return -1
	}
	bits := 0
	for v := n; v > 1; v >>= 1 {
		bits++
	}
	return bits
}

func encodeXBX(img image.Image, formatName string) ([]byte, error) {
	formatName = strings.ToLower(formatName)

	var (
		pixelData []byte
		w, h      int
		xboxFmt   int
		linear    bool
	)
	switch formatName {
	case "dxt1":
		pixelData, w, h = encodeDXT1(img)
		xboxFmt = fmtDXT1
	case "argb8888", "a8r8g8b8", "lin_a8r8g8b8":
		pixelData, w, h = encodeARGB8888Linear(img)
		xboxFmt = fmtLinA8R8G8B8
		linear = true
	default:
		return nil, fmt.Errorf("unsupported encode format: %s", formatName)
	}

	uLog := log2OrNeg(w)
	vLog := log2OrNeg(h)
	useSizeField := uLog < 0 || vLog < 0 || linear

	fmtDW := uint32(xboxFmt) << d3dFormatFormatShift
	var sizeDW uint32
	if !useSizeField {
		fmtDW |= uint32(uLog) << d3dFormatUSizeShift
		fmtDW |= uint32(vLog) << d3dFormatVSizeShift
	} else {
		sizeDW = (uint32(w-1) & d3dSizeWidthMask) |
			((uint32(h-1) << d3dSizeHeightShift) & d3dSizeHeightMask)
	}

	common := d3dCommonTypeTexture | 0x00000001 // refcount=1

	descriptor := make([]byte, 20)
	binary.LittleEndian.PutUint32(descriptor[0:4], common)
	binary.LittleEndian.PutUint32(descriptor[4:8], 0)  // data ptr
	binary.LittleEndian.PutUint32(descriptor[8:12], 0) // lock
	binary.LittleEndian.PutUint32(descriptor[12:16], fmtDW)
	binary.LittleEndian.PutUint32(descriptor[16:20], sizeDW)

	baseHeader := xprHeaderSize + len(descriptor)
	headerSize := ((baseHeader + d3dTextureAlignment - 1) / d3dTextureAlignment) * d3dTextureAlignment
	if headerSize < baseHeader {
		headerSize = baseHeader
	}
	totalSize := headerSize + len(pixelData)

	out := make([]byte, totalSize)
	binary.LittleEndian.PutUint32(out[0:4], xprMagic)
	binary.LittleEndian.PutUint32(out[4:8], uint32(totalSize))
	binary.LittleEndian.PutUint32(out[8:12], uint32(headerSize))
	copy(out[12:32], descriptor)
	for i := 32; i < headerSize; i++ {
		out[i] = 0xAD
	}
	copy(out[headerSize:], pixelData)
	return out, nil
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

const usageText = `xbx-convert — Xbox XPR0 (.xbx) texture <-> PNG converter

Usage:
  xbx-convert decode <input.xbx> [output.png]
  xbx-convert encode <input.png> [output.xbx] [--format dxt1|argb8888]
  xbx-convert info   <input.xbx>

Decode formats: DXT1/3/5, A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, X1R5G5B5,
                A4R4G4B4, L8, AL8, P8, and LIN_* variants
Encode formats: dxt1 (default, lossy), argb8888 (lossless, larger)
`

func usage() {
	fmt.Fprint(os.Stderr, usageText)
}

func cmdDecode(args []string) error {
	if len(args) < 1 {
		return errors.New("decode: missing input path")
	}
	inPath := args[0]
	outPath := ""
	if len(args) >= 2 {
		outPath = args[1]
	} else {
		ext := filepath.Ext(inPath)
		outPath = strings.TrimSuffix(inPath, ext) + ".png"
	}

	data, err := os.ReadFile(inPath)
	if err != nil {
		return err
	}
	img, err := decodeXBX(data)
	if err != nil {
		return err
	}
	f, err := os.Create(outPath)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		return err
	}
	fmt.Printf("decoded %s -> %s (%dx%d)\n", inPath, outPath, img.Bounds().Dx(), img.Bounds().Dy())
	return nil
}

func cmdEncode(args []string) error {
	// Hand-roll a tiny flag parser: positional args plus --format/-f.
	format := "dxt1"
	var positional []string
	for i := 0; i < len(args); i++ {
		a := args[i]
		switch {
		case a == "--format" || a == "-f":
			if i+1 >= len(args) {
				return fmt.Errorf("%s requires a value", a)
			}
			format = args[i+1]
			i++
		case strings.HasPrefix(a, "--format="):
			format = strings.TrimPrefix(a, "--format=")
		default:
			positional = append(positional, a)
		}
	}
	if len(positional) < 1 {
		return errors.New("encode: missing input path")
	}
	inPath := positional[0]
	outPath := ""
	if len(positional) >= 2 {
		outPath = positional[1]
	} else {
		ext := filepath.Ext(inPath)
		outPath = strings.TrimSuffix(inPath, ext) + ".xbx"
	}

	f, err := os.Open(inPath)
	if err != nil {
		return err
	}
	defer f.Close()
	img, _, err := image.Decode(f)
	if err != nil {
		return fmt.Errorf("reading %s: %w", inPath, err)
	}
	blob, err := encodeXBX(img, format)
	if err != nil {
		return err
	}
	if err := os.WriteFile(outPath, blob, 0644); err != nil {
		return err
	}
	w, h := img.Bounds().Dx(), img.Bounds().Dy()
	fmt.Printf("encoded %s -> %s (%dx%d, %s)\n", inPath, outPath, w, h, format)
	return nil
}

func cmdInfo(args []string) error {
	if len(args) < 1 {
		return errors.New("info: missing input path")
	}
	inPath := args[0]
	data, err := os.ReadFile(inPath)
	if err != nil {
		return err
	}
	h, err := parseHeader(data)
	if err != nil {
		return err
	}
	name, ok := formatNames[h.XboxFmt]
	if !ok {
		name = fmt.Sprintf("UNKNOWN(0x%02X)", h.XboxFmt)
	}
	fmt.Printf("file:        %s\n", inPath)
	fmt.Printf("size:        %d bytes\n", len(data))
	fmt.Printf("total_size:  %d\n", h.TotalSize)
	fmt.Printf("header_size: %d\n", h.HeaderSize)
	fmt.Printf("format:      %s (0x%02X)\n", name, h.XboxFmt)
	fmt.Printf("dimensions:  %dx%d\n", h.Width, h.Height)
	fmt.Printf("common:      0x%08X\n", h.Common)
	fmt.Printf("format_dw:   0x%08X\n", h.FormatDW)
	fmt.Printf("size_dw:     0x%08X\n", h.SizeDW)
	return nil
}

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	var err error
	switch os.Args[1] {
	case "decode":
		err = cmdDecode(os.Args[2:])
	case "encode":
		err = cmdEncode(os.Args[2:])
	case "info":
		err = cmdInfo(os.Args[2:])
	case "-h", "--help", "help":
		usage()
		return
	default:
		fmt.Fprintf(os.Stderr, "unknown command: %s\n\n", os.Args[1])
		usage()
		os.Exit(2)
	}
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		os.Exit(1)
	}
}
