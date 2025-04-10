#pragma once

#include "../Utils/autoinclude.h"
#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"

namespace V3::Memory {

    inline void PullChannelAW(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        verilated->mem_axi_0_aw_ready       = port.aw.ready;
    }

    inline void PushChannelAW(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        port.aw.valid   = verilated->mem_axi_0_aw_valid;
        port.aw.id      = verilated->mem_axi_0_aw_bits_id;
        port.aw.addr    = verilated->mem_axi_0_aw_bits_addr;
        port.aw.burst   = verilated->mem_axi_0_aw_bits_burst;
        port.aw.size    = verilated->mem_axi_0_aw_bits_size;
        port.aw.len     = verilated->mem_axi_0_aw_bits_len;
    }

    inline void PullChannelW(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        verilated->mem_axi_0_w_ready        = port.w.ready;
    }

    inline void PushChannelW(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        port.w.valid            = verilated->mem_axi_0_w_valid;
        port.w.strb             = verilated->mem_axi_0_w_bits_strb;
        port.w.last             = verilated->mem_axi_0_w_bits_last;
        port.w.data->data[0]    =  verilated->mem_axi_0_w_bits_data[0]        & 0xFF;
        port.w.data->data[1]    = (verilated->mem_axi_0_w_bits_data[0] >>  8) & 0xFF;
        port.w.data->data[2]    = (verilated->mem_axi_0_w_bits_data[0] >> 16) & 0xFF;
        port.w.data->data[3]    = (verilated->mem_axi_0_w_bits_data[0] >> 24) & 0xFF;
        port.w.data->data[4]    =  verilated->mem_axi_0_w_bits_data[1]        & 0xFF;
        port.w.data->data[5]    = (verilated->mem_axi_0_w_bits_data[1] >>  8) & 0xFF;
        port.w.data->data[6]    = (verilated->mem_axi_0_w_bits_data[1] >> 16) & 0xFF;
        port.w.data->data[7]    = (verilated->mem_axi_0_w_bits_data[1] >> 24) & 0xFF;
        port.w.data->data[8]    =  verilated->mem_axi_0_w_bits_data[2]        & 0xFF;
        port.w.data->data[9]    = (verilated->mem_axi_0_w_bits_data[2] >>  8) & 0xFF;
        port.w.data->data[10]   = (verilated->mem_axi_0_w_bits_data[2] >> 16) & 0xFF;
        port.w.data->data[11]   = (verilated->mem_axi_0_w_bits_data[2] >> 24) & 0xFF;
        port.w.data->data[12]   =  verilated->mem_axi_0_w_bits_data[3]        & 0xFF;
        port.w.data->data[13]   = (verilated->mem_axi_0_w_bits_data[3] >>  8) & 0xFF;
        port.w.data->data[14]   = (verilated->mem_axi_0_w_bits_data[3] >> 16) & 0xFF;
        port.w.data->data[15]   = (verilated->mem_axi_0_w_bits_data[3] >> 24) & 0xFF;
        port.w.data->data[16]   =  verilated->mem_axi_0_w_bits_data[4]        & 0xFF;
        port.w.data->data[17]   = (verilated->mem_axi_0_w_bits_data[4] >>  8) & 0xFF;
        port.w.data->data[18]   = (verilated->mem_axi_0_w_bits_data[4] >> 16) & 0xFF;
        port.w.data->data[19]   = (verilated->mem_axi_0_w_bits_data[4] >> 24) & 0xFF;
        port.w.data->data[20]   =  verilated->mem_axi_0_w_bits_data[5]        & 0xFF;
        port.w.data->data[21]   = (verilated->mem_axi_0_w_bits_data[5] >>  8) & 0xFF;
        port.w.data->data[22]   = (verilated->mem_axi_0_w_bits_data[5] >> 16) & 0xFF;
        port.w.data->data[23]   = (verilated->mem_axi_0_w_bits_data[5] >> 24) & 0xFF;
        port.w.data->data[24]   =  verilated->mem_axi_0_w_bits_data[6]        & 0xFF;
        port.w.data->data[25]   = (verilated->mem_axi_0_w_bits_data[6] >>  8) & 0xFF;
        port.w.data->data[26]   = (verilated->mem_axi_0_w_bits_data[6] >> 16) & 0xFF;
        port.w.data->data[27]   = (verilated->mem_axi_0_w_bits_data[6] >> 24) & 0xFF;
        port.w.data->data[28]   =  verilated->mem_axi_0_w_bits_data[7]        & 0xFF;
        port.w.data->data[29]   = (verilated->mem_axi_0_w_bits_data[7] >>  8) & 0xFF;
        port.w.data->data[30]   = (verilated->mem_axi_0_w_bits_data[7] >> 16) & 0xFF;
        port.w.data->data[31]   = (verilated->mem_axi_0_w_bits_data[7] >> 24) & 0xFF;
    }

