# XEX2 Format

Everything in a XEX header is **big-endian**. Everything in the PE image it wraps is
**little-endian**, because the PE format is little-endian regardless of target.

All of this was derived by unpacking XEX files produced by Microsoft's own `imagexex`,
not from documentation. The reference sample throughout is `XDK/bin/xbox/demofixer.xex`
from XDK 21256.3, which is unencrypted and basic-compressed, so it round-trips cleanly.

## File header

```
offset  size  field
0x00    4     magic, "XEX2"
0x04    4     module_flags
0x08    4     pe_data_offset      file offset of the image payload
0x0c    4     reserved
0x10    4     security_info_offset
0x14    4     optional_header_count
0x18    ...   optional_header_count entries of { u32 key; u32 value; }
```

### Optional header keys

The low byte of the key encodes how `value` is interpreted:

| low byte | meaning |
| --- | --- |
| `0x00` | `value` *is* the data (a flag word) |
| `0x01` | `value` *is* the data (a single u32) |
| `0xFF` | `value` is a file offset; a u32 size leads the data there |
| other | `value` is a file offset; data is `low_byte * 4` bytes |

Keys seen in the wild:

| key | name |
| --- | --- |
| `0x000002FF` | RESOURCE_INFO |
| `0x000003FF` | FILE_FORMAT_INFO |
| `0x00010100` | ENTRY_POINT |
| `0x00010201` | IMAGE_BASE_ADDRESS |
| `0x000103FF` | IMPORT_LIBRARIES |
| `0x00018002` | CHECKSUM_TIMESTAMP |
| `0x000183FF` | ORIGINAL_PE_NAME |
| `0x000200FF` | STATIC_LIBRARIES |
| `0x00020104` | TLS_INFO |
| `0x00020200` | DEFAULT_STACK_SIZE |
| `0x00030000` | SYSTEM_FLAGS |
| `0x00040006` | EXECUTION_INFO |
| `0x00040404` | LAN_KEY |

## FILE_FORMAT_INFO

```
u32 info_size
u16 encryption_type     0 = none, 1 = encrypted
u16 compression_type    0 = raw, 1 = basic, 2 = LZX, 3 = delta
```

For `compression_type == 1` (basic) the remainder is an array of
`{ u32 data_size; u32 zero_size; }` blocks, `(info_size - 8) / 8` of them. Reconstruct the
image by walking the blocks: copy `data_size` bytes from the payload, then append
`zero_size` zero bytes, and repeat. `cxex` emits this form. It is trivial to produce and
the loader accepts it.

Retail titles are `encryption=1, compression=2`. The XDK's own on-console tools are
`encryption=0, compression=1`, which is the combination worth imitating.

## The wrapped image is a memory image, not a file image

This is the detail most likely to waste a day. The payload is the image **as mapped**:
RVA equals offset within the payload. The PE section headers' `PointerToRawData` fields
are irrelevant to the XEX loader.

`imagexex` also **drops the sections it has consumed**. In `demofixer.xex` the PE headers
still describe seven sections, but the payload stops at `image_size` (`0xc000`), which
covers only `.rdata`, `.pdata`, `.text` and `.data`. The `.idata`, `.XBLD` and `.reloc`
sections lie beyond that and are simply not stored: imports have moved into the XEX
header, and relocations are unnecessary because the image is fixed at its base address.

```
[0] .rdata  rva=0x000400 vsize=0x2fc0
[1] .pdata  rva=0x003400 vsize=0x02e0
[2] .text   rva=0x004000 vsize=0x6440
[3] .data   rva=0x00b000 vsize=0x0860   <- image_size 0xc000 ends here
[4] .idata  rva=0x00c000                <- stripped
[5] .XBLD   rva=0x00d000                <- stripped
[6] .reloc  rva=0x00d200                <- stripped
```

The inner PE is PE32 (`opt_magic = 0x010b`), machine **`0x01F2`**
(`IMAGE_FILE_MACHINE_POWERPCBE`), section alignment `0x1000`, file alignment `0x200`.
Typical image bases are `0x82000000` (titles) and `0x92000000` (system/dev tools).

## Security info

At `security_info_offset`:

