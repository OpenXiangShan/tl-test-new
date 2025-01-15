#pragma once

#ifndef TLC_TEST_V3_PORTGEN_STATIC_H
#define TLC_TEST_V3_PORTGEN_STATIC_H

#include <string>


namespace V3::PortGen {

    std::string Generate(int coreCount, int tlULPerCore, int tlMPerCore);
}


#endif // TLC_TEST_V3_PORTGEN_DYNAMIC_H
