#include "TraceDispatcher.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../Events/TLSystemEvent.hpp"

namespace {
inline shared_tldata_t<DATASIZE> BuildTraceWriteData(uint64_t addr)
{
    auto data = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE; i++)
        data->data[i] = static_cast<uint8_t>((addr >> (i % 8)) + i);
    return data;
}
}

TraceDispatcher::TraceDispatcher(
    TLLocalConfig*      cfg,
    tl_agent::CAgent**  cAgents,
    size_t              cAgentCount,
    tl_agent::ULAgent** ulAgents,
    size_t              ulAgentCount,
    tl_agent::MAgent**  mAgents,
    size_t              mAgentCount,
    uint64_t*           cycles,
    const std::string&  tracePath)
    : cfg               (cfg)
    , cAgents           (cAgents)
    , ulAgents          (ulAgents)
    , mAgents           (mAgents)
    , cAgentCount       (cAgentCount)
    , ulAgentCount      (ulAgentCount)
    , mAgentCount       (mAgentCount)
    , cycles            (cycles)
    , numReadSent       (0)
    , numWriteSent      (0)
    , numEvictSent      (0)
    , numReadReceived   (0)
    , numWriteReceived  (0)
    , numEvictReceived  (0)
    , summaryPrinted    (false)
{
    std::ifstream traceFile(tracePath);
    if (!traceFile.is_open())
    {
        std::cerr << "[TraceDispatcher] failed to open trace file: " << tracePath << std::endl;
        std::abort();
    }

    std::string line;
    while (std::getline(traceFile, line))
    {
        std::istringstream iss(line);

        std::string op;
        uint64_t addr = 0;
        int agentId = -1;

        if (!(iss >> op >> std::hex >> addr >> agentId))
            continue;

        std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });

        uint8_t mappedOp;
        if (op == "LD" || op == "IF" || op == "MR")
            mappedOp = TraceOp::READ;
        else if (op == "CR")
            mappedOp = TraceOp::MODIFY;
        else if (op == "ST" || op == "MW")
            mappedOp = TraceOp::WRITE;
        else if (op == "EV")
            mappedOp = TraceOp::EVICT;
        else if (op == "FE")
            mappedOp = TraceOp::FENCE;
        else
            continue;

        entries.push_back({
            .agentId = agentId,
            .op = mappedOp,
            .addr = addr
        });
    }

    std::cout << "[TraceDispatcher] loaded " << entries.size() << " entries from " << tracePath << std::endl;
}

bool TraceDispatcher::is_fence_drained() const noexcept
{
    return numReadSent == numReadReceived
        && numWriteSent == numWriteReceived
        && numEvictSent == numEvictReceived;
}

void TraceDispatcher::print_summary() const
{
    std::cout << "[" << *cycles << "] [TRC] Trace summary: sent{R/W/E}="
              << numReadSent << "/" << numWriteSent << "/" << numEvictSent
              << ", recv{R/W/E}=" << numReadReceived << "/" << numWriteReceived << "/" << numEvictReceived
              << ", pending_entries=" << entries.size()
              << std::endl;
}

tl_agent::ActionDenialEnum TraceDispatcher::send_single_request(const TraceEntry& entry)
{
    const size_t cBase = 0;
    const size_t ulBase = cBase + cAgentCount;
    const size_t mBase = ulBase + ulAgentCount;

    if (entry.agentId < 0)
    {
        LogInfo("TRC", Append("Skip malformed trace entry: negative agentId").EndLine());
        return tl_agent::ActionDenial::MISS;
    }

    const size_t agentId = static_cast<size_t>(entry.agentId);

    if (agentId < ulBase)
    {
        auto* cAgent = cAgents[agentId - cBase];

        if (entry.op == TraceOp::READ)
            return cAgent->do_acquireBlock(entry.addr, TLParamAcquire::NtoB, 0);

        if (entry.op == TraceOp::WRITE || entry.op == TraceOp::MODIFY)
            return cAgent->do_acquirePerm(entry.addr, TLParamAcquire::NtoT, 0);

        if (entry.op == TraceOp::EVICT)
            return cAgent->do_releaseDataAuto(entry.addr, 0, false, false);

        return tl_agent::ActionDenial::MISS;
    }

    if (agentId < mBase)
    {
        auto* ulAgent = ulAgents[agentId - ulBase];

        if (entry.op == TraceOp::READ)
            return ulAgent->do_getAuto(entry.addr);

        if (entry.op == TraceOp::WRITE)
            return ulAgent->do_putfulldata(entry.addr, BuildTraceWriteData(entry.addr));

        if (entry.op == TraceOp::EVICT)
        {
            std::cout << "[" << *cycles << "] [TRC] Skip EV on ULAgent (agentId="
                      << entry.agentId << ", addr=0x" << std::hex << entry.addr << std::dec
                      << ")" << std::endl;
            return tl_agent::ActionDenial::MISS;
        }

        if (entry.op == TraceOp::MODIFY)
        {
            std::cout << "[" << *cycles << "] [TRC] Skip CR on ULAgent (agentId="
                      << entry.agentId << "), use MAgent for MR/CR/MW path"
                      << std::endl;
            return tl_agent::ActionDenial::MISS;
        }

        return tl_agent::ActionDenial::MISS;
    }

    if (agentId < mBase + mAgentCount)
    {
        auto* mAgent = mAgents[agentId - mBase];

        if (entry.op == TraceOp::READ)
            return mAgent->do_getAuto(entry.addr, false);

        if (entry.op == TraceOp::MODIFY)
            return mAgent->do_getAuto(entry.addr, true);

        if (entry.op == TraceOp::WRITE)
            return mAgent->do_putfulldata(entry.addr, BuildTraceWriteData(entry.addr));

        if (entry.op == TraceOp::EVICT)
        {
            std::cout << "[" << *cycles << "] [TRC] Skip EV on MAgent (agentId="
                      << entry.agentId << ", addr=0x" << std::hex << entry.addr << std::dec
                      << ")" << std::endl;
            return tl_agent::ActionDenial::MISS;
        }

        return tl_agent::ActionDenial::MISS;
    }

    LogInfo("TRC", Append("Skip trace entry with out-of-range agentId=", entry.agentId).EndLine());
    return tl_agent::ActionDenial::MISS;
}