    inline void PullChannelB(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        verilated->mem_axi_0_b_valid        = port.b.valid;
        verilated->mem_axi_0_b_bits_id      = port.b.id;
        verilated->mem_axi_0_b_bits_resp    = port.b.resp;
    }

    inline void PushChannelB(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        port.b.ready    = verilated->mem_axi_0_b_ready;
    }

    inline void PullChannelAR(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        verilated->mem_axi_0_ar_ready       = port.ar.ready;
    }

    inline void PushChannelAR(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        port.ar.valid   = verilated->mem_axi_0_ar_valid;
        port.ar.id      = verilated->mem_axi_0_ar_bits_id;
        port.ar.addr    = verilated->mem_axi_0_ar_bits_addr;
        port.ar.burst   = verilated->mem_axi_0_ar_bits_burst;
        port.ar.size    = verilated->mem_axi_0_ar_bits_size;
        port.ar.len     = verilated->mem_axi_0_ar_bits_len;
    }

    inline void PullChannelR(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        verilated->mem_axi_0_r_valid        = port.r.valid;
        verilated->mem_axi_0_r_bits_id      = port.r.id;
        verilated->mem_axi_0_r_bits_resp    = port.r.resp;
        verilated->mem_axi_0_r_bits_last    = port.r.last;
        verilated->mem_axi_0_r_bits_data[0] = (uint32_t(port.r.data->data[0]))
                                            | (uint32_t(port.r.data->data[1])   <<  8)
                                            | (uint32_t(port.r.data->data[2])   << 16)
                                            | (uint32_t(port.r.data->data[3])   << 24);
        verilated->mem_axi_0_r_bits_data[1] = (uint32_t(port.r.data->data[4]))
                                            | (uint32_t(port.r.data->data[5])   <<  8)
                                            | (uint32_t(port.r.data->data[6])   << 16)
                                            | (uint32_t(port.r.data->data[7])   << 24);
        verilated->mem_axi_0_r_bits_data[2] = (uint32_t(port.r.data->data[8]))
                                            | (uint32_t(port.r.data->data[9])   <<  8)
                                            | (uint32_t(port.r.data->data[10])  << 16)
                                            | (uint32_t(port.r.data->data[11])  << 24);
        verilated->mem_axi_0_r_bits_data[3] = (uint32_t(port.r.data->data[12]))
                                            | (uint32_t(port.r.data->data[13])  <<  8)
                                            | (uint32_t(port.r.data->data[14])  << 16)
                                            | (uint32_t(port.r.data->data[15])  << 24);
        verilated->mem_axi_0_r_bits_data[4] = (uint32_t(port.r.data->data[16]))
                                            | (uint32_t(port.r.data->data[17])  <<  8)
                                            | (uint32_t(port.r.data->data[18])  << 16)
                                            | (uint32_t(port.r.data->data[19])  << 24);
        verilated->mem_axi_0_r_bits_data[5] = (uint32_t(port.r.data->data[20]))
                                            | (uint32_t(port.r.data->data[21])  <<  8)
                                            | (uint32_t(port.r.data->data[22])  << 16)
                                            | (uint32_t(port.r.data->data[23])  << 24);
        verilated->mem_axi_0_r_bits_data[6] = (uint32_t(port.r.data->data[24]))
                                            | (uint32_t(port.r.data->data[25])  <<  8)
                                            | (uint32_t(port.r.data->data[26])  << 16)
                                            | (uint32_t(port.r.data->data[27])  << 24);
        verilated->mem_axi_0_r_bits_data[7] = (uint32_t(port.r.data->data[28]))
                                            | (uint32_t(port.r.data->data[29])  <<  8)
                                            | (uint32_t(port.r.data->data[30])  << 16)
                                            | (uint32_t(port.r.data->data[31])  << 24);
    }

    inline void PushChannelR(VTestTop* verilated, TLSequencer* tltest)
    {
        TLSequencer::MemoryAXIPort& port = tltest->MemoryAXI(0);
        port.r.ready    = verilated->mem_axi_0_r_ready;
    }
}
