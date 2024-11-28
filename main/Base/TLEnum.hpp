#pragma once
//
// Created by Kumonda221 (Ding Haonan) on 2024/03/20
//

#ifndef TLC_TEST_TLENUM_H
#define TLC_TEST_TLENUM_H

#include <string>

#include "../Utils/gravity_utility.hpp"


enum class TLOpcodeA {
    PutFullData     = 0,
    PutPartialData  = 1,
    ArithmeticData  = 2,
    LogicalData     = 3,
    Get             = 4,
    Intent          = 5,
    AcquireBlock    = 6,
    AcquirePerm     = 7,
    CBOClean        = 8,
    CBOFlush        = 9,
    CBOInval        = 10
};

enum class TLOpcodeB {
    PutFullData     = 0,
    PutPartialData  = 1,
    ArithmeticData  = 2,
    LogicalData     = 3,
    Get             = 4,
    Intent          = 5,
    ProbeBlock      = 6,
    ProbePerm       = 7
};

enum class TLOpcodeC {
    AccessAck       = 0,
    AccessAckData   = 1,
    HintAck         = 2,
//                  = 3,
    ProbeAck        = 4,
    ProbeAckData    = 5,
    Release         = 6,
    ReleaseData     = 7
};

enum class TLOpcodeD {
    AccessAck       = 0,
    AccessAckData   = 1,
    HintAck         = 2,
//                  = 3,
    Grant           = 4,
    GrantData       = 5,
    ReleaseAck      = 6,
//                  = 7,
    CBOAck          = 8
};

enum class TLOpcodeE {

};


enum class TLPermDirection {
    toT = 0,
    toB,
    toN
};
using TLParamProbe          = TLPermDirection;
using TLParamGrant          = TLPermDirection;
using TLParamGrantData      = TLPermDirection;


enum class TLPermPromotion {
    NtoB = 0,
    NtoT,
    BtoT
};
using TLParamAcquire        = TLPermPromotion;


enum class TLPermDemotion {
    TtoB = 0,
    TtoN,
    BtoN,
    TtoT,
    BtoB,
    NtoN
};
using TLParamRelease        = TLPermDemotion;
using TLParamProbeAck       = TLPermDemotion;

enum class TLPermission {
    //
    NIL = -1,
    //
    INVALID = 0,
    BRANCH,
    TRUNK,
    TIP
};


//
template <class Ta, class Tb>
inline constexpr bool TLEnumEquals(Ta a, Tb b) noexcept
{
    return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
}

template <class Ta, class Tb, class... Tbs>
inline constexpr bool TLEnumEquals(Ta a, Tb b, Tbs... bs) noexcept
{
    return TLEnumEquals(a, b) || TLEnumEquals(a, bs...);
}


//
inline std::string TLOpcodeAToString(TLOpcodeA opcode) noexcept
{
    switch (opcode)
    {
        case TLOpcodeA::PutFullData:    return "PutFullData";
        case TLOpcodeA::PutPartialData: return "PutPartialData";
        case TLOpcodeA::ArithmeticData: return "ArithmeticData";
        case TLOpcodeA::LogicalData:    return "LogicalData";
        case TLOpcodeA::Get:            return "Get";
        case TLOpcodeA::Intent:         return "Intent";
        case TLOpcodeA::AcquireBlock:   return "AcquireBlock";
        case TLOpcodeA::AcquirePerm:    return "AcquirePerm";
        case TLOpcodeA::CBOClean:       return "CBOClean";
        case TLOpcodeA::CBOFlush:       return "CBOFlush";
        case TLOpcodeA::CBOInval:       return "CBOInval";
        default:
            return Gravity::StringAppender("<unknown_opcode_A:", uint64_t(opcode), ">").ToString();
    }
}

inline std::string TLOpcodeBToString(TLOpcodeB opcode) noexcept
{
    switch (opcode)
    {
        case TLOpcodeB::PutFullData:    return "PutFullData";
        case TLOpcodeB::PutPartialData: return "PutPartialData";
        case TLOpcodeB::ArithmeticData: return "ArithmeticData";
        case TLOpcodeB::LogicalData:    return "LogicalData";
        case TLOpcodeB::Get:            return "Get";
        case TLOpcodeB::Intent:         return "Intent";
        case TLOpcodeB::ProbeBlock:     return "ProbeBlock";
        case TLOpcodeB::ProbePerm:      return "ProbePerm";
        default:
            return Gravity::StringAppender("<unknown_opcode_B:", uint64_t(opcode), ">").ToString();
    }
}

