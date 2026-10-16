// rtti <exe> <ClassName> [methods=40]: finds MSVC RTTI for ClassName and prints its vtable(s) and method RVAs.
// TypeDescriptor name ".?AV<Class>@@" -> TD at name-0x10 -> CompleteObjectLocator {1, off, cd, TD rva, CHD rva, self rva}
// -> vtable = (address of an absolute pointer to the COL) + 8.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: rtti <exe> <Class> [methods]\n"); return 1; }
    int methods = argc > 3 ? atoi(argv[3]) : 40;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
    auto nt = (IMAGE_NT_HEADERS64*)(img.data() + ((IMAGE_DOS_HEADER*)img.data())->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    uint64_t base = nt->OptionalHeader.ImageBase;
    auto off2rva = [&](size_t o) -> int64_t {
        for (int s = 0; s < nt->FileHeader.NumberOfSections; s++)
            if (o >= sec[s].PointerToRawData && o < sec[s].PointerToRawData + sec[s].SizeOfRawData)
                return (int64_t)o - sec[s].PointerToRawData + sec[s].VirtualAddress;
        return -1;
    };
    auto rva2off = [&](int64_t r) -> int64_t {
        for (int s = 0; s < nt->FileHeader.NumberOfSections; s++)
            if (r >= sec[s].VirtualAddress && r < (int64_t)sec[s].VirtualAddress + sec[s].SizeOfRawData)
                return r - sec[s].VirtualAddress + sec[s].PointerToRawData;
        return -1;
    };
    std::string name = std::string(".?AV") + argv[2] + "@@";
    std::vector<int64_t> tds;
    for (size_t i = 0; i + name.size() < img.size(); i++)
        if (img[i] == '.' && memcmp(&img[i], name.c_str(), name.size() + 1) == 0) tds.push_back(off2rva(i) - 0x10);
    for (int64_t td : tds) {
        printf("TypeDescriptor exe+%llx\n", (long long)td);
        for (size_t i = 0; i + 24 <= img.size(); i += 4) {
            uint32_t col[6]; memcpy(col, &img[i], 24);
            if (col[0] != 1 || col[3] != (uint32_t)td) continue;
            int64_t col_rva = off2rva(i);
            if (col[5] != (uint32_t)col_rva) continue;
            printf("  COL exe+%llx (offset %u)\n", (long long)col_rva, col[1]);
            uint64_t want = base + col_rva;
            for (size_t j = 0; j + 8 <= img.size(); j += 8) {
                uint64_t v; memcpy(&v, &img[j], 8);
                if (v != want) continue;
                int64_t vt = off2rva(j) + 8;
                printf("  vtable exe+%llx\n", (long long)vt);
                for (int m = 0; m < methods; m++) {
                    uint64_t fn; memcpy(&fn, &img[rva2off(vt) + 8 * m], 8);
                    if (fn < base || fn > base + nt->OptionalHeader.SizeOfImage) break;
                    printf("    [%2d] exe+%llx\n", m, (long long)(fn - base));
                }
            }
        }
    }
    return 0;
}
