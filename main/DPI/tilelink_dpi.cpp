#include "tilelink_dpi.hpp"
#include "memory_dpi.hpp"

#include "tilelink_dpi_configs.hpp"


#include "../Sequencer/TLSequencer.hpp"
#include "../Plugins/PluginManager.hpp"

#include "../System/TLSystem.hpp"

#include <functional>



/*
* =======================================================================
* IMPORTANT NOTICE:
* -----------------------------------------------------------------------
* The following rules must be obeyed in DPI-C calling host:
*   1. Procedure sequence of a TileLink cycle:
*       SystemTick() -> PushChannel*() -> SystemTock() -> PullChannel*()
*   2. Procedure sequence overall:
*       SystemInitialize() -> [< TileLink cycles >] -> SystemFinalize()
*   3. Procedure contraint:
*       A -> B -> C -> E -> D
* -----------------------------------------------------------------------
* Three states are designed for TL-Test TileLink subsystem:
*   - NOT_INITIALIZED   : system not initialized
*   - ALIVE             : system on the run
*   - FAILED            : system failed and stopped
*   - FINISHED          : system reached pre-designed finish
* Important rules for utilizing the subsystem:
*   1. Finalizing procedure is never called by the subsystem itself, and 
*      should be manually called outside the TL-Test TileLink subsystem 
*      (e.g. in Test Top).
*   2. Initializing procedure of plug-in systems of TL-Test TileLink 
*      subsystem should be called from the TLSystemInitialization<*>Event. 
*   3. Finalizing procedure of plug-in systems of TL-Test TileLink
*      subsystem should be called from the TLSystemFinalization<*>Event.
* =======================================================================
*/
static TLSequencer*     passive = nullptr;

/*
*/
static PluginManager*   plugins = nullptr;


/*
* DPI system interactions : IsAlive
*/
extern "C" int TileLinkSystemIsAlive()
{
    return passive->IsAlive();
}

/*
* DPI system interactions : IsFailed
*/
extern "C" int TileLinkSystemIsFailed()
{
    return passive->IsFailed();
}

/*
* DPI system interactions : IsFinished
*/
extern "C" int TileLinkSystemIsFinished()
{
    return passive->IsFinished();
}


/*
* DPI system interactions : Initialize
*/
extern "C" void TileLinkSystemInitialize()
{
    TLInitialize(&passive, &plugins, [](TLLocalConfig& cfg) -> void {
        cfg.coreCount               = TLTEST_LOCAL_CORE_COUNT;
        cfg.masterCountPerCoreTLC   = TLTEST_LOCAL_MASTER_COUNT_PER_CORE_TLC;
        cfg.masterCountPerCoreTLUL  = TLTEST_LOCAL_MASTER_COUNT_PER_CORE_TLUL;
    });
}

/*
* DPI system interactions : Finalize
*/
extern "C" void TileLinkSystemFinalize()
{
    TLFinalize(&passive, &plugins);
}

/*
* DPI system interactions : Tick
*/
extern "C" void TileLinkSystemTick(
    const uint64_t      cycles)
{
    passive->Tick(cycles);
}

/*
* DPI system interactions : Tock
*/
extern "C" void TileLinkSystemTock()
{
    passive->Tock();
}


/*
* DPI function to connect TileLink Channel A
*/
extern "C" void TileLinkPushChannelA(
    const int           deviceId,
    const uint8_t       ready)
{
    if (!passive)
        return;

    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.a.ready = ready;
}