```
0x00   u32   header_size
0x04   u32   image_size
0x08   u8[256]  rsa_signature
0x108  u32   image_info_size
0x10c  u32   image_flags
0x110  u32   load_address
0x114  u8[20]   section_digest
0x128  u32   import_table_count
0x12c  u8[20]   import_table_digest
0x140  u8[16]   media_id
0x150  u8[16]   aes_key
0x160  u32   export_table
0x164  u8[20]   header_digest
0x178  u32   region
0x17c  u32   allowed_media_types
0x180  u32   page_descriptor_count
0x184  ...   page descriptors
```

On an RGH/JTAG console the signature and digests are not validated, so they can be left
zero. `image_size` and `load_address` must be right.

## Imports

This is the part with no original-Xbox equivalent. Imports do **not** use the PE import
directory. They live in the `IMPORT_LIBRARIES` optional header, and the loader patches
addresses directly into the image.

```
u32 size
u32 string_table_size
u32 library_count
u8  string_table[string_table_size]     NUL-separated library names
    then library_count records:
    u32 size                            size of this record
    u8  next_import_digest[20]
    u32 id
    u32 version
    u32 version_min
    u16 name_index                      index into the string table
    u16 record_count
    u32 records[record_count]           addresses in the image to patch
```

Libraries are named `xboxkrnl.exe` and `xam.xex`, with versions like `2.0.21256.0`.
A single library may appear more than once with different version ranges; retail
`dash.xex` carries two `xam.xex` and two `xboxkrnl.exe` records.

`version` and `version_min` come straight from the import library. The archive members
in `xboxkrnl.lib` are all named `xboxkrnl.exe@21256.0+1861.0`, and `demofixer.xex`
records `version = 2.0.21256.0`, `version_min = 2.0.1861.0`, the two halves of that
name. Version words pack as `major:4 | minor:4 | build:16 | qfe:8`.

`id` is a per-library constant that `imagexex` stamps in. It does not appear to be
derived from anything in the image; the same values show up across files:

| library | id |
| --- | --- |
| `xam.xex` | `0xfca15c76` |
| `xboxkrnl.exe` | `0x45dc17e0` |

`next_import_digest` is a SHA1 chaining each library record to the next, so the final
record's is zero. It is not validated on an RGH/JTAG console, and `cxex` leaves it zero.
That field is the only byte-level difference between a `cxex` rebuild of `demofixer.xex`
and Microsoft's original.

### What the records point at

Each record is an address inside the loaded image. There are two kinds, and the kind is
determined by the import's type in the import library, not by anything in the XEX:

**Code imports** (`type 0` in `xboxkrnl.lib`) get **two** records: a pointer slot, then a
16-byte thunk.

**Data imports** (`type 2`) get **one** record: the pointer slot only.

So the record list is mostly alternating, but not reliably so. In `demofixer.xex` the
kernel library's records run `slot, thunk, slot, thunk, ...` until `0x92000448 0x9200044c`
appear back to back. Those two resolve to ordinals 89 and 430, `KeDebugMonitorData` and
`ExLoadedCommandLine`, both `type 2` in `xboxkrnl.lib`. Every unpaired slot in the file
is a data export, with no exceptions.

### Pointer slot contents

A u32, in a read-only data region, holding:

```
(library_index << 16) | ordinal
```

`demofixer.xex` slot `0x92000424` holds `0x00010103`: library 1 (`xboxkrnl.exe`),
ordinal `0x103` = 259 = `ObCreateSymbolicLink`. The xam slots hold `0x000001a6` and
friends, at library 0, ordinal 422. Ordinals match `xboxkrnl.lib` exactly, so the import
library is the authoritative source for both the ordinal and the type.

### Thunk contents

Sixteen bytes, at the end of `.text`:

```
010001a6    (0x01 << 24) | ordinal      placeholder, becomes the high half of the address
020001a6    (0x02 << 24) | ordinal      placeholder, becomes the low half
7d6903a6    mtctr r11
4e800420    bctr
```

The loader rewrites the two placeholder words into the instruction pair that materialises
the resolved function address in `r11`. The `0x01` / `0x02` tags tell it which half of the
address each word becomes; the ordinal in the low half identifies the function.

Note that a disassembler will reject the two placeholder words as invalid encodings and
may resynchronise in a way that makes the thunks look 8 bytes apart. They are 16.

