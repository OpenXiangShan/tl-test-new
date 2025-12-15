#include "TraceDispatcher.hpp"
#include "Fuzzer.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>

TraceDispatcher::TraceDispatcher(uint64_t *cycles, const std::string& path) {
    this->cycles = cycles;

    std::ifstream tracefile(path);
    if (!tracefile.is_open()) {
        std::cerr << "[TraceDispatcher] failed to open tracefile: " << path << std::endl;
        std::exit(EXIT_FAILURE);
    }

    entries.clear();

    for (std::string line; std::getline(tracefile, line); ) {
        std::istringstream iss(line);
        std::string op;
        uint64_t address;
        int agent_id;

        // trace format: <op> <address> <agent>   (op is two-letter code, check addr.py in NEMU-Matrix)
        if (!(iss >> op >> std::hex >> address >> agent_id))
            continue; // Skip malformed lines

        // captialize op
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);

        uint8_t mapped_op = 0xFF;
        if (op == "LD" || op == "IF" || op == "MR")
            mapped_op = uint8_t(Fuzzer::traceOp::READ);
        else if (op == "CR")
            mapped_op = uint8_t(Fuzzer::traceOp::MODIFY);
        else if (op == "ST" || op == "MW")
            mapped_op = uint8_t(Fuzzer::traceOp::WRITE);
        else if (op == "EV")
            mapped_op = uint8_t(Fuzzer::traceOp::EVICT);
        else if (op == "FE")
            mapped_op = uint8_t(Fuzzer::traceOp::FENCE);
        else
            continue;

        TraceEntry e;
        e.agentId = agent_id;
        e.op      = mapped_op;
        e.addr    = address;
        entries.push_back(e);
    }

    std::cerr << "[TraceDispatcher] loaded " << entries.size() << " entries from " << path << std::endl;
}

bool TraceDispatcher::send(const std::function<bool(int,const TraceEntry&)>& tryIssue)
{
    if (entries.empty()) return false;

    const TraceEntry& e = entries.front();

    if (e.op == uint8_t(Fuzzer::traceOp::FENCE)) {
        // check whether all previous requests have been completed
        if (numReadSent == numReadReceived &&
            numWriteSent == numWriteReceived &&
            numEvictSent == numEvictReceived) {
            // all previous requests completed, we can issue the fence
            LogX("%ld issue Fence(0x%lx)\n", *cycles/2, e.addr);
            entries.pop_front();
            return true;
        }
        return false;
    }

    if (tryIssue(e.agentId, e)) {
        LogX("%ld Agent %d: %s(0x%lx)\n", *cycles/2, e.agentId,
            (e.op == uint8_t(Fuzzer::traceOp::READ)   ? "Read" :
                e.op == uint8_t(Fuzzer::traceOp::MODIFY) ? "ReadModify" :
                e.op == uint8_t(Fuzzer::traceOp::WRITE)  ? "Write" :
                e.op == uint8_t(Fuzzer::traceOp::EVICT)  ? "Evict" : "Unknown"),
            e.addr);


        if (e.op == uint8_t(Fuzzer::traceOp::READ) || e.op == uint8_t(Fuzzer::traceOp::MODIFY))
            numReadSent++;
        else if (e.op == uint8_t(Fuzzer::traceOp::WRITE))
            numWriteSent++;
        else if (e.op == uint8_t(Fuzzer::traceOp::EVICT))
            numEvictSent++;

        entries.pop_front();
        return true;
    }
    return false;
}
