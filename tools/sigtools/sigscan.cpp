// Static signature scanner: sigscan <exe> <patterns.txt>
// patterns.txt lines: <name> <off|-> <hex bytes with ?? wildcards>
// Prints match count, match RVA, and (if off given) rel32 target RVA = match + off + 4 + rel32.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Section { uint32_t va, vsize, raw, rawsize; char name[9]; };

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: sigscan <exe> <patterns>\n"); return 1; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
    auto dos = (IMAGE_DOS_HEADER*)img.data();
    auto nt = (IMAGE_NT_HEADERS64*)(img.data() + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    std::vector<Section> secs;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        Section s{ sec[i].VirtualAddress, sec[i].Misc.VirtualSize, sec[i].PointerToRawData, sec[i].SizeOfRawData, {} };
        memcpy(s.name, sec[i].Name, 8);
        secs.push_back(s);
    }
    auto fileToRva = [&](size_t off) -> int64_t {
        for (auto& s : secs) if (off >= s.raw && off < s.raw + s.rawsize) return (int64_t)off - s.raw + s.va;
        return -1;
    };
    auto rvaToFile = [&](int64_t rva) -> int64_t {
        for (auto& s : secs) if (rva >= s.va && rva < s.va + s.rawsize) return rva - s.va + s.raw;
        return -1;
    };
    auto sectionOf = [&](int64_t rva) -> std::string {
        for (auto& s : secs) if (rva >= s.va && rva < s.va + s.vsize) return s.name;
        return "?";
    };
    printf("ImageBase %llx, %zu sections\n", (unsigned long long)nt->OptionalHeader.ImageBase, secs.size());

    std::ifstream pf(argv[2]);
    std::string line;
    while (std::getline(pf, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name, offs, tok;
        ss >> name >> offs;
        std::vector<int> pat;
        while (ss >> tok) pat.push_back(tok[0] == '?' ? -1 : (int)strtol(tok.c_str(), nullptr, 16));
        std::vector<size_t> hits;
        for (auto& s : secs) {
            if (strncmp(s.name, ".text", 5) != 0) continue;
            size_t end = s.raw + s.rawsize - pat.size();
            for (size_t i = s.raw; i < end; i++) {
                size_t j = 0;
                for (; j < pat.size(); j++) if (pat[j] >= 0 && img[i + j] != (uint8_t)pat[j]) break;
                if (j == pat.size()) hits.push_back(i);
            }
        }
        printf("%-10s hits=%zu\n", name.c_str(), hits.size());
        for (size_t h = 0; h < hits.size() && h < 4; h++) {
            int64_t rva = fileToRva(hits[h]);
            printf("    match=exe+%llx", (long long)rva);
            if (offs != "-") {
                int off = atoi(offs.c_str());
                int32_t rel; memcpy(&rel, &img[hits[h] + off], 4);
                int64_t tgt = rva + off + 4 + rel;
                uint8_t opc = img[hits[h] + off - 1];
                printf("  [op %02X] target=exe+%llx (%s)", opc, (long long)tgt, sectionOf(tgt).c_str());
            }
            printf("\n");
        }
    }
    return 0;
}
