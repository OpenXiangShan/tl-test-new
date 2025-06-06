//
// Created by wkf on 2021/10/29.
//

#ifndef TLC_TEST_FUZZER_H
#define TLC_TEST_FUZZER_H

#include "../Base/TLLocal.hpp"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/CAgent.h"
#include "../TLAgent/MMIOAgent.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>


#ifndef CFUZZER_RAND_RANGE_TAG
#   define CFUZZER_RAND_RANGE_TAG           0x80
#endif

#ifndef CFUZZER_RAND_RANGE_SET
#   define CFUZZER_RAND_RANGE_SET           0x80
#endif

#ifndef CFUZZER_RAND_RANGE_ALIAS
#   define CFUZZER_RAND_RANGE_ALIAS         0x4
#endif


#ifndef CFUZZER_RANGE_ITERATE_INTERVAL
#   define CFUZZER_RANGE_ITERATE_INTERVAL   (5 * 1000 * 1000)
#endif

#ifndef CFUZZER_RANGE_ITERATE_TARGET
#   define CFUZZER_RANGE_ITERATE_TARGET     24
#endif


#ifndef CFUZZER_FUZZ_STREAM_INTERVAL
#   define CFUZZER_FUZZ_STREAM_INTERVAL     2000
#endif

#ifndef CFUZZER_FUZZ_STREAM_STEP
#   define CFUZZER_FUZZ_STREAM_STEP         0x40
#endif

#ifndef CFUZZER_FUZZ_STREAM_START
#   define CFUZZER_FUZZ_STREAM_START        0x80000000
#endif

#ifndef CFUZZER_FUZZ_STREAM_END
#   define CFUZZER_FUZZ_STREAM_END          0xA0000000
#endif


class Fuzzer {
protected:
    uint64_t *cycles;
public:
    size_t              startupInterval;

    TLSequenceMode      mode;

    size_t              memoryStart;
    size_t              memoryEnd;
    size_t              mmioStart;
    size_t              mmioEnd;
    size_t              cmoStart;
    size_t              cmoEnd;

    size_t              fuzzARIRangeIndex;
    size_t              fuzzARIRangeIterationTime;
    size_t              fuzzARIRangeIterationInterval;
    size_t              fuzzARIRangeIterationCount;
    size_t              fuzzARIRangeIterationTarget;
    std::vector<int>    fuzzARIRangeOrdinal;

    size_t              fuzzStreamOffset;
    size_t              fuzzStreamInterval;
    size_t              fuzzStreamStepTime;
    bool                fuzzStreamEnded;
    size_t              fuzzStreamStep;
    size_t              fuzzStreamStart;
    size_t              fuzzStreamEnd;
public:
    Fuzzer() noexcept = default;
    virtual ~Fuzzer() noexcept = default;
    virtual void tick() = 0;
    inline void set_cycles(uint64_t *cycles) {
        this->cycles = cycles;
    }
    inline paddr_t remap_memory_address(paddr_t addr) {
        return ((addr % (memoryEnd - memoryStart)) + memoryStart);
    }
    inline paddr_t remap_mmio_address(paddr_t addr) {
        return ((addr % (mmioEnd - mmioStart)) + mmioStart);
    }
    inline paddr_t remap_cmo_address(paddr_t addr) {
        return ((addr % (cmoEnd - cmoStart)) + cmoStart);
    }
};

class ULFuzzer: public Fuzzer {
private:
    tl_agent::ULAgent *ulAgent;
public:
    ULFuzzer(tl_agent::ULAgent *ulAgent) noexcept;
    virtual ~ULFuzzer() noexcept = default;
    void randomTest(bool put);
    void caseTest();
    void caseTest2();
    void tick();
};

class MMIOFuzzer : public Fuzzer {
private:
    tl_agent::MMIOAgent* mmioAgent;
public:
    MMIOFuzzer(tl_agent::MMIOAgent* ulAgent) noexcept;
    virtual ~MMIOFuzzer() noexcept = default;
    void randomTest(bool put);
    void tick();
};


struct CFuzzRange {
    size_t      ordinal;
    uint64_t    maxTag;
    uint64_t    maxSet;
    uint64_t    maxAlias;

    inline bool operator<(const CFuzzRange& obj) const noexcept
    {
        return ordinal < obj.ordinal;
    }

    inline void swap(CFuzzRange& obj) const noexcept
    {

    }
};

class CFuzzer: public Fuzzer {
private:
    class BandwidthProfilerStatus {
    public:
        uint64_t    step;

        uint64_t    addr;
        uint64_t    cycle;
        uint64_t    count;

        uint64_t    distributionSplit;
        uint64_t    distributionCycle;
        uint64_t    distributionCount;

    public:
        class Distributed {
        public:
            uint64_t    cycle;
            uint64_t    count;
        };

        std::vector<Distributed>    distributions;
    };

private:
    tl_agent::CAgent*       cAgent;
    bool                    flagDone;

    BandwidthProfilerStatus bwprof;

public:
    CFuzzer(tl_agent::CAgent *cAgent) noexcept;
    virtual ~CFuzzer() noexcept = default;
    void randomTest(bool do_alias);
    void caseTest();
    void tick();
    bool done() const noexcept;
};


class UnifiedFuzzer : public Fuzzer {
private:
    TLLocalConfig*              cfg;

    tl_agent::CAgent**          cAgents;
    tl_agent::ULAgent**         ulAgents;
    tl_agent::MMIOAgent**       mmioAgents;

    size_t                      cAgentCount;
    size_t                      ulAgentCountPerC;
    size_t                      mmioAgentCount;

protected:
    struct FuzzedAddress {
        paddr_t base;
        size_t  size;

        bool IsInRange(paddr_t addr) noexcept;
        bool IsOverlappedRange(paddr_t base, size_t size) noexcept;
    };

    struct ExclusiveFuzzedAddress : public FuzzedAddress {
        size_t  agentIndex;
    };

    std::vector<FuzzedAddress>*         fuzzedLocally;
    std::vector<ExclusiveFuzzedAddress> fuzzedExclusively;

    std::unordered_set<paddr_t>*        monitoredGrant;

    struct {
        size_t              counterU;
        size_t              counterR;
        size_t              counterB;
        size_t              epoch;
        std::vector<int>    ordinal;
    } contextAnvil;

public:
    UnifiedFuzzer(TLLocalConfig*        cfg,
                  tl_agent::CAgent**    cAgents, 
                  size_t                cAgentCount,
                  tl_agent::ULAgent**   ulAgents,
                  size_t                ulAgentCountPerC,
                  tl_agent::MMIOAgent** mmioAgents,
                  size_t                mmioAgentCount);

    virtual ~UnifiedFuzzer() noexcept;

public:
    void OnGrant(tl_agent::GrantEvent& event) noexcept;

public:
    bool IsMode(TLUnifiedSequenceMode mode) const noexcept;

    void InitAnvil();
    void TickAnvil();

public:
    void tick();
};


#endif //TLC_TEST_FUZZER_H