extern "C" void TileLinkPullChannelA(
    const int           deviceId,
    uint8_t*            valid,
    uint8_t*            opcode,
    uint8_t*            param,
    uint8_t*            size,
    uint64_t*           source,
    uint64_t*           address,
    uint8_t*            user_needHint,
    uint64_t*           user_vaddr,
    uint8_t*            user_alias,
    uint32_t*           mask,
    uint64_t*           data0,
    uint64_t*           data1,
    uint64_t*           data2,
    uint64_t*           data3,
    uint8_t*            corrupt)
{
    if (!passive)
    {
        *valid          = 0;
        *opcode         = 0;
        *param          = 0;
        *size           = 0;
        *source         = 0;
        *address        = 0;
        *user_needHint  = 0;
        *user_vaddr     = 0;
        *user_alias     = 0;
        *mask           = 0;
        *data0          = 0;
        *data1          = 0;
        *data2          = 0;
        *data3          = 0;
        *corrupt        = 0;
        return;
    }

    TLSequencer::IOPort& port = passive->IO(deviceId);

    *valid          =  port.a.valid;
    *opcode         =  port.a.opcode;
    *param          =  port.a.param;
    *size           =  port.a.size;
    *source         =  port.a.source;
    *address        =  port.a.address;
    *user_needHint  =  port.a.needHint;
    *user_vaddr     =  port.a.vaddr;
    *user_alias     =  port.a.alias;
    *mask           =  port.a.mask;
    *data0          = (uint64_t(port.a.data->data[0]))
                    | (uint64_t(port.a.data->data[1])   << 8)
                    | (uint64_t(port.a.data->data[2])   << 16)
                    | (uint64_t(port.a.data->data[3])   << 24)
                    | (uint64_t(port.a.data->data[4])   << 32)
                    | (uint64_t(port.a.data->data[5])   << 40)
                    | (uint64_t(port.a.data->data[6])   << 48)
                    | (uint64_t(port.a.data->data[7])   << 56);
    *data1          = (uint64_t(port.a.data->data[8]))
                    | (uint64_t(port.a.data->data[9])   << 8)
                    | (uint64_t(port.a.data->data[10])  << 16)
                    | (uint64_t(port.a.data->data[11])  << 24)
                    | (uint64_t(port.a.data->data[12])  << 32)
                    | (uint64_t(port.a.data->data[13])  << 40)
                    | (uint64_t(port.a.data->data[14])  << 48)
                    | (uint64_t(port.a.data->data[15])  << 56);
    *data2          = (uint64_t(port.a.data->data[16]))
                    | (uint64_t(port.a.data->data[17])  << 8)
                    | (uint64_t(port.a.data->data[18])  << 16)
                    | (uint64_t(port.a.data->data[19])  << 24)
                    | (uint64_t(port.a.data->data[20])  << 32)
                    | (uint64_t(port.a.data->data[21])  << 40)
                    | (uint64_t(port.a.data->data[22])  << 48)
                    | (uint64_t(port.a.data->data[23])  << 56);
    *data3          = (uint64_t(port.a.data->data[24]))
                    | (uint64_t(port.a.data->data[25])  << 8)
                    | (uint64_t(port.a.data->data[26])  << 16)
                    | (uint64_t(port.a.data->data[27])  << 24)
                    | (uint64_t(port.a.data->data[28])  << 32)
                    | (uint64_t(port.a.data->data[29])  << 40)
                    | (uint64_t(port.a.data->data[30])  << 48)
                    | (uint64_t(port.a.data->data[31])  << 56);
    *corrupt        =  0;
}
//


/*
* DPI function to connect TileLink Channel B
*/
extern "C" void TileLinkPushChannelB(
    const int           deviceId,
    const uint8_t       valid,
    const uint8_t       opcode,
    const uint8_t       param,
    const uint8_t       size,
    const uint64_t      source,
    const uint64_t      address,
    const uint32_t      mask,
    const uint64_t      data0,
    const uint64_t      data1,
    const uint64_t      data2,
    const uint64_t      data3,
    const uint8_t       corrupt)
{
    if (!passive)
        return;

    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.b.valid        = valid;
    port.b.opcode       = opcode;
    port.b.param        = param;
    port.b.size         = size;
    port.b.address      = address;
    port.b.source       = source;
    port.b.alias        = data0;
}

