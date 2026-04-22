#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include "../Base/TLLocal.hpp"
#include "../TLAgent/CAgent.h"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/MAgent.h"

struct TraceEntry {
    int         agentId;
    uint8_t     op;
    uint64_t    addr;
};

class TraceDispatcher {
private:
    enum TraceOp : uint8_t {
        READ = 0,
        WRITE,
        MODIFY,
        EVICT,
        FENCE
    };

private:
    TLLocalConfig*      cfg;

    tl_agent::CAgent**  cAgents;
    tl_agent::ULAgent** ulAgents;
    tl_agent::MAgent**  mAgents;

    size_t              cAgentCount;
    size_t              ulAgentCount;
    size_t              mAgentCount;

    uint64_t*           cycles;

    std::deque<TraceEntry> entries;

    uint64_t            numReadSent;
    uint64_t            numWriteSent;
    uint64_t            numEvictSent;
    uint64_t            numReadReceived;
    uint64_t            numWriteReceived;
    uint64_t            numEvictReceived;
    bool                summaryPrinted;

private:
    tl_agent::ActionDenialEnum send_single_request(const TraceEntry& entry);

    bool is_fence_drained() const noexcept;
    void print_summary() const;
    void send();
    void receive();

public:
    TraceDispatcher(
        TLLocalConfig*      cfg,
        tl_agent::CAgent**  cAgents,
        size_t              cAgentCount,
        tl_agent::ULAgent** ulAgents,
        size_t              ulAgentCount,
        tl_agent::MAgent**  mAgents,
        size_t              mAgentCount,
        uint64_t*           cycles,
        const std::string&  tracePath);

    void tick();
    bool empty() const noexcept;
};
