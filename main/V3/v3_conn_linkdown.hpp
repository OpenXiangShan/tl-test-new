#pragma once

#include <type_traits>

#include "../Utils/autoinclude.h"
#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"


#define MACRO_ConceptLinkDown_Pn(n) \
template<typename T> \
concept ConceptLinkDown_P##n = requires { \
    { std::declval<T>().io_linkdown_##n } -> std::convertible_to<uint8_t>; \
}; \
template<typename, typename = void> \
struct has_port_linkdown_##n : std::false_type {}; \
\
template<ConceptLinkDown_P##n T> \
struct has_port_linkdown_##n<T, void> : std::true_type {}; \
\
template<typename T> \
void V3PushLinkDown##n( \
    T*       top, \
    uint8_t& linkdown) requires has_port_linkdown_##n<T>::value \
{ \
    linkdown = top->io_linkdown_##n; \
} \
\
template<typename T> \
void V3PushLinkDown##n( \
    T*       top, \
    uint8_t& linkdown) \
{ }

MACRO_ConceptLinkDown_Pn(0);
MACRO_ConceptLinkDown_Pn(1);
MACRO_ConceptLinkDown_Pn(2);
MACRO_ConceptLinkDown_Pn(3);
MACRO_ConceptLinkDown_Pn(4);
MACRO_ConceptLinkDown_Pn(5);
MACRO_ConceptLinkDown_Pn(6);
MACRO_ConceptLinkDown_Pn(7);
MACRO_ConceptLinkDown_Pn(8);
MACRO_ConceptLinkDown_Pn(9);
MACRO_ConceptLinkDown_Pn(10);
MACRO_ConceptLinkDown_Pn(11);
MACRO_ConceptLinkDown_Pn(12);
MACRO_ConceptLinkDown_Pn(13);
MACRO_ConceptLinkDown_Pn(14);
MACRO_ConceptLinkDown_Pn(15);

#define MACRO_PushLinkDown_Pn(n) \
    if constexpr (has_port_linkdown_##n<VTestTop>::value) \
    { \
        auto& io = tltest->LinkDown(n); \
\
        V3PushLinkDown##n<VTestTop>(verilated, \
            io); \
    }

inline void V3PushLinkDown(VTestTop* verilated, TLSequencer* tltest)
{
    MACRO_PushLinkDown_Pn(0);
    MACRO_PushLinkDown_Pn(1);
    MACRO_PushLinkDown_Pn(2);
    MACRO_PushLinkDown_Pn(3);
    MACRO_PushLinkDown_Pn(4);
    MACRO_PushLinkDown_Pn(5);
    MACRO_PushLinkDown_Pn(6);
    MACRO_PushLinkDown_Pn(7);
    MACRO_PushLinkDown_Pn(8);
    MACRO_PushLinkDown_Pn(9);
    MACRO_PushLinkDown_Pn(10);
    MACRO_PushLinkDown_Pn(11);
    MACRO_PushLinkDown_Pn(12);
    MACRO_PushLinkDown_Pn(13);
    MACRO_PushLinkDown_Pn(14);
    MACRO_PushLinkDown_Pn(15);
}

template<typename T>
concept ConceptLinkDown = ConceptLinkDown_P0<T>
                       || ConceptLinkDown_P1<T>
                       || ConceptLinkDown_P2<T>
                       || ConceptLinkDown_P3<T>
                       || ConceptLinkDown_P4<T>
                       || ConceptLinkDown_P5<T>
                       || ConceptLinkDown_P6<T>
                       || ConceptLinkDown_P7<T>
                       || ConceptLinkDown_P8<T>
                       || ConceptLinkDown_P9<T>
                       || ConceptLinkDown_P10<T>
                       || ConceptLinkDown_P11<T>
                       || ConceptLinkDown_P12<T>
                       || ConceptLinkDown_P13<T>
                       || ConceptLinkDown_P14<T>
                       || ConceptLinkDown_P15<T>
                       ;

template<typename, typename = void>
struct has_port_linkdown : public std::false_type {};

template<ConceptLinkDown T>
struct has_port_linkdown<T, void> : public std::true_type {};
