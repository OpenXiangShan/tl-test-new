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

    std::unordered_map<int, TLSequenceMode> sequenceModes;  // Agent sequence modes

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
};


class TLLocalContext {
public:
    virtual uint64_t                cycle() const noexcept = 0;
    virtual int                     sysId() const noexcept = 0;
    virtual unsigned int            sysSeed() const noexcept = 0;

    virtual const TLLocalConfig&    config() const noexcept = 0;
    virtual TLLocalConfig&          config() noexcept = 0;

    size_t                          GetAgentCount() const noexcept;
    size_t                          GetCAgentCount() const noexcept;
    size_t                          GetULAgentCount() const noexcept;
};

inline size_t TLLocalConfig::GetCAgentCount() const noexcept
{
    return coreCount * masterCountPerCoreTLC;
}

inline size_t TLLocalConfig::GetULAgentCount() const noexcept
{
    return coreCount * masterCountPerCoreTLUL;
}

inline size_t TLLocalConfig::GetAgentCount() const noexcept
{
    return GetCAgentCount() + GetULAgentCount();
}

inline size_t TLLocalContext::GetCAgentCount() const noexcept
{
    return config().GetCAgentCount();
}

inline size_t TLLocalContext::GetULAgentCount() const noexcept
{
    return config().GetULAgentCount();
}

inline size_t TLLocalContext::GetAgentCount() const noexcept
{
    return config().GetAgentCount();
}

#endif // TLC_TEST_TLLOCAL_H
