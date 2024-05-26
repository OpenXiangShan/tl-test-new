#pragma once

#ifndef TLC_TEST_V3_PORTGEN_DYNAMIC_H
#define TLC_TEST_V3_PORTGEN_DYNAMIC_H

#include "../Utils/autoinclude.h"

#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"

#include <cstdint>
#include <vector>


namespace V3::PortGen {

    void LoadDynamic(std::vector<std::string> includePaths, int coreCount, int tlULPerCore);

    void LoadStatic();

    uint64_t GetCoreCount();
    uint64_t GetULPortCountPerCore();

    void PushChannelA(VTestTop* verilated, TLSequencer* tltest);
    void PullChannelA(VTestTop* verilated, TLSequencer* tltest);

    void PushChannelB(VTestTop* verilated, TLSequencer* tltest);
    void PullChannelB(VTestTop* verilated, TLSequencer* tltest);

    void PushChannelC(VTestTop* verilated, TLSequencer* tltest);
    void PullChannelC(VTestTop* verilated, TLSequencer* tltest);

    void PushChannelD(VTestTop* verilated, TLSequencer* tltest);
    void PullChannelD(VTestTop* verilated, TLSequencer* tltest);

    void PushChannelE(VTestTop* verilated, TLSequencer* tltest);
    void PullChannelE(VTestTop* verilated, TLSequencer* tltest);
}


#endif // TLC_TEST_V3_PORTGEN_DYNAMIC_H
