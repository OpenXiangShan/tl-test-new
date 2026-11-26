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

inline shared_tldata_t<DATASIZE_MMIO> BuildTraceMMIOData(uint64_t addr)
{
    auto data = make_shared_tldata<DATASIZE_MMIO>();
    for (int i = 0; i < DATASIZE_MMIO; i++)
        data->data[i] = static_cast<uint8_t>((addr >> (i % 8)) + 0x5a + i);
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
    tl_agent::MMIOAgent** mmioAgents,
    size_t              mmioAgentCount,
    uint64_t*           cycles,
    const std::string&  tracePath)
    : cfg               (cfg)
    , cAgents           (cAgents)
    , ulAgents          (ulAgents)
    , mAgents           (mAgents)
    , mmioAgents        (mmioAgents)
    , cAgentCount       (cAgentCount)
    , ulAgentCount      (ulAgentCount)
    , mAgentCount       (mAgentCount)
    , mmioAgentCount    (mmioAgentCount)
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
            mappedOp = TraceOp::CREAD;
        else if (op == "ST" || op == "MW")
            mappedOp = TraceOp::WRITE;
        else if (op == "EV")
            mappedOp = TraceOp::EVICT;
        else if (op == "FE")
            mappedOp = TraceOp::FENCE;
        else if (op == "MG" || op == "MGR")
            mappedOp = TraceOp::MMIO_GET;
        else if (op == "MP" || op == "MPF")
            mappedOp = TraceOp::MMIO_PUT;
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
    for (size_t i = 0; i < mmioAgentCount; i++)
    {
        if (!mmioAgents[i]->idle())
            return false;
    }

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

    // MMIO ops use their own core-indexed agent namespace.
    if (entry.op == TraceOp::MMIO_GET)
    {
        if (agentId >= mmioAgentCount)
            return tl_agent::ActionDenial::MISS;
        return mmioAgents[agentId]->do_get(entry.addr & ~uint64_t(0x7), 3, 0xff)
             ? tl_agent::ActionDenial::ACCEPTED
             : tl_agent::ActionDenial::CHANNEL_RESOURCE;
    }

    if (entry.op == TraceOp::MMIO_PUT)
    {
        if (agentId >= mmioAgentCount)
            return tl_agent::ActionDenial::MISS;
        return mmioAgents[agentId]->do_putfulldata(entry.addr & ~uint64_t(0x7),
                                                   BuildTraceMMIOData(entry.addr))
             ? tl_agent::ActionDenial::ACCEPTED
             : tl_agent::ActionDenial::CHANNEL_RESOURCE;
    }

    if (agentId < ulBase)
    {
        auto* cAgent = cAgents[agentId - cBase];

        if (entry.op == TraceOp::READ)
            return cAgent->do_acquireBlock(entry.addr, TLParamAcquire::NtoB, 0);

        if (entry.op == TraceOp::WRITE)
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

        if (entry.op == TraceOp::CREAD)
        {
            std::cout << "[" << *cycles << "] [TRC] Skip CR on ULAgent (agentId="
                      << entry.agentId << "), use MAgent for CR path"
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

        if (entry.op == TraceOp::CREAD)
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
        if (*cycles % 1000 == 0)
            std::cout << "[TraceDispatcher] fence waiting at cycle " << *cycles << std::endl;

        if (is_fence_drained())
            entries.pop_front();
        return;
    }

    auto denial = send_single_request(entry);

    switch (denial.ordinal)
    {
        case tl_agent::ActionDenial::ACCEPTED.ordinal:
            std::cout << "[TraceDispatcher] issue agent=" << entry.agentId
                      << " op=" << static_cast<unsigned>(entry.op)
                      << " addr=0x" << std::hex << entry.addr << std::dec
                      << " cycle=" << *cycles << std::endl;

            // Golden-write snapshot: keep the exact payload bytes this run
            // issued, so the end-of-run memory compare does not have to
            // re-derive them (and stays self-consistent for unaligned or
            // overlapping addresses).
            if (entry.op == TraceOp::MMIO_PUT)
            {
                auto payload = BuildTraceMMIOData(entry.addr);
                auto& slot   = mmioWritten[entry.addr & ~uint64_t(0x7)];
                std::copy(payload->data, payload->data + DATASIZE_MMIO, slot.begin());
            }
            else if (entry.op == TraceOp::WRITE && static_cast<size_t>(entry.agentId) >= cAgentCount)
            {
                // TL-side full-block writes (UL/M agents). A CAgent WRITE is
                // a permission-only acquire and carries no data.
                auto payload = BuildTraceWriteData(entry.addr);
                auto& slot   = tlWritten[entry.addr & ~uint64_t(0x3F)];
                std::copy(payload->data, payload->data + DATASIZE, slot.begin());
            }

            if (entry.op == TraceOp::READ || entry.op == TraceOp::CREAD)
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

void TraceDispatcher::final_memory_verify(const uint8_t* pmem, uint64_t memStart, uint64_t memEnd) const noexcept
{
    uint64_t tlChecked = 0, tlOk = 0, tlResident = 0, tlFail = 0;
    uint64_t mmioChecked = 0, mmioOk = 0, mmioFail = 0;
    uint64_t ambiguous = 0;

    auto dumpFirstMismatch = [](uint64_t addr, const uint8_t* got, const uint8_t* exp, size_t n) {
        for (size_t i = 0; i < n; i++)
        {
            if (got[i] != exp[i])
            {
                std::cout << "[MEMCHK] first mismatch at 0x" << std::hex << addr + i << std::dec
                          << ": got 0x" << std::hex << unsigned(got[i])
                          << ", expected 0x" << unsigned(exp[i]) << std::dec << std::endl;
                return;
            }
        }
    };

    for (const auto& [block, bytes] : tlWritten)
    {
        if (block < memStart || block + DATASIZE > memEnd)
        {
            std::cout << "[MEMCHK] TL block 0x" << std::hex << block << std::dec
                      << " outside memory image, skipped" << std::endl;
            ambiguous++;
            continue;
        }

        // The MMIO window is mapped inside the memory image; a TL block
        // overlapping a written MMIO granule cannot be judged (two different
        // write patterns cover the same bytes).
        bool overlapsMmio = false;
        for (const auto& [granule, gbytes] : mmioWritten)
        {
            (void)gbytes;
            if (granule < block + DATASIZE && granule + DATASIZE_MMIO > block)
            {
                overlapsMmio = true;
                break;
            }
        }
        if (overlapsMmio)
        {
            ambiguous++;
            continue;
        }

        tlChecked++;
        const uint8_t* img = pmem + (block - memStart);

        bool matches = true, allZero = true;
        for (size_t i = 0; i < DATASIZE; i++)
        {
            if (img[i] != bytes[i]) matches = false;
            if (img[i] != 0)        allZero = false;
        }

        if (matches)
            tlOk++;
        else if (allZero)
            tlResident++; // dirty line may still be resident in L2/L3
        else
        {
            tlFail++;
            dumpFirstMismatch(block, img, bytes.data(), DATASIZE);
        }
    }

    for (const auto& [granule, bytes] : mmioWritten)
    {
        if (granule < memStart || granule + DATASIZE_MMIO > memEnd)
        {
            std::cout << "[MEMCHK] MMIO granule 0x" << std::hex << granule << std::dec
                      << " outside memory image, skipped" << std::endl;
            ambiguous++;
            continue;
        }

        bool overlapsTl = false;
        for (const auto& [block, bbytes] : tlWritten)
        {
            (void)bbytes;
            if (block < granule + DATASIZE_MMIO && block + DATASIZE > granule)
            {
                overlapsTl = true;
                break;
            }
        }
        if (overlapsTl)
        {
            ambiguous++;
            continue;
        }

        mmioChecked++;
        const uint8_t* img = pmem + (granule - memStart);

        bool matches = true;
        for (size_t i = 0; i < DATASIZE_MMIO; i++)
            if (img[i] != bytes[i]) { matches = false; break; }

        if (matches)
            mmioOk++;
        else
        {
            mmioFail++;
            dumpFirstMismatch(granule, img, bytes.data(), DATASIZE_MMIO);
        }
    }

    bool pass = (tlFail == 0 && mmioFail == 0);
    std::cout << "MEMCHK: TL checked=" << tlChecked << " ok=" << tlOk
              << " resident=" << tlResident << " fail=" << tlFail
              << " | MMIO checked=" << mmioChecked << " ok=" << mmioOk
              << " fail=" << mmioFail
              << " | ambiguous=" << ambiguous
              << " -> MEMCHK: " << (pass ? "PASS" : "FAIL") << std::endl;
}
