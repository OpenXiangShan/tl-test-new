#include <limits>

#include "CAgent.h"

namespace tl_agent {

    uint64_t CAgent::cacheline_set_index(paddr_t address) const
    {
        if (cfg->cAgentCacheSets == 0)
            return 0;

        const uint64_t line = static_cast<uint64_t>(address) / DATASIZE;
        return line % cfg->cAgentCacheSets;
    }

    size_t CAgent::resident_cacheline_count(uint64_t set_idx, int alias) const
    {
        size_t count = 0;

        for (const auto& kv : localBoard->get())
        {
            const paddr_t addr = kv.first;
            const auto& entry = kv.second;
            if (cacheline_set_index(addr) != set_idx)
                continue;
            if (entry->status[alias] != S_VALID)
                continue;
            if (TLEnumEquals(entry->privilege[alias], TLPermission::INVALID))
                continue;

            if (entry->status[alias] == S_VALID)
                count++;
        }

        return count;
    }

    bool CAgent::pick_lru_release_candidate(uint64_t set_idx, int alias, paddr_t& victim_addr) const
    {
        bool found = false;
        uint64_t oldest_ts = std::numeric_limits<uint64_t>::max();

        for (const auto& kv : localBoard->get())
        {
            const paddr_t addr = kv.first;
            const auto& entry = kv.second;

            if (cacheline_set_index(addr) != set_idx)
                continue;
            if (entry->status[alias] != S_VALID)
                continue;
            if (TLEnumEquals(entry->privilege[alias], TLPermission::INVALID))
                continue;

            if (!found || entry->time_stamp < oldest_ts)
            {
                oldest_ts = entry->time_stamp;
                victim_addr = addr;
                found = true;
            }
        }

        return found;
    }

    bool CAgent::try_evict_for_capacity(paddr_t incoming_addr, int alias)
    {
        const uint64_t sets = cfg->cAgentCacheSets;
        const uint64_t ways = cfg->cAgentCacheWays;
        if (sets == 0 || ways == 0)
            return true;

        if (localBoard->haskey(incoming_addr))
            return true;

        const uint64_t set_idx = cacheline_set_index(incoming_addr);
        if (resident_cacheline_count(set_idx, alias) < ways)
            return true;

        paddr_t victim = 0;
        if (!pick_lru_release_candidate(set_idx, alias, victim))
            return true;

        auto denial = do_releaseDataAuto(victim, alias, false, false);

        // If eviction is accepted or temporarily blocked by channel/resource/inflight,
        // defer current Acquire and retry later.
        if (denial.ordinal == ActionDenial::ACCEPTED.ordinal
            || denial.ordinal == ActionDenial::CHANNEL_CONGESTION.ordinal
            || denial.ordinal == ActionDenial::CHANNEL_RESOURCE.ordinal
            || denial.ordinal == ActionDenial::REJECTED_BY_PENDING_B.ordinal
            || denial.ordinal == ActionDenial::REJECTED_BY_INFLIGHT.ordinal)
            return false;

        // For terminal denials (e.g., MISS/PERMISSION), fail open to avoid livelock.
        return true;
    }

} // namespace tl_agent
