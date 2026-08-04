#include "TLAgentEvent.hpp"
//
// Created by Kumonda221 on 2025-05-28
//


// Implementation of: class TLAgentAcquireCompleteEvent
TLAgentAcquireCompleteEvent::TLAgentAcquireCompleteEvent(uint64_t sysId, paddr_t address) noexcept
    : sysId     (sysId)
    , address   (address)
{ }


// Implementation of: class TLAgentFlushAllPhaseChangeEvent
TLAgentFlushAllPhaseChangeEvent::TLAgentFlushAllPhaseChangeEvent(uint64_t sysId, int prevPhase, int nextPhase) noexcept
    : sysId     (sysId)
    , prevPhase (prevPhase)
    , nextPhase (nextPhase)
{ }
