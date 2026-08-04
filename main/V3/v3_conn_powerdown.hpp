#pragma once

#include <type_traits>

#include "../Utils/autoinclude.h"
#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"


#define MACRO_ConceptPowerDown_Pn(n) \
template<typename T> \
concept ConceptPowerDown_P##n = requires { \
    { std::declval<T>().io_powerdown_##n##_flushAll     } -> std::convertible_to<uint8_t>; \
    { std::declval<T>().io_powerdown_##n##_flushAllDone } -> std::convertible_to<uint8_t>; \
    { std::declval<T>().io_powerdown_##n##_cpuHalt      } -> std::convertible_to<uint8_t>; \
}; \
template<typename, typename = void> \
struct has_port_power_down_##n : std::false_type {}; \
\
template<ConceptPowerDown_P##n T> \
struct has_port_power_down_##n<T, void> : std::true_type {}; \
\
template<typename T> \
void V3PullPowerDown##n( \
    T*      top, \
    uint8_t flushAll, \
    uint8_t cpuHalt) requires has_port_power_down_##n<T>::value \
{ \
    top->io_powerdown_##n##_flushAll = flushAll; \
    top->io_powerdown_##n##_cpuHalt  = cpuHalt; \
} \
\
template<typename T> \
void V3PullPowerDown##n( \
    T*      top, \
    uint8_t flushAll, \
    uint8_t cpuHalt) \
{ } \
\
template<typename T> \
void V3PushPowerDown##n( \
    T*       top, \
    uint8_t& flushAllDone) requires has_port_power_down_##n<T>::value \
{ \
    flushAllDone = top->io_powerdown_##n##_flushAllDone; \
} \
\
template<typename T> \
void V3PushPowerDown##n( \
    T*       top, \
    uint8_t& flushAllDone) \
{ }

MACRO_ConceptPowerDown_Pn(0)
MACRO_ConceptPowerDown_Pn(1)
MACRO_ConceptPowerDown_Pn(2)
MACRO_ConceptPowerDown_Pn(3)
MACRO_ConceptPowerDown_Pn(4)
MACRO_ConceptPowerDown_Pn(5)
MACRO_ConceptPowerDown_Pn(6)
MACRO_ConceptPowerDown_Pn(7)
MACRO_ConceptPowerDown_Pn(8)
MACRO_ConceptPowerDown_Pn(9)
MACRO_ConceptPowerDown_Pn(10)
MACRO_ConceptPowerDown_Pn(11)
MACRO_ConceptPowerDown_Pn(12)
MACRO_ConceptPowerDown_Pn(13)
MACRO_ConceptPowerDown_Pn(14)
MACRO_ConceptPowerDown_Pn(15)

#define MACRO_PullPowerDown_Pn(n) \
    if constexpr (has_port_power_down_##n<VTestTop>::value) \
    { \
        auto& io = tltest->PowerDown(n); \
\
        V3PullPowerDown##n<VTestTop>(verilated, \
            io.flushAll, \
            io.cpuHalt); \
    } \

inline void V3PullPowerDown(VTestTop* verilated, TLSequencer* tltest)
{
    MACRO_PullPowerDown_Pn(0);
    MACRO_PullPowerDown_Pn(1);
    MACRO_PullPowerDown_Pn(2);
    MACRO_PullPowerDown_Pn(3);
    MACRO_PullPowerDown_Pn(4);
    MACRO_PullPowerDown_Pn(5);
    MACRO_PullPowerDown_Pn(6);
    MACRO_PullPowerDown_Pn(7);
    MACRO_PullPowerDown_Pn(8);
    MACRO_PullPowerDown_Pn(9);
    MACRO_PullPowerDown_Pn(10);
    MACRO_PullPowerDown_Pn(11);
    MACRO_PullPowerDown_Pn(12);
    MACRO_PullPowerDown_Pn(13);
    MACRO_PullPowerDown_Pn(14);
    MACRO_PullPowerDown_Pn(15);
}

#define MACRO_PushPowerDown_Pn(n) \
    if constexpr (has_port_power_down_##n<VTestTop>::value) \
    { \
        auto& io = tltest->PowerDown(n); \
\
        V3PushPowerDown##n<VTestTop>(verilated, \
            io.flushAllDone); \
    }

inline void V3PushPowerDown(VTestTop* verilated, TLSequencer* tltest)
{
    MACRO_PushPowerDown_Pn(0);
    MACRO_PushPowerDown_Pn(1);
    MACRO_PushPowerDown_Pn(2);
    MACRO_PushPowerDown_Pn(3);
    MACRO_PushPowerDown_Pn(4);
    MACRO_PushPowerDown_Pn(5);
    MACRO_PushPowerDown_Pn(6);
    MACRO_PushPowerDown_Pn(7);
    MACRO_PushPowerDown_Pn(8);
    MACRO_PushPowerDown_Pn(9);
    MACRO_PushPowerDown_Pn(10);
    MACRO_PushPowerDown_Pn(11);
    MACRO_PushPowerDown_Pn(12);
    MACRO_PushPowerDown_Pn(13);
    MACRO_PushPowerDown_Pn(14);
    MACRO_PushPowerDown_Pn(15);
}

template<typename T> \
concept ConceptPowerDown = ConceptPowerDown_P0<T>
                        || ConceptPowerDown_P1<T>
                        || ConceptPowerDown_P2<T>
                        || ConceptPowerDown_P3<T>
                        || ConceptPowerDown_P4<T>
                        || ConceptPowerDown_P5<T>
                        || ConceptPowerDown_P6<T>
                        || ConceptPowerDown_P7<T>
                        || ConceptPowerDown_P8<T>
                        || ConceptPowerDown_P9<T>
                        || ConceptPowerDown_P10<T>
                        || ConceptPowerDown_P11<T>
                        || ConceptPowerDown_P12<T>
                        || ConceptPowerDown_P13<T>
                        || ConceptPowerDown_P14<T>
                        || ConceptPowerDown_P15<T>
                        ;

template<typename, typename = void>
struct has_port_power_down : public std::false_type {};

template<ConceptPowerDown T>
struct has_port_power_down<T, void> : public std::true_type {};