extern "C" void TileLinkPullChannelB(
    const int           deviceId,
    uint8_t*            ready)
{
    if (!passive)
    {
        *ready = 0;
        return;
    }

    TLSequencer::IOPort& port = passive->IO(deviceId);
    *ready = port.b.ready;
}
//


/*
* DPI function to connect TileLink Channel C
*/
extern "C" void TileLinkPushChannelC(
    const int           deviceId,
    const uint8_t       ready)
{
    if (!passive)
        return;

    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.c.ready = ready;
}

extern "C" void TileLinkPullChannelC(
    const int           deviceId,
    uint8_t*            valid,
    uint8_t*            opcode,
    uint8_t*            param,
    uint8_t*            size,
    uint64_t*           source,
    uint64_t*           address,
    uint8_t*            user_needHint,
    uint64_t*           user_vaddr,
    uint8_t*            user_alias,
    uint64_t*           data0,
    uint64_t*           data1,
    uint64_t*           data2,
    uint64_t*           data3,
    uint8_t*            corrupt)
{
    if (!passive)
    {
        *valid          = 0;
        *opcode         = 0;
        *param          = 0;
        *size           = 0;
        *source         = 0;
        *address        = 0;
        *user_needHint  = 0;
        *user_vaddr     = 0;
        *user_alias     = 0;
        *data0          = 0;
        *data1          = 0;
        *data2          = 0;
        *data3          = 0;
        *corrupt        = 0;
        return;
    }

    TLSequencer::IOPort& port = passive->IO(deviceId);
    *valid          =  port.c.valid;
    *opcode         =  port.c.opcode;
    *param          =  port.c.param;
    *size           =  port.c.size;
    *source         =  port.c.source;
    *address        =  port.c.address;
    *user_needHint  =  port.c.needHint;
    *user_vaddr     =  port.c.vaddr;
    *user_alias     =  0;
    *data0      = (uint64_t(port.c.data->data[0]))
                | (uint64_t(port.c.data->data[1])   <<  8)
                | (uint64_t(port.c.data->data[2])   << 16)
                | (uint64_t(port.c.data->data[3])   << 24)
                | (uint64_t(port.c.data->data[4])   << 32)
                | (uint64_t(port.c.data->data[5])   << 40)
                | (uint64_t(port.c.data->data[6])   << 48)
                | (uint64_t(port.c.data->data[7])   << 56);
    *data1      = (uint64_t(port.c.data->data[8]))
                | (uint64_t(port.c.data->data[9])   <<  8)
                | (uint64_t(port.c.data->data[10])  << 16)
                | (uint64_t(port.c.data->data[11])  << 24)
                | (uint64_t(port.c.data->data[12])  << 32)
                | (uint64_t(port.c.data->data[13])  << 40)
                | (uint64_t(port.c.data->data[14])  << 48)
                | (uint64_t(port.c.data->data[15])  << 56);
    *data2      = (uint64_t(port.c.data->data[16]))
                | (uint64_t(port.c.data->data[17])  <<  8)
                | (uint64_t(port.c.data->data[18])  << 16)
                | (uint64_t(port.c.data->data[19])  << 24)
                | (uint64_t(port.c.data->data[20])  << 32)
                | (uint64_t(port.c.data->data[21])  << 40)
                | (uint64_t(port.c.data->data[22])  << 48)
                | (uint64_t(port.c.data->data[23])  << 56);
    *data3      = (uint64_t(port.c.data->data[24]))
                | (uint64_t(port.c.data->data[25])  <<  8)
                | (uint64_t(port.c.data->data[26])  << 16)
                | (uint64_t(port.c.data->data[27])  << 24)
                | (uint64_t(port.c.data->data[28])  << 32)
                | (uint64_t(port.c.data->data[29])  << 40)
                | (uint64_t(port.c.data->data[30])  << 48)
                | (uint64_t(port.c.data->data[31])  << 56);
    *corrupt    =  0;
}
//


