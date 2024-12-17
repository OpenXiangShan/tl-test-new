#pragma once

#include <cstdint>


/*
* DPI functions to connect AXI Channel AW
*/
extern "C" void MemoryAXIPullChannelAW(
    uint8_t*            ready);

extern "C" void MemoryAXIPushChannelAW(
    const uint8_t       valid,
    const uint32_t      id,
    const uint64_t      addr,
    const uint8_t       burst,
    const uint8_t       size,
    const uint8_t       len);

/*
* DPI functions to connect AXI Channel W
*/
extern "C" void MemoryAXIPullChannelW(
    uint8_t*            ready);

extern "C" void MemoryAXIPushChannelW(
    const uint8_t       valid,
    const uint64_t      strb,
    const uint8_t       last,
    const uint64_t      data0,
    const uint64_t      data1,
    const uint64_t      data2,
    const uint64_t      data3);

/*
* DPI functions to connect AXI Channel B
*/
extern "C" void MemoryAXIPullChannelB(
    uint8_t*            valid,
    uint32_t*           id,
    uint8_t*            resp);

extern "C" void MemoryAXIPushChannelB(
    const uint8_t       ready);

/*
* DPI functions to connect AXI Channel AR
*/
extern "C" void MemoryAXIPullChannelAR(
    uint8_t*            ready);

extern "C" void MemoryAXIPushChannelAR(
    const uint8_t       valid,
    const uint32_t      id,
    const uint64_t      addr,
    const uint8_t       burst,
    const uint8_t       size,
    const uint8_t       len);

/*
* DPI functions to connect AXI Channel R
*/
extern "C" void MemoryAXIPullChannelR(
    uint8_t*            valid,
    uint32_t*           id,
    uint8_t*            resp,
    uint8_t*            last,
    uint64_t*           data0,
    uint64_t*           data1,
    uint64_t*           data2,
    uint64_t*           data3);

extern "C" void MemoryAXIPushChannelR(
    const uint8_t       ready);
