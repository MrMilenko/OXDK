// cxex: build a XEX2 from a raw Xenon image, the way imagexex does.
//
// Takes an image laid out by RVA (see docs/XEX2.md) plus a manifest describing the
// imports, and writes an unencrypted, basic-compressed XEX2. That combination is what
// the XDK's own on-console tools ship as, and it is what an RGH/JTAG console will load.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// XEX headers are big-endian throughout.
void put32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void pad(std::vector<uint8_t> &v, size_t to) {
    while (v.size() < to) v.push_back(0);
}

// The PE inside a XEX is an ordinary little-endian PE32, so it needs its own writers.
void set32(std::vector<uint8_t> &v, size_t at, uint32_t x) {
    v[at + 0] = uint8_t(x);
    v[at + 1] = uint8_t(x >> 8);
    v[at + 2] = uint8_t(x >> 16);
    v[at + 3] = uint8_t(x >> 24);
}

void set16(std::vector<uint8_t> &v, size_t at, uint16_t x) {
    v[at + 0] = uint8_t(x);
    v[at + 1] = uint8_t(x >> 8);
}

// Minimal SHA1. The loader hashes the XEX header region, so cxex has to be able to
// compute it; pulling in a crypto library for one hash is not worth it.
struct Sha1 {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint8_t buf[64];
    size_t buflen = 0;
    uint64_t total = 0;

    static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

    void block(const uint8_t *p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) |
                   (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }

    void update(const uint8_t *p, size_t n) {
        total += n;
        while (n) {
            size_t take = 64 - buflen;
            if (take > n) take = n;
            memcpy(buf + buflen, p, take);
            buflen += take; p += take; n -= take;
            if (buflen == 64) { block(buf); buflen = 0; }
        }
    }

    void final(uint8_t out[20]) {
        uint64_t bits = total * 8;
        uint8_t pad0 = 0x80;
        update(&pad0, 1);
        uint8_t z = 0;
        while (buflen != 56) update(&z, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - i * 8));
        total -= 8;                       // the length field is not part of the message
        update(len, 8);
        for (int i = 0; i < 5; i++) {
            out[i*4+0] = uint8_t(h[i] >> 24); out[i*4+1] = uint8_t(h[i] >> 16);
            out[i*4+2] = uint8_t(h[i] >> 8);  out[i*4+3] = uint8_t(h[i]);
        }
    }
};

// The kernel's XexpVerifyXexHeaders hashes the header region in two pieces, tail first:
//
//   SHA1( file[sec + 0x17C .. pe_data_offset) || file[0 .. sec + 8) )
//
// compared against the 20 bytes at sec + 0x164. The skipped window [sec+8, sec+0x17C) is
// 0x174 bytes long, which is exactly the image_info_size constant: that region is covered
// by the RSA signature instead, which is why zeroing the signature still boots on RGH but
// touching any other header byte does not. Must run after the header region is final.
void seal_header(std::vector<uint8_t> &out, uint32_t sec_off, uint32_t pe_data_offset) {
    Sha1 s;
    s.update(out.data() + sec_off + 0x17C, pe_data_offset - (sec_off + 0x17C));
    s.update(out.data(), sec_off + 8);
    uint8_t dg[20];
    s.final(dg);
    memcpy(out.data() + sec_off + 0x164, dg, 20);
}

const uint32_t KEY_FILE_FORMAT_INFO = 0x000003FF;
const uint32_t KEY_ENTRY_POINT      = 0x00010100;
const uint32_t KEY_IMAGE_BASE       = 0x00010201;
const uint32_t KEY_IMPORT_LIBRARIES = 0x000103FF;
const uint32_t KEY_CHECKSUM_TIMESTAMP = 0x00018002;
const uint32_t KEY_ORIGINAL_PE_NAME = 0x000183FF;
const uint32_t KEY_STATIC_LIBRARIES = 0x000200FF;
const uint32_t KEY_TLS_INFO         = 0x00020104;
const uint32_t KEY_DEFAULT_STACK    = 0x00020200;
const uint32_t KEY_LAN_KEY          = 0x00040404;
const uint32_t KEY_SYSTEM_FLAGS     = 0x00030000;
const uint32_t KEY_EXECUTION_INFO   = 0x00040006;

// One imported symbol. Code imports carry a thunk address as well as a slot; data
// imports have no thunk, which is what thunk == 0 means here.
struct Import {
    uint32_t ordinal;
    uint32_t slot;
    uint32_t thunk;
};

struct Library {
    std::string name;
    uint32_t version;
    uint32_t version_min;
    // A per-library constant that imagexex stamps in; it does not appear to be derived
    // from anything in the image. Known values are in docs/XEX2.md.
    uint32_t id;
    std::vector<Import> imports;
};

struct Section {
    std::string name;
    uint32_t rva;
    uint32_t vsize;
    uint32_t chars;
};

struct Config {
    std::string image_path;
    std::string out_path;
    std::string pe_name = "default.exe";
    std::vector<Section> sections;
    uint32_t base = 0x82000000;
    uint32_t entry = 0;
    uint32_t stack = 0x40000;
    uint32_t title_id = 0;
    uint32_t system_flags = 0x00000001;
    std::vector<Library> libs;
};

std::vector<uint8_t> read_file(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "cxex: cannot open %s\n", path.c_str()); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(size_t(n) < 0 ? 0 : size_t(n));
    if (n > 0 && fread(d.data(), 1, size_t(n), f) != size_t(n)) {
        fprintf(stderr, "cxex: short read on %s\n", path.c_str());
        exit(1);
    }
    fclose(f);
    return d;
}