/*
* DPI function to connect TileLink Channel D
*/
extern "C" void TileLinkPushChannelD(
    const int           deviceId,
    const uint8_t       valid,
    const uint8_t       opcode,
    const uint8_t       param,
    const uint8_t       size,
    const uint64_t      source,
    const uint64_t      sink,
    const uint8_t       denied,
    const uint64_t      data0,
    const uint64_t      data1,
    const uint64_t      data2,
    const uint64_t      data3,
    const uint8_t       corrupt)
{
    if (!passive)
        return;

    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.d.valid            = valid;
    port.d.opcode           = opcode;
    port.d.param            = param;
    port.d.size             = size;
    port.d.source           = source;
    port.d.sink             = sink;
    port.d.denied           = denied;
    port.d.data->data[0]    =  data0        & 0xFF;
    port.d.data->data[1]    = (data0 >>  8) & 0xFF;
    port.d.data->data[2]    = (data0 >> 16) & 0xFF;
    port.d.data->data[3]    = (data0 >> 24) & 0xFF;
    port.d.data->data[4]    = (data0 >> 32) & 0xFF;
    port.d.data->data[5]    = (data0 >> 40) & 0xFF;
    port.d.data->data[6]    = (data0 >> 48) & 0xFF;
    port.d.data->data[7]    = (data0 >> 56) & 0xFF;
    port.d.data->data[8]    =  data1        & 0xFF;
    port.d.data->data[9]    = (data1 >>  8) & 0xFF;
    port.d.data->data[10]   = (data1 >> 16) & 0xFF;
    port.d.data->data[11]   = (data1 >> 24) & 0xFF;
    port.d.data->data[12]   = (data1 >> 32) & 0xFF;
    port.d.data->data[13]   = (data1 >> 40) & 0xFF;
    port.d.data->data[14]   = (data1 >> 48) & 0xFF;
    port.d.data->data[15]   = (data1 >> 56) & 0xFF;
    port.d.data->data[16]   =  data2        & 0xFF;
    port.d.data->data[17]   = (data2 >>  8) & 0xFF;
    port.d.data->data[18]   = (data2 >> 16) & 0xFF;
    port.d.data->data[19]   = (data2 >> 24) & 0xFF;
    port.d.data->data[20]   = (data2 >> 32) & 0xFF;
    port.d.data->data[21]   = (data2 >> 40) & 0xFF;
    port.d.data->data[22]   = (data2 >> 48) & 0xFF;
    port.d.data->data[23]   = (data2 >> 56) & 0xFF;
    port.d.data->data[24]   =  data3        & 0xFF;
    port.d.data->data[25]   = (data3 >>  8) & 0xFF;
    port.d.data->data[26]   = (data3 >> 16) & 0xFF;
    port.d.data->data[27]   = (data3 >> 24) & 0xFF;
    port.d.data->data[28]   = (data3 >> 32) & 0xFF;
    port.d.data->data[29]   = (data3 >> 40) & 0xFF;
    port.d.data->data[30]   = (data3 >> 48) & 0xFF;
    port.d.data->data[31]   = (data3 >> 56) & 0xFF;
    port.d.corrupt          = corrupt;
}

extern "C" void TileLinkPullChannelD(
    const int           deviceId,
    uint8_t*            ready)
{
    if (!passive)
    {
        *ready = 0;
        return;
    }

    TLSequencer::IOPort& port = passive->IO(deviceId);
    *ready = port.d.ready;
}
//


/*
* DPI function to connect TileLink Channel E
*/
extern "C" void TileLinkPushChannelE(
    const int           deviceId,
    const uint8_t       ready)
{
    if (!passive)
        return;

    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.e.ready = ready;
}

extern "C" void TileLinkPullChannelE(
    const int           deviceId,
    uint8_t*            valid,
    uint64_t*           sink)
{
    if (!passive)
    {
        *valid = 0;
        *sink  = 0;
        return;
    }

    TLSequencer::IOPort& port = passive->IO(deviceId);
    *valid = port.e.valid;
    *sink  = port.e.sink;
}
//