## Practical notes for emitting a XEX

- Emit `encryption=0, compression=1` (basic) with a single block: `data_size = image_size`,
  `zero_size = 0`. Nothing needs compressing.
- Lay the payload out by RVA, starting with the PE headers, and stop at `image_size`.
- Do not emit `.idata` or `.reloc` at all; there is nothing to relocate at a fixed base.
- Put the pointer slots in a read-only section and the thunks at the end of `.text`.
- Ordinals and code/data types both come from the XDK import libraries
  (`xboxkrnl.lib` is 905 import records; see `tools/xexutil/implib.py`).

## The header region is integrity checked

This is the thing that stops a hand-built XEX from loading, and it is not obvious from the
format alone. On an RGH/JTAG console the RSA signature is **not** enforced, but something
else validates the header region byte for byte, padding included.

Three single-variable experiments on real hardware, each using the same working app XEX
(Aurora, pulled off the console over XBDM) launched from the same path:

| change | result |
| --- | --- |
| none | loads |
| entire 256-byte RSA signature zeroed | **loads** |
| one byte of zero padding flipped `00` to `5A` | **rejected** |
| payload and security info byte-identical, header blobs relocated | **rejected** |

So the signature can be discarded, but the header bytes cannot move and cannot change.
`cxex` can reproduce a header it was given, which is why the demofixer round-trip passes,
but it cannot yet author one the console will accept.

The three digest fields are not plain SHA1 over any contiguous range of the header. Every
4-byte-aligned start/end pair was swept, across variants with the header digest zeroed, the
signature zeroed, and both, with no match. The scheme is chained or non-contiguous.

Two reverse-engineering assets bear on this. `XDK/bin/xbox/symsrv/` ships the debug kernel
**with its PDB**, which is the code performing the validation. `XDK/bin/win32/imagexex.exe`
writes valid headers and statically links SHA1 (round constants at file offsets `0x20c95`,
`0x20f13`, `0x21189`, `0x214a4`), with no CryptoAPI imports.

### Telling accept from reject over XBDM

The notification channel distinguishes them without needing a screen. A successful launch
shows one `execution reboot_title` then a direct `modload` of the target. A rejected one
shows `reboot_title`, a `modload` of `lhelper.xex`, a second `reboot_title`, and then the
dashboard loading again.

## The header hash

Reverse engineered out of `XexpVerifyXexHeaders` in the debug kernel the XDK ships with
symbols (`XDK/bin/xbox/symsrv/`), at VA `0x800a08d8`. This is the check that rejects a
hand-built XEX, and it is the reason the padding experiment above fails.

```
header_digest = SHA1( file[sec + 0x17C .. pe_data_offset)  ||  file[0 .. sec + 8) )
```

The tail range is hashed **first**, then the head range. The result is compared against the
twenty bytes at `sec + 0x164`; a mismatch returns `0xC0000221`
(`STATUS_IMAGE_CHECKSUM_MISMATCH`) with no debug output at all, which is why the console
says nothing useful when it refuses a file.

The window that is skipped, `[sec + 8, sec + 0x17C)`, is `0x174` bytes long, exactly the
`image_info_size` constant. That window holds the RSA signature, `image_flags`,
`load_address`, the three digest fields, `media_id` and the AES key, and it is covered by
the signature instead. That is precisely why zeroing the signature still boots on an RGH
console while touching any other header byte does not.

Everything else is covered: the magic, `module_flags`, `pe_data_offset`, the whole optional
header directory, every optional header blob, `header_size`, `image_size`,
`allowed_media_types`, `region`, every page descriptor, and all padding out to
`pe_data_offset`. So the digest has to be computed last, once the header region is final.

Verified by reproducing the stored digest of Aurora, demofixer, XellLaunch, xnetconfig and
finished.xex exactly.

### Loader structural constraints

`XexpLoadXexHeaders` reads the first 2048 bytes and requires, before it hashes anything:

- magic is `XEX2`
- `pe_data_offset` >= `0x800`
- `pe_data_offset` <= `0x10000`
- `pe_data_offset` is a multiple of **`0x800`**, not `0x1000`
- the reserved word at `0x0C` is zero