uint32_t parse_version(const std::string &s) {
    // "2.0.21256.0" packs as major:4 minor:4 build:16 qfe:8
    unsigned a = 0, b = 0, c = 0, d = 0;
    sscanf(s.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
    return ((a & 0xF) << 28) | ((b & 0xF) << 24) | ((c & 0xFFFF) << 8) | (d & 0xFF);
}

// Manifest lines, one directive per line:
//   base 0x82000000
//   entry 0x82001000
//   stack 0x40000
//   titleid 0xFFFE07D1
//   pename default.exe
//   library xboxkrnl.exe 2.0.21256.0
//   import <ordinal> <slot> [thunk]
void parse_manifest(const std::string &path, Config &cfg) {
    FILE *f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "cxex: cannot open manifest %s\n", path.c_str()); exit(1); }
    char line[512];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        char key[64] = {0};
        if (sscanf(p, "%63s", key) != 1) continue;
        char *rest = p + strlen(key);

        if (!strcmp(key, "base"))        cfg.base = uint32_t(strtoul(rest, nullptr, 0));
        else if (!strcmp(key, "entry"))  cfg.entry = uint32_t(strtoul(rest, nullptr, 0));
        else if (!strcmp(key, "stack"))  cfg.stack = uint32_t(strtoul(rest, nullptr, 0));
        else if (!strcmp(key, "titleid")) cfg.title_id = uint32_t(strtoul(rest, nullptr, 0));
        else if (!strcmp(key, "sysflags")) cfg.system_flags = uint32_t(strtoul(rest, nullptr, 0));
        else if (!strcmp(key, "pename")) {
            char nm[256] = {0};
            sscanf(rest, "%255s", nm);
            cfg.pe_name = nm;
        } else if (!strcmp(key, "section")) {
            char nm[16] = {0};
            unsigned long rva = 0, vsize = 0, chars = 0;
            if (sscanf(rest, "%15s %lx %lx %lx", nm, &rva, &vsize, &chars) != 4) {
                fprintf(stderr, "cxex: %s:%d: bad section line\n", path.c_str(), lineno);
                exit(1);
            }
            cfg.sections.push_back({nm, uint32_t(rva), uint32_t(vsize), uint32_t(chars)});
        } else if (!strcmp(key, "library")) {
            char nm[256] = {0}, ver[64] = {0}, vmin[64] = {0}, ids[64] = {0};
            int n = sscanf(rest, "%255s %63s %63s %63s", nm, ver, vmin, ids);
            if (n < 1) {
                fprintf(stderr, "cxex: %s:%d: bad library line\n", path.c_str(), lineno);
                exit(1);
            }
            Library lib;
            lib.name = nm;
            lib.version = ver[0] ? parse_version(ver) : 0;
            lib.version_min = vmin[0] ? parse_version(vmin) : lib.version;
            lib.id = ids[0] ? uint32_t(strtoul(ids, nullptr, 0)) : 0;
            cfg.libs.push_back(lib);
        } else if (!strcmp(key, "import")) {
            if (cfg.libs.empty()) {
                fprintf(stderr, "cxex: %s:%d: import before any library\n", path.c_str(), lineno);
                exit(1);
            }
            unsigned long ord = 0, slot = 0, thunk = 0;
            int n = sscanf(rest, "%lu %lx %lx", &ord, &slot, &thunk);
            if (n < 2) {
                fprintf(stderr, "cxex: %s:%d: bad import line\n", path.c_str(), lineno);
                exit(1);
            }
            Import im;
            im.ordinal = uint32_t(ord);
            im.slot = uint32_t(slot);
            im.thunk = (n >= 3) ? uint32_t(thunk) : 0;
            cfg.libs.back().imports.push_back(im);
        } else {
            fprintf(stderr, "cxex: %s:%d: unknown directive '%s'\n", path.c_str(), lineno, key);
            exit(1);
        }
    }
    fclose(f);
}

// IMPORT_LIBRARIES blob: a size, a string table of library names, then one record per
// library listing every address in the image the loader has to patch.
std::vector<uint8_t> build_imports(const Config &cfg, uint8_t chain_head[20]) {
    std::vector<uint8_t> strtab;
    for (const auto &lib : cfg.libs) {
        strtab.insert(strtab.end(), lib.name.begin(), lib.name.end());
        strtab.push_back(0);
    }
    while (strtab.size() % 4) strtab.push_back(0);

    std::vector<uint8_t> records;
    for (size_t i = 0; i < cfg.libs.size(); i++) {
        const Library &lib = cfg.libs[i];
        uint32_t count = 0;
        for (const auto &im : lib.imports) count += im.thunk ? 2 : 1;
        uint32_t rec_size = 40 + count * 4;

        put32(records, rec_size);
        for (int k = 0; k < 20; k++) records.push_back(0);  // next_import_digest
        put32(records, lib.id);
        put32(records, lib.version);
        put32(records, lib.version_min);
        put16(records, uint16_t(i));                         // name_index
        put16(records, uint16_t(count));
        for (const auto &im : lib.imports) {
            put32(records, im.slot);
            if (im.thunk) put32(records, im.thunk);
        }
    }

    // next_import_digest chains the library records backwards: each record stores the
    // SHA1 of the record that follows it, and the last stores zeros. Computed over
    // rec[0x04 .. record_size), i.e. from the digest field to the end of the record.
    {
        std::vector<size_t> starts;
        size_t off = 0;
        while (off + 40 <= records.size()) {
            starts.push_back(off);
            uint32_t rs = (uint32_t(records[off]) << 24) | (uint32_t(records[off+1]) << 16) |
                          (uint32_t(records[off+2]) << 8) | uint32_t(records[off+3]);
            if (!rs) break;
            off += rs;
        }
        uint8_t prev[20] = {0};
        for (size_t i = starts.size(); i-- > 0; ) {
            size_t o = starts[i];
            uint32_t rs = (uint32_t(records[o]) << 24) | (uint32_t(records[o+1]) << 16) |
                          (uint32_t(records[o+2]) << 8) | uint32_t(records[o+3]);
            memcpy(&records[o + 4], prev, 20);
            Sha1 sh;
            sh.update(&records[o + 4], rs - 4);
            sh.final(prev);
        }
        // The head of the chain, the SHA1 of the first record, is what goes in
        // security_info.import_table_digest.
        memcpy(chain_head, prev, 20);
    }

    std::vector<uint8_t> out;
    uint32_t total = 12 + uint32_t(strtab.size()) + uint32_t(records.size());
    put32(out, total);
    put32(out, uint32_t(strtab.size()));
    put32(out, uint32_t(cfg.libs.size()));
    out.insert(out.end(), strtab.begin(), strtab.end());
    out.insert(out.end(), records.begin(), records.end());
    return out;
}

