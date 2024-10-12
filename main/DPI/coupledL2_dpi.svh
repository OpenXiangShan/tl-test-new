//
// 
//

`include "tilelink_enum.svh"


/*
* DPI system interactions : IsAlive
*/
import "DPI-C" function int TileLinkSystemIsAlive();
//

/*
* DPI system interactions : IsFailed
*/
import "DPI-C" function int TileLinkSystemIsFailed();
//

/*
* DPI system interactions : IsFinished
*/
import "DPI-C" function int TileLinkSystemIsFinished();
//



/*
* DPI system interactions : Initialize
*/
import "DPI-C" function void TileLinkSystemInitialize();
//

/*
* DPI system interactions : Finalize
*/
import "DPI-C" function void TileLinkSystemFinalize();
//

/*
* DPI system interactions : Tick
*/
import "DPI-C" function void TileLinkSystemTick(
    input  longint          cycles);
//

/*
* DPI system interactions : Tock
*/
import "DPI-C" function void TileLinkSystemTock();


/*
* TileLink Channel A
*/
import "DPI-C" function void TileLinkPushChannelA (
    input   int             device_id,
    input   byte            ready
);

function void SvTileLinkPushChannelA (
    input   int             device_id,
    input   logic           resetn,
    input   logic           ready
);

    guard_ready:        assert (!resetn || !$isunknown(ready        )) else $fatal("TileLinkPushChannelA: 'ready' is unknown");

    if (resetn) begin

        TileLinkPushChannelA (
            device_id,
            ready
        );
    end

endfunction

import "DPI-C" function void TileLinkPullChannelA (
    input   int             device_id,
    output  byte            valid,
    output  byte            opcode,
    output  byte            param,
    output  byte            size,
    output  byte            source,
    output  longint         address,
    output  byte            user_needHint,
    output  longint         user_vaddr,
    output  byte            user_alias,
    output  int             mask,
    output  longint         data0,
    output  longint         data1,
    output  longint         data2,
    output  longint         data3,
    output  byte            corrupt
);

function void SvTileLinkPullChannelA (
    input   int             device_id,
    input   logic           resetn,
    output  logic           valid,
    output  logic [2:0]     opcode,
    output  logic [2:0]     param,
    output  logic [2:0]     size,
    output  logic [4:0]     source,
    output  logic [35:0]    address,
    output  logic           user_needHint,
    output  logic [35:0]    user_vaddr,
    output  logic [1:0]     user_alias,
    output  logic [31:0]    mask,
    output  logic [255:0]   data,
    output  logic           corrupt
);

    if (1) begin

        TileLinkPullChannelA (
            device_id,
            valid,
            opcode,
            param,
            size,
            source,
            address,
            user_needHint,
            user_vaddr,
            user_alias,
            mask,
            data[63:0],
            data[127:64],
            data[191:128],
            data[255:192],
            corrupt
        );
    end

endfunction
//


/*
* TileLink Channel B
*/
import "DPI-C" function void TileLinkPushChannelB (
    input   int             device_id,
    input   byte            valid,
    input   byte            opcode,
    input   byte            param,
    input   byte            size,
    input   byte            source,
    input   longint         address,
    input   int             mask,
    input   longint         data0,
    input   longint         data1,
    input   longint         data2,
    input   longint         data3,
    input   byte            corrupt
);

function void SvTileLinkPushChannelB (
    input   int             device_id,
    input   logic           resetn,
    input   logic           valid,
    input   logic [2:0]     opcode,
    input   logic [1:0]     param,
    input   logic [2:0]     size,
    input   logic [4:0]     source,
    input   logic [35:0]    address,
    input   logic [31:0]    mask,
    input   logic [255:0]   data,
    input   logic           corrupt
);

    guard_valid:        assert (!resetn || !$isunknown(valid        )) else $fatal("TileLinkPushChannelB: 'valid' is unknown");
    
    guard_opcode:       assert (!resetn || !valid || !$isunknown(opcode       )) else $fatal("TileLinkPushChannelB: 'opcode' is unknown");
    guard_param:        assert (!resetn || !valid || !$isunknown(param        )) else $fatal("TileLinkPushChannelB: 'param' is unknown");
    guard_size:         assert (!resetn || !valid || !$isunknown(size         )) else $fatal("TileLinkPushChannelB: 'size' is unknown");
    guard_source:       assert (!resetn || !valid || !$isunknown(source       )) else $fatal("TileLinkPushChannelB: 'source' is unknown");
    guard_address:      assert (!resetn || !valid || !$isunknown(address      )) else $fatal("TileLinkPushChannelB: 'address' is unknown");
    guard_mask:         assert (!resetn || !valid || !$isunknown(mask         )) else $fatal("TileLinkPushChannelB: 'mask' is unknown");

    // TODO: better data assertion

    /*
    guard_corrupt:      assert (!resetn || !valid || !$isunknown(corrupt      )) else $fatal("TileLinkPushChannelB: 'corrupt' is unknown");
    guard_data:         assert (!resetn || !valid || !$isunknown(data         )) else $fatal("TileLinkPushChannelB: 'data' is unknown");
    */

    if (resetn) begin

        TileLinkPushChannelB (
            device_id,
            valid,
            opcode,
            param,
            size,
            source,
            address,
            mask,
            data[63:0],
            data[127:64],
            data[191:128],
            data[255:192],
            corrupt
        );
    end

endfunction

import "DPI-C" function void TileLinkPullChannelB (
    input   int             device_id,
    output  byte            ready
);

function void SvTileLinkPullChannelB (
    input   int             device_id,
    input   logic           resetn,
    output  logic           ready
);

    if (1) begin

        TileLinkPullChannelB (
            device_id,
            ready
        );
    end

endfunction
//


/*
* TileLink Channel C
*/
import "DPI-C" function void TileLinkPushChannelC (
    input   int             device_id,
    input   byte            ready
);

function void SvTileLinkPushChannelC (
    input   int             device_id,
    input   logic           resetn,
    input   logic           ready
);

    guard_ready:        assert (!resetn || !$isunknown(ready        )) else $fatal("TileLinkPushChannelC: 'ready' is unknown");

    if (resetn) begin

        TileLinkPushChannelC (
            device_id,
            ready
        );
    end

endfunction

import "DPI-C" function void TileLinkPullChannelC (
    input   int             device_id,
    output  byte            valid,
    output  byte            opcode,
    output  byte            param,
    output  byte            size,
    output  byte            source,
    output  longint         address,
    output  byte            user_alias,
    output  longint         data0,
    output  longint         data1,
    output  longint         data2,
    output  longint         data3,
    output  bit             corrupt
);

function void SvTileLinkPullChannelC (
    input   int             device_id,
    input   logic           resetn,
    output  logic           valid,
    output  logic [2:0]     opcode,
    output  logic [2:0]     param,
    output  logic [2:0]     size,
    output  logic [4:0]     source,
    output  logic [35:0]    address,
    output  logic [1:0]     user_alias,
    output  logic [255:0]   data,
    output  logic           corrupt
);

    if (1) begin

        TileLinkPullChannelC (
            device_id,
            valid,
            opcode,
            param,
            size,
            source,
            address,
            user_alias,
            data[63:0],
            data[127:64],
            data[191:128],
            data[255:192],
            corrupt
        );
    end

endfunction
//


/*
* TileLink Channel D
*/
import "DPI-C" function void TileLinkPushChannelD (
    input   int             device_id,
    input   byte            valid,
    input   byte            opcode,
    input   byte            param,
    input   byte            size,
    input   byte            source,
    input   byte            sink,
    input   byte            denied,
    input   longint         data0,
    input   longint         data1,
    input   longint         data2,
    input   longint         data3,
    input   bit             corrupt
);

function void SvTileLinkPushChannelD (
    input   int             device_id,
    input   logic           resetn,
    input   logic           valid,
    input   logic [2:0]     opcode,
    input   logic [1:0]     param,
    input   logic [2:0]     size,
    input   logic [4:0]     source,
    input   logic [7:0]     sink,
    input   logic           denied,
    input   logic [255:0]   data,
    input   logic           corrupt
);

    guard_valid:        assert (!resetn || !$isunknown(valid        )) else $fatal("TileLinkPushChannelD: 'valid' is unknown");

    guard_opcode:       assert (!resetn || !valid || !$isunknown(opcode       )) else $fatal("TileLinkPushChannelD: 'opcode' is unknown");
    guard_param:        assert (!resetn || !valid || !$isunknown(param        )) else $fatal("TileLinkPushChannelD: 'param' is unknown");
    guard_size:         assert (!resetn || !valid || !$isunknown(size         )) else $fatal("TileLinkPushChannelD: 'size' is unknown");
    guard_source:       assert (!resetn || !valid || !$isunknown(source       )) else $fatal("TileLinkPushChannelD: 'source' is unknown");
    guard_sink:         assert (!resetn || !valid || !$isunknown(sink         )) else $fatal("TileLinkPushChannelD: 'sink' is unknown");
    guard_denied:       assert (!resetn || !valid || !$isunknown(denied       )) else $fatal("TileLinkPushChannelD: 'denied' is unknown");

    /*
    * NOTICE:
    * 1. Data related fields are not checked whatever unknown when they're not required.
    *    It's acceptable for DUT to send unknown values to improve PPA and reduce reset logic.
    *    In this case, on TileLink, for: 
    *       <Grant> 
    *    operations on Channel D, the data related fields are not required.
    *
    * 2. Not all data lanes are needed to be checked always, it depends on <size> & <address>.
    */ 

    // TODO: better data assertion

    /*
    guard_corrupt:      assert (!resetn || !valid || !$isunknown(corrupt      )) else $fatal("TileLinkPushChannelD: 'corrupt' is unknown");

    guard_data:         assert (!resetn || !valid || !$isunknown(data         )
                                        || (opcode == `Grant)               // skip <Grant>
        ) else $fatal("TileLinkPushChannelD: 'data' is unknown");
    */


    if (resetn) begin

        TileLinkPushChannelD (
            device_id,
            valid,
            opcode,
            param,
            size,
            source,
            sink,
            denied,
            data[63:0],
            data[127:64],
            data[191:128],
            data[255:192],
            corrupt
        );
    end

