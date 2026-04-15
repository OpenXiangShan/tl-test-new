
#include "Fuzzer.h"
#include "../Base/TLEnum.hpp"

#include "../Events/TLSystemEvent.hpp"

static std::vector<CFuzzRange> FUZZ_ARI_RANGES = {
    {.ordinal = 0, .maxTag = CFUZZER_RAND_RANGE_TAG, .maxSet = CFUZZER_RAND_RANGE_SET, .maxAlias = CFUZZER_RAND_RANGE_ALIAS},
    {.ordinal = 1, .maxTag = 0x1, .maxSet = 0x10, .maxAlias = 0x4},
    {.ordinal = 2, .maxTag = 0x10, .maxSet = 0x1, .maxAlias = 0x4}
};

static CFuzzRange FUZZ_STREAM_RANGE = {
    .ordinal = 0,
    .maxTag = 0x10,
    .maxSet = 0x10,
    .maxAlias = CFUZZER_RAND_RANGE_ALIAS
};

static inline size_t fact(size_t n) noexcept {
    size_t r = 1;
    for (size_t i = 1; i <= n; i++)
        r *= i;
    return r;
}

MFuzzer::MFuzzer(tl_agent::MAgent* mAgent) noexcept {
    this->mAgent = mAgent;

    this->startupInterval = 0;

    this->memoryStart = mAgent->config().memoryStart;
    this->memoryEnd = mAgent->config().memoryEnd;

    this->fuzzARIRangeIndex = 0;
    this->fuzzARIRangeIterationInterval = mAgent->config().fuzzARIInterval;
    this->fuzzARIRangeIterationTarget = mAgent->config().fuzzARITarget;

    this->fuzzARIRangeIterationCount = 0;
    this->fuzzARIRangeIterationTime = mAgent->config().fuzzARIInterval;

    this->fuzzStreamOffset = 0;
    this->fuzzStreamStepTime = mAgent->config().fuzzStreamStep;
    this->fuzzStreamEnded = false;
    this->fuzzStreamStep = mAgent->config().fuzzStreamStep;
    this->fuzzStreamInterval = mAgent->config().fuzzStreamInterval;
    this->fuzzStreamStart = mAgent->config().fuzzStreamStart;
    this->fuzzStreamEnd = mAgent->config().fuzzStreamEnd;

    this->putCoverageState = PutCoverageState::NEED_GET;
    this->putCoverageAddr = 0;
    this->putCoverageWaitCycles = 0;

    decltype(mAgent->config().sequenceModes.end()) modeInMap;
    if ((modeInMap = mAgent->config().sequenceModes.find(mAgent->sysId())) != mAgent->config().sequenceModes.end())
        this->mode = modeInMap->second;
    else
        this->mode = TLSequenceMode::FUZZ_ARI;

    if (this->mode == TLSequenceMode::FUZZ_ARI) {
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] in FUZZ_ARI mode").EndLine());

        for (size_t i = 0; i < FUZZ_ARI_RANGES.size(); i++)
            this->fuzzARIRangeOrdinal.push_back(i);

        size_t loop = mAgent->sysSeed() % fact(fuzzARIRangeOrdinal.size());
        for (size_t i = 0; i < loop; i++)
            std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());
    } else if (this->mode == TLSequenceMode::FUZZ_STREAM) {
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] in FUZZ_STREAM mode").EndLine());
    }
}

void MFuzzer::randomTest(bool put) {
    if (this->mode == TLSequenceMode::PASSIVE) {
        return;
    } else if (this->mode == TLSequenceMode::FUZZ_ARI || this->mode == TLSequenceMode::FUZZ_STREAM) {
        paddr_t addr = (CAGENT_RAND64(mAgent, "MFuzzer") % 0x400) << 6;

        if (this->mode == TLSequenceMode::FUZZ_ARI) {
            addr = ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxTag) << 13)
                + ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxSet) << 6);
            addr = remap_memory_address(addr);
        } else {
            addr = ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxTag) << 13)
                + ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxSet) << 6)
                + this->fuzzStreamOffset
                + this->fuzzStreamStart;
        }

        if (!put || CAGENT_RAND64(mAgent, "MFuzzer") % 2) {
            bool modify = (CAGENT_RAND64(mAgent, "MFuzzer") % 2) != 0;
            mAgent->do_getAuto(addr, modify);
        } else {
            auto putdata = make_shared_tldata<DATASIZE>();
            for (int i = 0; i < DATASIZE; i++)
                putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "MFuzzer");
            mAgent->do_putfulldata(addr, putdata);
        }
    }
}

