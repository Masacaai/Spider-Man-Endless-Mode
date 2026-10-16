// Finds rip-relative references (disp32 with target = next_ip + disp) to given RVAs in an exe's .text.
// Usage: xrefs <exe> <rva hex> [rva hex ...]
// Heuristic: treats every 4-byte window as a disp32 ending the instruction (works for lea/mov/call/jmp
// without trailing immediates), and reports the 3 bytes before it so the opcode can be eyeballed.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: xrefs <exe> <rva> ...\n"); return 1; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
    auto dos = (IMAGE_DOS_HEADER*)img.data();
    auto nt = (IMAGE_NT_HEADERS64*)(img.data() + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int a = 2; a < argc; a++) {
        int64_t target = strtoll(argv[a], nullptr, 16);
        int count = 0;
        for (int s = 0; s < nt->FileHeader.NumberOfSections; s++) {
            if (strncmp((const char*)sec[s].Name, ".text", 5) != 0) continue;
            const uint8_t* base = img.data() + sec[s].PointerToRawData;
            for (uint32_t i = 3; i + 4 <= sec[s].SizeOfRawData; i++) {
                int32_t disp; memcpy(&disp, base + i, 4);
                int64_t rva = (int64_t)sec[s].VirtualAddress + i;
                if (rva + 4 + disp == target) {
                    if (count < 64) printf("  %llx -> exe+%llx  bytes before: %02X %02X %02X\n", (long long)target, (long long)(rva - 3),
                        base[i - 3], base[i - 2], base[i - 1]);
                    count++;
                }
            }
        }
        printf("exe+%llx: %d xrefs\n", (long long)target, count);

        // Absolute 64-bit pointers (data tables, vtables) anywhere in the file.
        uint64_t abs = nt->OptionalHeader.ImageBase + target;
        int pcount = 0;
        for (size_t i = 0; i + 8 <= img.size(); i += 8) {
            uint64_t v; memcpy(&v, &img[i], 8);
            if (v != abs) continue;
            for (int s = 0; s < nt->FileHeader.NumberOfSections; s++) {
                if (i >= sec[s].PointerToRawData && i < sec[s].PointerToRawData + sec[s].SizeOfRawData) {
                    if (pcount < 8) printf("  ptr at exe+%llx (%.8s)\n", (long long)(i - sec[s].PointerToRawData + sec[s].VirtualAddress), sec[s].Name);
                    pcount++;
                }
            }
        }
        printf("exe+%llx: %d absolute pointers\n", (long long)target, pcount);
    }
    return 0;
}