inline std::string TLOpcodeCToString(TLOpcodeC opcode) noexcept
{
    switch (opcode)
    {
        case TLOpcodeC::AccessAck:      return "AccessAck";
        case TLOpcodeC::AccessAckData:  return "AccessAckData";
        case TLOpcodeC::HintAck:        return "HintAck";
        case TLOpcodeC::ProbeAck:       return "ProbeAck";
        case TLOpcodeC::ProbeAckData:   return "ProbeAckData";
        case TLOpcodeC::Release:        return "Release";
        case TLOpcodeC::ReleaseData:    return "ReleaseData";
        default:
            return Gravity::StringAppender("<unknown_opcode_C:", uint64_t(opcode), ">").ToString();
    }
}

inline std::string TLOpcodeDToString(TLOpcodeD opcode) noexcept
{
    switch (opcode)
    {
        case TLOpcodeD::AccessAck:      return "AccessAck";
        case TLOpcodeD::AccessAckData:  return "AccessAckData";
        case TLOpcodeD::HintAck:        return "HintAck";
        case TLOpcodeD::Grant:          return "Grant";
        case TLOpcodeD::GrantData:      return "GrantData";
        case TLOpcodeD::ReleaseAck:     return "ReleaseAck";
        case TLOpcodeD::CBOAck:         return "CBOAck";
        default:
            return Gravity::StringAppender("<unknown_opcode_D:", uint64_t(opcode), ">").ToString();
    }
}

inline std::string ProbeParamToString(TLParamProbe param) noexcept
{
    switch (param)
    {
        case TLParamProbe::toT:         return "toT";
        case TLParamProbe::toB:         return "toB";
        case TLParamProbe::toN:         return "toN";
        default:
            return Gravity::StringAppender("<unknown_probe_param:", uint64_t(param), ">").ToString();
    }
}

inline std::string GrantParamToString(TLParamGrant param) noexcept
{
    switch (param)
    {
        case TLParamGrant::toT:         return "toT";
        case TLParamGrant::toB:         return "toB";
        case TLParamGrant::toN:         return "toN";
        default:
            return Gravity::StringAppender("<unknown_grant_param:", uint64_t(param), ">").ToString();
    }
}

inline std::string GrantDataParamToString(TLParamGrantData param) noexcept
{
    switch (param)
    {
        case TLParamGrantData::toT:     return "toT";
        case TLParamGrantData::toB:     return "toB";
        case TLParamGrantData::toN:     return "toN";
        default:
            return Gravity::StringAppender("<unknown_grantdata_param:", uint64_t(param), ">").ToString();
    }
}

inline std::string AcquireParamToString(TLParamAcquire param) noexcept
{
    switch (param)
    {
        case TLParamAcquire::NtoB:      return "NtoB";
        case TLParamAcquire::NtoT:      return "NtoT";
        case TLParamAcquire::BtoT:      return "BtoT";
        default:
            return Gravity::StringAppender("<unknown_acquire_param:", uint64_t(param), ">").ToString();
    }
}

inline std::string ProbeAckParamToString(TLParamProbeAck param) noexcept
{
    switch (param)
    {
        case TLParamProbeAck::TtoB:     return "TtoB";
        case TLParamProbeAck::TtoN:     return "TtoN";
        case TLParamProbeAck::BtoN:     return "BtoN";
        case TLParamProbeAck::TtoT:     return "TtoT";
        case TLParamProbeAck::BtoB:     return "BtoB";
        case TLParamProbeAck::NtoN:     return "NtoN";
        default:
            return Gravity::StringAppender("<unknown_probeack_param:", uint64_t(param), ">").ToString();
    }
}

inline std::string ReleaseParamToString(TLParamRelease param) noexcept
{
    switch (param)
    {
        case TLParamRelease::TtoB:      return "TtoB";
        case TLParamRelease::TtoN:      return "TtoN";
        case TLParamRelease::BtoN:      return "BtoN";
        case TLParamRelease::TtoT:      return "TtoT";
        case TLParamRelease::BtoB:      return "BtoB";
        case TLParamRelease::NtoN:      return "NtoN";
        default:
            return Gravity::StringAppender("<unknown_release_param:", uint64_t(param), ">").ToString();
    }
}

inline std::string PrivilegeToString(TLPermission privilege) noexcept
{
    switch (privilege)
    {
        case TLPermission::INVALID:     return "INVALID";
        case TLPermission::BRANCH:      return "BRANCH";
        case TLPermission::TRUNK:       return "TRUNK";
        case TLPermission::TIP:         return "TIP";
        default:
            return Gravity::StringAppender("<unknown_privilege:", uint64_t(privilege), ">").ToString();
    }
}

#endif // TLC_TEST_TLENUM_H
