//
// Created by wkf on 2021/10/29.
//

#ifndef TLC_TEST_FUZZER_H
#define TLC_TEST_FUZZER_H

#include "../Base/TLLocal.hpp"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/MAgent.h"
#include "../TLAgent/CAgent.h"
#include "../TLAgent/MMIOAgent.h"

#include <cstdint>
#include <vector>
#include <queue>

#include <fstream>
#include <sstream>
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
    int index;
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
    // FIXME add
    enum bwTestState {
        acquire = 0,
        wait_acquire,
        releasing,
        wait_release,
        acquire2,
        wait_fence,
        wait_acquire2,
        exit_fuzzer
    };

    int state = bwTestState::acquire;

    uint32_t writeResponsedCount = 0;

    uint32_t blkSent = 0;  // r/w requests in Addrs that has been fired to cache
    uint32_t blkReceived = 0;  // responses that have received from cache
    uint32_t blkCountLimit = 1024; // for bandwidth test (used for prefill in trace test)
    uint32_t blkCountLimitTrace = 1024; // for trace test
    uint32_t blkSentFence = 0;   // reqs fired, for fence
    uint32_t blkReceivedFence = 0;    // resps received, for fence

    uint64_t perfCycleStart = 0;
    uint64_t perfCycleEnd = 0;

    enum traceOp {
        READ = 0,
        WRITE = 1,
        FENCE = 2
    };
    // preload data into L2 Cache
    std::queue<uint64_t> filledAddrs;
    // actual trace r/w
    std::queue<std::pair<uint8_t, uint64_t>> traceAddrs;

public:
    Fuzzer() noexcept = default;
    virtual ~Fuzzer() noexcept = default;
    virtual void tick() = 0;
    inline void set_cycles(uint64_t *cycles) { // set_cycles_ptr, actually
        this->cycles = cycles;
    }
    inline void set_index(int idx) {
        this->index = idx;
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

    void read_trace(const std::string& tracefile_path) {
        blkCountLimit = 0;
        blkCountLimitTrace = 0;

        std::ifstream tracefile(tracefile_path);
        if (!tracefile.is_open()) {
            throw std::runtime_error("Failed to open tracefile: " + tracefile_path);
        }

        printf("Agent %d: Reading tracefile %s\n", index, tracefile_path.c_str());

        // std::string line;
        // while (std::getline(tracefile, line)) {
        for (std::string line; std::getline(tracefile, line); ) {
            std::istringstream iss(line);
            char operation;
            uint64_t address;
            int bank_idx;

            // trace format: <operation> <address> <agent>
            if (!(iss >> operation >> std::hex >> address >> bank_idx)) {
                continue; // Skip malformed lines
            }

            // printf("Operation: %c, Address: 0x%lx, Agent: %d\n", operation, address, agent);

            if ((bank_idx + 2) == index) { // 0: cAgent, 1: ulAgent, 2~: mAgent
                switch (operation) {
                    case 'r':
                        traceAddrs.push(std::make_pair(traceOp::READ, address));
                        blkCountLimitTrace ++;
                        break;
                    case 'w':
                        traceAddrs.push(std::make_pair(traceOp::WRITE, address));
                        blkCountLimitTrace ++;
                        break;
                    case 'f':
                        traceAddrs.push(std::make_pair(traceOp::FENCE, address));
                        break;
                    case 'p':
                        filledAddrs.push(address);
                        blkCountLimit ++;
                        break;
                    default:
                        printf("Unknown operation: %c\n", operation);
                        continue; // Skip unknown operations
                }
            }
        }

        printf("Fuzzer %d: filledAddrs size: %d, traceAddrs size: %d\n", index, blkCountLimit, blkCountLimitTrace);
        tracefile.close();

        // no preload address, directly start acquire2 process
        if (blkCountLimit == 0) {
            state = bwTestState::acquire2;
        }
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

class MFuzzer: public Fuzzer {
private:
    tl_agent::MAgent *mAgent;
public:
    MFuzzer(tl_agent::MAgent *mAgent) noexcept;
    virtual ~MFuzzer() noexcept = default;
    void randomTest(bool put);
    void caseTest();
    void caseTest2();
    void bandwidthTest();
    void traceBandwidthTest();
    void traceTestWithFence();
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


#endif //TLC_TEST_FUZZER_H