Any failure returns `0xC000007B` (`STATUS_INVALID_IMAGE_FORMAT`).

## The import digest chain

Each library record in `IMPORT_LIBRARIES` carries a `next_import_digest` at `+0x04` that
chains it to the record after it. Records are walked in reverse:

```
prev = 20 zero bytes
for rec in reversed(records):
    rec[0x04:0x18] = prev
    prev = SHA1(rec[0x04 : record_size])
```

The last record therefore stores zeros. Implementing this makes `cxex` reproduce
demofixer's import table byte for byte, all 540 bytes, where the digest fields
were the only difference.

The record layout is slightly different from what a naive read suggests: `name_index` is a
single byte at `+0x25`, preceded by a zero byte at `+0x24`, with the import count as a u16
at `+0x26`. Reading a big-endian u16 at `+0x24` happens to give the right answer because
the leading byte is zero.

`imagexex` always places the import table **last**, ending flush against `pe_data_offset`
(demofixer: `0xde4 + 540 = 0x1000`). `cxex` does the same.

## The content hash chain

The header hash covers the header region. The image itself is covered by a second chain,
running backwards through the page descriptors:

```
H[n] = 20 zero bytes
H[i] = SHA1( image bytes of run i || be32(descriptor word i) || H[i+1] )
descriptor[i].digest = H[i+1]
security_info.section_digest (+0x114) = H[0]
```

Each descriptor stores the hash of the run that *follows* it, so the last descriptor's
digest is zero and the head of the chain lands in `section_digest`. The hash is taken over
the image as mapped, decrypted and decompressed, so the implicit zero fill out to
`image_size` is part of it.

`security_info.import_table_digest` (+0x12c) is the head of the *import* chain, which is
the SHA1 of the first library record. Both chains fall inside the `HV_IMAGE_INFO` window
that the header hash skips, so they are independent of it and can be filled in in any
order.

`cxex` computes all of this, and reproduces demofixer's `section_digest` and every page
descriptor digest exactly.

`aes_key` in an unencrypted image is not zero: `imagexex` stores the constant
`66e94bd4ef8a2c3b884cfa59ca342b2e`, which is AES-128 of a zero block under a zero key.

With these implemented, a `cxex` rebuild of demofixer differs from Microsoft's original
only in the RSA signature, which cannot be computed without the private key and which an
RGH console does not check, and in `header_digest`, which reflects the writer's own
header layout.

## Result

A XEX built this way loads and runs. The `samples/hello` program, hand-written PPC assembly
assembled by clang on macOS and wrapped by `cxex`, was launched on an RGH console and
produced its output over XBDM:

```
name="\Device\Harddisk0\Partition1\OXDK360\default.xex"
debugstr string=OXDK360: hello from a clang-assembled XEX
debugstr string=OXDK360: DbgPrint import thunk works
```

which also confirms the import thunk mechanism resolves and dispatches correctly.

## What the COFF-to-ELF translator has to handle

Surveying every relocation in 80 objects of `libcMT.lib`, the XDK's static libraries use a
much smaller set than the COFF spec allows. Outside debug sections there are only four:

| COFF | count | ELF equivalent |
| --- | --- | --- |
| `ADDR32` | 518 | `R_PPC_ADDR32` |
| `REL24` | 705 | `R_PPC_REL24` |
| `REFHI` + `PAIR` | 304 | `R_PPC_ADDR16_HA` |
| `REFLO` | 309 | `R_PPC_ADDR16_LO` |

`SECREL` and `SECTION`, which together are the most numerous types in the archive at 1705
each, appear only inside `.debug$S` and `.debug$T`. Those sections are dropped, so the
translator never has to implement them.

Note that the relocation type numbers are the standard `IMAGE_REL_PPC_*` values:
`SECREL` is `0x0B` and `SECTION` is `0x0C`, not the values some references list.

### pe_data_offset alignment in practice

The loader only enforces a `0x800` multiple, and a XEX with a `0x800`-aligned header does
load. But across 73 shipped XEX files every single one uses a multiple of `0x1000`, and
everything downstream of the header check works in 4 KB units: `XexpLoadXexHeaders` commits
`round_up(pe_data_offset, 0x1000)`, and `XexpTransferXexHeaders` hands the hypervisor a
page list of `round_up(pe_data_offset, 0x1000) >> 12` entries. `cxex` rounds to `0x1000`
to match `imagexex` rather than sitting on the bare minimum.