/*
* DPI function to connect TileLink MMIO Channel A
*/
extern "C" void TileLinkMMIOPushChannelA(
    const int           deviceId,
    const uint8_t       ready)
{
    if (!passive)
        return;

    TLSequencer::MMIOPort& port = passive->MMIO(deviceId);
    port.a.ready = ready;
}

extern "C" void TileLinkMMIOPullChannelA(
    const int           deviceId,
    uint8_t*            valid,
    uint8_t*            opcode,
    uint8_t*            param,
    uint8_t*            size,
    uint64_t*           source,
    uint64_t*           address,
    uint8_t*            mask,
    uint64_t*           data,
    uint8_t*            corrupt)
{
    if (!passive)
    {
        *valid   = 0;
        *opcode  = 0;
        *param   = 0;
        *size    = 0;
        *source  = 0;
        *address = 0;
        *mask    = 0;
        *data    = 0;
        *corrupt = 0;
        return;
    }

    TLSequencer::MMIOPort& port = passive->MMIO(deviceId);
    *valid          = port.a.valid;
    *opcode         = port.a.opcode;
    *param          = port.a.param;
    *size           = port.a.size;
    *source         = port.a.source;
    *address        = port.a.address;
    *mask           = port.a.mask;
    *data           = (uint64_t(port.a.data->data[0]))
                    | (uint64_t(port.a.data->data[1])   <<  8)
                    | (uint64_t(port.a.data->data[2])   << 16)
                    | (uint64_t(port.a.data->data[3])   << 24)
                    | (uint64_t(port.a.data->data[4])   << 32)
                    | (uint64_t(port.a.data->data[5])   << 40)
                    | (uint64_t(port.a.data->data[6])   << 48)
                    | (uint64_t(port.a.data->data[7])   << 56);
    *corrupt        = port.a.corrupt;
}
//


/*
* DPI function to connect TileLink MMIO Channel D
*/
extern "C" void TileLinkMMIOPushChannelD(
    const int           deviceId,
    const uint8_t       valid,
    const uint8_t       opcode,
    const uint8_t       param,
    const uint8_t       size,
    const uint64_t      source,
    const uint64_t      sink,
    const uint8_t       denied,
    const uint64_t      data,
    const uint8_t       corrupt)
{
    if (!passive)
        return;

    TLSequencer::MMIOPort& port = passive->MMIO(deviceId);
    port.d.valid            = valid;
    port.d.opcode           = opcode;
    port.d.param            = param;
    port.d.size             = size;
    port.d.source           = source;
    port.d.sink             = sink;
    port.d.denied           = denied;
    port.d.data->data[0]    =  data        & 0xFF;
    port.d.data->data[1]    = (data >>  8) & 0xFF;
    port.d.data->data[2]    = (data >> 16) & 0xFF;
    port.d.data->data[3]    = (data >> 24) & 0xFF;
    port.d.data->data[4]    = (data >> 32) & 0xFF;
    port.d.data->data[5]    = (data >> 40) & 0xFF;
    port.d.data->data[6]    = (data >> 48) & 0xFF;
    port.d.data->data[7]    = (data >> 56) & 0xFF;
    port.d.corrupt          = corrupt;
}

extern "C" void TileLinkMMIOPullChannelD(
    const int           deviceId,
    uint8_t*            ready)
{
    if (!passive)
    {
        *ready = 0;
        return;
    }

    TLSequencer::MMIOPort& port = passive->MMIO(deviceId);
    *ready          = port.d.ready;
}
//


/*
* DPI functions to connect Memory Backend AXI Channel AW
*/
extern "C" void MemoryAXIPullChannelAW(
    const int           portId,
    uint8_t*            ready)
{
    if (!passive)
    {
        *ready = 0;
        return;
    }

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    *ready = port.aw.ready;
}

