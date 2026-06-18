#pragma once

#include <type_traits>

#include "../Utils/autoinclude.h"
#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"


#define MACRO_ConceptResetSep_Pn(n) \
template<typename T> \
concept ConceptResetSep_P##n = requires { \
    { std::declval<T>().io_resetsep_##n } -> std::convertible_to<uint8_t>; \
}; \
template<typename, typename = void> \
struct has_port_resetsep_##n : std::false_type {}; \
\
template<ConceptResetSep_P##n T> \
struct has_port_resetsep_##n<T, void> : std::true_type {}; \
\
template<typename T> \
void V3PullResetSep##n( \
    T*      top, \
    uint8_t resetsep) requires has_port_resetsep_##n<T>::value \
{ \
    top->io_resetsep_##n = resetsep; \
} \
\
template<typename T> \
void V3PullResetSep##n( \
    T*      top, \
    uint8_t resetsep) \
{ }

MACRO_ConceptResetSep_Pn(0);
MACRO_ConceptResetSep_Pn(1);
MACRO_ConceptResetSep_Pn(2);
MACRO_ConceptResetSep_Pn(3);
MACRO_ConceptResetSep_Pn(4);
MACRO_ConceptResetSep_Pn(5);
MACRO_ConceptResetSep_Pn(6);
MACRO_ConceptResetSep_Pn(7);
MACRO_ConceptResetSep_Pn(8);
MACRO_ConceptResetSep_Pn(9);
MACRO_ConceptResetSep_Pn(10);
MACRO_ConceptResetSep_Pn(11);
MACRO_ConceptResetSep_Pn(12);
MACRO_ConceptResetSep_Pn(13);
MACRO_ConceptResetSep_Pn(14);
MACRO_ConceptResetSep_Pn(15);

#define MACRO_PullResetSep_Pn(n) \
    if constexpr (has_port_resetsep_##n<VTestTop>::value) \
    { \
        auto& io = tltest->ResetSep(n); \
\
        V3PullResetSep##n<VTestTop>(verilated, \
            io); \
    }

inline void V3PullResetSep(VTestTop* verilated, TLSequencer* tltest)
{
    MACRO_PullResetSep_Pn(0);
    MACRO_PullResetSep_Pn(1);
    MACRO_PullResetSep_Pn(2);
    MACRO_PullResetSep_Pn(3);
    MACRO_PullResetSep_Pn(4);
    MACRO_PullResetSep_Pn(5);
    MACRO_PullResetSep_Pn(6);
    MACRO_PullResetSep_Pn(7);
    MACRO_PullResetSep_Pn(8);
    MACRO_PullResetSep_Pn(9);
    MACRO_PullResetSep_Pn(10);
    MACRO_PullResetSep_Pn(11);
    MACRO_PullResetSep_Pn(12);
    MACRO_PullResetSep_Pn(13);
    MACRO_PullResetSep_Pn(14);
    MACRO_PullResetSep_Pn(15);
}

template<typename T>
concept ConceptResetSep = ConceptResetSep_P0<T>
                       || ConceptResetSep_P1<T>
                       || ConceptResetSep_P2<T>
                       || ConceptResetSep_P3<T>
                       || ConceptResetSep_P4<T>
                       || ConceptResetSep_P5<T>
                       || ConceptResetSep_P6<T>
                       || ConceptResetSep_P7<T>
                       || ConceptResetSep_P8<T>
                       || ConceptResetSep_P9<T>
                       || ConceptResetSep_P10<T>
                       || ConceptResetSep_P11<T>
                       || ConceptResetSep_P12<T>
                       || ConceptResetSep_P13<T>
                       || ConceptResetSep_P14<T>
                       || ConceptResetSep_P15<T>
                       ;

template<typename, typename = void>
struct has_port_resetsep : public std::false_type {};

template<ConceptResetSep T>
struct has_port_resetsep<T, void> : public std::true_type {};