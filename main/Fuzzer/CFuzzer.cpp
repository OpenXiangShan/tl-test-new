//
// Created by wkf on 2021/10/29.
//

#include "../Base/TLEnum.hpp"

#include "../Events/TLSystemEvent.hpp"
#include "Fuzzer.h"

#include <algorithm>


#define     BWPROF_STRIDE_ELEMENT       64


static std::vector<CFuzzRange> FUZZ_ARI_RANGES = {
    { .ordinal = 0, .maxTag = CFUZZER_RAND_RANGE_TAG,     .maxSet = CFUZZER_RAND_RANGE_SET,   .maxAlias = CFUZZER_RAND_RANGE_ALIAS    },
    { .ordinal = 1, .maxTag = 0x1,                        .maxSet = 0x10,                     .maxAlias = 0x4                         },
    { .ordinal = 2, .maxTag = 0x10,                       .maxSet = 0x1,                      .maxAlias = 0x4                         }
};

static CFuzzRange FUZZ_STREAM_RANGE = {
      .ordinal = 0, .maxTag = 0x10,                        .maxSet = 0x10,                      .maxAlias = CFUZZER_RAND_RANGE_ALIAS
};

static inline size_t fact(size_t n) noexcept
{
    size_t r = 1;
    for (size_t i = 1; i <= n; i++)
        r *= i;
    return r;
}