extern "C" void MemoryAXIPushChannelAW(
    const int           portId,
    const uint8_t       valid,
    const uint32_t      id,
    const uint64_t      addr,
    const uint8_t       burst,
    const uint8_t       size,
    const uint8_t       len)
{
    if (!passive)
        return;

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    port.aw.valid   = valid;
    port.aw.id      = id;
    port.aw.addr    = addr;
    port.aw.burst   = burst;
    port.aw.size    = size;
    port.aw.len     = len;
}
//


/*
* DPI functions to connect Memory Backend AXI Channel W
*/
extern "C" void MemoryAXIPullChannelW(
    const int           portId,
    uint8_t*            ready)
{
    if (!passive)
    {
        *ready = 0;
        return;
    }

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    *ready = port.w.ready;
}

extern "C" void MemoryAXIPushChannelW(
    const int           portId,
    const uint8_t       valid,
    const uint64_t      strb,
    const uint8_t       last,
    const uint64_t      data0,
    const uint64_t      data1,
    const uint64_t      data2,
    const uint64_t      data3)
{
    if (!passive)
        return;

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    port.w.valid            = valid;
    port.w.strb             = strb;
    port.w.last             = last;
    port.w.data->data[0]    =  data0        & 0xFF;
    port.w.data->data[1]    = (data0 >>  8) & 0xFF;
    port.w.data->data[2]    = (data0 >> 16) & 0xFF;
    port.w.data->data[3]    = (data0 >> 24) & 0xFF;
    port.w.data->data[4]    = (data0 >> 32) & 0xFF;
    port.w.data->data[5]    = (data0 >> 40) & 0xFF;
    port.w.data->data[6]    = (data0 >> 48) & 0xFF;
    port.w.data->data[7]    = (data0 >> 56) & 0xFF;
    port.w.data->data[8]    =  data1        & 0xFF;
    port.w.data->data[9]    = (data1 >>  8) & 0xFF;
    port.w.data->data[10]   = (data1 >> 16) & 0xFF;
    port.w.data->data[11]   = (data1 >> 24) & 0xFF;
    port.w.data->data[12]   = (data1 >> 32) & 0xFF;
    port.w.data->data[13]   = (data1 >> 40) & 0xFF;
    port.w.data->data[14]   = (data1 >> 48) & 0xFF;
    port.w.data->data[15]   = (data1 >> 56) & 0xFF;
    port.w.data->data[16]   =  data2        & 0xFF;
    port.w.data->data[17]   = (data2 >>  8) & 0xFF;
    port.w.data->data[18]   = (data2 >> 16) & 0xFF;
    port.w.data->data[19]   = (data2 >> 24) & 0xFF;
    port.w.data->data[20]   = (data2 >> 32) & 0xFF;
    port.w.data->data[21]   = (data2 >> 40) & 0xFF;
    port.w.data->data[22]   = (data2 >> 48) & 0xFF;
    port.w.data->data[23]   = (data2 >> 56) & 0xFF;
    port.w.data->data[24]   =  data3        & 0xFF;
    port.w.data->data[25]   = (data3 >>  8) & 0xFF;
    port.w.data->data[26]   = (data3 >> 16) & 0xFF;
    port.w.data->data[27]   = (data3 >> 24) & 0xFF;
    port.w.data->data[28]   = (data3 >> 32) & 0xFF;
    port.w.data->data[29]   = (data3 >> 40) & 0xFF;
    port.w.data->data[30]   = (data3 >> 48) & 0xFF;
    port.w.data->data[31]   = (data3 >> 56) & 0xFF;
}
//


/*
* DPI functions to connect Memory Backend AXI Channel B
*/
extern "C" void MemoryAXIPullChannelB(
    const int           portId,
    uint8_t*            valid,
    uint32_t*           id,
    uint8_t*            resp)
{
    if (!passive)
    {
        *valid  = 0;
        *id     = 0;
        *resp   = 0;
        return;
    }

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    *valid  = port.b.valid;
    *id     = port.b.id;
    *resp   = port.b.resp;
}

