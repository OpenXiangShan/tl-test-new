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
#include <cstdio>
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
    snoop_schedule_.clear();
    const char* latency_env = std::getenv("TLTEST_CHISN_LATENCY");
    if (latency_env != nullptr && latency_env[0] != '\0') {
      char* end = nullptr;
      const unsigned long long parsed = std::strtoull(latency_env, &end, 10);
      if (end != nullptr && *end == '\0' && parsed > 0) {
        core_.set_response_latency(parsed);
      }
    }
  }

  // Stimulus hook: hand a snoop to the model once the due cycle arrives.
  // Called from drive() (the negedge before the posedge the DUT samples
  // on), so a due_cycle of N puts the RXSNP flit on the wires during
  // model cycle N.
  void queue_snoop(int due_cycle, uint64_t addr, int opcode, bool ret) {
    ScheduledSnoop snoop;
    snoop.due_cycle = due_cycle;
    snoop.addr = addr;
    snoop.opcode = static_cast<uint32_t>(opcode);
    snoop.ret_to_src = ret;
    snoop_schedule_.push_back(snoop);
  }

  std::string snoop_summary() const { return core_.snoop_summary(); }

  std::string dump_mem(uint64_t addr, int len) const {
    std::string out;
    const std::vector<uint8_t> bytes = core_.read(addr, len);
    char hex[4];
    for (size_t i = 0; i < bytes.size(); ++i) {
      std::snprintf(hex, sizeof(hex), "%02x", bytes[i]);
      out += hex;
    }
    return out;
  }

  void drive(svLogic* tx_req_ready, svLogic* tx_rsp_ready,
             svLogic* tx_dat_ready, svLogic* rx_rsp_valid,
             svLogicVecVal* rx_rsp_opcode, svLogicVecVal* rx_rsp_txn,
             svLogicVecVal* rx_rsp_dbid, svLogic* rx_dat_valid,
             svLogicVecVal* rx_dat_opcode, svLogicVecVal* rx_dat_txn,
             svLogicVecVal* rx_dat_dataid, svLogicVecVal* rx_dat_datasrc,
             svLogicVecVal* rx_dat_data, svLogicVecVal* rx_dat_check,
             svLogic* rx_snp_valid, svLogicVecVal* rx_snp_opcode,
             svLogicVecVal* rx_snp_txn, svLogicVecVal* rx_snp_srcid,
             svLogicVecVal* rx_snp_addr, svLogic* rx_snp_ret) {
    // Release every scheduled snoop whose due cycle has come (issue_snoop
    // presents the flit immediately when the SNP slot is free).
    if (!snoop_schedule_.empty()) {
      const uint64_t now = core_.cycle();
      for (auto it = snoop_schedule_.begin(); it != snoop_schedule_.end();) {
        if (static_cast<uint64_t>(it->due_cycle) <= now) {
          std::printf("[ChiSnSnp] issue op=0x%x addr=0x%llx ret=%d at model cycle %llu\n",
                      it->opcode, static_cast<unsigned long long>(it->addr),
                      it->ret_to_src ? 1 : 0,
                      static_cast<unsigned long long>(now));
          core_.issue_snoop(it->addr, it->opcode, it->ret_to_src);
          it = snoop_schedule_.erase(it);
        } else {
          ++it;
        }
      }
    }

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

    // RXSNP: the wire address drops the 3 low zero bits (RXSNP.scala
    // rebuilds it with Cat(snp.addr, 0.U(3.W))), and 45 bits fit two
    // svLogicVecVal words.
    const bool snp_valid = core_.snp_out_valid();
    snp_out_driven_ = snp_valid;
    put_scalar(rx_snp_valid, snp_valid);
    if (snp_valid) {
      const cute::verification::ChiSnp& snp = core_.snp_out();
      const uint32_t opcode = snp.opcode;
      const uint32_t txn = snp.txn_id;
      const uint32_t srcid = snp.src_id;
      const uint64_t addr = snp.addr >> 3;
      const uint32_t words[2] = {static_cast<uint32_t>(addr & 0xffffffffu),
                                 static_cast<uint32_t>(addr >> 32)};
      const uint32_t ret = snp.ret_to_src ? 1 : 0;
      put_vector(rx_snp_opcode, 1, &opcode, 1);
      put_vector(rx_snp_txn, 1, &txn, 1);
      put_vector(rx_snp_srcid, 1, &srcid, 1);
      put_vector(rx_snp_addr, 2, words, 2);
      put_scalar(rx_snp_ret, ret != 0);
    } else {
      put_vector(rx_snp_opcode, 1, nullptr, 0);
      put_vector(rx_snp_txn, 1, nullptr, 0);
      put_vector(rx_snp_srcid, 1, nullptr, 0);
      put_vector(rx_snp_addr, 2, nullptr, 0);
      put_scalar(rx_snp_ret, false);
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
               svLogic rx_dat_ready, const svLogicVecVal* tx_rsp_resp,
               const svLogicVecVal* tx_dat_resp, svLogic rx_snp_ready) {
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
      dat.resp = static_cast<uint32_t>(get_vector(tx_dat_resp, 1));
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
      rsp.resp = static_cast<uint32_t>(get_vector(tx_rsp_resp, 1));
      core_.on_rsp_fire(rsp);
    }
    if (rsp_out_driven_ && is_one(rx_rsp_ready)) core_.rsp_out_consume();
    if (dat_out_driven_ && is_one(rx_dat_ready)) core_.dat_out_consume();
    if (snp_out_driven_ && is_one(rx_snp_ready)) core_.snp_out_consume();
    core_.tick();
  }

  bool has_error() const { return !core_.error().empty(); }
  const std::string& message() const { return core_.error(); }

 private:
  struct ScheduledSnoop {
    int due_cycle = 0;
    uint64_t addr = 0;
    uint32_t opcode = 0;
    bool ret_to_src = false;
  };

  ChiSnCore core_{kDefaultResponseLatency};
  std::vector<ScheduledSnoop> snoop_schedule_;
  bool rsp_out_driven_ = false;
  bool dat_out_driven_ = false;
  bool snp_out_driven_ = false;
};

