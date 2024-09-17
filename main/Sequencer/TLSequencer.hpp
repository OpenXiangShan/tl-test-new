#pragma once
//
// Created by Kumonda221 on 04/04/24
//

#ifndef TLC_TEST_SEQUENCER_H
#define TLC_TEST_SEQUENCER_H

#include <cstdint>

#include "../Utils/ScoreBoard.h"
#include "../Utils/ULScoreBoard.h"
#include "../Utils/Common.h"
#include "../TLAgent/ULAgent.h"
#include "../TLAgent/CAgent.h"
#include "../Fuzzer/Fuzzer.h"

/*
* Procedure sequence of a TileLink cycle:
*   Tick() -> FireChannel*() -> Tock() -> UpdateChannel*()
*/
class TLSequencer {
public:
    enum class State {
        NOT_INITIALIZED = 0,
        ALIVE,
        FAILED,
        FINISHED
    };

public:
    using BaseAgent         = tl_agent::BaseAgent;
    using ULAgent           = tl_agent::ULAgent;
    using CAgent            = tl_agent::CAgent;

    using ReqField          = tl_agent::ReqField;
    using RespField         = tl_agent::RespField;
    using EchoField         = tl_agent::EchoField;

    using BundleChannelA    = tl_agent::BundleChannelA<ReqField, EchoField, DATASIZE>;
    using BundleChannelB    = tl_agent::BundleChannelB;
    using BundleChannelC    = tl_agent::BundleChannelC<ReqField, EchoField, DATASIZE>;
    using BundleChannelD    = tl_agent::BundleChannelD<RespField, EchoField, DATASIZE>;
    using BundleChannelE    = tl_agent::BundleChannelE;
    using Bundle            = tl_agent::Bundle<ReqField, RespField, EchoField, DATASIZE>;

    using IOChannelA        = tl_agent::BundleChannelA<ReqField, EchoField, BEATSIZE>;
    using IOChannelB        = tl_agent::BundleChannelB;
    using IOChannelC        = tl_agent::BundleChannelC<ReqField, EchoField, BEATSIZE>;
    using IOChannelD        = tl_agent::BundleChannelD<RespField, EchoField, BEATSIZE>;
    using IOChannelE        = tl_agent::BundleChannelE;
    using IOPort            = tl_agent::Bundle<ReqField, RespField, EchoField, BEATSIZE>;

private:
    State                   state;

    GlobalBoard<paddr_t>*   globalBoard;
    UncachedBoard<paddr_t>* uncachedBoard;

    TLLocalConfig           config;

    BaseAgent**             agents;
    Fuzzer**                fuzzers;

    IOPort**                io;

    uint64_t                cycles;

public:
    TLSequencer() noexcept;
    ~TLSequencer() noexcept;

    const TLLocalConfig&    GetLocalConfig() const noexcept;

    State       GetState() const noexcept;
    bool        IsAlive() const noexcept;
    bool        IsFailed() const noexcept;
    bool        IsFinished() const noexcept;

    size_t      GetAgentCount() const noexcept;
    size_t      GetCAgentCount() const noexcept;
    size_t      GetULAgentCount() const noexcept;

    void        Initialize(const TLLocalConfig& cfg) noexcept;
    void        Finalize() noexcept;
    void        Tick(uint64_t cycles) noexcept;
    void        Tock() noexcept;

    IOPort*     IO() noexcept;
    IOPort&     IO(int deviceId) noexcept;
};


#endif //TLC_TEST_SEQUENCER_H
