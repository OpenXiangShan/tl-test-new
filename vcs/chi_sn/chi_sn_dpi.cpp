// DPI glue that binds ChiSnCore -- the pure-logic CHI Subordinate Node
// functional model developed in CUTE-Test (src/chi_sn_core.cpp, synced from
// CUTE-Test master 168dae7 which includes the TxnID-reuse/DBID-grant-window
// fix of PR #6) -- to the tl-test L2SN testbench (vcs/tb/vcs_tb_l2sn.sv).
//
// The per-cycle wire semantics follow CUTE-Test's cute_vcs_dpi_l2.cpp so both
// harnesses drive the same CoupledL2 identically:
//   - the SN never backpressures the DUT (tx.req/rsp/dat ready constantly 1)
//   - CompData beats carry wire dataID = beat_index * 2
//   - the 256-bit DAT payload is 8 little-endian 32-bit words
//   - dataCheck is odd parity per byte (L2Param dataCheck = "oddparity")
//   - write data beats are matched by the granted DBID, never by TxnID
// The tl-test runner owns stimulus and completion; this glue only moves CHI
// flits across the DPI boundary and surfaces the model's protocol checker.

#include <cstdint>
#include <cstdlib>
#include <string>

#include "svdpi.h"

#include "cute_verification/chi_sn_core.h"

namespace {

using cute::verification::ChiDat;
using cute::verification::ChiReq;
using cute::verification::ChiRsp;
using cute::verification::ChiSnCore;
using cute::verification::kChiDataBytes;

constexpr uint64_t kDefaultResponseLatency = 8;

bool is_one(svLogic value) { return value == sv_1; }

uint64_t get_vector(const svLogicVecVal* value, size_t words) {
  uint64_t result = 0;
  for (size_t index = 0; index < words && index < 2; ++index) {
    result |= static_cast<uint64_t>(value[index].aval) << (index * 32);
  }
  return result;
}

void put_vector(svLogicVecVal* destination, size_t words,
                const uint32_t* source, size_t source_words) {
  for (size_t index = 0; index < words; ++index) {
    destination[index].aval = index < source_words ? source[index] : 0;
    destination[index].bval = 0;
  }
}

void put_scalar(svLogic* destination, bool value) {
  *destination = value ? sv_1 : sv_0;
}

class Runner {
 public:
  void initialize() {
    core_ = ChiSnCore(kDefaultResponseLatency);
    const char* latency_env = std::getenv("TLTEST_CHISN_LATENCY");
    if (latency_env != nullptr && latency_env[0] != '\0') {
      char* end = nullptr;
      const unsigned long long parsed = std::strtoull(latency_env, &end, 10);
      if (end != nullptr && *end == '\0' && parsed > 0) {
        core_.set_response_latency(parsed);
      }
    }
  }