void MFuzzer::tick() {
    if (this->startupInterval < mAgent->config().startupCycle) {
        this->startupInterval++;
        return;
    }

    if (this->mode == TLSequenceMode::FUZZ_ARI) {
        if (this->putCoverageState != PutCoverageState::DONE) {
            switch (this->putCoverageState) {
                case PutCoverageState::NEED_GET: {
                    paddr_t addr = ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxTag) << 13)
                        + ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxSet) << 6);
                    addr = remap_memory_address(addr);
                    bool modify = (CAGENT_RAND64(mAgent, "MFuzzer") % 2) != 0;
                    if (mAgent->do_getAuto(addr, modify) == tl_agent::ActionDenial::ACCEPTED) {
                        this->putCoverageAddr = addr;
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::WAIT_GET_RESP;
                    }
                    break;
                }

                case PutCoverageState::WAIT_GET_RESP: {
                    this->putCoverageWaitCycles++;
                    if (mAgent->is_m_fired()) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::NEED_PUT;
                    } else if (this->putCoverageWaitCycles > 50000) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::NEED_GET;
                    }
                    break;
                }

                case PutCoverageState::NEED_PUT: {
                    auto putdata = make_shared_tldata<DATASIZE>();
                    for (int i = 0; i < DATASIZE; i++)
                        putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "MFuzzer");

                    if (mAgent->do_putfulldata(this->putCoverageAddr, putdata) == tl_agent::ActionDenial::ACCEPTED) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::WAIT_PUT_ACK;
                    }
                    break;
                }

                case PutCoverageState::WAIT_PUT_ACK: {
                    this->putCoverageWaitCycles++;
                    if (mAgent->is_d_fired()) {
                        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] M Put path covered").EndLine());
                        this->putCoverageState = PutCoverageState::DONE;
                    } else if (this->putCoverageWaitCycles > 50000) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::NEED_GET;
                    }
                    break;
                }

                case PutCoverageState::DONE:
                    break;
            }
        } else {
            this->randomTest(false);
        }

        if (this->mAgent->cycle() >= this->fuzzARIRangeIterationTime) {
            this->fuzzARIRangeIterationTime += this->fuzzARIRangeIterationInterval;
            this->fuzzARIRangeIndex++;

            if (this->fuzzARIRangeIndex == fuzzARIRangeOrdinal.size()) {
                this->fuzzARIRangeIndex = 0;
                this->fuzzARIRangeIterationCount++;
                std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());
            }
        }

        if (this->fuzzARIRangeIterationCount == this->fuzzARIRangeIterationTarget)
            TLSystemFinishEvent().Fire();
    } else if (this->mode == TLSequenceMode::FUZZ_STREAM) {
        if (this->putCoverageState != PutCoverageState::DONE) {
            switch (this->putCoverageState) {
                case PutCoverageState::NEED_GET: {
                    paddr_t addr = ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxTag) << 13)
                        + ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxSet) << 6)
                        + this->fuzzStreamOffset
                        + this->fuzzStreamStart;
                    bool modify = (CAGENT_RAND64(mAgent, "MFuzzer") % 2) != 0;
                    if (mAgent->do_getAuto(addr, modify) == tl_agent::ActionDenial::ACCEPTED) {
                        this->putCoverageAddr = addr;
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::WAIT_GET_RESP;
                    }
                    break;
                }

                case PutCoverageState::WAIT_GET_RESP: {
                    this->putCoverageWaitCycles++;
                    if (mAgent->is_m_fired()) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::NEED_PUT;
                    } else if (this->putCoverageWaitCycles > 50000) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::NEED_GET;
                    }
                    break;
                }

                case PutCoverageState::NEED_PUT: {
                    auto putdata = make_shared_tldata<DATASIZE>();
                    for (int i = 0; i < DATASIZE; i++)
                        putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "MFuzzer");

                    if (mAgent->do_putfulldata(this->putCoverageAddr, putdata) == tl_agent::ActionDenial::ACCEPTED) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::WAIT_PUT_ACK;
                    }
                    break;
                }

                case PutCoverageState::WAIT_PUT_ACK: {
                    this->putCoverageWaitCycles++;
                    if (mAgent->is_d_fired()) {
                        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] M Put path covered").EndLine());
                        this->putCoverageState = PutCoverageState::DONE;
                    } else if (this->putCoverageWaitCycles > 50000) {
                        this->putCoverageWaitCycles = 0;
                        this->putCoverageState = PutCoverageState::NEED_GET;
                    }
                    break;
                }

                case PutCoverageState::DONE:
                    break;
            }
        } else {
            this->randomTest(false);
        }

        if (this->mAgent->cycle() >= this->fuzzStreamStepTime) {
            this->fuzzStreamStepTime += this->fuzzStreamInterval;
            this->fuzzStreamOffset += this->fuzzStreamStep;
        }

        if (this->fuzzStreamEnded)
            TLSystemFinishEvent().Fire();
    }

    if (this->mAgent->config().maximumCycle && (*cycles > this->mAgent->config().maximumCycle)) {
        LogInfo(this->mAgent->cycle(), Append("Maximum cycle count exceeded").EndLine());
        TLSystemFinishEvent().Fire();
    }
}
