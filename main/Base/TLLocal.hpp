#pragma once

#ifndef TLC_TEST_TLLOCAL_H
#define TLC_TEST_TLLOCAL_H

#include <cstdint>
#include <cstddef>
#include <unordered_map>


enum class TLSequenceMode {
    PASSIVE         = 0,
    FUZZ_ARI,
    FUZZ_STREAM,
    FUZZ_STREAM_GS,
    FUZZ_COUNTER,
    FUZZ_COUNTER_SYNC,
    STREAM_COPY2,
    STREAM_MULTI
};

struct TLLocalConfig {
public:
    uint64_t            seed;

    unsigned int        coreCount;                          // L1-L2 system count
    unsigned int        masterCountPerCoreTLC;              // TL-C master count per core
    unsigned int        masterCountPerCoreTLUL;             // TL-UL master count per core
    unsigned int        masterCountPerCoreTLM;             // TL-M master count per core

    std::unordered_map<int, TLSequenceMode> sequenceModes;  // Agent sequence modes

    uint64_t            memoryEnable;                           // Memory (cacheable) region enable
    uint64_t            memoryStart;                            // Memory (cacheable) region start address
    uint64_t            memoryEnd;                              // Memory (cacheable) region end address

    uint64_t            memoryOOOR;                             // Memory (cacheable) backend out-of-order read

    uint64_t            mmioEnable;                             // MMIO region enable
    uint64_t            mmioStart;                              // MMIO region start address
    uint64_t            mmioEnd;                                // MMIO region end address

    uint64_t            cmoEnable;                              // CMO region enable
    uint64_t            cmoEnableCBOClean;                      // CMO enable 'cbo.clean'
    uint64_t            cmoEnableCBOFlush;                      // CMO enable 'cbo.flush'
    uint64_t            cmoEnableCBOInval;                      // CMO enable 'cbo.inval'
    uint64_t            cmoStart;                               // CMO region start address
    uint64_t            cmoEnd;                                 // CMO region end address

    uint64_t            fuzzARIInterval;                        // Fuzz Auto Range Iteration interval
    uint64_t            fuzzARITarget;                          // Fuzz Auto Range Iteration target

    uint64_t            fuzzStreamInterval;                     // Fuzz Stream interval
    uint64_t            fuzzStreamStep;                         // Fuzz Stream step
    uint64_t            fuzzStreamStart;                        // Fuzz Stream start address
    uint64_t            fuzzStreamEnd;                          // Fuzz Stream end address

public:
    size_t                          GetAgentCount() const noexcept;
    size_t                          GetCAgentCount() const noexcept;
    size_t                          GetULAgentCount() const noexcept;
    size_t                          GetMAgentCount() const noexcept;
};


class TLLocalContext {
public:
    virtual uint64_t                cycle() const noexcept = 0;
    virtual int                     sys() const noexcept = 0;
    virtual int                     sysId() const noexcept = 0;
    virtual unsigned int            sysSeed() const noexcept = 0;

    virtual const TLLocalConfig&    config() const noexcept = 0;
    virtual TLLocalConfig&          config() noexcept = 0;

    virtual bool                    mainSys() const noexcept;

    size_t                          GetAgentCount() const noexcept;
    size_t                          GetCAgentCount() const noexcept;
    size_t                          GetULAgentCount() const noexcept;
    size_t                          GetMAgentCount() const noexcept;
};

inline size_t TLLocalConfig::GetCAgentCount() const noexcept
{
    return coreCount * masterCountPerCoreTLC;
}

inline size_t TLLocalConfig::GetULAgentCount() const noexcept
{
    return coreCount * masterCountPerCoreTLUL;
}

inline size_t TLLocalConfig::GetMAgentCount() const noexcept
{
    return coreCount * masterCountPerCoreTLM;
}

inline size_t TLLocalConfig::GetAgentCount() const noexcept
{
    return GetCAgentCount() + GetULAgentCount() + GetMAgentCount();
}

inline bool TLLocalContext::mainSys() const noexcept
{
    return true;
}

inline size_t TLLocalContext::GetCAgentCount() const noexcept
{
    return config().GetCAgentCount();
}

inline size_t TLLocalContext::GetULAgentCount() const noexcept
{
    return config().GetULAgentCount();
}

inline size_t TLLocalContext::GetMAgentCount() const noexcept
{
    return config().GetMAgentCount();
}

inline size_t TLLocalContext::GetAgentCount() const noexcept
{
    return config().GetAgentCount();
}

#endif // TLC_TEST_TLLOCAL_H
