#pragma once
//
// Created by Kumonda221 on 2024-05-06
//

#ifndef TLC_TEST_EVENTS_SYSTEM_EVENT_H
#define TLC_TEST_EVENTS_SYSTEM_EVENT_H

#include "../Utils/gravity_eventbus.hpp"
#include "../Sequencer/TLSequencer.hpp"


/*
* TL-Test TileLink subsystem initialization events
*/
class TLSystemInitializationPreEvent : public Gravity::Event<TLSystemInitializationPreEvent> 
{ 
public:
    TLLocalConfig*  const config;

public:
    TLSystemInitializationPreEvent(TLLocalConfig* config) noexcept;
};

class TLSystemInitializationPostEvent : public Gravity::Event<TLSystemInitializationPostEvent>
{ 
public:
    TLSequencer*    const system;

public:
    TLSystemInitializationPostEvent(TLSequencer* system) noexcept;
};
//


/*
* TL-Test TileLink subsystem finalization events
* -----------------------------------------------------------------------
* IMPORTANT:
*   1. Finalization events are required to be handled silently
*      (no throw, no termination, no assertion failure, ...).
*/
class TLSystemFinalizationPreEvent : public Gravity::Event<TLSystemFinalizationPreEvent>
{ 
public:
    TLSequencer*    const system;

public:
    TLSystemFinalizationPreEvent(TLSequencer* system) noexcept;
};

class TLSystemFinalizationPostEvent : public Gravity::Event<TLSystemFinalizationPostEvent>
{ };
//



/*
* TL-Test TileLink subsystem finish events
* -----------------------------------------------------------------------
* This event is called by TL-Test components to finish the main routine
* and never called by master routine (e.g. TLSequencer).
*/
class TLSystemFinishEvent : public Gravity::Event<TLSystemFinishEvent>
{ };
//


/*
* TL-Test TileLink subsystem finished events
* -----------------------------------------------------------------------
* This event is called by main routine to broadcast the finished state of
* main routine and never called by TL-Test components.
* It is guaranteed that all circuit activity was ended after this event.
*/
class TLSystemFinishedEvent : public Gravity::Event<TLSystemFinishedEvent>
{ };

class TLSystemFailedEvent : public Gravity::Event<TLSystemFailedEvent>
{ };
//


#endif // TLC_TEST_EVENTS_SYSTEM_EVENT_H