// The loader expects the slot to hold (library_index << 16) | ordinal, and each code
// import to have a 16-byte thunk of two tagged placeholder words plus mtctr/bctr. The
// placeholders are rewritten by the loader into the address load.
void patch_image(std::vector<uint8_t> &img, const Config &cfg) {
    auto store_be = [&](uint32_t addr, uint32_t val) {
        uint32_t off = addr - cfg.base;
        if (off + 4 > img.size()) {
            fprintf(stderr, "cxex: address 0x%08x is outside the image\n", addr);
            exit(1);
        }
        img[off + 0] = uint8_t(val >> 24);
        img[off + 1] = uint8_t(val >> 16);
        img[off + 2] = uint8_t(val >> 8);
        img[off + 3] = uint8_t(val);
    };

    for (size_t i = 0; i < cfg.libs.size(); i++) {
        uint32_t tag = (uint32_t(i) << 16);
        for (const auto &im : cfg.libs[i].imports) {
            store_be(im.slot, tag | im.ordinal);
            if (im.thunk) {
                store_be(im.thunk + 0, 0x01000000u | tag | im.ordinal);
                store_be(im.thunk + 4, 0x02000000u | tag | im.ordinal);
                store_be(im.thunk + 8, 0x7d6903a6u);   // mtctr r11
                store_be(im.thunk + 12, 0x4e800420u);  // bctr
            }
        }
    }
}

// Write PE32 headers into the first SizeOfHeaders bytes of the image. Field values
// mirror what the Xenon linker emits for demofixer.exe: subsystem 14 (Xbox), section
// alignment 0x1000, file alignment 0x200.
//
// PointerToRawData is set equal to the RVA here. The XEX loader maps from the memory
// image and ignores it, and keeping the two equal means anything that unpacks the XEX
// and walks the section table reads the bytes it expects.
const uint32_t kSizeOfHeaders = 0x400;

void synth_pe(std::vector<uint8_t> &img, const Config &cfg, uint32_t image_size) {
    if (img.size() < kSizeOfHeaders) {
        fprintf(stderr, "cxex: image is smaller than the PE headers\n");
        exit(1);
    }
    for (const auto &s : cfg.sections) {
        if (s.rva < kSizeOfHeaders) {
            fprintf(stderr, "cxex: section %s at 0x%x overlaps the headers\n",
                    s.name.c_str(), s.rva);
            exit(1);
        }
    }

    const size_t pe = 0x80;
    const size_t opt = pe + 24;
    const size_t sectab = opt + 224;
    if (sectab + cfg.sections.size() * 40 > kSizeOfHeaders) {
        fprintf(stderr, "cxex: %zu sections do not fit in the header area\n",
                cfg.sections.size());
        exit(1);
    }

    img[0] = 'M'; img[1] = 'Z';
    set32(img, 0x3c, uint32_t(pe));

    img[pe] = 'P'; img[pe + 1] = 'E';
    set16(img, pe + 4, 0x01F2);                            // IMAGE_FILE_MACHINE_POWERPCBE
    set16(img, pe + 6, uint16_t(cfg.sections.size()));
    set16(img, pe + 20, 224);                              // SizeOfOptionalHeader
    set16(img, pe + 22, 0x0102);                           // EXECUTABLE_IMAGE | 32BIT_MACHINE

    uint32_t code_size = 0, data_size = 0, base_code = 0;
    for (const auto &s : cfg.sections) {
        if (s.chars & 0x20000000u) {                       // MEM_EXECUTE
            code_size += s.vsize;
            if (!base_code || s.rva < base_code) base_code = s.rva;
        } else {
            data_size += s.vsize;
        }
    }

    set16(img, opt + 0, 0x010b);                           // PE32
    img[opt + 2] = 10;                                     // linker version, cosmetic
    set32(img, opt + 4, code_size);
    set32(img, opt + 8, data_size);
    set32(img, opt + 16, cfg.entry - cfg.base);            // AddressOfEntryPoint
    set32(img, opt + 20, base_code);
    set32(img, opt + 24, kSizeOfHeaders);                  // BaseOfData
    set32(img, opt + 28, cfg.base);                        // ImageBase
    set32(img, opt + 32, 0x1000);                          // SectionAlignment
    set32(img, opt + 36, 0x200);                           // FileAlignment
    set16(img, opt + 40, 5);                               // OS major
    set16(img, opt + 44, 5);                               // Image major
    set16(img, opt + 48, 1);                               // Subsystem major
    set32(img, opt + 56, image_size);                      // SizeOfImage
    set32(img, opt + 60, kSizeOfHeaders);                  // SizeOfHeaders
    set16(img, opt + 68, 14);                              // IMAGE_SUBSYSTEM_XBOX
    set32(img, opt + 72, cfg.stack);                       // SizeOfStackReserve
    set32(img, opt + 76, cfg.stack);                       // SizeOfStackCommit
    set32(img, opt + 80, 0x100000);                        // SizeOfHeapReserve
    set32(img, opt + 84, 0x1000);                          // SizeOfHeapCommit
    set32(img, opt + 92, 16);                              // NumberOfRvaAndSizes

    // Data directory 3 is the exception table. The loader registers it at load time,
    // and every module the console reports has a non-empty one.
    for (const auto &s : cfg.sections) {
        if (s.name == ".pdata") {
            set32(img, opt + 96 + 3 * 8, s.rva);
            set32(img, opt + 96 + 3 * 8 + 4, s.vsize);
        }
    }

    for (size_t i = 0; i < cfg.sections.size(); i++) {
        const Section &s = cfg.sections[i];
        size_t e = sectab + i * 40;
        for (size_t k = 0; k < 8 && k < s.name.size(); k++) img[e + k] = uint8_t(s.name[k]);
        set32(img, e + 8, s.vsize);
        set32(img, e + 12, s.rva);
        set32(img, e + 16, (s.vsize + 0x1FF) & ~0x1FFu);   // SizeOfRawData
        set32(img, e + 20, s.rva);                         // PointerToRawData
        set32(img, e + 36, s.chars);
    }
}