void TraceDispatcher::send()
{
    if (*cycles < cfg->startupCycle)
        return;

    if (entries.empty())
        return;

    const TraceEntry& entry = entries.front();

    if (entry.op == TraceOp::FENCE)
    {
        std::cout << "[TraceDispatcher] fence waiting at cycle " << *cycles << std::endl;

        if (is_fence_drained())
            entries.pop_front();
        return;
    }

    std::cout << "[TraceDispatcher] issue agent=" << entry.agentId
              << " op=" << static_cast<unsigned>(entry.op)
              << " addr=0x" << std::hex << entry.addr << std::dec
              << " cycle=" << *cycles << std::endl;

    auto denial = send_single_request(entry);

    switch (denial.ordinal)
    {
        case tl_agent::ActionDenial::ACCEPTED.ordinal:
            if (entry.op == TraceOp::READ || entry.op == TraceOp::MODIFY)
                numReadSent++;
            else if (entry.op == TraceOp::WRITE)
                numWriteSent++;
            else if (entry.op == TraceOp::EVICT)
                numEvictSent++;

            entries.pop_front();
            break;

        case tl_agent::ActionDenial::MISS.ordinal:
        case tl_agent::ActionDenial::PERMISSION.ordinal:
            entries.pop_front();
            break;

        case tl_agent::ActionDenial::CHANNEL_CONGESTION.ordinal:
        case tl_agent::ActionDenial::CHANNEL_RESOURCE.ordinal:
        case tl_agent::ActionDenial::REJECTED_BY_PENDING_B.ordinal:
        case tl_agent::ActionDenial::REJECTED_BY_INFLIGHT.ordinal:
            break;

        default:
            std::cerr << "[TraceDispatcher] unhandled ActionDenial ordinal: " << denial.ordinal << std::endl;
            std::abort();
            break;
    }
}

void TraceDispatcher::receive()
{
    for (size_t i = 0; i < cAgentCount; i++)
    {
        if (cAgents[i]->recv_readAck())
            numReadReceived++;
        if (cAgents[i]->recv_writeAck())
            numWriteReceived++;
        if (cAgents[i]->recv_evictAck())
            numEvictReceived++;
    }

    for (size_t i = 0; i < ulAgentCount; i++)
    {
        if (ulAgents[i]->recv_readAck())
            numReadReceived++;
        if (ulAgents[i]->recv_writeAck())
            numWriteReceived++;
    }

    for (size_t i = 0; i < mAgentCount; i++)
    {
        if (mAgents[i]->recv_readAck())
            numReadReceived++;
        if (mAgents[i]->recv_writeAck())
            numWriteReceived++;
    }
}

void TraceDispatcher::tick()
{
    send();
    receive();

    if (entries.empty() && is_fence_drained() && !summaryPrinted)
    {
        print_summary();
        summaryPrinted = true;
        LogInfo("TRC", Append("TraceDispatcher completed all requests at cycle ", *cycles).EndLine());
        TLSystemFinishEvent().Fire();
    }
}

bool TraceDispatcher::empty() const noexcept
{
    return entries.empty();
}
