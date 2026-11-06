#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include "kona-dcf-test-material.hpp"
int main() {
    mkdir("Player-Data", 0700);
    mkdir("Player-Data/2-fss", 0700);
    for (const char* name : {"k0", "k1", "r0", "r1", "r2"}) {
        std::ifstream f(std::string("Player-Data/2-fss/") + name);
        if (f.good()) { std::cerr << "Refusing to overwrite DCF material\n"; return 2; }
    }
    KonaDcfTestMaterial::gen_fake_dcf(1, 64);
    std::cout << "DCF L2 test material generated in current directory\n";
}
