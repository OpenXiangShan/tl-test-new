#pragma once
//
// Created by Kumonda221 on 2025-05-28
//

#ifndef TLC_TEST_EVENTS_AGENT_EVENT_H
#define TLC_TEST_EVENTS_AGENT_EVENT_H

#include "../Utils/gravity_eventbus.hpp"
#include "../Utils/Common.h"


/*
* TL-Test TileLink agent events
*/
class TLAgentAcquireCompleteEvent : public Gravity::Event<TLAgentAcquireCompleteEvent>
{
public:
    uint64_t        sysId;         // System ID
    paddr_t         address;       // Address of the acquired block

public:
    TLAgentAcquireCompleteEvent(uint64_t sysId, paddr_t address) noexcept;
};


class TLAgentFlushAllPhaseChangeEvent : public Gravity::Event<TLAgentFlushAllPhaseChangeEvent>
{
public:
    uint64_t        sysId;
    int             prevPhase;
    int             nextPhase;

public:
    TLAgentFlushAllPhaseChangeEvent(uint64_t sysId, int prevPhase, int nextPhase) noexcept;
};
//


#endif // TLC_TEST_EVENTS_AGENT_EVENT_H