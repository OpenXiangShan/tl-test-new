#pragma once

#include <string>
#include <deque>
#include <cstdint>
#include <functional>

struct TraceEntry {
    int     agentId;
    uint8_t op;    // use Fuzzer::traceOp values
    uint64_t addr;
};

class TraceDispatcher {
private:
    uint64_t *cycles;

    std::deque<TraceEntry> entries;
    uint32_t numReadSent = 0;
    uint32_t numWriteSent = 0;
    uint32_t numEvictSent = 0;
    uint32_t numReadReceived = 0;
    uint32_t numWriteReceived = 0;
    uint32_t numEvictReceived = 0;

public:
    // load whole trace file preserving file order
    TraceDispatcher(uint64_t *cycles, const std::string& tracePath);

    // try to issue current front entry by calling tryIssue(agentId, entry).
    // If tryIssue returns true, pop front and return true; otherwise leave it.
    bool send(const std::function<bool(int,const TraceEntry&)>& tryIssue);

    void receive(bool readAck, bool writeAck, bool evictAck) noexcept {
        if (readAck)  numReadReceived++;
        if (writeAck) numWriteReceived++;
        if (evictAck) numEvictReceived++;
    }

    bool empty() const noexcept { return entries.empty(); }
};
