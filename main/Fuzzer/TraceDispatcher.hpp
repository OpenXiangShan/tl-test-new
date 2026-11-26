#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

#include "../Base/TLLocal.hpp"
#include "../TLAgent/CAgent.h"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/MAgent.h"
#include "../TLAgent/MMIOAgent.h"

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
        CREAD, // C Matrix Read, acquire write permisson
        EVICT,
        FENCE,
        MMIO_GET,
        MMIO_PUT
    };

private:
    TLLocalConfig*      cfg;

    tl_agent::CAgent**  cAgents;
    tl_agent::ULAgent** ulAgents;
    tl_agent::MAgent**  mAgents;
    tl_agent::MMIOAgent** mmioAgents;

    size_t              cAgentCount;
    size_t              ulAgentCount;
    size_t              mAgentCount;
    size_t              mmioAgentCount;

    uint64_t*           cycles;

    std::deque<TraceEntry> entries;

    uint64_t            numReadSent;
    uint64_t            numWriteSent;
    uint64_t            numEvictSent;
    uint64_t            numReadReceived;
    uint64_t            numWriteReceived;
    uint64_t            numEvictReceived;
    bool                summaryPrinted;

    // Golden-write snapshots, keyed by the aligned block/granule address and
    // holding the exact bytes this dispatcher issued (TL writes always cover
    // a full 64B block; MMIO writes a full 8B granule).
    std::unordered_map<uint64_t, std::array<uint8_t, DATASIZE>>     tlWritten;
    std::unordered_map<uint64_t, std::array<uint8_t, DATASIZE_MMIO>> mmioWritten;

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
        tl_agent::MMIOAgent** mmioAgents,
        size_t              mmioAgentCount,
        uint64_t*           cycles,
        const std::string&  tracePath);

    void tick();
    bool empty() const noexcept;

    // End-of-run golden compare of the memory image against every write this
    // dispatcher issued. TL blocks whose bytes are neither the pattern nor
    // all-zero count as RESIDENT (a dirty line may legally still sit in an
    // L2/L3 way); MMIO granules are compared strictly (device memory has no
    // residency). TL blocks overlapping a written MMIO granule are skipped in
    // both directions (the two patterns differ). Prints one "MEMCHK:" verdict
    // line; never throws.
    void final_memory_verify(const uint8_t* pmem, uint64_t memStart, uint64_t memEnd) const noexcept;
};
