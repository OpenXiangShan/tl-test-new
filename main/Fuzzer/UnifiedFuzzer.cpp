//
// Created by dhn on 2025/05/20.
//

#include <algorithm>
#include <vector>

#include "../Utils/gravity_utility.hpp"
#include "../Utils/gravity_eventbus.hpp"

#include "../Base/TLEnum.hpp"

#include "../Events/TLSystemEvent.hpp"
#include "Fuzzer.h"

// Implementations of UnifiedFuzzer

bool UnifiedFuzzer::FuzzedAddress::IsInRange(paddr_t addr) noexcept
{
    return (addr >= this->base) && (addr < (this->base + this->size));
}

bool UnifiedFuzzer::FuzzedAddress::IsOverlappedRange(paddr_t base, size_t size) noexcept
{
    return std::max(this->base, base) <= std::min(this->base + this->size - 1, base + size - 1);
}

UnifiedFuzzer::UnifiedFuzzer(TLLocalConfig*         cfg,
                             tl_agent::CAgent**     cAgents,
                             size_t                 cAgentCount,
                             tl_agent::ULAgent**    ulAgents,
                             size_t                 ulAgentCountPerC,
                             tl_agent::MMIOAgent**  mmioAgents,
                             size_t                 mmioAgentCount)
    : cfg               (cfg)
    , cAgents           (cAgents)
    , ulAgents          (ulAgents)
    , mmioAgents        (mmioAgents)
    , cAgentCount       (cAgentCount)
    , ulAgentCountPerC  (ulAgentCountPerC)
    , mmioAgentCount    (mmioAgentCount)
    , fuzzedLocally     (new std::vector<FuzzedAddress>[cAgentCount])
    , fuzzedExclusively ()
    , monitoredGrant    (new std::unordered_set<paddr_t>[cAgentCount])
{
    this->startupInterval = 0;

    // Initialize and check context of Anvil mode
    if (IsMode(TLUnifiedSequenceMode::ANVIL))
        InitAnvil();

    // register global event listener
    Gravity::RegisterListener(Gravity::MakeListener(
        "UnifiedFuzzer::OnGrant",0,&UnifiedFuzzer::OnGrant,this
    ));
}

UnifiedFuzzer::~UnifiedFuzzer() noexcept
{
    Gravity::UnregisterListener("UnifiedFuzzer::OnGrant", &UnifiedFuzzer::OnGrant);

    delete[] fuzzedLocally;
    delete[] monitoredGrant;
}

void UnifiedFuzzer::OnGrant(tl_agent::GrantEvent& event) noexcept
{
    monitoredGrant[event.coreId].erase(event.address);
}

bool UnifiedFuzzer::IsMode(TLUnifiedSequenceMode mode) const noexcept
{
    return cfg->unifiedSequenceEnable && cfg->unifiedSequenceMode == mode;
}

void UnifiedFuzzer::InitAnvil()
{
    contextAnvil.counterU   = 0;
    contextAnvil.counterR   = 0;
    contextAnvil.counterB   = 0;
    contextAnvil.epoch      = 0;
    contextAnvil.ordinal    = {};

    decltype(cfg->unifiedSequenceModeAnvil)& cfgAnvil = cfg->unifiedSequenceModeAnvil;

    for (int i = 0; i < (cfgAnvil.widthB + 1); i++)
        contextAnvil.ordinal.push_back(i);

    tlsys_assert(cfgAnvil.size >= 8, Gravity::StringAppender(
        "configured 'size' = ", cfgAnvil.size, " of Anvil mode must be >= 8").ToString());

    tlsys_assert(cfgAnvil.widthB >= 1, Gravity::StringAppender(
        "configured 'widthB' = ", cfgAnvil.widthB, " of Anvil mode must be >= 1").ToString());

    tlsys_assert(cAgentCount >= (1 + cfgAnvil.widthB), Gravity::StringAppender(
        "coherent agent count = ", cAgentCount, " of Anvil mode must be >= (1 + widthB = ", (1 + cfgAnvil.widthB) , ")").ToString());

    tlsys_assert(!cfgAnvil.noise || cAgentCount >= 4, Gravity::StringAppender(
        "coherent agent count = ", cAgentCount, " of Anvil mode with noise generation enabled must be >= 4").ToString());

    if (!cfgAnvil.thresholdR)
        cfgAnvil.thresholdR = cfgAnvil.size * 3 / 4;

    if (!cfgAnvil.thresholdB)
        cfgAnvil.thresholdB = cfgAnvil.size * 3 / 4;

    // TODO: hello information
}

