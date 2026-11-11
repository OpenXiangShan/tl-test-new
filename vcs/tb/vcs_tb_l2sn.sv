// L2SN testbench: the TestTopMatrixL2SN variant (openLLC/L3/AXI removed)
// replayed by the tl-test DPI Guest Mode runner, with the downstream CHI
// link answered by ChiSnCore -- the CHI Subordinate Node functional model
// ported from CUTE-Test (vcs/chi_sn/). The TileLink/matrix/MMIO loop is
// identical to vcs_tb.sv; only the memory backend changes:
//
//   vcs_tb.sv      L2 -> openLLC L3 -> mem_axi_0 -> tl-test AXI memory
//   vcs_tb_l2sn.sv L2 -> io_chi_0 (Decoupled CHI) -> ChiSnCore model
//
// ChiSnDrive runs at the negedge (rx valid/bits + tx readys settle a
// half-cycle before the posedge), ChiSnObserve samples the handshake fires
// in the posedge active region and aborts on model protocol errors. The
// model never backpressures; rx_snp is quiet in this baseline, so traces
// must not depend on external snoops. Build the model library with
// vcs/scripts/build_chisn_dpi.sh and load it via
// TLTEST_VCS_EXTRA_DPI_LIBS=.../libchisn_dpi.so (run_vcs.sh wires it in).

`timescale 1ns/1ps

`include "coupledL2_dpi.svh"