endfunction

import "DPI-C" function void TileLinkPullChannelD (
    input   int             device_id,
    output  byte            ready
);

function void SvTileLinkPullChannelD (
    input   int             device_id,
    input   logic           resetn,
    output  logic           ready
);

    if (1) begin

        TileLinkPullChannelD (
            device_id,
            ready
        );
    end

endfunction
//


/*
* TileLink Channel E
*/
import "DPI-C" function void TileLinkPushChannelE (
    input   int             device_id,
    input   byte            ready
);

function void SvTileLinkPushChannelE (
    input   int             device_id,
    input   logic           resetn,
    input   logic           ready
);

    guard_ready:        assert (!resetn || !$isunknown(ready        )) else $fatal("TileLinkPushChannelE: 'ready' is unknown");

    if (resetn) begin

        TileLinkPushChannelE (
            device_id,
            ready
        );
    end

endfunction 

import "DPI-C" function void TileLinkPullChannelE (
    input   int             device_id,
    output  byte            valid,
    output  byte            sink
);

function void SvTileLinkPullChannelE (
    input   int             device_id,
    input   logic           resetn,
    output  logic           valid,
    output  logic [7:0]     sink
);

    if (1) begin

        TileLinkPullChannelE (
            device_id,
            valid,
            sink
        );
    end

endfunction
//