void UnifiedFuzzer::TickAnvil()
{
    if (!cfg->memoryEnable)
    {
        LogWarn(*cycles, Append("[UnifiedFuzzer] [Anvil] memory is not enabled, skipping Anvil tick").EndLine());
        return;
    }

    decltype(cfg->unifiedSequenceModeAnvil)& cfgAnvil = cfg->unifiedSequenceModeAnvil;

    bool stageUComplete = false;
    bool stageRComplete = false;
    bool stageBComplete = false;

    if (contextAnvil.counterU < cfgAnvil.size)
    {
        paddr_t address = cfg->memoryStart + contextAnvil.counterU * DATASIZE;
        size_t agentIndex = contextAnvil.ordinal[0];

        auto denial = cAgents[agentIndex]->do_acquirePerm(
            address, TLParamAcquire::NtoT, 0);
        
        if (denial == tl_agent::ActionDenial::CHANNEL_CONGESTION            // A not ready
         || denial == tl_agent::ActionDenial::CHANNEL_RESOURCE              // A ID not available
         || denial == tl_agent::ActionDenial::REJECTED_BY_PENDING_B)        // B Probe crossing
            ;
        else
            contextAnvil.counterU++;

        if (denial)
            monitoredGrant[agentIndex].insert(address);
    }
    else
        stageUComplete = true;

    if (contextAnvil.counterR < cfgAnvil.size)
    {
        paddr_t address = cfg->memoryStart + contextAnvil.counterR * DATASIZE;
        size_t agentIndex = contextAnvil.ordinal[0];

        if (contextAnvil.counterR >= contextAnvil.counterU)
            ; // don't skip and retry on running ahead
        else if (contextAnvil.counterU >= cfgAnvil.thresholdR && 
            monitoredGrant[agentIndex].find(address) == monitoredGrant[agentIndex].end())
        {
            auto denial = cAgents[agentIndex]->do_releaseDataAuto(
                address, 0, true, false);

            if (denial == tl_agent::ActionDenial::CHANNEL_CONGESTION        // C not ready
             || denial == tl_agent::ActionDenial::CHANNEL_RESOURCE          // C ID not available
             || denial == tl_agent::ActionDenial::REJECTED_BY_PENDING_B)    // B Probe crossing
                ;
            else
                contextAnvil.counterR++;
        }
    }
    else
        stageRComplete = true; 

    if (contextAnvil.counterB < cfgAnvil.size)
    {
        if (contextAnvil.counterR >= cfgAnvil.thresholdB)
        {
            for (int i = 0; i < cfgAnvil.widthB && contextAnvil.counterB < cfgAnvil.size; i++)
            {
                if (contextAnvil.counterB >= contextAnvil.counterR)
                {
                    // don't skip and retry on running ahead, emitting warning
                    LogWarn(*cycles, Hex().ShowBase()
                        .Append("[UnifiedFuzzer] [Anvil] sequence B running ahead of R at ")
                        .Append(cfg->memoryStart + contextAnvil.counterB * DATASIZE)
                        .Append(" with width ", cfgAnvil.widthB)
                        .Append(", might be a unexpected Pressure Relief")
                        .EndLine());

                    break;
                }

                {
                    // alternative AcquirePerm toT operation on B stage
                    auto denial = cAgents[contextAnvil.ordinal[1 + i]]->do_acquireBlock(
                        cfg->memoryStart + contextAnvil.counterB * DATASIZE, TLParamAcquire::NtoB, 0);
                
                    if (denial == tl_agent::ActionDenial::CHANNEL_CONGESTION
                     || denial == tl_agent::ActionDenial::CHANNEL_RESOURCE
                     || denial == tl_agent::ActionDenial::REJECTED_BY_PENDING_B)
                        ;
                    else
                    {
                        contextAnvil.counterB++;
                        continue;
                    }
                }

                if (ulAgentCountPerC)
                {
                    // alternative Get operation on B stage
                    auto denial = ulAgents[contextAnvil.ordinal[1 + i] * ulAgentCountPerC]->do_getAuto(
                        cfg->memoryStart + contextAnvil.counterB * DATASIZE);

                    if (denial == tl_agent::ActionDenial::CHANNEL_CONGESTION
                    || denial == tl_agent::ActionDenial::CHANNEL_RESOURCE)
                        ;
                    else
                        contextAnvil.counterB++;
                }
            }
        }
    }
    else
        stageBComplete = true;

    if (cfgAnvil.noise)
    {
        // TODO
    }

    if (stageUComplete && stageRComplete && stageBComplete)
    {
        contextAnvil.epoch++;

        if (cfgAnvil.epoch && contextAnvil.epoch >= cfgAnvil.epoch)
        {
            LogInfo(*cycles, Append("[UnifiedFuzzer] [Anvil] epoch ", contextAnvil.epoch, " completed and epoch target reached").EndLine());
            TLSystemFinishEvent().Fire();
        }
        else
            LogInfo(*cycles, Append("[UnifiedFuzzer] [Anvil] epoch ", contextAnvil.epoch, " completed").EndLine());

        contextAnvil.counterU = 0;
        contextAnvil.counterR = 0;
        contextAnvil.counterB = 0;

        std::next_permutation(contextAnvil.ordinal.begin(), contextAnvil.ordinal.end());
    }
}

void UnifiedFuzzer::tick()
{
    if (!cfg->unifiedSequenceEnable)
        return;

    if (this->startupInterval < cfg->startupCycle)
    {
        this->startupInterval++;
        return;
    }

    switch (cfg->unifiedSequenceMode)
    {
        case TLUnifiedSequenceMode::PASSIVE:
            break;

        case TLUnifiedSequenceMode::ANVIL:
            TickAnvil();
            break;

        default:
            break;
    }

    if (this->cfg->maximumCycle && (*cycles > this->cfg->maximumCycle))
    {
        LogInfo(*cycles, Append("Maximum cycle count exceeded").EndLine());
        TLSystemFinishEvent().Fire();
    }
}
