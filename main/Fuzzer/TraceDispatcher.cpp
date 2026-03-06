#include "TraceDispatcher.hpp"
#include "Fuzzer.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>

using OP = Fuzzer::traceOp;

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
        std::string token1;
        std::string token2;
        uint64_t address = 0;
        uint64_t vaddr   = 0;
        int agent_id = 0;

        if (!(iss >> op))
            continue;

        // trace format: (1) <op> V=0x<vaddr> P=0x<paddr> <agent>
        //               (2) <op> 0x<paddr> <agent> [0x<vaddr>] (legacy, vaddr defaults to paddr)
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);
        if ((iss >> token1) && token1.rfind("V=", 0) == 0) {
            token1 = token1.substr(2);
            if (!(iss >> token2) || token2.rfind("P=", 0) != 0)
                continue;
            token2 = token2.substr(2);
            vaddr   = std::stoull(token1, nullptr, 16);
            address = std::stoull(token2, nullptr, 16);
            if (!(iss >> agent_id))
                continue;
        } else {
            iss.clear();
            iss.str(line);
            iss >> op;
            if (!(iss >> std::hex >> address >> agent_id))
                continue;
            if (!(iss >> std::hex >> vaddr))
                vaddr = address;
        }

        uint8_t mapped_op = 0xFF;
        if (op == "LD" || op == "IF" || op == "MR")
            mapped_op = OP::READ;
        else if (op == "CR")
            mapped_op = OP::MODIFY;
        else if (op == "ST" || op == "MW")
            mapped_op = OP::WRITE;
        else if (op == "EV")
            mapped_op = OP::EVICT;
        else if (op == "FE")
            mapped_op = OP::FENCE;
        else
            continue;

        TraceEntry e;
        e.agentId = agent_id;
        e.op      = mapped_op;
        e.addr    = address;
        e.user    = vaddr;
        entries.push_back(e);
    }

    std::cerr << "[TraceDispatcher] loaded " << entries.size()
              << " entries from " << path << std::endl;
}

bool TraceDispatcher::send(const std::function<bool(int,const TraceEntry&,bool*)>& tryIssue)
{
    if (entries.empty()) return false;

    const TraceEntry& e = entries.front();

    if (*cycles % 10000 == 0) {
        LogX("%ld [Dump] sent: R=%d W=%d E=%d, recvd: R=%d W=%d E=%d, ",
            *cycles/2,
            numReadSent, numWriteSent, numEvictSent,
            numReadReceived, numWriteReceived, numEvictReceived);
        LogX("     reqs left: %lu\n", entries.size());
        if (e.op == OP::FENCE) {
            LogX("      FENCE waiting\n");
        }
    }

    if (e.op == OP::FENCE) {
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

    bool skip = false;
    if (tryIssue(e.agentId, e, &skip)) {
        if (e.op == OP::READ || e.op == OP::MODIFY)
            numReadSent++;
        else if (e.op == OP::WRITE)
            numWriteSent++;
        else if (e.op == OP::EVICT && !skip)
            numEvictSent++;

        entries.pop_front();
        return true;
    }
    return false;
}
