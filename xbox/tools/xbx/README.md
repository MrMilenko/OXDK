# xbx-convert

Bidirectional converter between Xbox XPR0 (`.xbx`) textures and standard image formats. Two implementations live here:

- **`go/`**: pure Go, zero dependencies, ships as a single static binary. Recommended for skinners.
- **`xbx_convert.py`**: the original Python implementation, needing Python 3 and Pillow. Functionally equivalent.

Both produce identical output. Use whichever you prefer.

## For Skinners

If you want to modify the artwork in a dashboard skin, your workflow is:

1. **Decode** the `.xbx` files in the skin to PNG.
2. **Edit** the PNGs in your image editor of choice.
3. **Encode** them back to `.xbx`.
4. **Replace** the originals and repackage the skin.

This applies to:

- [UIX-Lite](https://github.com/OfficialTeamUIX/UIX-Lite) skin assets
- [Theseus](https://github.com/MrMilenko/Theseus) / UIX-Desktop skin assets
- Any other dashboard or app that uses XPR0 textures

> **Note:** The tool was built primarily for Dashboard development work, but it works on any XPR0 texture file. The same workflow applies to development on `.xbx` / `.xpr` assets in your own projects.

### Get the binary

Pre-built binaries for Linux, macOS, and Windows (amd64 and arm64) are attached to each [release](https://github.com/MrMilenko/OXDK/releases). Download the one for your platform. No install needed, it is a single executable.

| Platform | File |
| --- | --- |
| Linux x86_64 | `xbx-convert-linux-amd64` |
| Linux ARM64 | `xbx-convert-linux-arm64` |
| macOS Intel | `xbx-convert-darwin-amd64` |
| macOS Apple Silicon | `xbx-convert-darwin-arm64` |
| Windows x86_64 | `xbx-convert-windows-amd64.exe` |
| Windows ARM64 | `xbx-convert-windows-arm64.exe` |

On Linux/macOS, mark it executable after downloading:

```sh
chmod +x xbx-convert-darwin-arm64
```

### Usage

```sh
# Decode an .xbx texture to PNG. The output path is optional and defaults to the input with .png
xbx-convert decode logo.xbx logo.png

# Edit logo.png in Photoshop / GIMP / Aseprite / whatever

# Encode the modified PNG back to .xbx (DXT1, the format most skins use)
xbx-convert encode logo.png logo.xbx

# If you need lossless (larger file), use ARGB8888
xbx-convert encode logo.png logo.xbx --format argb8888

# Inspect a file's header (dimensions, format, sizes)
xbx-convert info logo.xbx
```

### Picking a format when re-encoding

- **DXT1** (default): 4:1 lossy compression. What most original Xbox skin textures use. Pick this unless you have a specific reason not to.
- **ARGB8888**: uncompressed, lossless, about 8x larger. Use when you need exact alpha or when the original was uncompressed.

Run `xbx-convert info original.xbx` first to see what format the original used, and match it.

## For Developers

### Build from source

```sh
cd tools/xbx/go
go build -trimpath -ldflags="-s -w" -o xbx-convert .
```

Requires Go 1.21+. No external dependencies.

### Cross-compile all platforms

```sh
cd tools/xbx/go
./build-all.sh
# Output lands in dist/
```

### Format support

**Decode:** DXT1, DXT3, DXT5, A8R8G8B8, X8R8G8B8, R5G6B5, A1R5G5B5, X1R5G5B5, A4R4G4B4, L8, AL8, P8 (as grayscale), and their `LIN_*` (non-swizzled) variants.

**Encode:** DXT1 (default, lossy) and ARGB8888 (lossless, larger).

Handles both swizzled (Morton / Z-order) and linear layouts with NV2A 64-byte pitch alignment.

### Python version

`xbx_convert.py` is the original Pillow-based implementation, kept around for reference and for anyone who'd rather have the source in Python:

```sh
./xbx_convert.py decode input.xbx output.png
./xbx_convert.py encode input.png output.xbx [--format dxt1|argb8888]
./xbx_convert.py info input.xbx
```

Same CLI, same behavior. Requires `pip install Pillow`.