// Page descriptors tell the loader the protection of every page in the image, as
// (page_count << 4) | info run-length pairs. info is 1 for execute, 2 for read/write
// and 3 for read-only; a descriptor with info 0 describes pages with no access at all
// and the loader will refuse the image.
//
// demofixer.xex decodes as 4 read-only pages (headers, .rdata, .pdata), 7 executable
// (.text) and 1 read-write (.data), summing to its 12 pages.
const uint8_t PAGE_EXECUTE = 1, PAGE_WRITE = 2, PAGE_READONLY = 3;

std::vector<uint32_t> page_descriptors(const std::vector<uint8_t> &img,
                                       const Config &cfg, uint32_t image_size,
                                       uint32_t page_size) {
    uint32_t npages = image_size / page_size;
    std::vector<uint8_t> prot(npages, PAGE_READONLY);  // headers and anything unclaimed

    auto apply = [&](uint32_t rva, uint32_t vsize, uint32_t chars) {
        if (!vsize) return;
        uint8_t p = (chars & 0x20000000u) ? PAGE_EXECUTE
                  : (chars & 0x80000000u) ? PAGE_WRITE
                                          : PAGE_READONLY;
        uint32_t first = rva / page_size;
        uint32_t last = (rva + vsize - 1) / page_size;
        for (uint32_t i = first; i <= last && i < npages; i++) prot[i] = p;
    };

    if (!cfg.sections.empty()) {
        for (const auto &s : cfg.sections) apply(s.rva, s.vsize, s.chars);
    } else {
        // The image carries its own PE headers, so read the section table back out.
        if (img.size() > 0x40 && img[0] == 'M' && img[1] == 'Z') {
            auto get32 = [&](size_t o) {
                return uint32_t(img[o]) | (uint32_t(img[o+1]) << 8) |
                       (uint32_t(img[o+2]) << 16) | (uint32_t(img[o+3]) << 24);
            };
            auto get16 = [&](size_t o) {
                return uint16_t(uint32_t(img[o]) | (uint32_t(img[o+1]) << 8));
            };
            size_t pe = get32(0x3c);
            if (pe + 24 < img.size() && img[pe] == 'P' && img[pe+1] == 'E') {
                uint16_t nsec = get16(pe + 6);
                uint16_t ohsz = get16(pe + 20);
                size_t tab = pe + 24 + ohsz;
                for (uint16_t i = 0; i < nsec && tab + i * 40 + 40 <= img.size(); i++) {
                    size_t e = tab + i * 40;
                    apply(get32(e + 12), get32(e + 8), get32(e + 36));
                }
            }
        }
    }

    // Run-length encode, but never let a descriptor cross a 64K boundary. Every XEX
    // Microsoft ships obeys this: across 48 real files (dash, xam, Xbdm, PlayReady,
    // AvatarPreviewerXbox with 1093 descriptors, ...) no descriptor spans more than
    // 0x10000 bytes and none straddles a 64K granule, and 5247 merges of adjacent
    // same-protection pages are declined to keep it that way. finished.xex splits one
    // unbroken read-only stretch into 3/16/16/11 pages, breaking on each boundary.
    // For a 64K image the rule degenerates to one descriptor per page, which is what
    // Aurora (191 for 191 pages) and XellLaunch (3 for 3) do. Getting this wrong is
    // fatal: b_XapiInitProcess.xex merged rva 0-0x1FFFF into one descriptor and the
    // loader refused it silently.
    const uint32_t pages_per_granule = 0x10000 / page_size;
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < npages; ) {
        uint32_t limit = ((i / pages_per_granule) + 1) * pages_per_granule;
        if (limit > npages) limit = npages;
        uint32_t j = i;
        while (j < limit && prot[j] == prot[i]) j++;
        out.push_back(((j - i) << 4) | prot[i]);
        i = j;
    }
    return out;
}

