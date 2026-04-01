#include "Fuzzer.h"
#include "TraceDispatcher.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>

#include "../Events/TLSystemEvent.hpp"

TraceDispatcher::TraceDispatcher(
    TLLocalConfig*              cfg,
    tl_agent::CAgent**          cAgents,
    size_t                      cAgentCount,
    tl_agent::ULAgent**         ulAgents,
    size_t                      ulAgentCount,
    uint64_t                    *cycles, 
    const std::string&          path
): cfg               (cfg),
   cAgents           (cAgents),
   ulAgents          (ulAgents),
   cAgentCount       (cAgentCount),
   ulAgentCount      (ulAgentCount),
   cycles            (cycles)
{
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
            mapped_op = uint8_t(traceOp::READ);
        else if (op == "CR")
            mapped_op = uint8_t(traceOp::MODIFY);
        else if (op == "ST" || op == "MW")
            mapped_op = uint8_t(traceOp::WRITE);
        else if (op == "EV")
            mapped_op = uint8_t(traceOp::EVICT);
        else if (op == "FE")
            mapped_op = uint8_t(traceOp::FENCE);
        else
            continue;

        TraceEntry e;
        e.agentId = agent_id;
        e.op      = mapped_op;
        e.addr    = address;
        entries.push_back(e);
    }

    std::cout << "[TraceDispatcher] loaded " << entries.size() << " entries from " << path << std::endl;
}


tl_agent::ActionDenialEnum TraceDispatcher::send_single_request(TraceEntry e) {
    // TODO: FIXME: for now only consider 1C + 1UL for 1 core
    assert(e.agentId < 2);

    bool isC = e.agentId == 0;
    bool isUL = e.agentId == 1;
    bool isRead = (e.op == uint8_t(traceOp::READ) || e.op == uint8_t(traceOp::MODIFY));
    bool isWrite = (e.op == uint8_t(traceOp::WRITE));
    bool isEvict = (e.op == uint8_t(traceOp::EVICT));

    if (isC) {
        switch (e.op) {
            case uint8_t(traceOp::READ):
                return cAgents[0]->do_acquireBlock(e.addr, TLParamAcquire::NtoB, 0);
            case uint8_t(traceOp::WRITE):
                return cAgents[0]->do_acquirePerm(e.addr, TLParamAcquire::NtoT, 0);
            case uint8_t(traceOp::EVICT):
                return cAgents[0]->do_releaseDataAuto(e.addr, 0, false, false);
            default:
                printf("Unknown operation %d for C Agent\n", e.op);
                assert(false);
        }
    }
    else if (isUL) {
        switch (e.op) {
            case uint8_t(traceOp::READ):
                return ulAgents[0]->do_getAuto(e.addr);
            case uint8_t(traceOp::WRITE):
                {
                    auto putdata = make_shared_tldata<DATASIZE>();
                    for (int i = 0; i < DATASIZE; i++)
                        putdata->data[i] = (uint8_t)rand();
                    return ulAgents[0]->do_putfulldata(e.addr, putdata);
                }
            case uint8_t(traceOp::EVICT):
                printf("Evict operation not supported for UL Agent\n");
                assert(false);
            default:
                printf("Unknown operation %d for UL Agent\n", e.op);
                assert(false);
        }
    }
    else {
        printf("Invalid agent ID %d\n", e.agentId);
        assert(false);
    }

    return tl_agent::ActionDenial::ACCEPTED;
}


void TraceDispatcher::send()
{
    if (entries.empty()) return;

    const TraceEntry& e = entries.front();

    bool isFence = (e.op == uint8_t(traceOp::FENCE));
    bool isRead = (e.op == uint8_t(traceOp::READ) || e.op == uint8_t(traceOp::MODIFY));
    bool isWrite = (e.op == uint8_t(traceOp::WRITE));
    bool isEvict = (e.op == uint8_t(traceOp::EVICT));


    if ((*cycles) % 10000 == 0) {
        LogX("%ld [Dump] sent: R=%d W=%d E=%d, recvd: R=%d W=%d E=%d, ",
            *cycles/2,
            numReadSent, numWriteSent, numEvictSent,
            numReadReceived, numWriteReceived, numEvictReceived);
        LogX("     reqs left: %lu\n", entries.size());
        if (e.op == uint8_t(traceOp::FENCE)) {
            LogX("      FENCE waiting\n");
        }
    }

    if (isFence) {
        // check whether all previous requests have been completed
        if (numReadSent == numReadReceived &&
            numWriteSent == numWriteReceived &&
            numEvictReceived >= numEvictSent) {
            // all previous requests completed, we can issue the fence
            LogX("%ld issue Fence(0x%lx)\n", *cycles/2, e.addr);
            entries.pop_front();
        }
        return;
    } else {/*
        LogX("%ld Agent %d: %s(0x%lx)\n", *cycles/2, e.agentId,
            (e.op == uint8_t(traceOp::READ)   ? "Read" :
            e.op == uint8_t(traceOp::MODIFY) ? "ReadModify" :
            e.op == uint8_t(traceOp::WRITE)  ? "Write" :
            e.op == uint8_t(traceOp::EVICT)  ? "Evict" : "Unknown"),
            e.addr);
            */
    }

    auto denial = send_single_request(e);

    switch (denial.ordinal) {
        case tl_agent::ActionDenial::CHANNEL_CONGESTION.ordinal:
        case tl_agent::ActionDenial::CHANNEL_RESOURCE.ordinal:
        case tl_agent::ActionDenial::REJECTED_BY_PENDING_B.ordinal:
        case tl_agent::ActionDenial::REJECTED_BY_INFLIGHT.ordinal:
            // try again later
            break;

        case tl_agent::ActionDenial::ACCEPTED.ordinal:
            // successfully sent
            if (e.op == uint8_t(traceOp::READ) || e.op == uint8_t(traceOp::MODIFY))
                numReadSent++;
            else if (e.op == uint8_t(traceOp::WRITE))
                numWriteSent++;
            else if (e.op == uint8_t(traceOp::EVICT))
                numEvictSent++;

            entries.pop_front();
            break;

        case tl_agent::ActionDenial::MISS.ordinal:
            assert(isEvict);
        case tl_agent::ActionDenial::PERMISSION.ordinal:
            // if acquire already hit / releaseData_Auto miss
            // just pop and continue
            entries.pop_front();
            break;

        default:
            printf("Unknown denial code %d\n", denial.ordinal);
            assert(false);
    }

}

void TraceDispatcher::receive()
{
    for (size_t i = 0; i < cAgentCount; i++) {
        tl_agent::CAgent* cAgent = cAgents[i];
        if (cAgent->recv_readAck()) numReadReceived++;
        if (cAgent->recv_writeAck()) numWriteReceived++;
        if (cAgent->recv_evictAck()) numEvictReceived++;
    }

    for (size_t i = 0; i < ulAgentCount; i++) {
        tl_agent::ULAgent* ulAgent = ulAgents[i];
        if (ulAgent->recv_readAck()) numReadReceived++;
        if (ulAgent->recv_writeAck()) numWriteReceived++;
    }
}

void TraceDispatcher::tick()
{
    send();
    receive();

    if (empty() &&
        numReadSent == numReadReceived &&
        numWriteSent == numWriteReceived &&
        numEvictReceived >= numEvictSent)
    {
        std::cout << "[TraceDispatcher] all requests completed at cycle " << (*cycles)/2 << std::endl;
        TLSystemFinishEvent().Fire();
    }
}