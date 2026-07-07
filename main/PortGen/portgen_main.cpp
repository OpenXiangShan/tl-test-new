#include <iostream>
#include <fstream>

#include <unistd.h>

#include "portgen_static.hpp"

#include "../Utils/TLDefault.h"
#include "../Utils/inicpp.hpp"


void usage()
{
    std::cout << "Usage: [-c <core_count>] [-U <port_count>] [-L <port_count>]" << std::endl;
    std::cout << " -c <core_count>         - Specify default core count" << std::endl;
    std::cout << " -C <port_count>         - Specify default TL-C port count per-core" << std::endl;
    std::cout << " -L <port_count>         - Specify default TL-UL port count per-core" << std::endl;
}

int main(int argc, char** argv)
{
    //
    uint64_t coreCount                  = TLTEST_DEFAULT_CORE_COUNT;
    uint64_t masterCountPerCoreTLC      = TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLC;
    uint64_t masterCountPerCoreTLUL     = TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLUL;
    uint64_t masterCountPerCoreTLM     = TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLM;

    //
    char* endptr = NULL;

    int o;
    while ((o = getopt(argc, argv, "c:C:L:h")) != -1)
    {
        switch (o)
        {
            case 'c':
                coreCount = strtoul(optarg, &endptr, 0);
                break;

            case 'C':
                masterCountPerCoreTLC = strtoul(optarg, &endptr, 0);
                break;

            case 'L':
                masterCountPerCoreTLUL = strtoul(optarg, &endptr, 0);
                break;

            case 'h':
                usage();
                return 0;

            case '?':
                std::cout << "error: unknown option: " << char(optopt) << std::endl;
                usage();
                return 1;
        }
    }

    // read configuration override
    inicpp::IniManager ini("tltest.ini");

#   define INI_OVERRIDE_INT(section_name, key, target) \
    { \
        auto section = ini[section_name]; \
        if (section.isKeyExist(key)) \
        { \
            target = section.toInt(key); \
            std::cout << "[PortGen] Configuration override: " << key << " -> " << #target << " = " << target << std::endl; \
        } \
    } \


    INI_OVERRIDE_INT("tltest.config", "core",           coreCount);
    INI_OVERRIDE_INT("tltest.config", "core.tl_c",      masterCountPerCoreTLC);
    INI_OVERRIDE_INT("tltest.config", "core.tl_ul",     masterCountPerCoreTLUL);
    INI_OVERRIDE_INT("tltest.config", "core.tl_m",      masterCountPerCoreTLM);

#   undef INI_OVERRIDE_INT

    //
    std::ofstream fout("portgen.cpp");

    if (!fout.is_open())
    {
        std::cout << "Failed to open file: portgen.cpp" << std::endl;
        return 1;
    }

    fout << V3::PortGen::Generate(coreCount, masterCountPerCoreTLUL, masterCountPerCoreTLM);

    fout.flush();
    fout.close();
}