int build(Config &cfg) {
    std::vector<uint8_t> img = read_file(cfg.image_path);

    // Page size follows the base address: images below 0x90000000 are mapped with 64K
    // pages, at or above with 4K. Verified against three real files: XellLaunch at
    // 0x82000000 has image_size 0x30000 described by 3 page descriptors, while
    // DashLaunch and demofixer at 0x92000000 have 939 and 12 for 0x3ab000 and 0xc000.
    // Getting this wrong makes the loader reject the image outright.
    const uint32_t page_size = (cfg.base < 0x90000000u) ? 0x10000u : 0x1000u;

    // The loader maps whole pages; the image size it is told must cover the payload.
    while (img.size() % page_size) img.push_back(0);
    uint32_t image_size = uint32_t(img.size());

    // Declaring sections means the image arrived as raw code and data with a hole where
    // the headers belong, so fill it in. An image that already carries its own PE
    // headers, such as one unpacked from an existing XEX, declares none and is left be.
    if (!cfg.sections.empty()) synth_pe(img, cfg, image_size);

    patch_image(img, cfg);

    uint8_t import_head[20] = {0};
    std::vector<uint8_t> imports = build_imports(cfg, import_head);

    // FILE_FORMAT_INFO: unencrypted, basic compression, one block, nothing zero-filled.
    std::vector<uint8_t> fmt;
    put32(fmt, 8 + 8);
    put16(fmt, 0);  // encryption: none
    put16(fmt, 1);  // compression: basic
    put32(fmt, image_size);
    put32(fmt, 0);

    std::vector<uint8_t> exec;
    put32(exec, 0);             // media_id
    put32(exec, 0);             // version
    put32(exec, 0);             // base_version
    put32(exec, cfg.title_id);
    exec.push_back(0);          // platform
    exec.push_back(0);          // executable_type
    exec.push_back(0);          // disc_number
    exec.push_back(0);          // disc_count
    put32(exec, 0);             // savegame_id

    // Every real XEX carries these; the loader appears to expect them. TLS_INFO with
    // 64 slots and no data is what the XDK's own tools emit for a title using no TLS.
    std::vector<uint8_t> tls;
    put32(tls, 64);   // slot_count
    put32(tls, 0);    // raw_data_address
    put32(tls, 0);    // data_size
    put32(tls, 0);    // raw_data_size

    std::vector<uint8_t> checksum;
    put32(checksum, 0);   // checksum, not validated on RGH
    put32(checksum, 0);   // timestamp

    std::vector<uint8_t> lankey(16, 0);

    // Entries are an 8-byte name then major/minor/build as u16, an approval byte and a
    // qfe byte. Every real XEX declares what it was built against; we declare the
    // kernel the ordinals came from.
    std::vector<uint8_t> statlibs;
    {
        struct Lib { const char *name; uint16_t maj, min, build; uint8_t approval, qfe; };
        const Lib libs[] = {
            {"XBOXKRNL", 2, 0, 21256, 0x40, 0},
            {"LINK", 10, 0, 11886, 0x44, 0},
        };
        put32(statlibs, uint32_t(4 + sizeof libs / sizeof libs[0] * 16));
        for (const auto &l : libs) {
            char nm[8] = {0};
            strncpy(nm, l.name, 8);
            for (int k = 0; k < 8; k++) statlibs.push_back(uint8_t(nm[k]));
            put16(statlibs, l.maj);
            put16(statlibs, l.min);
            put16(statlibs, l.build);
            statlibs.push_back(l.approval);
            statlibs.push_back(l.qfe);
        }
    }

    std::vector<uint8_t> pename;
    {
        std::string n = cfg.pe_name;
        uint32_t sz = uint32_t(4 + n.size() + 1);
        while (sz % 4) sz++;
        put32(pename, sz);
        pename.insert(pename.end(), n.begin(), n.end());
        pename.push_back(0);
        while (pename.size() < sz) pename.push_back(0);
    }

    // Optional headers that store data out of line need offsets, so lay the blobs out
    // first and record where each landed.
    struct Opt { uint32_t key, value; };
    std::vector<Opt> opts;
    std::vector<uint8_t> blobs;

    // EXECUTION_INFO declares a title id, media id and disc numbering. Aurora, which is
    // an ordinary application, omits it entirely; declaring one appears to invite media
    // and licence checks a homebrew app cannot satisfy. Emit it only when the manifest
    // actually asks for a title id.
    const bool want_exec = cfg.title_id != 0;
    const uint32_t kOptCount = 11 + (want_exec ? 1 : 0);
    uint32_t dir_end = 24 + kOptCount * 8;
    // imagexex reserves 128 zero bytes between the end of the optional header directory
    // and the security info. Every real file does it: demofixer, XellLaunch and Aurora
    // all place security_info_offset exactly 128 bytes past their directory.
    uint32_t sec_off = dir_end + 128;
    // Fixed part is 0x184, then 24 bytes per page descriptor. Confirmed against real
    // files: demofixer has 3 descriptors and header_size 0x1cc = 0x184 + 3 * 24.
    std::vector<uint32_t> pages = page_descriptors(img, cfg, image_size, page_size);
    const uint32_t kSecuritySize = 0x184 + uint32_t(pages.size()) * 24;

    // Content hashes, chained backwards over the page descriptor runs:
    //   H[n] = 20 zero bytes
    //   H[i] = SHA1( image bytes of run i || be32(descriptor word i) || H[i+1] )
    // Each descriptor stores H[i+1], the hash of the run that follows it, and the head
    // H[0] goes in section_digest. The hash covers the image as mapped, so the implicit
    // zero fill out to image_size counts.
    std::vector<std::vector<uint8_t>> page_digests(pages.size(),
                                                   std::vector<uint8_t>(20, 0));
    uint8_t chain[20] = {0};
    {
        std::vector<uint32_t> starts(pages.size());
        uint32_t cur = 0;
        for (size_t i = 0; i < pages.size(); i++) {
            starts[i] = cur;
            cur += (pages[i] >> 4) * page_size;
        }
        for (size_t i = pages.size(); i-- > 0; ) {
            memcpy(page_digests[i].data(), chain, 20);
            uint32_t len = (pages[i] >> 4) * page_size;
            if (starts[i] + len > img.size()) len = uint32_t(img.size()) - starts[i];
            uint8_t be[4] = {uint8_t(pages[i] >> 24), uint8_t(pages[i] >> 16),
                             uint8_t(pages[i] >> 8), uint8_t(pages[i])};
            Sha1 sh;
            sh.update(img.data() + starts[i], len);
            sh.update(be, 4);
            sh.update(chain, 20);
            sh.final(chain);
        }
    }
    uint32_t blob_base = sec_off + kSecuritySize;

    auto add_blob = [&](uint32_t key, const std::vector<uint8_t> &data) {
        uint32_t at = blob_base + uint32_t(blobs.size());
        opts.push_back({key, at});
        blobs.insert(blobs.end(), data.begin(), data.end());
        while (blobs.size() % 4) blobs.push_back(0);
    };
    // The import table goes last, ending flush against pe_data_offset, the way imagexex
    // lays it out. Its offset is patched in once the region size is known.
    size_t import_opt_index = 0;

    // Keys must appear in ascending order; the loader binary-searches them.
    add_blob(KEY_FILE_FORMAT_INFO, fmt);
    opts.push_back({KEY_ENTRY_POINT, cfg.entry});
    opts.push_back({KEY_IMAGE_BASE, cfg.base});
    import_opt_index = opts.size();
    opts.push_back({KEY_IMPORT_LIBRARIES, 0});      // patched below
    add_blob(KEY_CHECKSUM_TIMESTAMP, checksum);
    add_blob(KEY_ORIGINAL_PE_NAME, pename);
    add_blob(KEY_STATIC_LIBRARIES, statlibs);
    add_blob(KEY_TLS_INFO, tls);
    opts.push_back({KEY_DEFAULT_STACK, cfg.stack});
    opts.push_back({KEY_SYSTEM_FLAGS, cfg.system_flags});
    if (want_exec) add_blob(KEY_EXECUTION_INFO, exec);
    add_blob(KEY_LAN_KEY, lankey);

    if (opts.size() != kOptCount) {
        fprintf(stderr, "cxex: internal error, %zu headers but %u reserved\n",
                opts.size(), kOptCount);
        return 1;
    }
    std::sort(opts.begin(), opts.end(),
              [](const Opt &a, const Opt &b) { return a.key < b.key; });

    uint32_t import_end = uint32_t(blob_base + blobs.size()) + uint32_t(imports.size());
    // The loader only enforces a 0x800 multiple, but the header is handed to the
    // hypervisor as whole 4 KiB pages (round_up(pe_data_offset, 0x1000) >> 12 of them),
    // and every XEX the XDK ships is a 0x1000 multiple. Match that.
    uint32_t pe_data_offset = (import_end + 0xFFF) & ~0xFFFu;
    if (pe_data_offset < 0x1000) pe_data_offset = 0x1000;
    if (pe_data_offset > 0x10000) {
        fprintf(stderr, "cxex: header region %u bytes, loader limit is 0x10000\n",
                pe_data_offset);
        return 1;
    }
    uint32_t import_at = pe_data_offset - uint32_t(imports.size());
    while (blob_base + blobs.size() < import_at) blobs.push_back(0);
    blobs.insert(blobs.end(), imports.begin(), imports.end());
    opts[import_opt_index].value = import_at;

    std::vector<uint8_t> out;
    out.push_back('X'); out.push_back('E'); out.push_back('X'); out.push_back('2');
    put32(out, 0x00000001);      // module_flags: title module
    put32(out, pe_data_offset);
    put32(out, 0);               // reserved
    put32(out, sec_off);
    put32(out, kOptCount);
    for (const auto &o : opts) { put32(out, o.key); put32(out, o.value); }

    // Security info. Signature and digests stay zero: an RGH/JTAG console does not
    // check them, and there is no key to sign with anyway.
    pad(out, sec_off);
    put32(out, kSecuritySize);
    put32(out, image_size);
    for (int i = 0; i < 256; i++) out.push_back(0);   // rsa_signature
    // Fixed 0x174, not derived from the header size. Aurora has 191 page descriptors
    // and XellLaunch has 3, and both store 0x174 here.
    put32(out, 0x174);                                // image_info_size
    // Bit 0x10000000 declares 4K page granularity. Set in every XDK module (all mapped
    // with 4K pages) and clear in XellLaunch and Aurora (both 64K), so derive it from the
    // page size rather than hardcoding, or a 0x92000000-based build describes 4K pages
    // under a flag claiming 64K.
    put32(out, page_size == 0x1000 ? 0x10000000u : 0u);   // image_flags
    put32(out, cfg.base);                             // load_address
    out.insert(out.end(), chain, chain + 20);          // section_digest = H[0]
    put32(out, uint32_t(cfg.libs.size()));            // import_table_count
    out.insert(out.end(), import_head, import_head + 20);  // import_table_digest
    for (int i = 0; i < 16; i++) out.push_back(0);    // media_id
    // imagexex stores this constant in every unencrypted image; it is AES-128 of a zero
    // block under a zero key. Encrypted images carry a real wrapped session key instead.
    static const uint8_t kNullKey[16] = {0x66,0xe9,0x4b,0xd4,0xef,0x8a,0x2c,0x3b,
                                         0x88,0x4c,0xfa,0x59,0xca,0x34,0x2b,0x2e};
    out.insert(out.end(), kNullKey, kNullKey + 16);    // aes_key
    put32(out, 0);                                    // export_table
    for (int i = 0; i < 20; i++) out.push_back(0);    // header_digest
    put32(out, 0xFFFFFFFFu);                          // region: all
    put32(out, 0xFFFFFFFFu);                          // allowed_media_types
    put32(out, uint32_t(pages.size()));
    for (size_t i = 0; i < pages.size(); i++) {
        put32(out, pages[i]);
        out.insert(out.end(), page_digests[i].begin(), page_digests[i].end());
    }
    if (out.size() != sec_off + kSecuritySize) {
        fprintf(stderr, "cxex: internal error, security info is %zu bytes, expected %u\n",
                out.size() - sec_off, kSecuritySize);
        return 1;
    }
    pad(out, blob_base);

    out.insert(out.end(), blobs.begin(), blobs.end());
    pad(out, pe_data_offset);
    seal_header(out, sec_off, pe_data_offset);
    out.insert(out.end(), img.begin(), img.end());

    FILE *f = fopen(cfg.out_path.c_str(), "wb");
    if (!f) { fprintf(stderr, "cxex: cannot write %s\n", cfg.out_path.c_str()); return 1; }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);

    printf("cxex: %s\n", cfg.out_path.c_str());
    printf("  base 0x%08x  entry 0x%08x  image 0x%x bytes\n", cfg.base, cfg.entry, image_size);
    printf("  %zu libraries, payload at 0x%x, total %zu bytes\n",
           cfg.libs.size(), pe_data_offset, out.size());
    printf("  %zu page descriptors at %uK granularity\n", pages.size(), page_size / 1024);
    return 0;
}

