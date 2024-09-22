#pragma once

#ifndef TLC_TEST_TLLOCAL_H
#define TLC_TEST_TLLOCAL_H

#include <cstdint>
#include <cstddef>


struct TLLocalConfig {
public:
    uint64_t            seed;

    unsigned int        coreCount;                          // L1-L2 system count
    unsigned int        masterCountPerCoreTLC;              // TL-C master count per core
    unsigned int        masterCountPerCoreTLUL;             // TL-UL master count per core

    uint64_t            ariInterval;                        // Auto Range Iteration interval
    uint64_t            ariTarget;                          // Auto Range Iteration target

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
