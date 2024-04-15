#include "tilelink_dpi.hpp"

#include "svdpi.h"

#include "../Sequencer/TLSequencer.hpp"


/*
* IMPORTANT NOTICE:
* The following rules must be obeyed in DPI-C calling host:
*   1. Procedure sequence of a TileLink cycle:
*       SystemTick() -> PushChannel*() -> SystemTock() -> PullChannel*()
*   2. Procedure sequence overall:
*       SystemInitialize() -> [< TileLink cycles >] -> SystemFinalize()
*   3. Procedure contraint:
*       A -> B -> C -> E -> D
*/
static TLSequencer* passive;


/*
* DPI system interactions : Initialize
*/
extern "C" void TileLinkSystemInitialize()
{
    glbl.cfg.verbose                = true;
    glbl.cfg.verbose_detailed_dpi   = false;

    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(Append("tl_test system initialized").EndLine());
    
    passive = new TLSequencer;
    passive->Initialize();
}

/*
* DPI system interactions : Finalize
*/
extern "C" void TileLinkSystemFinalize()
{
    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(Append("tl-test system finalized").EndLine());

    passive->Finalize();
    delete passive;
}

/*
* DPI system interactions : Tick
*/
extern "C" void TileLinkSystemTick(
    const uint64_t      cycles)
{
    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(Append("[tl-test-passive] tick at cycle: ", cycles));

    passive->Tick(cycles);
}

/*
* DPI system interactions : Tock
*/
extern "C" void TileLinkSystemTock()
{
    passive->Tock();

    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(Append("[tl-test-passive] tock"));
}


/*
* DPI function to connect TileLink Channel A
*/
extern "C" void TileLinkPushChannelA(
    const int           deviceId,
    const uint8_t       ready)
{
    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.a.ready = ready;

    passive->FireChannelA();
}

extern "C" void TileLinkPullChannelA(
    const int           deviceId,
    uint8_t*            valid,
    uint8_t*            opcode,
    uint8_t*            param,
    uint8_t*            size,
    uint8_t*            source,
    uint64_t*           address,
    uint8_t*            user_alias,
    uint32_t*           mask,
    uint64_t*           data0,
    uint64_t*           data1,
    uint64_t*           data2,
    uint64_t*           data3,
    uint8_t*            corrupt)
{
    passive->UpdateChannelA();

    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(Append("[tl-test-passive] UpdateChannelA()"));

    TLSequencer::IOPort& port = passive->IO(deviceId);

    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(Append("[tl-test-passive] Fetched IO: deviceId: ", deviceId));

    *valid      =  port.a.valid;
    *opcode     =  port.a.opcode;
    *param      =  port.a.param;
    *size       =  port.a.size;
    *source     =  port.a.source;
    *address    =  port.a.address;
    *user_alias =  port.a.alias;
    *mask       =  port.a.mask;
    *data0      = (uint64_t(port.a.data->data[0]))
                | (uint64_t(port.a.data->data[1])   << 8)
                | (uint64_t(port.a.data->data[2])   << 16)
                | (uint64_t(port.a.data->data[3])   << 24)
                | (uint64_t(port.a.data->data[4])   << 32)
                | (uint64_t(port.a.data->data[5])   << 40)
                | (uint64_t(port.a.data->data[6])   << 48)
                | (uint64_t(port.a.data->data[7])   << 56);
    *data1      = (uint64_t(port.a.data->data[8]))
                | (uint64_t(port.a.data->data[9])   << 8)
                | (uint64_t(port.a.data->data[10])  << 16)
                | (uint64_t(port.a.data->data[11])  << 24)
                | (uint64_t(port.a.data->data[12])  << 32)
                | (uint64_t(port.a.data->data[13])  << 40)
                | (uint64_t(port.a.data->data[14])  << 48)
                | (uint64_t(port.a.data->data[15])  << 56);
    *data2      = (uint64_t(port.a.data->data[16]))
                | (uint64_t(port.a.data->data[17])  << 8)
                | (uint64_t(port.a.data->data[18])  << 16)
                | (uint64_t(port.a.data->data[19])  << 24)
                | (uint64_t(port.a.data->data[20])  << 32)
                | (uint64_t(port.a.data->data[21])  << 40)
                | (uint64_t(port.a.data->data[22])  << 48)
                | (uint64_t(port.a.data->data[23])  << 56);
    *data3      = (uint64_t(port.a.data->data[24]))
                | (uint64_t(port.a.data->data[25])  << 8)
                | (uint64_t(port.a.data->data[26])  << 16)
                | (uint64_t(port.a.data->data[27])  << 24)
                | (uint64_t(port.a.data->data[28])  << 32)
                | (uint64_t(port.a.data->data[29])  << 40)
                | (uint64_t(port.a.data->data[30])  << 48)
                | (uint64_t(port.a.data->data[31])  << 56);
    *corrupt    =  0;

    if (glbl.cfg.verbose_detailed_dpi)
        TLTP_LOG_GLOBAL(ShowBase()
            .Append("[tl-test-passive] PullChannelA:").EndLine()
            .Dec().Append("   -> valid    = ", (uint32_t)*valid     ).EndLine()
            .Hex().Append("   -> opcode   = ", (uint32_t)*opcode    ).EndLine()
            .Hex().Append("   -> address  = ", (uint64_t)*address   ).EndLine()
            .EndLine());
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
    const uint8_t       source,
    const uint64_t      address,
    const uint32_t      mask,
    const uint64_t      data0,
    const uint64_t      data1,
    const uint64_t      data2,
    const uint64_t      data3,
    const uint8_t       corrupt)
{
    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.b.valid        = valid;
    port.b.opcode       = opcode;
    port.b.param        = param;
    port.b.size         = size;
    port.b.address      = address;
    port.b.source       = source;
    port.b.alias        = data0;

    passive->FireChannelB();
}