uint32_t get32(const std::vector<uint8_t> &d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o+1]) << 16) |
           (uint32_t(d[o+2]) << 8) | uint32_t(d[o+3]);
}

// Rewrap: take a XEX that is known to boot, keep its payload and every optional header
// blob byte for byte, and regenerate only the things cxex normally authors: the file
// header, the optional header directory and its offsets, and the security info layout.
//
// This is a bisect, not a build. If a rewrapped known-good XEX still boots, everything
// cxex writes is acceptable to the loader and the fault lies in the image we generate.
// If it stops booting, the fault is in cxex, and the field can be narrowed from there.
int rewrap(const std::string &src_path, const std::string &out_path,
           bool preserve_offsets) {
    std::vector<uint8_t> src = read_file(src_path);
    if (src.size() < 24 || src[0] != 'X' || src[1] != 'E' || src[2] != 'X' || src[3] != '2') {
        fprintf(stderr, "cxex: %s is not a XEX2\n", src_path.c_str());
        return 1;
    }
    uint32_t module_flags = get32(src, 4);
    uint32_t src_pe_off   = get32(src, 8);
    uint32_t src_sec_off  = get32(src, 16);
    uint32_t count        = get32(src, 20);

    struct Hdr { uint32_t key; bool is_inline; uint32_t value; uint32_t src_value;
                 std::vector<uint8_t> data; };
    std::vector<Hdr> hdrs;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key = get32(src, 24 + i * 8);
        uint32_t val = get32(src, 24 + i * 8 + 4);
        uint32_t sz = key & 0xFF;
        Hdr h;
        h.key = key;
        h.src_value = val;
        if (sz == 0x00 || sz == 0x01) {
            h.is_inline = true;
            h.value = val;
        } else {
            h.is_inline = false;
            h.value = 0;
            uint32_t len = (sz == 0xFF) ? get32(src, val) : sz * 4;
            if (val + len > src.size()) {
                fprintf(stderr, "cxex: header 0x%08x runs past end of file\n", key);
                return 1;
            }
            h.data.assign(src.begin() + val, src.begin() + val + len);
        }
        hdrs.push_back(h);
    }

    // Values the loader must still agree with, lifted from the source.
    const size_t sb = src_sec_off + 8 + 256;
    uint32_t image_size   = get32(src, src_sec_off + 4);
    uint32_t image_flags  = get32(src, sb + 4);
    uint32_t load_address = get32(src, sb + 8);
    uint32_t import_count = get32(src, sb + 32);
    size_t   after        = sb + 32 + 4 + 20;          // media_id starts here
    uint32_t export_table = get32(src, after + 32);
    uint32_t region       = get32(src, after + 32 + 4 + 20);
    uint32_t allowed      = get32(src, after + 32 + 4 + 20 + 4);
    uint32_t npages       = get32(src, after + 32 + 4 + 20 + 8);
    size_t   pd_off       = after + 32 + 4 + 20 + 12;

    std::vector<uint8_t> sec_digest(src.begin() + sb + 12, src.begin() + sb + 12 + 20);
    std::vector<uint8_t> imp_digest(src.begin() + sb + 36, src.begin() + sb + 36 + 20);
    std::vector<uint8_t> hdr_digest(src.begin() + after + 36, src.begin() + after + 36 + 20);
    std::vector<uint8_t> media(src.begin() + after, src.begin() + after + 16);
    std::vector<uint8_t> aeskey(src.begin() + after + 16, src.begin() + after + 32);
    std::vector<uint8_t> sig(src.begin() + src_sec_off + 8, src.begin() + src_sec_off + 8 + 256);

    std::vector<uint32_t> pages;
    std::vector<std::vector<uint8_t>> page_digests;
    for (uint32_t i = 0; i < npages; i++) {
        pages.push_back(get32(src, pd_off + i * 24));
        page_digests.emplace_back(src.begin() + pd_off + i * 24 + 4,
                                  src.begin() + pd_off + i * 24 + 24);
    }

    uint32_t dir_end = 24 + count * 8;
    uint32_t sec_off = dir_end + 128;   // see the note in build()
    uint32_t sec_size = 0x184 + npages * 24;
    uint32_t blob_base = sec_off + sec_size;

    // Two layouts, so placement can be bisected against a file known to boot.
    // preserve_offsets keeps every blob exactly where the source had it, which should
    // reproduce the source byte for byte; otherwise blobs are packed in key order the
    // way cxex normally emits them.
    std::sort(hdrs.begin(), hdrs.end(),
              [](const Hdr &a, const Hdr &b) { return a.key < b.key; });

    uint32_t pe_data_offset;
    std::vector<uint8_t> hdrblobs;    // everything from blob_base to pe_data_offset
    if (preserve_offsets) {
        pe_data_offset = src_pe_off;
        hdrblobs.assign(src.begin() + blob_base, src.begin() + pe_data_offset);
        // values already hold the source offsets
        for (auto &h : hdrs) if (!h.is_inline) h.value = h.src_value;
    } else {
        // imagexex always puts IMPORT_LIBRARIES last, ending exactly at pe_data_offset
        // (demofixer: 0xde4 + 540 = 0x1000). Each import library record carries a
        // next_import_digest, so anything placed after the import table would fall
        // inside whatever that digest covers. Keep the same arrangement.
        uint32_t import_len = 0;
        for (auto &h : hdrs)
            if (h.key == KEY_IMPORT_LIBRARIES) import_len = uint32_t(h.data.size());

        for (auto &h : hdrs) {
            if (h.is_inline || h.key == KEY_IMPORT_LIBRARIES) continue;
            h.value = blob_base + uint32_t(hdrblobs.size());
            hdrblobs.insert(hdrblobs.end(), h.data.begin(), h.data.end());
            while (hdrblobs.size() % 4) hdrblobs.push_back(0);
        }
        uint32_t end = uint32_t(blob_base + hdrblobs.size()) + import_len;
        pe_data_offset = (end + 0xFFF) & ~0xFFFu;
        if (pe_data_offset < 0x1000) pe_data_offset = 0x1000;
        uint32_t import_at = pe_data_offset - import_len;
        while (blob_base + hdrblobs.size() < import_at) hdrblobs.push_back(0);
        for (auto &h : hdrs) {
            if (h.key != KEY_IMPORT_LIBRARIES) continue;
            h.value = import_at;
            hdrblobs.insert(hdrblobs.end(), h.data.begin(), h.data.end());
        }
    }
    std::vector<uint8_t> &blobs = hdrblobs;

    std::vector<uint8_t> out;
    out.push_back('X'); out.push_back('E'); out.push_back('X'); out.push_back('2');
    put32(out, module_flags);
    put32(out, pe_data_offset);
    put32(out, 0);
    put32(out, sec_off);
    put32(out, count);
    for (const auto &h : hdrs) {
        put32(out, h.key);
        put32(out, h.is_inline ? h.value : h.value);
    }

    pad(out, sec_off);
    put32(out, sec_size);
    put32(out, image_size);
    out.insert(out.end(), sig.begin(), sig.end());
    put32(out, 0x174);
    put32(out, image_flags);
    put32(out, load_address);
    out.insert(out.end(), sec_digest.begin(), sec_digest.end());
    put32(out, import_count);
    out.insert(out.end(), imp_digest.begin(), imp_digest.end());
    out.insert(out.end(), media.begin(), media.end());
    out.insert(out.end(), aeskey.begin(), aeskey.end());
    put32(out, export_table);
    out.insert(out.end(), hdr_digest.begin(), hdr_digest.end());
    put32(out, region);
    put32(out, allowed);
    put32(out, npages);
    for (uint32_t i = 0; i < npages; i++) {
        put32(out, pages[i]);
        out.insert(out.end(), page_digests[i].begin(), page_digests[i].end());
    }
    if (out.size() != sec_off + sec_size) {
        fprintf(stderr, "cxex: rewrap security info is %zu bytes, expected %u\n",
                out.size() - sec_off, sec_size);
        return 1;
    }

    pad(out, blob_base);
    out.insert(out.end(), blobs.begin(), blobs.end());
    pad(out, pe_data_offset);
    seal_header(out, sec_off, pe_data_offset);
    out.insert(out.end(), src.begin() + src_pe_off, src.end());

    FILE *f = fopen(out_path.c_str(), "wb");
    if (!f) { fprintf(stderr, "cxex: cannot write %s\n", out_path.c_str()); return 1; }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);

    printf("cxex: rewrapped %s -> %s\n", src_path.c_str(), out_path.c_str());
    printf("  %u headers, %u page descriptors, image 0x%x\n", count, npages, image_size);
    printf("  payload 0x%x -> 0x%x, total %zu bytes (source was %zu)\n",
           src_pe_off, pe_data_offset, out.size(), src.size());
    return 0;
}

