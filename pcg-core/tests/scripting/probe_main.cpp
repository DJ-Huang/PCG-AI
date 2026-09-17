#include "runtime_probe.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#ifdef PCG_SCRIPTING_WITH_CORE
#include "pcg_api.h"
#endif

int main(int argc, char** argv) {
#ifdef PCG_SCRIPTING_WITH_CORE
    const char* version = pcg_get_version();
    if (!version || !*version) return 1;
#endif
    if (argc == 2) return pcg_scripting_run_probe(argv[1]);
    if (argc == 3 && std::string(argv[1]) == "--benchmark") {
        char* end = nullptr;
        const unsigned long samples = std::strtoul(argv[2], &end, 10);
        if (end == argv[2] || *end || samples < 5 || samples > 10000) return 2;
        return pcg_scripting_benchmark(static_cast<unsigned>(samples));
    }
    std::cerr << "Usage: probe <case> | --benchmark <5..10000>\n";
    return 2;
}