  void drive(svLogic* tx_req_ready, svLogic* tx_rsp_ready,
             svLogic* tx_dat_ready, svLogic* rx_rsp_valid,
             svLogicVecVal* rx_rsp_opcode, svLogicVecVal* rx_rsp_txn,
             svLogicVecVal* rx_rsp_dbid, svLogic* rx_dat_valid,
             svLogicVecVal* rx_dat_opcode, svLogicVecVal* rx_dat_txn,
             svLogicVecVal* rx_dat_dataid, svLogicVecVal* rx_dat_datasrc,
             svLogicVecVal* rx_dat_data, svLogicVecVal* rx_dat_check) {
    // The SN never backpressures the DUT (same as ChiSnModel).
    put_scalar(tx_req_ready, true);
    put_scalar(tx_rsp_ready, true);
    put_scalar(tx_dat_ready, true);

    const bool rsp_valid = core_.rsp_out_valid();
    rsp_out_driven_ = rsp_valid;
    put_scalar(rx_rsp_valid, rsp_valid);
    if (rsp_valid) {
      const ChiRsp& rsp = core_.rsp_out();
      const uint32_t opcode = rsp.opcode;
      const uint32_t txn = rsp.txn_id;
      const uint32_t dbid = rsp.db_id;
      put_vector(rx_rsp_opcode, 1, &opcode, 1);
      put_vector(rx_rsp_txn, 1, &txn, 1);
      put_vector(rx_rsp_dbid, 1, &dbid, 1);
    } else {
      put_vector(rx_rsp_opcode, 1, nullptr, 0);
      put_vector(rx_rsp_txn, 1, nullptr, 0);
      put_vector(rx_rsp_dbid, 1, nullptr, 0);
    }

    const bool dat_valid = core_.dat_out_valid();
    dat_out_driven_ = dat_valid;
    put_scalar(rx_dat_valid, dat_valid);
    if (dat_valid) {
      const ChiDat& dat = core_.dat_out();
      const uint32_t opcode = dat.opcode;
      const uint32_t txn = dat.txn_id;
      // On the wire dataID counts 32B halves (first=0, last=2).
      const uint32_t dataid = dat.data_id * 2;
      const uint32_t datasource = 0;
      put_vector(rx_dat_opcode, 1, &opcode, 1);
      put_vector(rx_dat_txn, 1, &txn, 1);
      put_vector(rx_dat_dataid, 1, &dataid, 1);
      put_vector(rx_dat_datasrc, 1, &datasource, 1);
      // 256-bit port = 8 x 32-bit words, little-endian bytes per word.
      uint32_t words[8];
      for (size_t word = 0; word < 8; ++word) {
        uint32_t packed = 0;
        for (size_t b = 0; b < 4; ++b) {
          packed |= static_cast<uint32_t>(dat.data[word * 4 + b]) << (8 * b);
        }
        words[word] = packed;
      }
      put_vector(rx_dat_data, 8, words, 8);
      // L2Param dataCheck = "oddparity": bit i complements byte i so the
      // data+check byte pair always has odd weight.
      uint32_t data_check = 0;
      for (size_t i = 0; i < kChiDataBytes; ++i) {
        if ((__builtin_popcount(dat.data[i]) & 1) == 0) {
          data_check |= 1u << i;
        }
      }
      put_vector(rx_dat_check, 1, &data_check, 1);
    } else {
      put_vector(rx_dat_opcode, 1, nullptr, 0);
      put_vector(rx_dat_txn, 1, nullptr, 0);
      put_vector(rx_dat_dataid, 1, nullptr, 0);
      put_vector(rx_dat_datasrc, 1, nullptr, 0);
      put_vector(rx_dat_data, 8, nullptr, 0);
      put_vector(rx_dat_check, 1, nullptr, 0);
    }
  }

  void observe(svLogic tx_req_valid, const svLogicVecVal* tx_req_opcode,
               const svLogicVecVal* tx_req_txn,
               const svLogicVecVal* tx_req_size,
               const svLogicVecVal* tx_req_addr, svLogic tx_rsp_valid,
               const svLogicVecVal* tx_rsp_opcode,
               const svLogicVecVal* tx_rsp_txn, svLogic tx_dat_valid,
               const svLogicVecVal* tx_dat_opcode,
               const svLogicVecVal* tx_dat_txn,
               const svLogicVecVal* tx_dat_dataid,
               const svLogicVecVal* tx_dat_be,
               const svLogicVecVal* tx_dat_check,
               const svLogicVecVal* tx_dat_data, svLogic rx_rsp_ready,
               svLogic rx_dat_ready) {
    // --- CHI handshake observation (SN side ready is constantly 1) ---
    if (is_one(tx_req_valid)) {
      ChiReq req;
      req.opcode = static_cast<uint32_t>(get_vector(tx_req_opcode, 1));
      req.txn_id = static_cast<uint32_t>(get_vector(tx_req_txn, 1));
      req.size = static_cast<uint8_t>(get_vector(tx_req_size, 1));
      req.addr = get_vector(tx_req_addr, 2);
      core_.on_req_fire(req);
    }
    if (is_one(tx_dat_valid)) {
      ChiDat dat;
      dat.opcode = static_cast<uint32_t>(get_vector(tx_dat_opcode, 1));
      dat.txn_id = static_cast<uint32_t>(get_vector(tx_dat_txn, 1));
      dat.data_id = static_cast<uint32_t>(get_vector(tx_dat_dataid, 1)) / 2;
      dat.be = static_cast<uint32_t>(get_vector(tx_dat_be, 1));
      dat.data_check = static_cast<uint32_t>(get_vector(tx_dat_check, 1));
      for (size_t word = 0; word < 8; ++word) {
        const uint32_t packed = static_cast<uint32_t>(
            get_vector(tx_dat_data + word, 1));
        for (size_t b = 0; b < 4; ++b) {
          dat.data[word * 4 + b] = static_cast<uint8_t>(packed >> (8 * b));
        }
      }
      core_.on_dat_fire(dat);
    }
    if (is_one(tx_rsp_valid)) {
      ChiRsp rsp;
      rsp.opcode = static_cast<uint32_t>(get_vector(tx_rsp_opcode, 1));
      rsp.txn_id = static_cast<uint32_t>(get_vector(tx_rsp_txn, 1));
      core_.on_rsp_fire(rsp);
    }
    if (rsp_out_driven_ && is_one(rx_rsp_ready)) core_.rsp_out_consume();
    if (dat_out_driven_ && is_one(rx_dat_ready)) core_.dat_out_consume();
    core_.tick();
  }