module tltest_vcs_tb;

  localparam int DEV_L1D    = 0;
  localparam int DEV_MATRIX = 1;
  localparam int DEV_MMIO   = 0;   // MMIO agents are indexed by core

  // Matrix user fields of Channel A are not part of the generic DPI
  // signature; they are exported by the extra entries in libtltest_dpi.so.
  import "DPI-C" function void TileLinkMatrixPullChannelAUser(
    input int device_id,
    output byte matrix,
    output byte ameIndex);
  // Matrix data-return channel (matrixDataOut_*): a Decoupled 512-bit output
  // beside the TileLink channels.
  import "DPI-C" function void TileLinkMatrixPushChannelM(
    input int device_id,
    input byte valid,
    input longint source,
    input longint data0, input longint data1,
    input longint data2, input longint data3,
    input longint data4, input longint data5,
    input longint data6, input longint data7);
  import "DPI-C" function void TileLinkMatrixPullChannelMReady(
    input int device_id,
    output byte ready);

  // ChiSnCore (CHI Subordinate Node functional model) DPI entries, see
  // vcs/chi_sn/chi_sn_dpi.cpp and vcs/scripts/build_chisn_dpi.sh. Wire
  // semantics (never backpressure, dataID = beat*2, odd-parity dataCheck)
  // follow CUTE-Test's cute_vcs_dpi_l2.cpp.
  import "DPI-C" function void ChiSnInit();
  import "DPI-C" function void ChiSnDrive(
    output logic tx_req_ready, output logic tx_rsp_ready,
    output logic tx_dat_ready,
    output logic rx_rsp_valid, output logic [4:0] rx_rsp_opcode,
    output logic [11:0] rx_rsp_txn, output logic [11:0] rx_rsp_dbid,
    output logic rx_dat_valid, output logic [3:0] rx_dat_opcode,
    output logic [11:0] rx_dat_txn, output logic [1:0] rx_dat_dataid,
    output logic [3:0] rx_dat_datasrc, output logic [255:0] rx_dat_data,
    output logic [31:0] rx_dat_check
  );
  import "DPI-C" function void ChiSnObserve(
    input logic tx_req_valid, input logic [6:0] tx_req_opcode,
    input logic [11:0] tx_req_txn, input logic [2:0] tx_req_size,
    input logic [47:0] tx_req_addr,
    input logic tx_rsp_valid, input logic [4:0] tx_rsp_opcode,
    input logic [11:0] tx_rsp_txn,
    input logic tx_dat_valid, input logic [3:0] tx_dat_opcode,
    input logic [11:0] tx_dat_txn, input logic [1:0] tx_dat_dataid,
    input logic [31:0] tx_dat_be, input logic [31:0] tx_dat_check,
    input logic [255:0] tx_dat_data,
    input logic rx_rsp_ready, input logic rx_dat_ready
  );
  import "DPI-C" function bit ChiSnHasError();
  import "DPI-C" function string ChiSnGetMessage();

  // =====================================================================
  // Clock / reset / misc DUT inputs
  // =====================================================================
  logic        clock;
  logic        reset;
  logic [63:0] timer;
  logic        logEnable;
  logic        clean;
  logic        dump;

  longint unsigned sim_time;

  // =====================================================================
  // Device 0: L1D TileLink-C port (master_port_0_0), all 5 channels.
  // a_bits_size/c_bits_param/c_bits_size DO exist on this TestTop: in the
  // ANSI port list they carry no direction of their own and inherit
  // "input [2:0]" from the preceding port line. Leaving them unconnected
  // floats size/param to z, and SinkC computes last = r_counter |
  // ~(0x3f << c_size)[5] — the z makes every ProbeAckData (opcode 5)
  // produce an x last, which X-poisons the MSHR task arbiter and hangs the
  // whole slice (first matrix Get probes a dirty L1D line).
  // =====================================================================
  wire  p0_a_ready;
  logic p0_a_valid;
  logic [3:0]   p0_a_opcode;
  logic [2:0]   p0_a_param;
  logic [2:0]   p0_a_size;
  logic [4:0]   p0_a_source;
  logic [47:0]  p0_a_address;
  logic [1:0]   p0_a_alias;
  logic [31:0]  p0_a_mask;
  logic [255:0] p0_a_data;
  logic         p0_a_corrupt;

  logic         p0_b_ready;
  wire          p0_b_valid;
  wire  [2:0]   p0_b_opcode;
  wire  [1:0]   p0_b_param;
  wire  [2:0]   p0_b_size;
  wire  [4:0]   p0_b_source;
  wire  [47:0]  p0_b_address;
  wire  [31:0]  p0_b_mask;
  wire  [255:0] p0_b_data;
  wire          p0_b_corrupt;

  wire  p0_c_ready;
  logic p0_c_valid;
  logic [2:0]   p0_c_opcode;
  logic [2:0]   p0_c_param;
  logic [2:0]   p0_c_size;
  logic [4:0]   p0_c_source;
  logic [47:0]  p0_c_address;
  logic [1:0]   p0_c_alias;
  logic [255:0] p0_c_data;
  logic         p0_c_corrupt;

  logic         p0_d_ready;
  wire          p0_d_valid;
  wire  [3:0]   p0_d_opcode;
  wire  [1:0]   p0_d_param;
  wire  [2:0]   p0_d_size;
  wire  [4:0]   p0_d_source;
  wire  [7:0]   p0_d_sink;
  wire          p0_d_denied;
  wire  [255:0] p0_d_data;
  wire          p0_d_corrupt;

  wire  p0_e_ready;
  logic p0_e_valid;
  logic [7:0]   p0_e_sink;

  // =====================================================================
  // Device 1: Matrix TileLink port (master_m_port_0_0_0), channels A/D
  // =====================================================================
  wire  p2_a_ready;
  logic p2_a_valid;
  logic [3:0]   p2_a_opcode;
  logic [2:0]   p2_a_param;
  logic [2:0]   p2_a_size;
  logic [4:0]   p2_a_source;
  logic [47:0]  p2_a_address;
  logic [63:0]  p2_a_ameIndex;
  logic [1:0]   p2_a_matrix;
  logic [31:0]  p2_a_mask;
  logic [255:0] p2_a_data;
  logic         p2_a_corrupt;

  logic         p2_d_ready;
  wire          p2_d_valid;
  wire  [3:0]   p2_d_opcode;
  wire  [1:0]   p2_d_param;
  wire  [2:0]   p2_d_size;
  wire  [4:0]   p2_d_source;
  wire  [7:0]   p2_d_sink;
  wire          p2_d_denied;
  wire  [255:0] p2_d_data;
  wire          p2_d_corrupt;

  // L2 -> Matrix data return path. In the current TestTop build this
  // channel never fires (matrix read data returns on channel D), matching
  // the Verilator host mode where the PortGen leaves it undriven/unobserved.
  logic         mdat_ready;
  wire          mdat_valid;
  wire  [63:0]  mdat_sourceId;
  wire  [511:0] mdat_data;
  wire  [2:0]   mdat_channel;

  // L2 -> L1 hint: DUT outputs, observed only
  wire          hint_valid;
  wire          hint_isKeyword;

  // MMIO uncached client port (exposed by TestTopMatrix.scala). Only A/D are
  // present; the DPI MMIO agent drives A and sinks D.
  logic         mmio_a_valid;
  logic  [3:0]  mmio_a_opcode;
  logic  [2:0]  mmio_a_param;
  logic  [1:0]  mmio_a_size;
  logic  [3:0]  mmio_a_source;
  logic  [47:0] mmio_a_address;
  logic  [7:0]  mmio_a_mask;
  logic  [63:0] mmio_a_data;
  logic         mmio_a_corrupt;
  wire          mmio_a_ready;
  logic         mmio_d_ready;
  wire          mmio_d_valid;
  wire  [3:0]   mmio_d_opcode;
  wire  [1:0]   mmio_d_param;
  wire          mmio_d_size;
  wire   [3:0]  mmio_d_source;
  wire          mmio_d_sink;
  wire          mmio_d_denied;
  wire  [63:0]  mmio_d_data;
  wire          mmio_d_corrupt;

  // The MMIO bridge has no traffic until the first DPI request; its idle
  // valid/ready can still be X out of reset. Present a clean idle value to
  // the DPI guards; real transfers are only accepted once both are known.
  wire          mmio_a_ready_v = $isunknown(mmio_a_ready) ? 1'b0 : mmio_a_ready;
  wire          mmio_d_valid_v = $isunknown(mmio_d_valid) ? 1'b0 : mmio_d_valid;

  // =====================================================================
  // CHI downstream link (io_chi_0): tx channels are DUT outputs observed
  // by the ChiSnCore model, rx channels are DUT inputs driven by it. The
  // field set and widths below are generated from the elaborated
  // TestTopMatrixL2SN port list (dut/XSCache/build/l2sn/TestTop.sv);
  // firtool emits continuation lines that inherit direction+width from the
  // previous line, so the widths follow ANSI rules exactly.
  // =====================================================================
  logic            io_chi_0_tx_req_ready;
  wire             io_chi_0_tx_req_valid;
  wire  [3:0]      io_chi_0_tx_req_bits_qos;
  wire  [10:0]     io_chi_0_tx_req_bits_tgtID;
  wire  [10:0]     io_chi_0_tx_req_bits_srcID;
  wire  [11:0]     io_chi_0_tx_req_bits_txnID;
  wire  [10:0]     io_chi_0_tx_req_bits_returnNID;
  wire             io_chi_0_tx_req_bits_stashNIDValid;
  wire  [11:0]     io_chi_0_tx_req_bits_returnTxnID;
  wire  [6:0]      io_chi_0_tx_req_bits_opcode;
  wire  [2:0]      io_chi_0_tx_req_bits_size;
  wire  [47:0]     io_chi_0_tx_req_bits_addr;
  wire             io_chi_0_tx_req_bits_ns;
  wire             io_chi_0_tx_req_bits_likelyshared;
  wire             io_chi_0_tx_req_bits_allowRetry;
  wire  [1:0]      io_chi_0_tx_req_bits_order;
  wire  [3:0]      io_chi_0_tx_req_bits_pCrdType;
  wire             io_chi_0_tx_req_bits_memAttr_allocate;
  wire             io_chi_0_tx_req_bits_memAttr_cacheable;
  wire             io_chi_0_tx_req_bits_memAttr_device;
  wire             io_chi_0_tx_req_bits_memAttr_ewa;
  wire             io_chi_0_tx_req_bits_snpAttr;
  wire  [7:0]      io_chi_0_tx_req_bits_lpIDWithPadding;
  wire             io_chi_0_tx_req_bits_snoopMe;
  wire             io_chi_0_tx_req_bits_expCompAck;
  wire  [1:0]      io_chi_0_tx_req_bits_tagOp;
  wire             io_chi_0_tx_req_bits_traceTag;
  wire             io_chi_0_tx_req_bits_mpam_perfMonGroup;
  wire  [8:0]      io_chi_0_tx_req_bits_mpam_partID;
  wire             io_chi_0_tx_req_bits_mpam_mpamNS;
  wire  [3:0]      io_chi_0_tx_req_bits_rsvdc;
  logic            io_chi_0_tx_rsp_ready;
  wire             io_chi_0_tx_rsp_valid;
  wire  [3:0]      io_chi_0_tx_rsp_bits_qos;
  wire  [10:0]     io_chi_0_tx_rsp_bits_tgtID;
  wire  [10:0]     io_chi_0_tx_rsp_bits_srcID;
  wire  [11:0]     io_chi_0_tx_rsp_bits_txnID;
  wire  [4:0]      io_chi_0_tx_rsp_bits_opcode;
  wire  [1:0]      io_chi_0_tx_rsp_bits_respErr;
  wire  [2:0]      io_chi_0_tx_rsp_bits_resp;
  wire  [2:0]      io_chi_0_tx_rsp_bits_fwdState;
  wire  [2:0]      io_chi_0_tx_rsp_bits_cBusy;
  wire  [11:0]     io_chi_0_tx_rsp_bits_dbID;
  wire  [3:0]      io_chi_0_tx_rsp_bits_pCrdType;
  wire  [1:0]      io_chi_0_tx_rsp_bits_tagOp;
  wire             io_chi_0_tx_rsp_bits_traceTag;
  logic            io_chi_0_tx_dat_ready;
  wire             io_chi_0_tx_dat_valid;
  wire  [3:0]      io_chi_0_tx_dat_bits_qos;
  wire  [10:0]     io_chi_0_tx_dat_bits_tgtID;
  wire  [10:0]     io_chi_0_tx_dat_bits_srcID;
  wire  [11:0]     io_chi_0_tx_dat_bits_txnID;
  wire  [10:0]     io_chi_0_tx_dat_bits_homeNID;
  wire  [3:0]      io_chi_0_tx_dat_bits_opcode;
  wire  [1:0]      io_chi_0_tx_dat_bits_respErr;
  wire  [2:0]      io_chi_0_tx_dat_bits_resp;
  wire  [3:0]      io_chi_0_tx_dat_bits_dataSource;
  wire  [2:0]      io_chi_0_tx_dat_bits_cBusy;
  wire  [11:0]     io_chi_0_tx_dat_bits_dbID;
  wire  [1:0]      io_chi_0_tx_dat_bits_ccID;
  wire  [1:0]      io_chi_0_tx_dat_bits_dataID;
  wire  [1:0]      io_chi_0_tx_dat_bits_tagOp;
  wire  [7:0]      io_chi_0_tx_dat_bits_tag;
  wire  [1:0]      io_chi_0_tx_dat_bits_tu;
  wire             io_chi_0_tx_dat_bits_traceTag;
  wire  [3:0]      io_chi_0_tx_dat_bits_rsvdc;
  wire  [31:0]     io_chi_0_tx_dat_bits_be;
  wire  [255:0]    io_chi_0_tx_dat_bits_data;
  wire  [31:0]     io_chi_0_tx_dat_bits_dataCheck;
  wire  [3:0]      io_chi_0_tx_dat_bits_poison;
  wire             io_chi_0_rx_rsp_ready;
  logic            io_chi_0_rx_rsp_valid;
  wire  [3:0]      io_chi_0_rx_rsp_bits_qos;
  wire  [10:0]     io_chi_0_rx_rsp_bits_tgtID;
  wire  [10:0]     io_chi_0_rx_rsp_bits_srcID;
  logic [11:0]     io_chi_0_rx_rsp_bits_txnID;
  logic [4:0]      io_chi_0_rx_rsp_bits_opcode;
  wire  [1:0]      io_chi_0_rx_rsp_bits_respErr;
  wire  [2:0]      io_chi_0_rx_rsp_bits_resp;
  wire  [2:0]      io_chi_0_rx_rsp_bits_fwdState;
  wire  [2:0]      io_chi_0_rx_rsp_bits_cBusy;
  logic [11:0]     io_chi_0_rx_rsp_bits_dbID;
  wire  [3:0]      io_chi_0_rx_rsp_bits_pCrdType;
  wire  [1:0]      io_chi_0_rx_rsp_bits_tagOp;
  wire             io_chi_0_rx_rsp_bits_traceTag;
  wire             io_chi_0_rx_dat_ready;
  logic            io_chi_0_rx_dat_valid;
  wire  [3:0]      io_chi_0_rx_dat_bits_qos;
  wire  [10:0]     io_chi_0_rx_dat_bits_tgtID;
  wire  [10:0]     io_chi_0_rx_dat_bits_srcID;
  logic [11:0]     io_chi_0_rx_dat_bits_txnID;
  wire  [10:0]     io_chi_0_rx_dat_bits_homeNID;
  logic [3:0]      io_chi_0_rx_dat_bits_opcode;
  wire  [1:0]      io_chi_0_rx_dat_bits_respErr;
  wire  [2:0]      io_chi_0_rx_dat_bits_resp;
  logic [3:0]      io_chi_0_rx_dat_bits_dataSource;
  wire  [2:0]      io_chi_0_rx_dat_bits_cBusy;
  wire  [11:0]     io_chi_0_rx_dat_bits_dbID;
  wire  [1:0]      io_chi_0_rx_dat_bits_ccID;
  logic [1:0]      io_chi_0_rx_dat_bits_dataID;
  wire  [1:0]      io_chi_0_rx_dat_bits_tagOp;
  wire  [7:0]      io_chi_0_rx_dat_bits_tag;
  wire  [1:0]      io_chi_0_rx_dat_bits_tu;
  wire             io_chi_0_rx_dat_bits_traceTag;
  wire  [3:0]      io_chi_0_rx_dat_bits_rsvdc;
  wire  [31:0]     io_chi_0_rx_dat_bits_be;
  logic [255:0]    io_chi_0_rx_dat_bits_data;
  logic [31:0]     io_chi_0_rx_dat_bits_dataCheck;
  wire  [3:0]      io_chi_0_rx_dat_bits_poison;
  wire             io_chi_0_rx_snp_ready;
  wire             io_chi_0_rx_snp_valid;
  wire  [3:0]      io_chi_0_rx_snp_bits_qos;
  wire  [10:0]     io_chi_0_rx_snp_bits_srcID;
  wire  [11:0]     io_chi_0_rx_snp_bits_txnID;
  wire  [10:0]     io_chi_0_rx_snp_bits_fwdNID;
  wire  [11:0]     io_chi_0_rx_snp_bits_fwdTxnID;
  wire  [4:0]      io_chi_0_rx_snp_bits_opcode;
  wire  [44:0]     io_chi_0_rx_snp_bits_addr;
  wire             io_chi_0_rx_snp_bits_ns;
  wire             io_chi_0_rx_snp_bits_doNotGoToSD;
  wire             io_chi_0_rx_snp_bits_retToSrc;
  wire             io_chi_0_rx_snp_bits_traceTag;
  wire             io_chi_0_rx_snp_bits_mpam_perfMonGroup;
  wire  [8:0]      io_chi_0_rx_snp_bits_mpam_partID;
  wire             io_chi_0_rx_snp_bits_mpam_mpamNS;

  // SN-side inputs the model never asserts are tied to zero (same policy as
  // CUTE-Test vcs/vcs_tb_l2.sv): resp/respErr are valid as 0 for every
  // opcode ChiSnCore emits, and the L2 does not consume the tag/mpam/qos
  // fields on the rx side. rx_snp stays quiet in this baseline; active
  // snoop injection is the follow-up extension.
  assign io_chi_0_rx_rsp_bits_qos = '0;
  assign io_chi_0_rx_rsp_bits_tgtID = '0;
  assign io_chi_0_rx_rsp_bits_srcID = '0;
  assign io_chi_0_rx_rsp_bits_respErr = '0;
  assign io_chi_0_rx_rsp_bits_resp = '0;
  assign io_chi_0_rx_rsp_bits_fwdState = '0;
  assign io_chi_0_rx_rsp_bits_cBusy = '0;
  assign io_chi_0_rx_rsp_bits_pCrdType = '0;
  assign io_chi_0_rx_rsp_bits_tagOp = '0;
  assign io_chi_0_rx_rsp_bits_traceTag = '0;
  assign io_chi_0_rx_dat_bits_qos = '0;
  assign io_chi_0_rx_dat_bits_tgtID = '0;
  assign io_chi_0_rx_dat_bits_srcID = '0;
  assign io_chi_0_rx_dat_bits_homeNID = '0;
  assign io_chi_0_rx_dat_bits_respErr = '0;
  assign io_chi_0_rx_dat_bits_resp = '0;
  assign io_chi_0_rx_dat_bits_cBusy = '0;
  assign io_chi_0_rx_dat_bits_dbID = '0;
  assign io_chi_0_rx_dat_bits_ccID = '0;
  assign io_chi_0_rx_dat_bits_tagOp = '0;
  assign io_chi_0_rx_dat_bits_tag = '0;
  assign io_chi_0_rx_dat_bits_tu = '0;
  assign io_chi_0_rx_dat_bits_traceTag = '0;
  assign io_chi_0_rx_dat_bits_rsvdc = '0;
  assign io_chi_0_rx_dat_bits_be = '0;
  assign io_chi_0_rx_dat_bits_poison = '0;
  assign io_chi_0_rx_snp_valid = '0;
  assign io_chi_0_rx_snp_bits_qos = '0;
  assign io_chi_0_rx_snp_bits_srcID = '0;
  assign io_chi_0_rx_snp_bits_txnID = '0;
  assign io_chi_0_rx_snp_bits_fwdNID = '0;
  assign io_chi_0_rx_snp_bits_fwdTxnID = '0;
  assign io_chi_0_rx_snp_bits_opcode = '0;
  assign io_chi_0_rx_snp_bits_addr = '0;
  assign io_chi_0_rx_snp_bits_ns = '0;
  assign io_chi_0_rx_snp_bits_doNotGoToSD = '0;
  assign io_chi_0_rx_snp_bits_retToSrc = '0;
  assign io_chi_0_rx_snp_bits_traceTag = '0;
  assign io_chi_0_rx_snp_bits_mpam_perfMonGroup = '0;
  assign io_chi_0_rx_snp_bits_mpam_partID = '0;
  assign io_chi_0_rx_snp_bits_mpam_mpamNS = '0;
  // =====================================================================
  // DUT
  // =====================================================================
  TestTop dut (
    .clock                                (clock),
    .reset                                (reset),
    .time_sim                             (timer),
    .log_dump                             (dump),
    .log_clean                            (clean),
    // L2 -> L1 hint outputs (observed only)
    .io_l1_0_l2Hint_valid                 (hint_valid),
    .io_l1_0_l2Hint_bits_isKeyword        (hint_isKeyword),
    // L1D TileLink-C port
    .master_port_0_0_a_ready              (p0_a_ready),
    .master_port_0_0_a_valid              (p0_a_valid),
    .master_port_0_0_a_bits_opcode        (p0_a_opcode),
    .master_port_0_0_a_bits_param         (p0_a_param),
    .master_port_0_0_a_bits_size          (p0_a_size),
    .master_port_0_0_a_bits_source        (p0_a_source),
    .master_port_0_0_a_bits_address       (p0_a_address),
    .master_port_0_0_a_bits_user_alias    (p0_a_alias),
    .master_port_0_0_a_bits_mask          (p0_a_mask),
    .master_port_0_0_a_bits_data          (p0_a_data),
    .master_port_0_0_a_bits_corrupt       (p0_a_corrupt),
    .master_port_0_0_b_ready              (p0_b_ready),
    .master_port_0_0_b_valid              (p0_b_valid),
    .master_port_0_0_b_bits_opcode        (p0_b_opcode),
    .master_port_0_0_b_bits_param         (p0_b_param),
    .master_port_0_0_b_bits_size          (p0_b_size),
    .master_port_0_0_b_bits_source        (p0_b_source),
    .master_port_0_0_b_bits_address       (p0_b_address),
    .master_port_0_0_b_bits_mask          (p0_b_mask),
    .master_port_0_0_b_bits_data          (p0_b_data),
    .master_port_0_0_b_bits_corrupt       (p0_b_corrupt),
    .master_port_0_0_c_ready              (p0_c_ready),
    .master_port_0_0_c_valid              (p0_c_valid),
    .master_port_0_0_c_bits_opcode        (p0_c_opcode),
    .master_port_0_0_c_bits_param         (p0_c_param),
    .master_port_0_0_c_bits_size          (p0_c_size),
    .master_port_0_0_c_bits_source        (p0_c_source),
    .master_port_0_0_c_bits_address       (p0_c_address),
    .master_port_0_0_c_bits_user_alias    (p0_c_alias),
    .master_port_0_0_c_bits_data          (p0_c_data),
    .master_port_0_0_c_bits_corrupt       (p0_c_corrupt),
    .master_port_0_0_d_ready              (p0_d_ready),
    .master_port_0_0_d_valid              (p0_d_valid),
    .master_port_0_0_d_bits_opcode        (p0_d_opcode),
    .master_port_0_0_d_bits_param         (p0_d_param),
    .master_port_0_0_d_bits_size          (p0_d_size),
    .master_port_0_0_d_bits_source        (p0_d_source),
    .master_port_0_0_d_bits_sink          (p0_d_sink),
    .master_port_0_0_d_bits_denied        (p0_d_denied),
    .master_port_0_0_d_bits_data          (p0_d_data),
    .master_port_0_0_d_bits_corrupt       (p0_d_corrupt),
    .master_port_0_0_e_ready              (p0_e_ready),
    .master_port_0_0_e_valid              (p0_e_valid),
    .master_port_0_0_e_bits_sink          (p0_e_sink),
    // Matrix TileLink port
    .master_m_port_0_0_0_a_ready          (p2_a_ready),
    .master_m_port_0_0_0_a_valid          (p2_a_valid),
    .master_m_port_0_0_0_a_bits_opcode    (p2_a_opcode),
    .master_m_port_0_0_0_a_bits_param     (p2_a_param),
    .master_m_port_0_0_0_a_bits_size      (p2_a_size),
    .master_m_port_0_0_0_a_bits_source    (p2_a_source),
    .master_m_port_0_0_0_a_bits_address   (p2_a_address),
    .master_m_port_0_0_0_a_bits_user_matrix     (p2_a_matrix),
    .master_m_port_0_0_0_a_bits_user_AmeIndex   (p2_a_ameIndex),
    .master_m_port_0_0_0_a_bits_mask      (p2_a_mask),
    .master_m_port_0_0_0_a_bits_data      (p2_a_data),
    .master_m_port_0_0_0_a_bits_corrupt   (p2_a_corrupt),
    .master_m_port_0_0_0_d_ready          (p2_d_ready),
    .master_m_port_0_0_0_d_valid          (p2_d_valid),
    .master_m_port_0_0_0_d_bits_opcode    (p2_d_opcode),
    .master_m_port_0_0_0_d_bits_param     (p2_d_param),
    .master_m_port_0_0_0_d_bits_size      (p2_d_size),
    .master_m_port_0_0_0_d_bits_source    (p2_d_source),
    .master_m_port_0_0_0_d_bits_sink      (p2_d_sink),
    .master_m_port_0_0_0_d_bits_denied    (p2_d_denied),
    .master_m_port_0_0_0_d_bits_data      (p2_d_data),
    .master_m_port_0_0_0_d_bits_corrupt   (p2_d_corrupt),
    // Matrix data return (left quiescent, see note above)
    .matrixDataOut_0_0_ready              (mdat_ready),
    .matrixDataOut_0_0_valid              (mdat_valid),
    .matrixDataOut_0_0_bits_sourceId      (mdat_sourceId),
    .matrixDataOut_0_0_bits_data_data     (mdat_data),
    .matrixDataOut_0_0_bits_channel       (mdat_channel),
    // MMIO uncached client (A/D only)
    .mmio_port_0_0_a_ready                 (mmio_a_ready),
    .mmio_port_0_0_a_valid                 (mmio_a_valid),
    .mmio_port_0_0_a_bits_opcode           (mmio_a_opcode),
    .mmio_port_0_0_a_bits_param            (mmio_a_param),
    .mmio_port_0_0_a_bits_size             (mmio_a_size),
    .mmio_port_0_0_a_bits_source           (mmio_a_source),
    .mmio_port_0_0_a_bits_address          (mmio_a_address),
    .mmio_port_0_0_a_bits_mask             (mmio_a_mask),
    .mmio_port_0_0_a_bits_data             (mmio_a_data),
    .mmio_port_0_0_a_bits_corrupt          (mmio_a_corrupt),
    .mmio_port_0_0_d_ready                 (mmio_d_ready),
    .mmio_port_0_0_d_valid                 (mmio_d_valid),
    .mmio_port_0_0_d_bits_opcode           (mmio_d_opcode),
    .mmio_port_0_0_d_bits_param            (mmio_d_param),
    .mmio_port_0_0_d_bits_size             (mmio_d_size),
    .mmio_port_0_0_d_bits_source           (mmio_d_source),
    .mmio_port_0_0_d_bits_sink             (mmio_d_sink),
    .mmio_port_0_0_d_bits_denied           (mmio_d_denied),
    .mmio_port_0_0_d_bits_data             (mmio_d_data),
    .mmio_port_0_0_d_bits_corrupt          (mmio_d_corrupt),
    // CHI downstream link (SN model side)
    .io_chi_0_tx_req_ready                                (io_chi_0_tx_req_ready),
    .io_chi_0_tx_req_valid                                (io_chi_0_tx_req_valid),
    .io_chi_0_tx_req_bits_qos                             (io_chi_0_tx_req_bits_qos),
    .io_chi_0_tx_req_bits_tgtID                           (io_chi_0_tx_req_bits_tgtID),
    .io_chi_0_tx_req_bits_srcID                           (io_chi_0_tx_req_bits_srcID),
    .io_chi_0_tx_req_bits_txnID                           (io_chi_0_tx_req_bits_txnID),
    .io_chi_0_tx_req_bits_returnNID                       (io_chi_0_tx_req_bits_returnNID),
    .io_chi_0_tx_req_bits_stashNIDValid                   (io_chi_0_tx_req_bits_stashNIDValid),
    .io_chi_0_tx_req_bits_returnTxnID                     (io_chi_0_tx_req_bits_returnTxnID),
    .io_chi_0_tx_req_bits_opcode                          (io_chi_0_tx_req_bits_opcode),
    .io_chi_0_tx_req_bits_size                            (io_chi_0_tx_req_bits_size),
    .io_chi_0_tx_req_bits_addr                            (io_chi_0_tx_req_bits_addr),
    .io_chi_0_tx_req_bits_ns                              (io_chi_0_tx_req_bits_ns),
    .io_chi_0_tx_req_bits_likelyshared                    (io_chi_0_tx_req_bits_likelyshared),
    .io_chi_0_tx_req_bits_allowRetry                      (io_chi_0_tx_req_bits_allowRetry),
    .io_chi_0_tx_req_bits_order                           (io_chi_0_tx_req_bits_order),
    .io_chi_0_tx_req_bits_pCrdType                        (io_chi_0_tx_req_bits_pCrdType),
    .io_chi_0_tx_req_bits_memAttr_allocate                (io_chi_0_tx_req_bits_memAttr_allocate),
    .io_chi_0_tx_req_bits_memAttr_cacheable               (io_chi_0_tx_req_bits_memAttr_cacheable),
    .io_chi_0_tx_req_bits_memAttr_device                  (io_chi_0_tx_req_bits_memAttr_device),
    .io_chi_0_tx_req_bits_memAttr_ewa                     (io_chi_0_tx_req_bits_memAttr_ewa),
    .io_chi_0_tx_req_bits_snpAttr                         (io_chi_0_tx_req_bits_snpAttr),
    .io_chi_0_tx_req_bits_lpIDWithPadding                 (io_chi_0_tx_req_bits_lpIDWithPadding),
    .io_chi_0_tx_req_bits_snoopMe                         (io_chi_0_tx_req_bits_snoopMe),
    .io_chi_0_tx_req_bits_expCompAck                      (io_chi_0_tx_req_bits_expCompAck),
    .io_chi_0_tx_req_bits_tagOp                           (io_chi_0_tx_req_bits_tagOp),
    .io_chi_0_tx_req_bits_traceTag                        (io_chi_0_tx_req_bits_traceTag),
    .io_chi_0_tx_req_bits_mpam_perfMonGroup               (io_chi_0_tx_req_bits_mpam_perfMonGroup),
    .io_chi_0_tx_req_bits_mpam_partID                     (io_chi_0_tx_req_bits_mpam_partID),
    .io_chi_0_tx_req_bits_mpam_mpamNS                     (io_chi_0_tx_req_bits_mpam_mpamNS),
    .io_chi_0_tx_req_bits_rsvdc                           (io_chi_0_tx_req_bits_rsvdc),
    .io_chi_0_tx_rsp_ready                                (io_chi_0_tx_rsp_ready),
    .io_chi_0_tx_rsp_valid                                (io_chi_0_tx_rsp_valid),
    .io_chi_0_tx_rsp_bits_qos                             (io_chi_0_tx_rsp_bits_qos),
    .io_chi_0_tx_rsp_bits_tgtID                           (io_chi_0_tx_rsp_bits_tgtID),
    .io_chi_0_tx_rsp_bits_srcID                           (io_chi_0_tx_rsp_bits_srcID),
    .io_chi_0_tx_rsp_bits_txnID                           (io_chi_0_tx_rsp_bits_txnID),
    .io_chi_0_tx_rsp_bits_opcode                          (io_chi_0_tx_rsp_bits_opcode),
    .io_chi_0_tx_rsp_bits_respErr                         (io_chi_0_tx_rsp_bits_respErr),
    .io_chi_0_tx_rsp_bits_resp                            (io_chi_0_tx_rsp_bits_resp),
    .io_chi_0_tx_rsp_bits_fwdState                        (io_chi_0_tx_rsp_bits_fwdState),
    .io_chi_0_tx_rsp_bits_cBusy                           (io_chi_0_tx_rsp_bits_cBusy),
    .io_chi_0_tx_rsp_bits_dbID                            (io_chi_0_tx_rsp_bits_dbID),
    .io_chi_0_tx_rsp_bits_pCrdType                        (io_chi_0_tx_rsp_bits_pCrdType),
    .io_chi_0_tx_rsp_bits_tagOp                           (io_chi_0_tx_rsp_bits_tagOp),
    .io_chi_0_tx_rsp_bits_traceTag                        (io_chi_0_tx_rsp_bits_traceTag),
    .io_chi_0_tx_dat_ready                                (io_chi_0_tx_dat_ready),
    .io_chi_0_tx_dat_valid                                (io_chi_0_tx_dat_valid),
    .io_chi_0_tx_dat_bits_qos                             (io_chi_0_tx_dat_bits_qos),
    .io_chi_0_tx_dat_bits_tgtID                           (io_chi_0_tx_dat_bits_tgtID),
    .io_chi_0_tx_dat_bits_srcID                           (io_chi_0_tx_dat_bits_srcID),
    .io_chi_0_tx_dat_bits_txnID                           (io_chi_0_tx_dat_bits_txnID),
    .io_chi_0_tx_dat_bits_homeNID                         (io_chi_0_tx_dat_bits_homeNID),
    .io_chi_0_tx_dat_bits_opcode                          (io_chi_0_tx_dat_bits_opcode),
    .io_chi_0_tx_dat_bits_respErr                         (io_chi_0_tx_dat_bits_respErr),
    .io_chi_0_tx_dat_bits_resp                            (io_chi_0_tx_dat_bits_resp),
    .io_chi_0_tx_dat_bits_dataSource                      (io_chi_0_tx_dat_bits_dataSource),
    .io_chi_0_tx_dat_bits_cBusy                           (io_chi_0_tx_dat_bits_cBusy),
    .io_chi_0_tx_dat_bits_dbID                            (io_chi_0_tx_dat_bits_dbID),
    .io_chi_0_tx_dat_bits_ccID                            (io_chi_0_tx_dat_bits_ccID),
    .io_chi_0_tx_dat_bits_dataID                          (io_chi_0_tx_dat_bits_dataID),
    .io_chi_0_tx_dat_bits_tagOp                           (io_chi_0_tx_dat_bits_tagOp),
    .io_chi_0_tx_dat_bits_tag                             (io_chi_0_tx_dat_bits_tag),
    .io_chi_0_tx_dat_bits_tu                              (io_chi_0_tx_dat_bits_tu),
    .io_chi_0_tx_dat_bits_traceTag                        (io_chi_0_tx_dat_bits_traceTag),
    .io_chi_0_tx_dat_bits_rsvdc                           (io_chi_0_tx_dat_bits_rsvdc),
    .io_chi_0_tx_dat_bits_be                              (io_chi_0_tx_dat_bits_be),
    .io_chi_0_tx_dat_bits_data                            (io_chi_0_tx_dat_bits_data),
    .io_chi_0_tx_dat_bits_dataCheck                       (io_chi_0_tx_dat_bits_dataCheck),
    .io_chi_0_tx_dat_bits_poison                          (io_chi_0_tx_dat_bits_poison),
    .io_chi_0_rx_rsp_ready                                (io_chi_0_rx_rsp_ready),
    .io_chi_0_rx_rsp_valid                                (io_chi_0_rx_rsp_valid),
    .io_chi_0_rx_rsp_bits_qos                             (io_chi_0_rx_rsp_bits_qos),
    .io_chi_0_rx_rsp_bits_tgtID                           (io_chi_0_rx_rsp_bits_tgtID),
    .io_chi_0_rx_rsp_bits_srcID                           (io_chi_0_rx_rsp_bits_srcID),
    .io_chi_0_rx_rsp_bits_txnID                           (io_chi_0_rx_rsp_bits_txnID),
    .io_chi_0_rx_rsp_bits_opcode                          (io_chi_0_rx_rsp_bits_opcode),
    .io_chi_0_rx_rsp_bits_respErr                         (io_chi_0_rx_rsp_bits_respErr),
    .io_chi_0_rx_rsp_bits_resp                            (io_chi_0_rx_rsp_bits_resp),
    .io_chi_0_rx_rsp_bits_fwdState                        (io_chi_0_rx_rsp_bits_fwdState),
    .io_chi_0_rx_rsp_bits_cBusy                           (io_chi_0_rx_rsp_bits_cBusy),
    .io_chi_0_rx_rsp_bits_dbID                            (io_chi_0_rx_rsp_bits_dbID),
    .io_chi_0_rx_rsp_bits_pCrdType                        (io_chi_0_rx_rsp_bits_pCrdType),
    .io_chi_0_rx_rsp_bits_tagOp                           (io_chi_0_rx_rsp_bits_tagOp),
    .io_chi_0_rx_rsp_bits_traceTag                        (io_chi_0_rx_rsp_bits_traceTag),
    .io_chi_0_rx_dat_ready                                (io_chi_0_rx_dat_ready),
    .io_chi_0_rx_dat_valid                                (io_chi_0_rx_dat_valid),
    .io_chi_0_rx_dat_bits_qos                             (io_chi_0_rx_dat_bits_qos),
    .io_chi_0_rx_dat_bits_tgtID                           (io_chi_0_rx_dat_bits_tgtID),
    .io_chi_0_rx_dat_bits_srcID                           (io_chi_0_rx_dat_bits_srcID),
    .io_chi_0_rx_dat_bits_txnID                           (io_chi_0_rx_dat_bits_txnID),
    .io_chi_0_rx_dat_bits_homeNID                         (io_chi_0_rx_dat_bits_homeNID),
    .io_chi_0_rx_dat_bits_opcode                          (io_chi_0_rx_dat_bits_opcode),
    .io_chi_0_rx_dat_bits_respErr                         (io_chi_0_rx_dat_bits_respErr),
    .io_chi_0_rx_dat_bits_resp                            (io_chi_0_rx_dat_bits_resp),
    .io_chi_0_rx_dat_bits_dataSource                      (io_chi_0_rx_dat_bits_dataSource),
    .io_chi_0_rx_dat_bits_cBusy                           (io_chi_0_rx_dat_bits_cBusy),
    .io_chi_0_rx_dat_bits_dbID                            (io_chi_0_rx_dat_bits_dbID),
    .io_chi_0_rx_dat_bits_ccID                            (io_chi_0_rx_dat_bits_ccID),
    .io_chi_0_rx_dat_bits_dataID                          (io_chi_0_rx_dat_bits_dataID),
    .io_chi_0_rx_dat_bits_tagOp                           (io_chi_0_rx_dat_bits_tagOp),
    .io_chi_0_rx_dat_bits_tag                             (io_chi_0_rx_dat_bits_tag),
    .io_chi_0_rx_dat_bits_tu                              (io_chi_0_rx_dat_bits_tu),
    .io_chi_0_rx_dat_bits_traceTag                        (io_chi_0_rx_dat_bits_traceTag),
    .io_chi_0_rx_dat_bits_rsvdc                           (io_chi_0_rx_dat_bits_rsvdc),
    .io_chi_0_rx_dat_bits_be                              (io_chi_0_rx_dat_bits_be),
    .io_chi_0_rx_dat_bits_data                            (io_chi_0_rx_dat_bits_data),
    .io_chi_0_rx_dat_bits_dataCheck                       (io_chi_0_rx_dat_bits_dataCheck),
    .io_chi_0_rx_dat_bits_poison                          (io_chi_0_rx_dat_bits_poison),
    .io_chi_0_rx_snp_ready                                (io_chi_0_rx_snp_ready),
    .io_chi_0_rx_snp_valid                                (io_chi_0_rx_snp_valid),
    .io_chi_0_rx_snp_bits_qos                             (io_chi_0_rx_snp_bits_qos),
    .io_chi_0_rx_snp_bits_srcID                           (io_chi_0_rx_snp_bits_srcID),
    .io_chi_0_rx_snp_bits_txnID                           (io_chi_0_rx_snp_bits_txnID),
    .io_chi_0_rx_snp_bits_fwdNID                          (io_chi_0_rx_snp_bits_fwdNID),
    .io_chi_0_rx_snp_bits_fwdTxnID                        (io_chi_0_rx_snp_bits_fwdTxnID),
    .io_chi_0_rx_snp_bits_opcode                          (io_chi_0_rx_snp_bits_opcode),
    .io_chi_0_rx_snp_bits_addr                            (io_chi_0_rx_snp_bits_addr),
    .io_chi_0_rx_snp_bits_ns                              (io_chi_0_rx_snp_bits_ns),
    .io_chi_0_rx_snp_bits_doNotGoToSD                     (io_chi_0_rx_snp_bits_doNotGoToSD),
    .io_chi_0_rx_snp_bits_retToSrc                        (io_chi_0_rx_snp_bits_retToSrc),
    .io_chi_0_rx_snp_bits_traceTag                        (io_chi_0_rx_snp_bits_traceTag),
    .io_chi_0_rx_snp_bits_mpam_perfMonGroup               (io_chi_0_rx_snp_bits_mpam_perfMonGroup),
    .io_chi_0_rx_snp_bits_mpam_partID                     (io_chi_0_rx_snp_bits_mpam_partID),
    .io_chi_0_rx_snp_bits_mpam_mpamNS                     (io_chi_0_rx_snp_bits_mpam_mpamNS)
  );

  always #5 clock = ~clock;

  // =====================================================================
  // Reset & initialization (mirrors v3_main: 10 reset cycles, then the
  // tl-test subsystem is already initialized before the first TileLink
  // cycle; sim_time counts two steps per cycle like the Verilator time)
  // =====================================================================
  initial begin
    clock     = 1'b0;
    reset     = 1'b1;
    timer     = 64'b0;
    logEnable = 1'b0;
    clean     = 1'b0;
    dump      = 1'b0;
    mdat_ready = 1'b0;
    sim_time  = 0;

    // quiesce DUT inputs before the first DPI cycle
    p0_a_valid = 0; p0_a_opcode = 0; p0_a_param = 0; p0_a_size = 0;
    p0_a_source = 0; p0_a_address = 0; p0_a_alias = 0;
    p0_a_mask = 0; p0_a_data = 0; p0_a_corrupt = 0;
    p0_b_ready = 0;
    p0_c_valid = 0; p0_c_opcode = 0; p0_c_param = 0; p0_c_size = 0;
    p0_c_source = 0;
    p0_c_address = 0; p0_c_alias = 0; p0_c_data = 0; p0_c_corrupt = 0;
    p0_d_ready = 0;
    p0_e_valid = 0; p0_e_sink = 0;
    p2_a_valid = 0; p2_a_opcode = 0; p2_a_param = 0; p2_a_size = 0;
    p2_a_source = 0; p2_a_address = 0; p2_a_ameIndex = 0; p2_a_matrix = 0;
    p2_a_mask = 0; p2_a_data = 0; p2_a_corrupt = 0;
    p2_d_ready = 0;
    mmio_a_valid = 0; mmio_a_opcode = 0; mmio_a_param = 0; mmio_a_size = 0;
    mmio_a_source = 0; mmio_a_address = 0; mmio_a_mask = 0; mmio_a_data = 0;
    mmio_a_corrupt = 0; mmio_d_ready = 0;
    // quiesce the CHI rx side before the first DPI drive: SN always ready,
    // no responses in flight, no snoops
    io_chi_0_tx_req_ready = 1'b1;
    io_chi_0_tx_rsp_ready = 1'b1;
    io_chi_0_tx_dat_ready = 1'b1;
    io_chi_0_rx_rsp_valid = 1'b0;
    io_chi_0_rx_rsp_bits_opcode = '0; io_chi_0_rx_rsp_bits_txnID = '0;
    io_chi_0_rx_rsp_bits_dbID = '0;
    io_chi_0_rx_dat_valid = 1'b0;
    io_chi_0_rx_dat_bits_opcode = '0; io_chi_0_rx_dat_bits_txnID = '0;
    io_chi_0_rx_dat_bits_dataID = '0; io_chi_0_rx_dat_bits_dataSource = '0;
    io_chi_0_rx_dat_bits_data = '0; io_chi_0_rx_dat_bits_dataCheck = '0;

    TileLinkSystemInitialize();
    ChiSnInit();

    repeat (10) @(negedge clock);
    #1 reset = 1'b0;
    sim_time = 20;
  end

  // =====================================================================
  // Per-cycle DPI exchange, channel order A -> B -> C -> E -> D as
  // required by main/DPI/tilelink_dpi.cpp
  // =====================================================================

  // scratch holders matching the Sv* wrapper widths
  logic         pu_valid;
  logic [3:0]   pu_a_opcode;
  logic [2:0]   pu_param;
  logic [2:0]   pu_size;
  logic [63:0]  pu_source;
  logic [47:0]  pu_address;
  logic         pu_needHint;
  logic [35:0]  pu_vaddr;
  logic [1:0]   pu_alias;
  logic [31:0]  pu_mask;
  logic [255:0] pu_data;
  logic         pu_corrupt;
  logic [63:0]  pu_sink;
  byte          pu_matrix;
  byte          pu_ameIndex;
  byte          pu_m_ready;

  // MMIO pull holders (MMIOAgent A/D only). Match the DPI-C scalar types
  // exactly so VCS copies every output back.
  byte          mmio_pu_valid;
  byte          mmio_pu_opcode;
  byte          mmio_pu_param;
  byte          mmio_pu_size;
  byte          mmio_pu_mask;
  longint       mmio_pu_source;
  longint       mmio_pu_address;
  longint       mmio_pu_data;
  byte          mmio_pu_corrupt;

  // Save TL-C D at the actual handshake edge. Reading live D at negedge
  // can see the next queue head after ready rises and lose the consumed beat.
  typedef struct packed {
    logic valid;
    logic [3:0] opcode;
    logic [1:0] param;
    logic [2:0] size;
    logic [4:0] source;
    logic [7:0] sink;
    logic denied;
    logic [255:0] data;
    logic corrupt;
  } cagent_d_sample_t;
  cagent_d_sample_t p0_d_sampled;
  always @(posedge clock) begin
    if (reset) p0_d_sampled <= '0;
    else p0_d_sampled <= '{p0_d_valid, p0_d_opcode, p0_d_param, p0_d_size,
                           p0_d_source, p0_d_sink, p0_d_denied, p0_d_data, p0_d_corrupt};
  end


  always @(negedge clock) begin
    if (!reset) begin
      #1ps;

      // ---- ChiSnCore drive (SN -> DUT): rx valid/bits and tx readys.
      //      Values settle here at the negedge, one full half-cycle before
      //      the DUT samples them at the posedge.
      ChiSnDrive(
        io_chi_0_tx_req_ready, io_chi_0_tx_rsp_ready, io_chi_0_tx_dat_ready,
        io_chi_0_rx_rsp_valid, io_chi_0_rx_rsp_bits_opcode,
        io_chi_0_rx_rsp_bits_txnID, io_chi_0_rx_rsp_bits_dbID,
        io_chi_0_rx_dat_valid, io_chi_0_rx_dat_bits_opcode,
        io_chi_0_rx_dat_bits_txnID, io_chi_0_rx_dat_bits_dataID,
        io_chi_0_rx_dat_bits_dataSource, io_chi_0_rx_dat_bits_data,
        io_chi_0_rx_dat_bits_dataCheck
      );

      // ---- Tick ----
      TileLinkSystemTick(sim_time);

      // ---- Push valid+bits (DUT outputs -> tl-test) ----
      // TL-C D uses the saved posedge fields so changing D.ready cannot
      // make Tock observe a queue head from the next transaction.
      // The other channels retain their existing adapter sampling.
      SvTileLinkPushChannelB(DEV_L1D, 1'b1, p0_b_valid, p0_b_opcode, p0_b_param,
                             p0_b_size, {{59{1'b0}}, p0_b_source},
                             p0_b_address, p0_b_mask, p0_b_data, p0_b_corrupt);
      SvTileLinkPushChannelD(DEV_L1D, 1'b1, p0_d_sampled.valid, p0_d_sampled.opcode, p0_d_sampled.param,
                             p0_d_sampled.size, {{59{1'b0}}, p0_d_sampled.source},
                             {{56{1'b0}}, p0_d_sampled.sink}, p0_d_sampled.denied,
                             p0_d_sampled.data, p0_d_sampled.corrupt);

      SvTileLinkPushChannelD(DEV_MATRIX, 1'b1, p2_d_valid, p2_d_opcode, p2_d_param,
                             p2_d_size, {{59{1'b0}}, p2_d_source},
                             {{56{1'b0}}, p2_d_sink}, p2_d_denied, p2_d_data, p2_d_corrupt);

      // MMIO client: A ready and D response are DUT outputs.
      SvTileLinkMMIOPushChannelA(DEV_MMIO, !reset, mmio_a_ready_v);
      SvTileLinkMMIOPushChannelD(DEV_MMIO, !reset, mmio_d_valid_v, mmio_d_opcode,
                                 mmio_d_param, {2'b0, mmio_d_size},
                                 {{60{1'b0}}, mmio_d_source},
                                 {{63{1'b0}}, mmio_d_sink},
                                 mmio_d_denied, mmio_d_data, mmio_d_corrupt);
      TileLinkMatrixPushChannelM(DEV_MATRIX, mdat_valid,
                                 {{57{1'b0}}, mdat_sourceId},
                                 mdat_data[63:0],   mdat_data[127:64], mdat_data[191:128],
                                 mdat_data[255:192],mdat_data[319:256],mdat_data[383:320],
                                 mdat_data[447:384],mdat_data[511:448]);

      // ---- Tock ----
      // Fires beats of the preceding posedge, using the ready values pushed
      // at the END of the previous cycle (see below) — like v3_main, whose
      // Tock reads the A/C/E ready sampled after Tock of the prior iteration.
      TileLinkSystemTock();

      // ---- Pull (tl-test -> DUT inputs) ----
      SvTileLinkPullChannelA(DEV_L1D, 1'b1, p0_a_valid, p0_a_opcode, p0_a_param,
                             pu_size, pu_source, pu_address, pu_needHint, pu_vaddr,
                             pu_alias, p0_a_mask, p0_a_data, p0_a_corrupt);
      p0_a_source  <= pu_source[4:0];
      p0_a_address <= pu_address;
      p0_a_alias   <= pu_alias;
      p0_a_size    <= pu_size;

      SvTileLinkPullChannelB(DEV_L1D, 1'b1, p0_b_ready);

      SvTileLinkPullChannelC(DEV_L1D, 1'b1, p0_c_valid, p0_c_opcode, p0_c_param,
                             pu_size, pu_source, pu_address, pu_needHint, pu_vaddr,
                             pu_alias, p0_c_data, p0_c_corrupt);
      p0_c_source  <= pu_source[4:0];
      p0_c_address <= pu_address;
      p0_c_alias   <= pu_alias;
      p0_c_size    <= pu_size;

      SvTileLinkPullChannelE(DEV_L1D, 1'b1, p0_e_valid, pu_sink);
      p0_e_sink <= pu_sink[7:0];

      SvTileLinkPullChannelD(DEV_L1D, 1'b1, p0_d_ready);

      SvTileLinkPullChannelA(DEV_MATRIX, 1'b1, p2_a_valid, p2_a_opcode, p2_a_param,
                             pu_size, pu_source, pu_address, pu_needHint, pu_vaddr,
                             pu_alias, p2_a_mask, p2_a_data, p2_a_corrupt);
      p2_a_source  <= pu_source[4:0];
      p2_a_address <= pu_address;
      p2_a_size    <= pu_size;
      TileLinkMatrixPullChannelAUser(DEV_MATRIX, pu_matrix, pu_ameIndex);
      p2_a_matrix   <= pu_matrix[1:0];
      p2_a_ameIndex <= {56'b0, pu_ameIndex[7:0]};

      SvTileLinkPullChannelD(DEV_MATRIX, 1'b1, p2_d_ready);
      TileLinkMatrixPullChannelMReady(DEV_MATRIX, pu_m_ready);
      mdat_ready <= pu_m_ready;

      // Call the imported DPI function directly: the SV wrapper's
      // multi-output copy-back is unreliable for the byte-wide fields here.
      TileLinkMMIOPullChannelA(DEV_MMIO, mmio_pu_valid, mmio_pu_opcode,
                               mmio_pu_param, mmio_pu_size, mmio_pu_source,
                               mmio_pu_address, mmio_pu_mask, mmio_pu_data,
                               mmio_pu_corrupt);
      mmio_a_valid   <= mmio_pu_valid[0];
      mmio_a_opcode  <= mmio_pu_opcode[3:0];
      mmio_a_param   <= mmio_pu_param[2:0];
      mmio_a_size    <= mmio_pu_size[1:0];
      mmio_a_source  <= mmio_pu_source[3:0];
      mmio_a_address <= mmio_pu_address;
      mmio_a_mask    <= mmio_pu_mask;
      mmio_a_data    <= mmio_pu_data;
      mmio_a_corrupt <= mmio_pu_corrupt[0];

      SvTileLinkMMIOPullChannelD(DEV_MMIO, !reset, mmio_d_ready);

      // ---- Push readys (DUT outputs -> tl-test), late phase ----
      // The nonblocking assignments above land at the end of this timestep;
      // after one more delta the DUT's combinational ready outputs settle
      // with the NEW request applied. Sampling here gives exactly the ready
      // that will gate the request at the upcoming posedge — mirroring
      // v3_main's PushA/PushC/PushE placement (after Tock, before posedge).
      // The next Tock reads these values; sampling them any earlier desyncs
      // the C++ fire accounting whenever the DUT pulses a ready low.
      #1ps;
      SvTileLinkPushChannelA(DEV_L1D, 1'b1, p0_a_ready);
      SvTileLinkPushChannelC(DEV_L1D, 1'b1, p0_c_ready);
      SvTileLinkPushChannelE(DEV_L1D, 1'b1, p0_e_ready);

      SvTileLinkPushChannelA(DEV_MATRIX, 1'b1, p2_a_ready);

      // v3_main's build logs "Not passing ticking time to verilated TestTop":
      // the Verilator baseline never drives timer, so keep it at 0 here too.
      sim_time <= sim_time + 2;
    end
  end

  // =====================================================================
  // ChiSnCore observe (DUT -> SN): handshake fires are sampled in the
  // active region of the posedge, the same pre-edge point CUTE-Test's
  // harness uses; the model's protocol checker failures abort the run.
  // =====================================================================
  always @(posedge clock) begin
    if (!reset) begin
      ChiSnObserve(
        io_chi_0_tx_req_valid, io_chi_0_tx_req_bits_opcode,
        io_chi_0_tx_req_bits_txnID, io_chi_0_tx_req_bits_size,
        io_chi_0_tx_req_bits_addr,
        io_chi_0_tx_rsp_valid, io_chi_0_tx_rsp_bits_opcode,
        io_chi_0_tx_rsp_bits_txnID,
        io_chi_0_tx_dat_valid, io_chi_0_tx_dat_bits_opcode,
        io_chi_0_tx_dat_bits_txnID, io_chi_0_tx_dat_bits_dataID,
        io_chi_0_tx_dat_bits_be, io_chi_0_tx_dat_bits_dataCheck,
        io_chi_0_tx_dat_bits_data,
        io_chi_0_rx_rsp_ready, io_chi_0_rx_dat_ready
      );
      if (ChiSnHasError())
        $fatal(1, "ChiSnCore: %s", ChiSnGetMessage());
    end
  end

  // =====================================================================
  // Debug waveform window: +vcd_start=<sim_time> +vcd_stop=<sim_time>
  // (compile with -debug_access+all). Dumps the full hierarchy only inside
  // the window so post-mortem analysis of a hang stays a manageable VCD.
  // =====================================================================
  longint unsigned vcd_start, vcd_stop;
  bit vcd_on;
  initial begin
    if (!$value$plusargs("vcd_start=%d", vcd_start)) vcd_start = '1;
    if (!$value$plusargs("vcd_stop=%d",  vcd_stop))  vcd_stop  = '1;
  end
  always @(negedge clock) begin
    if (!vcd_on && sim_time >= vcd_start && sim_time < vcd_stop) begin
      $dumpfile("wave.vcd");
      $dumpvars(0, tltest_vcs_tb);
      vcd_on = 1'b1;
    end else if (vcd_on && sim_time >= vcd_stop) begin
      $dumpoff();
      vcd_on = 1'b0;
    end
  end

  // =====================================================================
  // Completion check, same criteria as the v3 main loop exit
  // =====================================================================
  always @(posedge clock) begin
    if (!reset) begin
      if ((sim_time % 10000) == 0)
        $display("[%0d] [tl-test-new] Simulation time elapsed: %0dps", sim_time, sim_time);

      if (TileLinkSystemIsAlive() == 0) begin
        if (TileLinkSystemIsFinished() != 0) begin
          $display("[%0d] [tl-test-new-FINAL] tl-test subsystem FINISHED.", sim_time);
          TileLinkSystemFinalize();
          $finish;
        end else begin
          TileLinkSystemFinalize();
          $fatal(1, "[%0d] [tl-test-new-FINAL] tl-test subsystem FAILED / no longer alive", sim_time);
        end
      end
    end
  end

endmodule
