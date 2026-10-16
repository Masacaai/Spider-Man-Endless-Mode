// toctool: offline asset-path checker for MSMR's asset_archive\toc.
//   toctool hash <path>...                    print asset id(s)
//   toctool check <toc> <candidates.txt>      print candidates present in toc (zone keys and all assets)
// Hash and toc format from Tkachov/ALERT (dat1lib/crc64.py, types/toc.py, types/dat1.py; GPLv3).
#include <lz4.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

static uint64_t g_table[256];

static void InitTable() {
    const uint64_t poly = 0xC96C5795D7870F42ull;   // reflected ECMA-182
    for (uint64_t i = 0; i < 256; i++) {
        uint64_t t = i;
        for (int k = 0; k < 8; k++) t = (t >> 1) ^ ((t & 1) ? poly : 0);
        g_table[i] = t;
    }
}

static std::string Normalize(std::string s) {
    for (auto& c : s) {
        if (c == '\\') c = '/';
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

static uint64_t AssetId(const std::string& path) {
    std::string s = Normalize(path);   // lowercase, forward slashes (verified against ALERT's test vector)
    uint64_t crc = 0xC96C5795D7870F42ull;
    for (unsigned char b : s) crc = (crc >> 8) ^ g_table[(crc ^ b) & 0xFF];
    return (crc >> 2) | 0x8000000000000000ull;
}

struct Toc {
    std::unordered_set<uint64_t> all, zones;
    std::vector<uint64_t> ids;                 // section 506D7B8A, in order
    std::vector<uint8_t> meta, dupes, archives; // sections 65BCF461, DCD720B5, 398ABFF0 (raw)
};

static bool LoadToc(const char* path, Toc& toc) {
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
    if (raw.size() < 8) return false;
    uint32_t magic, size;
    memcpy(&magic, &raw[0], 4);
    memcpy(&size, &raw[4], 4);
    if (magic != 0x77AF12AF) { printf("bad toc magic %08X\n", magic); return false; }
    std::vector<uint8_t> data(size);
    uLongf out = size;
    if (uncompress(data.data(), &out, raw.data() + 8, (uLong)(raw.size() - 8)) != Z_OK || out != size) {
        printf("zlib failed\n");
        return false;
    }
    uint32_t dmagic; memcpy(&dmagic, &data[0], 4);
    if (dmagic != 0x44415431) { printf("bad DAT1 magic %08X\n", dmagic); return false; }
    uint16_t nsec; memcpy(&nsec, &data[12], 2);
    for (int i = 0; i < nsec; i++) {
        uint32_t tag, off, sz;
        memcpy(&tag, &data[16 + 12 * i], 4);
        memcpy(&off, &data[16 + 12 * i + 4], 4);
        memcpy(&sz, &data[16 + 12 * i + 8], 4);
        if (tag == 0x65BCF461) toc.meta.assign(&data[off], &data[off] + sz);
        if (tag == 0xDCD720B5) toc.dupes.assign(&data[off], &data[off] + sz);
        if (tag == 0x398ABFF0) toc.archives.assign(&data[off], &data[off] + sz);
        auto* set = tag == 0x506D7B8A ? &toc.all : tag == 0x6D921D7B ? &toc.zones : nullptr;
        if (!set) continue;
        for (uint32_t k = 0; k + 8 <= sz; k += 8) {
            uint64_t id; memcpy(&id, &data[off + k], 8);
            set->insert(id);
            if (tag == 0x506D7B8A) toc.ids.push_back(id);
        }
    }
    printf("toc: %zu asset ids, %zu zone keys\n", toc.all.size(), toc.zones.size());
    return true;
}

// Reads [offset, offset+size) of an archive's uncompressed stream. Handles DSAR (LZ4 blocks, type 3;
// layout from ALERT's dsar_codec.py) and plain archives.
static bool ReadArchiveRange(const std::string& path, uint64_t offset, uint32_t size, std::vector<char>& out) {
    std::ifstream a(path, std::ios::binary);
    if (!a.good()) { printf("cannot open %s\n", path.c_str()); return false; }
    uint32_t hdr[4];
    a.read((char*)hdr, sizeof(hdr));
    out.clear();
    if (hdr[0] != 0x52415344) {   // not DSAR: raw
        out.resize(size);
        a.seekg(offset);
        a.read(out.data(), size);
        return a.gcount() == size;
    }
    uint32_t blocks = hdr[2];
    a.seekg(32);
    struct Block { uint64_t real_off, comp_off; uint32_t real_size, comp_size; uint8_t type; uint8_t pad[7]; };
    std::vector<Block> table(blocks);
    a.read((char*)table.data(), blocks * sizeof(Block));
    for (auto& b : table) {
        uint64_t b_end = b.real_off + b.real_size;
        if (b_end <= offset || b.real_off >= offset + size) continue;
        if (b.type != 3) { printf("unsupported DSAR block type %u\n", b.type); return false; }
        std::vector<char> comp(b.comp_size), dec(b.real_size);
        a.seekg(b.comp_off);
        a.read(comp.data(), b.comp_size);
        int n = LZ4_decompress_safe(comp.data(), dec.data(), (int)b.comp_size, (int)b.real_size);
        if (n != (int)b.real_size) { printf("lz4 failed at block %llx\n", (unsigned long long)b.real_off); return false; }
        uint64_t from = std::max<uint64_t>(offset, b.real_off), to = std::min<uint64_t>(offset + size, b_end);
        out.insert(out.end(), dec.begin() + (from - b.real_off), dec.begin() + (to - b.real_off));
    }
    return out.size() == size;
}

int main(int argc, char** argv) {
    InitTable();
    if (g_table[1] != 0xB32E4CBE03A75F6Full) { printf("crc table mismatch\n"); return 2; }
    if (argc >= 3 && strcmp(argv[1], "hash") == 0) {
        for (int i = 2; i < argc; i++)
            printf("%016llX  %s\n", (unsigned long long)AssetId(argv[i]), argv[i]);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "check") == 0) {
        Toc toc;
        if (!LoadToc(argv[2], toc)) return 1;
        std::ifstream c(argv[3]);
        int hits = 0, total = 0;
        for (std::string line; std::getline(c, line); ) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            total++;
            uint64_t id = AssetId(line);
            bool z = toc.zones.count(id), a = toc.all.count(id);
            if (z || a) { hits++; printf("%s %016llX %s\n", z ? "ZONE " : "ASSET", (unsigned long long)id, line.c_str()); }
        }
        printf("%d/%d candidates found\n", hits, total);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "extract") == 0) {
        // extract <toc> <asset path> <out file>: copies the asset's bytes (first copy) from its archive.
        Toc toc;
        if (!LoadToc(argv[2], toc)) return 1;
        uint64_t id = AssetId(argv[3]);
        auto it = std::find(toc.ids.begin(), toc.ids.end(), id);
        if (it == toc.ids.end()) { printf("not in toc: %016llX\n", (unsigned long long)id); return 1; }
        size_t idx = it - toc.ids.begin();
        uint32_t m[3]; memcpy(m, &toc.meta[idx * 12], 12);       // dupe count, size, first dupe index
        uint32_t dp[2]; memcpy(dp, &toc.dupes[m[2] * 8], 8);      // archive index, offset
        const char* name = (const char*)&toc.archives[dp[0] * 72 + 8];
        std::string dir(argv[2]);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        printf("asset %016llX idx=%zu size=%u copies=%u archive=%u (%s) offset=%u\n", (unsigned long long)id, idx, m[1], m[0], dp[0], name, dp[1]);
        std::vector<char> buf;
        if (!ReadArchiveRange(dir + name, dp[1], m[1], buf)) return 1;
        std::ofstream(argv[4], std::ios::binary).write(buf.data(), buf.size());
        printf("wrote %zu bytes\n", buf.size());
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "extractid") == 0) {
        // extractid <toc> <hex asset id> <out file>
        Toc toc;
        if (!LoadToc(argv[2], toc)) return 1;
        uint64_t id = strtoull(argv[3], nullptr, 16);
        auto it = std::find(toc.ids.begin(), toc.ids.end(), id);
        if (it == toc.ids.end()) { printf("not in toc\n"); return 1; }
        size_t idx = it - toc.ids.begin();
        uint32_t m[3]; memcpy(m, &toc.meta[idx * 12], 12);
        uint32_t dp[2]; memcpy(dp, &toc.dupes[m[2] * 8], 8);
        std::string dir(argv[2]);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        std::vector<char> buf;
        if (!ReadArchiveRange(dir + (const char*)&toc.archives[dp[0] * 72 + 8], dp[1], m[1], buf)) return 1;
        std::ofstream(argv[4], std::ios::binary).write(buf.data(), buf.size());
        printf("wrote %zu bytes (archive %s)\n", buf.size(), (const char*)&toc.archives[dp[0] * 72 + 8]);
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "extractlist") == 0) {
        // extractlist <toc> <paths.txt> <out dir>: extracts many assets with one toc load.
        Toc toc;
        if (!LoadToc(argv[2], toc)) return 1;
        std::unordered_map<uint64_t, size_t> index;
        for (size_t i = 0; i < toc.ids.size(); i++) index.emplace(toc.ids[i], i);
        std::string dir(argv[2]);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        std::ifstream list(argv[3]);
        int ok = 0, total = 0;
        for (std::string line; std::getline(list, line); ) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            total++;
            auto it = index.find(AssetId(line));
            if (it == index.end()) continue;
            uint32_t m[3]; memcpy(m, &toc.meta[it->second * 12], 12);
            uint32_t dp[2]; memcpy(dp, &toc.dupes[m[2] * 8], 8);
            std::vector<char> buf;
            if (!ReadArchiveRange(dir + (const char*)&toc.archives[dp[0] * 72 + 8], dp[1], m[1], buf)) continue;
            std::string name = line;
            for (auto& ch : name) if (ch == '/' || ch == '\\' || ch == ':') ch = '_';
            std::ofstream(std::string(argv[4]) + "\\" + name, std::ios::binary).write(buf.data(), buf.size());
            ok++;
        }
        printf("extracted %d/%d\n", ok, total);
        return 0;
    }
    printf("usage: toctool hash <path>... | check <toc> <candidates.txt> | extract <toc> <path> <out> | extractlist <toc> <list> <dir>\n");
    return 1;
}