extern "C" void TileLinkPullChannelB(
    const int           deviceId,
    uint8_t*            ready)
{
    passive->UpdateChannelB();

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
    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.c.ready = ready;

    passive->FireChannelC();
}

extern "C" void TileLinkPullChannelC(
    const int           deviceId,
    uint8_t*            valid,
    uint8_t*            opcode,
    uint8_t*            param,
    uint8_t*            size,
    uint8_t*            source,
    uint64_t*           address,
    uint8_t*            user_alias,
    uint64_t*           data0,
    uint64_t*           data1,
    uint64_t*           data2,
    uint64_t*           data3,
    uint8_t*            corrupt)
{
    passive->UpdateChannelC();

    TLSequencer::IOPort& port = passive->IO(deviceId);
    *valid      =  port.c.valid;
    *opcode     =  port.c.opcode;
    *param      =  port.c.param;
    *size       =  port.c.size;
    *source     =  port.c.source;
    *address    =  port.c.address;
    *user_alias =  0;
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
    const uint8_t       source,
    const uint8_t       sink,
    const uint8_t       denied,
    const uint64_t      data0,
    const uint64_t      data1,
    const uint64_t      data2,
    const uint64_t      data3,
    const uint8_t       corrupt)
{
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

    passive->FireChannelD();
}

extern "C" void TileLinkPullChannelD(
    const int           deviceId,
    uint8_t*            ready)
{
    passive->UpdateChannelD();

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
    TLSequencer::IOPort& port = passive->IO(deviceId);
    port.e.ready = ready;

    passive->FireChannelE();
}

extern "C" void TileLinkPullChannelE(
    const int           deviceId,
    uint8_t*            valid,
    uint8_t*            sink)
{
    passive->UpdateChannelE();

    TLSequencer::IOPort& port = passive->IO(deviceId);
    *valid = port.e.valid;
    *sink  = port.e.sink;
}
//
