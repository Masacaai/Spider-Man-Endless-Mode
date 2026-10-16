// Offline check: maps Spider-Man.exe as a read-only image (no code runs) and resolves all natives
// with the same code the script uses in-game. Usage: resolve_offline.exe <path to Spider-Man.exe> <log>
#include "../src/log.h"
#include "../src/natives.h"
#include "../src/scan.h"

#include <Windows.h>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: resolve_offline <exe> <log>\n"); return 2; }
    HMODULE h = LoadLibraryExA(argv[1], nullptr, LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!h) { printf("map failed: %lu\n", GetLastError()); return 2; }
    scan::OverrideGameBaseForTesting((uintptr_t)h & ~(uintptr_t)3);   // low bits flag a resource mapping
    logging::Init(argv[2]);
    int ok = natives::ResolveAll();
    printf("%d/%d natives at expected addresses\n", ok, (int)natives::Count);
    return ok == natives::Count ? 0 : 1;
}