Runner runner;

}  // namespace

extern "C" void ChiSnInit() { runner.initialize(); }

// Schedule an RXSNP injection: the flit goes out during model cycle
// due_cycle (parsed by the tb from the +chisnp=<file> schedule).
extern "C" void ChiSnQueueSnoop(int due_cycle, long long unsigned addr,
                                int opcode, unsigned char ret_to_src) {
  runner.queue_snoop(due_cycle, addr, opcode, ret_to_src != 0);
}

extern "C" void ChiSnDrive(
    svLogic* tx_req_ready, svLogic* tx_rsp_ready, svLogic* tx_dat_ready,
    svLogic* rx_rsp_valid, svLogicVecVal* rx_rsp_opcode,
    svLogicVecVal* rx_rsp_txn, svLogicVecVal* rx_rsp_dbid,
    svLogic* rx_dat_valid, svLogicVecVal* rx_dat_opcode,
    svLogicVecVal* rx_dat_txn, svLogicVecVal* rx_dat_dataid,
    svLogicVecVal* rx_dat_datasrc, svLogicVecVal* rx_dat_data,
    svLogicVecVal* rx_dat_check, svLogic* rx_snp_valid,
    svLogicVecVal* rx_snp_opcode, svLogicVecVal* rx_snp_txn,
    svLogicVecVal* rx_snp_srcid, svLogicVecVal* rx_snp_addr,
    svLogic* rx_snp_ret) {
  runner.drive(tx_req_ready, tx_rsp_ready, tx_dat_ready, rx_rsp_valid,
               rx_rsp_opcode, rx_rsp_txn, rx_rsp_dbid, rx_dat_valid,
               rx_dat_opcode, rx_dat_txn, rx_dat_dataid, rx_dat_datasrc,
               rx_dat_data, rx_dat_check, rx_snp_valid, rx_snp_opcode,
               rx_snp_txn, rx_snp_srcid, rx_snp_addr, rx_snp_ret);
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
    svLogic rx_dat_ready, const svLogicVecVal* tx_rsp_resp,
    const svLogicVecVal* tx_dat_resp, svLogic rx_snp_ready) {
  runner.observe(tx_req_valid, tx_req_opcode, tx_req_txn, tx_req_size,
                 tx_req_addr, tx_rsp_valid, tx_rsp_opcode, tx_rsp_txn,
                 tx_dat_valid, tx_dat_opcode, tx_dat_txn, tx_dat_dataid,
                 tx_dat_be, tx_dat_check, tx_dat_data, rx_rsp_ready,
                 rx_dat_ready, tx_rsp_resp, tx_dat_resp, rx_snp_ready);
}

extern "C" unsigned char ChiSnHasError() {
  return runner.has_error() ? 1 : 0;
}

extern "C" const char* ChiSnGetMessage() { return runner.message().c_str(); }

// Active-snoop audit trail: issued/completed/outstanding plus the last
// records with response opcode, resp field and returned data preview.
extern "C" const char* ChiSnSnoopSummary() {
  static std::string summary;
  summary = runner.snoop_summary();
  return summary.c_str();
}

// Hex dump of the SN's sparse memory (data snooped back from the L2 lands
// here), for stimulus-side data-integrity checks.
extern "C" const char* ChiSnDumpMem(long long unsigned addr, int len) {
  static std::string dump;
  dump = runner.dump_mem(addr, len);
  return dump.c_str();
}