## Translating the XDK libraries

`tools/coff2elf` rewrites a Xenon COFF object into a PowerPC big-endian ELF32 object that
`ld.lld` will link. Relocation arithmetic is verified: an independent check recomputed all
12223 relocations in `libcMT.lib` from the original COFF at two different link bases and
found none wrong, and the second base deliberately drives the low half negative, which is
what distinguishes `ADDR16_HA` from `ADDR16_HI`.

One trap is worth recording because it fails silently. ELF gives `@` a meaning MSVC does
not: `ld.lld` reads `name@version`, and `name@@version` as a default-version definition.
Every MSVC C++ mangled name contains `@@`, so passing the names through unchanged makes
lld **merge distinct symbols**. In `undname.obj` that resolved 215 relocations to the
wrong function with no diagnostic at all; across the XDK it affects 2233 defined externals
in 441 objects. There is no lld option to disable version parsing, so the translator
escapes the character. `??0DName@@QAA@PAV0@@Z` and `??0DName@@QAA@_J@Z` now link to
`0x820010e0` and `0x82001b90` as they should, rather than both to the latter.

The other thing that had to be solved before more than one object could be linked is
COMDAT. MSVC puts every inline function, every template instantiation and every floating
point constant in one, so `ceil.obj` and `floor.obj` together fail on
`duplicate symbol: __real@3ff0000000000000`. Each COMDAT now becomes an ELF section group,
and `ASSOCIATIVE` sections join the group of the `.text` they describe instead of forming
one of their own, which is what keeps `.pdata` and `.xdata` with their function. All 687
translatable members of `libcMT.lib` link in one command. Two selection kinds do not
survive the trip: `NODUPLICATES` discards a second definition rather than rejecting it, and
`LARGEST` keeps the first copy rather than the biggest, because ELF groups have no way to
express either. A COMDAT whose leader is a static symbol is deliberately left ungrouped, as
`lld-link` also does, which additionally avoids 283 relocations in `libcMT.lib` that point
from one COMDAT at a static symbol in another and would otherwise reference a discarded
section.

## Compiling against the XDK headers

Microsoft's headers do compile under clang, with the same shape of gates OXDK uses for the
original Xbox:

```
-fms-extensions -fms-compatibility -fms-compatibility-version=14
-D_WIN32 -D_M_PPCBE -D_M_PPC -D_PPC_ -D_XBOX -D_XENON -DXBOX -D_SIZE_T_DEFINED -D_W64=
-I <XDK>/include/xbox
-I <XDK>/TechPreview/Jul12Compiler/include/xbox
```

`_WIN32` is what `crtdefs.h` and `vadefs.h` check, `_PPC_` selects the architecture block in
`winnt.h` that defines `CONTEXT`, and `_M_PPCBE` is what the XDK's own headers test. The
TechPreview compiler tree supplies the CRT headers (`excpt.h`, `stdarg.h`, `sys/types.h`)
that `xtl.h` expects the compiler to bring.

### The VMX128 wall

`xtl.h` includes `d3dx9.h`, which includes `xboxmath.h`, which includes `xnamath.h`, which
refuses to compile without VMX128 support:

```
#error xnamath.h requires VMX128 compiler support for XBOX 360
```

VMX128 is Xenon's vector ISA, with 128 vector registers rather than AltiVec's 32. The XDK
headers use **138 distinct VMX128 intrinsics**, and clang implements none of them.

Code that includes `d3d9.h` directly rather than the `xtl.h` umbrella avoids the whole
chain, which is what `samples/d3dclear` does. Anything that includes `xtl.h`, which is
most real projects, hits it immediately. `oxdk360/header-shim/xnamath.h` gets past the
`#error` by defining the guard and the compatibility types, far enough to reveal that the
next obstacle is the intrinsics themselves rather than the header.

Two ways past it, both substantial: implement the VMX128 intrinsics in LLVM's PowerPC
backend, or reimplement XNA math as scalar C so nothing needs them. The first is the real
fix and would also serve any code using vectors directly; the second is mechanical and
would unblock ports that only use the math incidentally.
