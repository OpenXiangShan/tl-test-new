#pragma once

#include <cstdint>


/*
* DPI functions to connect AXI Channel AW
*/
extern "C" void MemoryAXIPullChannelAW(
    const int           portId,
    uint8_t*            ready);

extern "C" void MemoryAXIPushChannelAW(
    const int           portId,
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
    const int           portId,
    uint8_t*            ready);

extern "C" void MemoryAXIPushChannelW(
    const int           portId,
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
    const int           portId,
    uint8_t*            valid,
    uint32_t*           id,
    uint8_t*            resp);

extern "C" void MemoryAXIPushChannelB(
    const int           portId,
    const uint8_t       ready);

/*
* DPI functions to connect AXI Channel AR
*/
extern "C" void MemoryAXIPullChannelAR(
    const int           portId,
    uint8_t*            ready);

extern "C" void MemoryAXIPushChannelAR(
    const int           portId,
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
    const int           portId,
    uint8_t*            valid,
    uint32_t*           id,
    uint8_t*            resp,
    uint8_t*            last,
    uint64_t*           data0,
    uint64_t*           data1,
    uint64_t*           data2,
    uint64_t*           data3);

extern "C" void MemoryAXIPushChannelR(
    const int           portId,
    const uint8_t       ready);
