/*
* AXI Channel AW
*/
import "DPI-C" function void MemoryAXIPullChannelAW (
    output  byte                ready
);

function void SvMemoryAXIPullChannelAW (
    output  logic               ready
);

    if (1) begin
        
        MemoryAXIPullChannelAW(
            ready
        );
    end

endfunction

import "DPI-C" function void MemoryAXIPushChannelAW (
    input   byte                valid,
    input   int                 id,
    input   longint             addr,
    input   byte                burst,
    input   byte                size,
    input   byte                len
);

function void SvMemoryAXIPushChannelAW (
    input   logic               resetn,
    input   logic               valid,
    input   logic [10:0]        id,
    input   logic [47:0]        addr,
    input   logic [1:0]         burst,
    input   logic [2:0]         size,
    input   logic [7:0]         len
);

    guard_valid:    assert (!resetn || !$isunknown(valid)) else $fatal("MemoryAXIPushChannelAW: 'valid' is unknown");

    guard_id:       assert (!resetn || !valid || !$isunknown(id     )) else $fatal("MemoryAXIPushChannelAW: 'id' is unknown");
    guard_addr:     assert (!resetn || !valid || !$isunknown(addr   )) else $fatal("MemoryAXIPushChannelAW: 'addr' is unknown");
    guard_burst:    assert (!resetn || !valid || !$isunknown(burst  )) else $fatal("MemoryAXIPushChannelAW: 'burst' is unknown");
    guard_size:     assert (!resetn || !valid || !$isunknown(size   )) else $fatal("MemoryAXIPushChannelAW: 'size' is unknown");
    guard_len:      assert (!resetn || !valid || !$isunknown(len    )) else $fatal("MemoryAXIPushChannelAW: 'len' is unknown");

    if (resetn) begin

        MemoryAXIPushChannelAW (
            valid,
            id,
            addr,
            burst,
            size,
            len
        );
    end

endfunction


/*
* AXI Channel W
*/
import "DPI-C" function void MemoryAXIPullChannelW (
    output  byte                ready
);

function void SvMemoryAXIPullChannelW (
    output  logic               ready
);

    if (1) begin

        MemoryAXIPullChannelW (
            ready
        );
    end

endfunction

import "DPI-C" function void MemoryAXIPushChannelW (
    input   byte                valid,
    input   longint             strb,
    input   byte                last,
    input   longint             data0,
    input   longint             data1,
    input   longint             data2,
    input   longint             data3
);

function void SvMemoryAXIPushChannelW (
    input   logic               resetn,
    input   logic               valid,
    input   logic [31:0]        strb,
    input   logic               last,
    input   logic [255:0]       data
);

    guard_valid:    assert (!resetn || !$isunknown(valid)) else $fatal("MemoryAXIPushChannelW: 'valid' is unknown");

    guard_strb:     assert (!resetn || !valid || !$isunknown(strb   )) else $fatal("MemoryAXIPushChannelW: 'strb' is unknown");
    guard_last:     assert (!resetn || !valid || !$isunknown(last   )) else $fatal("MemoryAXIPushChannelW: 'last' is unknown");

    if (resetn) begin

        MemoryAXIPushChannelW ( 
            valid,
            strb,
            last,
            data[63:0],
            data[127:64],
            data[191:128],
            data[255:192]
        );
    end

endfunction


/*
* AXI Channel B
*/
import "DPI-C" function void MemoryAXIPullChannelB (
    output  byte                valid,
    output  int                 id,
    output  byte                resp
);

function void SvMemoryAXIPullChannelB (
    output  logic               valid,
    output  logic [10:0]        id,
    output  logic [1:0]         resp
);

    if (1) begin

        MemoryAXIPullChannelB (
            valid,
            id,
            resp
        );
    end

endfunction

import "DPI-C" function void MemoryAXIPushChannelB (
    input   byte                ready
);

function void SvMemoryAXIPushChannelB (
    input   logic               resetn,
    input   logic               ready
);

    guard_ready:    assert (!resetn || !$isunknown(ready)) else $fatal("MemoryAXIPushChannelB: 'ready' is unknown");

    if (resetn) begin

        MemoryAXIPushChannelB (
            ready
        );
    end

endfunction


/*
* AXI Channel AR
*/
import "DPI-C" function void MemoryAXIPullChannelAR (
    output  byte                ready
);

function void SvMemoryAXIPullChannelAR (
    output  logic               ready
);

    if (1) begin

        MemoryAXIPullChannelAR (
            ready
        );
    end

endfunction

import "DPI-C" function void MemoryAXIPushChannelAR (
    input   byte                valid,
    input   int                 id,
    input   longint             addr,
    input   byte                burst,
    input   byte                size,
    input   byte                len
);

function void SvMemoryAXIPushChannelAR (
    input   logic               resetn,
    input   logic               valid,
    input   logic [10:0]        id,
    input   logic [47:0]        addr,
    input   logic [1:0]         burst,
    input   logic [2:0]         size,
    input   logic [7:0]         len
);

    guard_valid:    assert (!resetn || !$isunknown(valid)) else $fatal("MemoryAXIPushChannelAR: 'valid' is unknown");

    guard_id:       assert (!resetn || !valid || !$isunknown(id     )) else $fatal("MemoryAXIPushChannelAR: 'id' is unknown");
    guard_addr:     assert (!resetn || !valid || !$isunknown(addr   )) else $fatal("MemoryAXIPushChannelAR: 'addr' is unknown");
    guard_burst:    assert (!resetn || !valid || !$isunknown(burst  )) else $fatal("MemoryAXIPushChannelAR: 'burst' is unknown");
    guard_size:     assert (!resetn || !valid || !$isunknown(size   )) else $fatal("MemoryAXIPushChannelAR: 'size' is unknown");
    guard_len:      assert (!resetn || !valid || !$isunknown(len    )) else $fatal("MemoryAXIPushChannelAR: 'len' is unknown");

    if (resetn) begin

        MemoryAXIPushChannelAR (
            valid,
            id,
            addr,
            burst,
            size,
            len
        );
    end

endfunction


/*
* AXI Channel R
*/
import "DPI-C" function void MemoryAXIPullChannelR (
    output  byte                valid,
    output  int                 id,
    output  byte                resp,
    output  byte                last,
    output  longint             data0,
    output  longint             data1,
    output  longint             data2,
    output  longint             data3
);

function void SvMemoryAXIPullChannelR (
    output  logic               valid,
    output  logic [10:0]        id,
    output  logic [1:0]         resp,
    output  logic               last,
    output  logic [255:0]       data
);

    if (1) begin

        MemoryAXIPullChannelR (
            valid,
            id,
            resp,
            last,
            data[63:0],
            data[127:64],
            data[191:128],
            data[255:192]
        );
    end

endfunction

import "DPI-C" function void MemoryAXIPushChannelR (
    input   byte                ready
);

function void SvMemoryAXIPushChannelR (
    input   logic               resetn,
    input   logic               ready
);

    guard_ready:    assert (!resetn || !$isunknown(ready)) else $fatal("MemoryAXIPushChannelR: 'ready' is unknown");

    if (resetn) begin

        MemoryAXIPushChannelR (
            ready
        );
    end

endfunction
