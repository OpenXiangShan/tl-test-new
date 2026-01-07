#pragma once

#include <cstddef>
#include <string>
#include <deque>
#include <cstdint>
#include <functional>

#include "../Base/TLLocal.hpp"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/CAgent.h"
#include "../TLAgent/MMIOAgent.h"

struct TraceEntry {
    int     agentId;
    uint8_t op;    // use Fuzzer::traceOp values
    uint64_t addr;
};

class TraceDispatcher {
private:
    TLLocalConfig*              cfg;

    tl_agent::CAgent**          cAgents;
    tl_agent::ULAgent**         ulAgents;
    // TODO: add MMIOAgent if needed
    // tl_agent::MMIOAgent**       mmioAgents;

    size_t                    cAgentCount;
    size_t                    ulAgentCount;

    uint64_t *cycles;

    std::deque<TraceEntry> entries;
    uint32_t numReadSent = 0;
    uint32_t numWriteSent = 0;
    uint32_t numEvictSent = 0;
    uint32_t numReadReceived = 0;
    uint32_t numWriteReceived = 0;
    uint32_t numEvictReceived = 0;

    enum traceOp {
        READ = 0,
        WRITE = 1,
        MODIFY = 2,
        EVICT = 3,
        FENCE = 4
    };

public:
    // load whole trace file preserving file order
    TraceDispatcher(TLLocalConfig*              cfg,
                    tl_agent::CAgent**          cAgents,
                    size_t                      cAgentCount,
                    tl_agent::ULAgent**         ulAgents,
                    size_t                      ulAgentCount,
                    uint64_t                    *cycles, 
                    const std::string&          tracePath);

    // send a single request (read/write/evict) from trace
    tl_agent::ActionDenialEnum send_single_request(TraceEntry e);
    void send();

    void receive();

    void tick();

    bool empty() const noexcept { return entries.empty(); }
};
