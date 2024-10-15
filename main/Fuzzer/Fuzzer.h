//
// Created by wkf on 2021/10/29.
//

#ifndef TLC_TEST_FUZZER_H
#define TLC_TEST_FUZZER_H

#include "../Base/TLLocal.hpp"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/CAgent.h"

#include <vector>


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
    Fuzzer() noexcept = default;
    virtual ~Fuzzer() noexcept = default;
    virtual void tick() = 0;
    inline void set_cycles(uint64_t *cycles) {
        this->cycles = cycles;
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
    tl_agent::CAgent*   cAgent;

    TLSequenceMode      mode;

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
    CFuzzer(tl_agent::CAgent *cAgent) noexcept;
    virtual ~CFuzzer() noexcept = default;
    void randomTest(bool do_alias);
    void caseTest();
    void tick();
};

#endif //TLC_TEST_FUZZER_H