CFuzzer::CFuzzer(tl_agent::CAgent *cAgent) noexcept {
    this->cAgent = cAgent;

    this->startupInterval               = 0;

    this->memoryStart                   = cAgent->config().memoryStart;
    this->memoryEnd                     = cAgent->config().memoryEnd;

    this->cmoStart                      = cAgent->config().cmoStart;
    this->cmoEnd                        = cAgent->config().cmoEnd;

    this->fuzzARIRangeIndex             = 0;
    this->fuzzARIRangeIterationInterval = cAgent->config().fuzzARIInterval;
    this->fuzzARIRangeIterationTarget   = cAgent->config().fuzzARITarget;

    this->fuzzARIRangeIterationCount    = 0;
    this->fuzzARIRangeIterationTime     = cAgent->config().fuzzARIInterval;

    this->fuzzStreamOffset              = 0;
    this->fuzzStreamStepTime            = cAgent->config().fuzzStreamStep;
    this->fuzzStreamEnded               = false;
    this->fuzzStreamStep                = cAgent->config().fuzzStreamStep;
    this->fuzzStreamInterval            = cAgent->config().fuzzStreamInterval;
    this->fuzzStreamStart               = cAgent->config().fuzzStreamStart;
    this->fuzzStreamEnd                 = cAgent->config().fuzzStreamEnd;

    this->bwprof.step                   = cAgent->config().fuzzStreamStep / BWPROF_STRIDE_ELEMENT;
    this->bwprof.addr                   = cAgent->config().fuzzStreamStart;
    this->bwprof.cycle                  = 0;
    this->bwprof.count                  = 0;

    if (!this->bwprof.step)
    {
        LogWarn(this->cAgent->cycle(), 
            Append("[Bandwidth Profiler] step was set to 0 below ", BWPROF_STRIDE_ELEMENT, ", overrided as 1 cache block")
            .EndLine());

        this->bwprof.step = 1;
    }

    this->bwprof.distributionSplit      = (4 * 1024) / (BWPROF_STRIDE_ELEMENT * this->bwprof.step);
    this->bwprof.distributionCycle      = 0;
    this->bwprof.distributionCount      = 0;

    decltype(cAgent->config().sequenceModes.end()) modeInMap;
    if ((modeInMap = cAgent->config().sequenceModes.find(cAgent->sysId()))
            != cAgent->config().sequenceModes.end())
        this->mode = modeInMap->second;
    else
        this->mode = TLSequenceMode::FUZZ_ARI;

    if (this->mode == TLSequenceMode::FUZZ_ARI)
    {
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] in FUZZ_ARI mode").EndLine());

        for (size_t i = 0; i < FUZZ_ARI_RANGES.size(); i++)
            this->fuzzARIRangeOrdinal.push_back(i);

        size_t loop = cAgent->sysSeed() % fact(fuzzARIRangeOrdinal.size());
        for (size_t i = 0; i < loop; i++)
            std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());

        LogInfo(this->cAgent->cycle(), Append("Initial Fuzz Set: index = ", this->fuzzARIRangeIndex, ", permutation: "));
        LogEx(
            std::cout << "[ ";
            for (size_t i = 0; i < fuzzARIRangeOrdinal.size(); i++)
                std::cout << fuzzARIRangeOrdinal[i] << " ";
            std::cout << "]";
        );
        LogEx(std::cout << std::endl);
    }
    else if (this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] in FUZZ_STREAM mode").EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream steps ")
            .Hex().ShowBase().Append(this->fuzzStreamStep).EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream starts at ")
            .Hex().ShowBase().Append(this->fuzzStreamStart).EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream ends at ")
            .Hex().ShowBase().Append(this->fuzzStreamEnd).EndLine());
    }
    else if (this->mode == TLSequenceMode::BWPROF_STREAM_STRIDE_READ)
    {
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] in BWPROF_STREAM_STRIDE_READ mode").EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream stride steps ")
            .Hex().ShowBase().Append(this->fuzzStreamStep).EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream starts at ")
            .Hex().ShowBase().Append(this->fuzzStreamStart).EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream ends at ")
            .Hex().ShowBase().Append(this->fuzzStreamEnd).EndLine());
         LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] had Bandwidth Profiler enabled, "
            "test would be ended automatically by range").EndLine());
    }

    this->flagDone = false;

    //
    Gravity::RegisterListener(
        Gravity::MakeListener<TLSystemFinishEvent>(
            Gravity::StringAppender("tltest.cfuzzer.bwprof.finish", uint64_t(this)).ToString(),
            0,
            [this] (TLSystemFinishEvent& event) -> void {

                if (this->mode == TLSequenceMode::BWPROF_STREAM_STRIDE_READ)
                {
                    this->bwprof.distributions.push_back({
                        .cycle = this->bwprof.distributionCycle,
                        .count = this->bwprof.distributionCount
                    });

                    LogFinal(this->cAgent->cycle(), Append("================================================================").EndLine());
                    LogFinal(this->cAgent->cycle(), Append("Bandwidth Profiler of CFuzzer [", this->cAgent->sysId(), "]").EndLine());
                    LogFinal(this->cAgent->cycle(), Append("Address start    : ").Hex().ShowBase().Append(this->fuzzStreamStart).EndLine());
                    LogFinal(this->cAgent->cycle(), Append("Address stopped  : ").Hex().ShowBase().Append(this->bwprof.addr).EndLine());
                    LogFinal(this->cAgent->cycle(), Append("Mode             : Read-only").EndLine());
                    LogFinal(this->cAgent->cycle(), Append("Stride           : ", this->fuzzStreamStep).EndLine());

                    uint64_t totalCount = 0;
                    LogFinal(this->cAgent->cycle(), Append("Bandwidth distribution >>").EndLine());
                    for (auto& distribution : this->bwprof.distributions)
                    {
                        if (!distribution.cycle)
                            break;

                        totalCount += distribution.count;

                        double bytePerCycle = double(distribution.count) * BWPROF_STRIDE_ELEMENT / distribution.cycle;

                        LogFinal(this->cAgent->cycle(), Append()
                            .Append("    ", GetBase1024B(totalCount * BWPROF_STRIDE_ELEMENT))
                            .Append(" => ")
                            .Append(Gravity::StringAppender().NextWidth(6).Precision(3).Right().Append(bytePerCycle).Append("B/cycle").ToString())
                            .Append(" (", GetBase1024B(bytePerCycle * 1000 * 1000 * 1000) ,"/s per GHz)").EndLine());
                    }
                }

                 

                if (1) // switch off latency map profiler
                {
                    #define LogFinalLatencyMap(cat, op) \
                        LogFinal(this->cAgent->cycle(), Append("----------------------------------------------------------------").EndLine()); \
                        if (!this->cAgent->latencyMap##cat[int(TLOpcode##cat::op)].empty()) \
                        { \
                            LogFinal(this->cAgent->cycle(), Append(#op " >>").EndLine()); \
                            LogFinal(this->cAgent->cycle(), Append("(", this->cAgent->latencyMap##cat[int(TLOpcode##cat::op)].size(), " entires of distribution in total)").EndLine()); \
                            for (int i = 0; i < 1000; i++) \
                            { \
                                auto iterLatencyMap = this->cAgent->latencyMap##cat[int(TLOpcode##cat::op)].find(i); \
                                if (iterLatencyMap == this->cAgent->latencyMap##cat[int(TLOpcode##cat::op)].end()) \
                                    continue; \
                                if (!iterLatencyMap->second) \
                                    continue; \
                                LogFinal(this->cAgent->cycle(), Append("  ") \
                                    .NextWidth(5).Right().Append(i * 10).Append(" - ").NextWidth(5).Right().Append(i * 10 + 9).Append(": ") \
                                    .Left().Append(iterLatencyMap->second) \
                                    .EndLine()); \
                            } \
                        }

                    LogFinal(this->cAgent->cycle(), Append("================================================================").EndLine());
                    LogFinal(this->cAgent->cycle(), Append("Latency Map Profiler of CFuzzer [", this->cAgent->sysId(), "]").EndLine());
                    LogFinalLatencyMap(A, AcquirePerm);
                    LogFinalLatencyMap(A, AcquireBlock);
                    LogFinalLatencyMap(A, CBOClean);
                    LogFinalLatencyMap(A, CBOFlush);
                    LogFinalLatencyMap(A, CBOInval);
                    LogFinalLatencyMap(C, Release);
                    LogFinalLatencyMap(C, ReleaseData);

                    #undef LogFinalLatencyMap
                }

                LogFinal(this->cAgent->cycle(), Append("================================================================").EndLine());
            }
        )
    );
}

void CFuzzer::randomTest(bool do_alias) {
    paddr_t addr;
    int     alias;
    if (this->mode == TLSequenceMode::FUZZ_ARI || this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        if (this->mode == TLSequenceMode::FUZZ_ARI)
        {
            // Tag + Set + Offset
            addr  = ((CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxTag) << 13) 
                  + ((CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxSet) << 6);
            alias = (do_alias) ? (CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxAlias) : 0;
        }
        else // FUZZ_STREAM
        {
            addr  = ((CAGENT_RAND64(cAgent, "CFUZZER") % FUZZ_STREAM_RANGE.maxTag) << 13)
                  + ((CAGENT_RAND64(cAgent, "CFUZZER") % FUZZ_STREAM_RANGE.maxSet) << 6)
                  + this->fuzzStreamOffset
                  + this->fuzzStreamStart;
            alias = (do_alias) ? (CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_STREAM_RANGE.maxAlias) : 0;

            if (addr >= this->fuzzStreamEnd)
            {
                this->fuzzStreamEnded = true;
                return;
            }
        }

        if (CAGENT_RAND64(cAgent, "CFuzzer") % 4)
        {
            if (!cAgent->config().memoryEnable)
                return;

            addr = remap_memory_address(addr);

            if (CAGENT_RAND64(cAgent, "CFuzzer") % 2) {
                if (CAGENT_RAND64(cAgent, "CFuzzer") % 3) {
                    if (CAGENT_RAND64(cAgent, "CFuzzer") % 2) {
                        cAgent->do_acquireBlock(addr, TLParamAcquire::NtoT, alias); // AcquireBlock NtoT
                    } else {
                        cAgent->do_acquireBlock(addr, TLParamAcquire::NtoB, alias); // AcquireBlock NtoB
                    }
                } else {
                    cAgent->do_acquirePerm(addr, TLParamAcquire::NtoT, alias); // AcquirePerm
                }
            } else {
                /*
                uint8_t* putdata = new uint8_t[DATASIZE];
                for (int i = 0; i < DATASIZE; i++) {
                    putdata[i] = (uint8_t)CAGENT_RAND64(cAgent, "CFuzzer");
                }
                cAgent->do_releaseData(addr, tl_agent::TtoN, putdata); // ReleaseData
                */
                cAgent->do_releaseDataAuto(addr, alias, 
                    CAGENT_RAND64(cAgent, "CFuzzer") & 0x1,
                    CAGENT_RAND64(cAgent, "CFuzzer") & 0x1); // feel free to releaseData according to its priv
            }
        }
        else if (cAgent->config().cmoEnable)
        {
            addr = remap_cmo_address(addr);

            bool alwaysHit = (CAGENT_RAND64(cAgent, "CFuzzer") % 8) == 0;

            switch (CAGENT_RAND64(cAgent, "CFuzzer") % 3)
            {
                case 0: // cbo.clean
                    if (cAgent->config().cmoEnableCBOClean)
                        cAgent->do_cbo_clean(addr, alwaysHit);
                    break;

                case 1: // cbo.flush
                    if (cAgent->config().cmoEnableCBOFlush)
                        cAgent->do_cbo_flush(addr, alwaysHit);
                    break;

                case 2: // cbo.inval
                    if (cAgent->config().cmoEnableCBOInval)
                        cAgent->do_cbo_inval(addr, alwaysHit);
                    break;

                default:
                    break;
            }
        }
    }
}

void CFuzzer::caseTest() {
    if (*cycles == 100) {
        this->cAgent->do_acquireBlock(0x1040, TLParamAcquire::NtoT, 0);
    }
    if (*cycles == 300) {
        auto putdata = make_shared_tldata<DATASIZE>();
        for (int i = 0; i < DATASIZE; i++) {
            putdata->data[i] = (uint8_t)CAGENT_RAND64(cAgent, "CFuzzer");
        }
        this->cAgent->do_releaseData(0x1040, TLParamRelease::TtoN, putdata, 0);
    }
    if (*cycles == 400) {
      this->cAgent->do_acquireBlock(0x1040, TLParamAcquire::NtoT, 0);
    }
}

void CFuzzer::tick() {

    if (this->startupInterval < cAgent->config().startupCycle)
    {
        this->startupInterval++;
        return;
    }

    if (this->mode == TLSequenceMode::FUZZ_ARI)
    {
        this->randomTest(true);

        if (this->cAgent->cycle() >= this->fuzzARIRangeIterationTime)
        {
            this->fuzzARIRangeIterationTime += this->fuzzARIRangeIterationInterval;
            this->fuzzARIRangeIndex++;

            if (this->fuzzARIRangeIndex == fuzzARIRangeOrdinal.size())
            {
                this->fuzzARIRangeIndex = 0;
                this->fuzzARIRangeIterationCount++;

                std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());
            }

            LogInfo(this->cAgent->cycle(), Append("Fuzz Set switched: index = ", this->fuzzARIRangeIndex, ", permutation: "));
            LogEx(
                std::cout << "[ ";
                for (size_t i = 0; i < fuzzARIRangeOrdinal.size(); i++)
                    std::cout << fuzzARIRangeOrdinal[i] << " ";
                std::cout << "]";
            );
            LogEx(std::cout << std::endl);
        }

        if (this->fuzzARIRangeIterationCount == this->fuzzARIRangeIterationTarget)
        {
            TLSystemFinishEvent().Fire();
        }
    }
    else if (this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        this->randomTest(true);

        if (this->cAgent->cycle() >= this->fuzzStreamStepTime)
        {
            this->fuzzStreamStepTime += this->fuzzStreamInterval;
            this->fuzzStreamOffset   += this->fuzzStreamStep;
        }

        if (this->fuzzStreamEnded)
        {
            TLSystemFinishEvent().Fire();
        }
    }
    else if (this->mode == TLSequenceMode::BWPROF_STREAM_STRIDE_READ)
    {
        this->bwprof.cycle++;
        this->bwprof.distributionCycle++;

        if (this->cAgent->do_acquireBlock(this->bwprof.addr, TLParamAcquire::NtoB, 0))
        {
            this->bwprof.addr += BWPROF_STRIDE_ELEMENT * this->bwprof.step;
            this->bwprof.count++;

            this->bwprof.distributionCount++;
        }

        if (this->bwprof.count == this->bwprof.distributionSplit)
        {
            this->bwprof.distributions.push_back({
                .cycle = this->bwprof.distributionCycle,
                .count = this->bwprof.distributionCount
            });

            this->bwprof.distributionSplit *= 2;
            this->bwprof.distributionCycle = 0;
            this->bwprof.distributionCount = 0;
        }

        if (this->bwprof.addr >= this->fuzzStreamEnd)
            flagDone = true;
    }

    if (flagDone)
    {
        TLSystemFinishEvent().Fire();
    }

    if (this->cAgent->config().maximumCycle && (*cycles > this->cAgent->config().maximumCycle))
    {
        LogInfo(this->cAgent->cycle(), Append("Maximum cycle count exceeded").EndLine());
        TLSystemFinishEvent().Fire();
    }
}

bool CFuzzer::done() const noexcept
{
    return flagDone;
}