extern "C" void MemoryAXIPushChannelB(
    const int           portId,
    const uint8_t       ready)
{
    if (!passive)
        return;

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    port.b.ready = ready;
}
//


/*
* DPI functions to connect Memory Backend AXI Channel AR
*/
extern "C" void MemoryAXIPullChannelAR(
    const int           portId,
    uint8_t*            ready)
{
    if (!passive)
    {
        *ready = 0;
        return;
    }

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    *ready = port.ar.ready;
}

extern "C" void MemoryAXIPushChannelAR(
    const int           portId,
    const uint8_t       valid,
    const uint32_t      id,
    const uint64_t      addr,
    const uint8_t       burst,
    const uint8_t       size,
    const uint8_t       len)
{
    if (!passive)
        return;

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    port.ar.valid   = valid;
    port.ar.id      = id;
    port.ar.addr    = addr;
    port.ar.burst   = burst;
    port.ar.size    = size;
    port.ar.len     = len;
}
//


/*
* DPI functions to connect Memory Backend AXI Channel R
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
    uint64_t*           data3)
{
    if (!passive)
    {
        *valid = 0;
        *id    = 0;
        *resp  = 0;
        *last  = 0;
        *data0 = 0;
        *data1 = 0;
        *data2 = 0;
        *data3 = 0;
        return;
    }

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    *valid      = port.r.valid;
    *id         = port.r.id;
    *resp       = port.r.resp;
    *last       = port.r.last;
    *data0      = (uint64_t(port.r.data->data[0]))
                | (uint64_t(port.r.data->data[1])   <<  8)
                | (uint64_t(port.r.data->data[2])   << 16)
                | (uint64_t(port.r.data->data[3])   << 24)
                | (uint64_t(port.r.data->data[4])   << 32)
                | (uint64_t(port.r.data->data[5])   << 40)
                | (uint64_t(port.r.data->data[6])   << 48)
                | (uint64_t(port.r.data->data[7])   << 56);
    *data1      = (uint64_t(port.r.data->data[8]))
                | (uint64_t(port.r.data->data[9])   <<  8)
                | (uint64_t(port.r.data->data[10])  << 16)
                | (uint64_t(port.r.data->data[11])  << 24)
                | (uint64_t(port.r.data->data[12])  << 32)
                | (uint64_t(port.r.data->data[13])  << 40)
                | (uint64_t(port.r.data->data[14])  << 48)
                | (uint64_t(port.r.data->data[15])  << 56);
    *data2      = (uint64_t(port.r.data->data[16]))
                | (uint64_t(port.r.data->data[17])  <<  8)
                | (uint64_t(port.r.data->data[18])  << 16)
                | (uint64_t(port.r.data->data[19])  << 24)
                | (uint64_t(port.r.data->data[20])  << 32)
                | (uint64_t(port.r.data->data[21])  << 40)
                | (uint64_t(port.r.data->data[22])  << 48)
                | (uint64_t(port.r.data->data[23])  << 56);
    *data3      = (uint64_t(port.r.data->data[24]))
                | (uint64_t(port.r.data->data[25])  <<  8)
                | (uint64_t(port.r.data->data[26])  << 16)
                | (uint64_t(port.r.data->data[27])  << 24)
                | (uint64_t(port.r.data->data[28])  << 32)
                | (uint64_t(port.r.data->data[29])  << 40)
                | (uint64_t(port.r.data->data[30])  << 48)
                | (uint64_t(port.r.data->data[31])  << 56);
}

extern "C" void MemoryAXIPushChannelR(
    const int           portId,
    const uint8_t       ready)
{
    if (!passive)
        return;

    TLSequencer::MemoryAXIPort& port = passive->MemoryAXI(portId);
    port.r.ready = ready;
}
//