void usage() {
    fprintf(stderr,
        "usage: cxex -m manifest -i image -o out.xex\n"
        "       cxex -r known-good.xex -o out.xex     rewrap bisect\n"
        "\n"
        "  -m  manifest describing base, entry, imports (see docs/XEX2.md)\n"
        "  -i  raw image laid out by RVA\n"
        "  -o  output XEX2\n");
}

}  // namespace

int main(int argc, char **argv) {
    Config cfg;
    std::string manifest, rewrap_src;
    bool preserve = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-p") preserve = true;
        else if (a == "-r" && i + 1 < argc) rewrap_src = argv[++i];
        else if (a == "-m" && i + 1 < argc) manifest = argv[++i];
        else if (a == "-i" && i + 1 < argc) cfg.image_path = argv[++i];
        else if (a == "-o" && i + 1 < argc) cfg.out_path = argv[++i];
        else { usage(); return 1; }
    }
    if (!rewrap_src.empty()) {
        if (cfg.out_path.empty()) { usage(); return 1; }
        return rewrap(rewrap_src, cfg.out_path, preserve);
    }
    if (manifest.empty() || cfg.image_path.empty() || cfg.out_path.empty()) {
        usage();
        return 1;
    }
    parse_manifest(manifest, cfg);
    if (!cfg.entry) {
        fprintf(stderr, "cxex: manifest has no entry point\n");
        return 1;
    }
    return build(cfg);
}
