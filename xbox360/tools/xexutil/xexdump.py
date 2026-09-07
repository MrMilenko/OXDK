#!/usr/bin/env python3
"""Parse XEX2 headers. Everything in a XEX is big-endian."""
import struct, sys

def u32(d, o): return struct.unpack_from('>I', d, o)[0]
def u16(d, o): return struct.unpack_from('>H', d, o)[0]

# high 24 bits = id, low 8 = size class
KEYS = {
    0x000002FF: "RESOURCE_INFO",
    0x000003FF: "FILE_FORMAT_INFO",
    0x000004FF: "DELTA_PATCH_DESCRIPTOR",
    0x00000405: "BASE_REFERENCE",
    0x000080FF: "BOUNDING_PATH",
    0x00008105: "DEVICE_ID",
    0x00010001: "ORIGINAL_BASE_ADDRESS",
    0x00010100: "ENTRY_POINT",
    0x00010201: "IMAGE_BASE_ADDRESS",
    0x000103FF: "IMPORT_LIBRARIES",
    0x00018002: "CHECKSUM_TIMESTAMP",
    0x00018102: "ENABLED_FOR_CALLCAP",
    0x00018200: "ENABLED_FOR_FASTCAP",
    0x000183FF: "ORIGINAL_PE_NAME",
    0x000200FF: "STATIC_LIBRARIES",
    0x00020104: "TLS_INFO",
    0x00020200: "DEFAULT_STACK_SIZE",
    0x00020301: "DEFAULT_FILESYSTEM_CACHE_SIZE",
    0x00020401: "DEFAULT_HEAP_SIZE",
    0x00028002: "PAGE_HEAP_SIZE_AND_FLAGS",
    0x00030000: "SYSTEM_FLAGS",
    0x00040006: "EXECUTION_INFO",
    0x00040310: "GAME_RATINGS",
    0x00040404: "LAN_KEY",
    0x000405FF: "XBOX360_LOGO",
    0x000406FF: "MULTIDISC_MEDIA_IDS",
    0x000407FF: "ALTERNATE_TITLE_IDS",
    0x00040801: "ADDITIONAL_TITLE_MEMORY",
    0x00E10402: "EXPORTS_BY_NAME",
}

ENCRYPT = {0: "none", 1: "encrypted"}
COMPRESS = {0: "none/raw", 1: "compressed(basic)", 2: "compressed(normal)", 3: "delta"}

def main(path):
    d = open(path, 'rb').read()
    if d[:4] != b'XEX2':
        print(f"{path}: not XEX2 (magic={d[:4]!r})"); return
    module_flags = u32(d, 4)
    pe_off       = u32(d, 8)
    reserved     = u32(d, 12)
    sec_off      = u32(d, 16)
    count        = u32(d, 20)
    print(f"=== {path}")
    print(f"  module_flags=0x{module_flags:08x}  pe_data_offset=0x{pe_off:x}  "
          f"security_info@0x{sec_off:x}  optional_headers={count}")

    for i in range(count):
        key = u32(d, 24 + i*8)
        val = u32(d, 24 + i*8 + 4)
        name = KEYS.get(key, f"UNKNOWN_0x{key:08x}")
        sz = key & 0xFF
        if sz == 0x01:
            print(f"  [{name:28s}] inline=0x{val:08x}")
        elif sz == 0x00:
            print(f"  [{name:28s}] flag=0x{val:08x}")
        elif sz == 0xFF:
            dsz = u32(d, val)
            print(f"  [{name:28s}] @0x{val:x} size={dsz}")
            if name == "IMPORT_LIBRARIES":
                dump_imports(d, val)
            elif name == "FILE_FORMAT_INFO":
                enc = u16(d, val+4); comp = u16(d, val+6)
                print(f"      encryption={enc}({ENCRYPT.get(enc,'?')}) "
                      f"compression={comp}({COMPRESS.get(comp,'?')})")
            elif name == "ORIGINAL_PE_NAME":
                print(f"      name={d[val+4:val+dsz].split(b'0')[0].rstrip(chr(0).encode())!r}")
        else:
            print(f"  [{name:28s}] @0x{val:x} size={sz*4}")
            if name == "EXECUTION_INFO":
                media, ver, bver = u32(d,val), u32(d,val+4), u32(d,val+8)
                title = u32(d, val+12)
                platform, exetype, disc, discs = d[val+16], d[val+17], d[val+18], d[val+19]
                print(f"      title_id=0x{title:08x} media_id=0x{media:08x} "
                      f"platform={platform} exe_type={exetype} disc {disc}/{discs}")

    dump_security(d, sec_off)
    print(f"  first bytes at pe_data_offset: {d[pe_off:pe_off+4].hex()}")


def dump_security(d, o):
    # u32 header_size, u32 image_size, u8 rsa_sig[256], u32 image_info_size,
    # u32 image_flags, u32 load_address, u8 section_digest[20],
    # u32 import_table_count, u8 import_digest[20], u8 media_id[16],
    # u8 aes_key[16], u32 export_table, u8 header_digest[20], u32 region,
    # u32 allowed_media, u32 page_descriptor_count
    hdr_size  = u32(d, o)
    img_size  = u32(d, o + 4)
    base      = o + 8 + 256
    img_flags = u32(d, base + 4)
    load_addr = u32(d, base + 8)
    imp_count = u32(d, base + 32)
    export_tb = u32(d, base + 32 + 4 + 20 + 16 + 16)
    region    = u32(d, base + 32 + 4 + 20 + 16 + 16 + 4 + 20)
    allowed   = u32(d, region_off := base + 32 + 4 + 20 + 16 + 16 + 4 + 20 + 4)
    npages    = u32(d, region_off + 4)
    print(f"  security_info: header_size=0x{hdr_size:x} image_size=0x{img_size:x} "
          f"load_address=0x{load_addr:08x}")
    print(f"      image_flags=0x{img_flags:08x} import_tables={imp_count} "
          f"export_table=0x{export_tb:x} page_descriptors={npages}")

def dump_imports(d, off):
    size = u32(d, off)
    str_tbl_size = u32(d, off+4)
    lib_count = u32(d, off+8)
    print(f"      string_table_size={str_tbl_size} libraries={lib_count}")
    p = off + 12
    names = []
    end = p + str_tbl_size
    raw = d[p:end]
    for chunk in raw.split(b'\0'):
        if chunk: names.append(chunk.decode('latin1'))
    print(f"      libs={names}")
    p = end
    # record: u32 size, u8 digest[20], u32 id, u32 version, u32 version_min,
    #         u16 name_index, u16 count, u32 records[count]
    for i in range(lib_count):
        if p + 40 > len(d): break
        lib_size = u32(d, p)
        ver      = u32(d, p + 28)
        ver_min  = u32(d, p + 32)
        name_idx = u16(d, p + 36)
        count    = u16(d, p + 38)
        nm = names[name_idx] if name_idx < len(names) else f"?{name_idx}"
        print(f"      lib[{i}] {nm:14s} size={lib_size} records={count} "
              f"ver={ver>>28}.{(ver>>24)&0xf}.{(ver>>8)&0xffff}.{ver&0xff}")
        recs = [u32(d, p + 40 + j*4) for j in range(min(count, 6))]
        print(f"         first records: {[hex(r) for r in recs]}")
        p += lib_size

if __name__ == '__main__':
    for a in sys.argv[1:]:
        main(a); print()