  bool has_error() const { return !core_.error().empty(); }
  const std::string& message() const { return core_.error(); }

 private:
  ChiSnCore core_{kDefaultResponseLatency};
  bool rsp_out_driven_ = false;
  bool dat_out_driven_ = false;
};

Runner runner;

}  // namespace

extern "C" void ChiSnInit() { runner.initialize(); }

extern "C" void ChiSnDrive(
    svLogic* tx_req_ready, svLogic* tx_rsp_ready, svLogic* tx_dat_ready,
    svLogic* rx_rsp_valid, svLogicVecVal* rx_rsp_opcode,
    svLogicVecVal* rx_rsp_txn, svLogicVecVal* rx_rsp_dbid,
    svLogic* rx_dat_valid, svLogicVecVal* rx_dat_opcode,
    svLogicVecVal* rx_dat_txn, svLogicVecVal* rx_dat_dataid,
    svLogicVecVal* rx_dat_datasrc, svLogicVecVal* rx_dat_data,
    svLogicVecVal* rx_dat_check) {
  runner.drive(tx_req_ready, tx_rsp_ready, tx_dat_ready, rx_rsp_valid,
               rx_rsp_opcode, rx_rsp_txn, rx_rsp_dbid, rx_dat_valid,
               rx_dat_opcode, rx_dat_txn, rx_dat_dataid, rx_dat_datasrc,
               rx_dat_data, rx_dat_check);
}

extern "C" void ChiSnObserve(
    svLogic tx_req_valid, const svLogicVecVal* tx_req_opcode,
    const svLogicVecVal* tx_req_txn, const svLogicVecVal* tx_req_size,
    const svLogicVecVal* tx_req_addr, svLogic tx_rsp_valid,
    const svLogicVecVal* tx_rsp_opcode, const svLogicVecVal* tx_rsp_txn,
    svLogic tx_dat_valid, const svLogicVecVal* tx_dat_opcode,
    const svLogicVecVal* tx_dat_txn, const svLogicVecVal* tx_dat_dataid,
    const svLogicVecVal* tx_dat_be, const svLogicVecVal* tx_dat_check,
    const svLogicVecVal* tx_dat_data, svLogic rx_rsp_ready,
    svLogic rx_dat_ready) {
  runner.observe(tx_req_valid, tx_req_opcode, tx_req_txn, tx_req_size,
                 tx_req_addr, tx_rsp_valid, tx_rsp_opcode, tx_rsp_txn,
                 tx_dat_valid, tx_dat_opcode, tx_dat_txn, tx_dat_dataid,
                 tx_dat_be, tx_dat_check, tx_dat_data, rx_rsp_ready,
                 rx_dat_ready);
}

extern "C" unsigned char ChiSnHasError() {
  return runner.has_error() ? 1 : 0;
}

extern "C" const char* ChiSnGetMessage() { return runner.message().c_str(); }
