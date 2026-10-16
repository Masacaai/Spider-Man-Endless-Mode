// grepall <toc> <needle> [needle...]: decompresses every archive referenced by the toc (DSAR/LZ4 or raw)
// and reports which assets contain any needle (case-sensitive bytes). Prints asset id + archive offset.
#include <lz4.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

struct Loc { uint64_t id; uint32_t size; };

#include <functional>
#include <unordered_map>

// Boyer-Moore-Horspool searchers, built once per needle.
static std::unordered_map<std::string, std::boyer_moore_horspool_searcher<std::string::const_iterator>>* g_searchers;

static const char* memmem_like(const char* hay, ptrdiff_t len, const std::string& nd) {
    if (len < (ptrdiff_t)nd.size()) return nullptr;
    auto& s = g_searchers->at(nd);
    auto r = s(hay, hay + len);
    return r.first == hay + len ? nullptr : r.first;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: grepall <toc> <needle>...\n"); return 1; }
    std::ifstream tf(argv[1], std::ios::binary);
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(tf)), {});
    uint32_t size; memcpy(&size, &raw[4], 4);
    std::vector<uint8_t> data(size);
    uLongf out = size;
    if (uncompress(data.data(), &out, raw.data() + 8, (uLong)(raw.size() - 8)) != Z_OK) { printf("zlib failed\n"); return 1; }
    uint16_t nsec; memcpy(&nsec, &data[12], 2);
    const uint8_t *ids = nullptr, *meta = nullptr, *dupes = nullptr, *arch = nullptr;
    uint32_t ids_sz = 0, arch_sz = 0;
    for (int i = 0; i < nsec; i++) {
        uint32_t tag, off, sz;
        memcpy(&tag, &data[16 + 12 * i], 4); memcpy(&off, &data[20 + 12 * i], 4); memcpy(&sz, &data[24 + 12 * i], 4);
        if (tag == 0x506D7B8A) { ids = &data[off]; ids_sz = sz; }
        if (tag == 0x65BCF461) meta = &data[off];
        if (tag == 0xDCD720B5) dupes = &data[off];
        if (tag == 0x398ABFF0) { arch = &data[off]; arch_sz = sz; }
    }
    size_t n_assets = ids_sz / 8, n_arch = arch_sz / 72;
    // Per archive: sorted (offset -> asset) for mapping hits back to assets.
    std::vector<std::map<uint64_t, Loc>> by_arch(n_arch);
    for (size_t i = 0; i < n_assets; i++) {
        uint32_t m[3]; memcpy(m, meta + i * 12, 12);
        for (uint32_t d = 0; d < m[0]; d++) {
            uint32_t dp[2]; memcpy(dp, dupes + (m[2] + d) * 8, 8);
            uint64_t id; memcpy(&id, ids + i * 8, 8);
            if (dp[0] < n_arch) by_arch[dp[0]][dp[1]] = { id, m[1] };
        }
    }
    std::string dir(argv[1]);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    std::vector<std::string> needles(argv + 2, argv + argc);
    static std::vector<std::string> keep(needles);
    g_searchers = new std::unordered_map<std::string, std::boyer_moore_horspool_searcher<std::string::const_iterator>>();
    for (auto& nd : keep) g_searchers->emplace(nd, std::boyer_moore_horspool_searcher<std::string::const_iterator>(nd.begin(), nd.end()));
    std::map<std::pair<uint64_t, std::string>, int> found;

    for (size_t a = 0; a < n_arch; a++) {
        std::string name = (const char*)(arch + a * 72 + 8);
        std::ifstream f(dir + name, std::ios::binary);
        if (!f.good()) continue;
        f.seekg(0, std::ios::end);
        const long long file_len = (long long)f.tellg();
        f.seekg(0);
        if (file_len < 32) { fprintf(stderr, "archive %zu/%zu %s (skipped, %lld bytes)\n", a + 1, n_arch, name.c_str(), file_len); continue; }
        uint32_t hdr[4]; f.read((char*)hdr, 16);
        auto scan = [&](const char* buf, size_t len, uint64_t base) {
            for (const auto& nd : needles) {
                const char* p = buf;
                while ((p = (const char*)memmem_like(p, buf + len - p, nd)) != nullptr) {
                    uint64_t pos = base + (p - buf);
                    auto it = by_arch[a].upper_bound(pos);
                    if (it != by_arch[a].begin()) {
                        --it;
                        if (pos < it->first + it->second.size && found[{ it->second.id, nd }]++ == 0) {
                            printf("HIT %016llX  %s  (archive %s)\n", (unsigned long long)it->second.id, nd.c_str(), name.c_str());
                            fflush(stdout);
                        }
                    }
                    p += nd.size();
                }
            }
        };
        if (hdr[0] != 0x52415344) {   // raw archive
            const size_t len = (size_t)file_len;
            std::vector<char> buf(1 << 26);
            for (size_t pos = 0; pos < len; pos += buf.size() - 256) {
                f.clear(); f.seekg(pos); f.read(buf.data(), buf.size());
                scan(buf.data(), (size_t)f.gcount(), pos);
            }
        } else {
            struct Block { uint64_t real_off, comp_off; uint32_t real_size, comp_size; uint8_t type; uint8_t pad[7]; };
            std::vector<Block> table(hdr[2]);
            f.seekg(32); f.read((char*)table.data(), table.size() * sizeof(Block));
            std::vector<char> comp, dec;
            for (auto& b : table) {
                comp.resize(b.comp_size); dec.resize(b.real_size);
                f.seekg(b.comp_off); f.read(comp.data(), b.comp_size);
                if (b.type != 3 || LZ4_decompress_safe(comp.data(), dec.data(), (int)b.comp_size, (int)b.real_size) != (int)b.real_size) continue;
                scan(dec.data(), dec.size(), b.real_off);
            }
        }
        fprintf(stderr, "archive %zu/%zu %s\n", a + 1, n_arch, name.c_str());
    }
    for (auto& [k, c] : found) printf("%016llX  %-28s x%d\n", (unsigned long long)k.first, k.second.c_str(), c);
    printf("%zu asset/needle hits\n", found.size());
    return 0;
}
