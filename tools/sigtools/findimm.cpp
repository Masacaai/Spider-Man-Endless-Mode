// findimm <exe> <hex u32> [...]: every occurrence of the little-endian u32 inside .text, with the 3 bytes before it
// (to tell cmp eax,imm32 = 3D / mov edx,imm32 = BA / etc. apart). Prints exe RVAs.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: findimm <exe> <hex>...\n"); return 1; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
    auto nt = (IMAGE_NT_HEADERS64*)(img.data() + ((IMAGE_DOS_HEADER*)img.data())->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int a = 2; a < argc; a++) {
        uint32_t v = (uint32_t)strtoul(argv[a], nullptr, 16);
        int count = 0;
        for (int s = 0; s < nt->FileHeader.NumberOfSections; s++) {
            if (strncmp((const char*)sec[s].Name, ".text", 5) != 0) continue;
            const uint8_t* b = img.data() + sec[s].PointerToRawData;
            for (uint32_t i = 3; i + 4 <= sec[s].SizeOfRawData; i++) {
                if (memcmp(b + i, &v, 4) != 0) continue;
                if (count < 40) printf("  %08X at exe+%x  before: %02X %02X %02X\n", v, sec[s].VirtualAddress + i, b[i - 3], b[i - 2], b[i - 1]);
                count++;
            }
        }
        printf("%08X: %d hits\n", v, count);
    }
    return 0;
}
