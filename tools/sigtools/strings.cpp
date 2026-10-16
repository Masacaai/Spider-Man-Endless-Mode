// strings <file> [min_len=6]: prints "<file offset hex> <string>" for printable ASCII runs.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: strings <file> [min_len]\n"); return 1; }
    size_t min_len = argc > 2 ? strtoul(argv[2], nullptr, 10) : 6;
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), {});
    size_t start = 0;
    for (size_t i = 0; i <= d.size(); i++) {
        bool printable = i < d.size() && d[i] >= 0x20 && d[i] < 0x7f;
        if (printable) continue;
        if (i - start >= min_len) printf("%08zx %.*s\n", start, (int)(i - start), &d[start]);
        start = i + 1;
    }
    return 0;
}
