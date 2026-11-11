#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace cute {
namespace verification {

// CHI opcodes used on the L2 downstream (SN-facing) link.
// Values mirror XSAICache xscache/chi/Opcode.scala (Issue Eb encodings).
// The SN side classifies requests into read-like (CompData back) and
// write-like (DBIDResp -> data -> Comp) groups; cacheable variants such as
// ReadNotSharedDirty/WriteBack* behave identically from the SN's viewpoint.
enum ChiReqOpcode : uint32_t {
  kChiReadShared = 0x01,
  kChiReadClean = 0x02,
  kChiReadOnce = 0x03,
  kChiReadNoSnp = 0x04,
  kChiPcrdReturn = 0x05,
  kChiReadUnique = 0x07,
  kChiCleanShared = 0x08,
  kChiCleanInvalid = 0x09,
  kChiMakeInvalid = 0x0A,
  kChiMakeUnique = 0x0C,
  kChiReadNotSharedDirty = 0x26,
  kChiWriteEvictFull = 0x15,
  kChiWriteCleanFull = 0x17,
  kChiWriteUniquePtl = 0x18,
  kChiWriteUniqueFull = 0x19,
  kChiWriteBackPtl = 0x1A,
  kChiWriteBackFull = 0x1B,
  kChiWriteNoSnpPtl = 0x1C,
  kChiWriteNoSnpFull = 0x1D,
  kChiWriteEvictOrEvict = 0x42,
};

enum ChiRspOpcode : uint32_t {
  kChiCompAck = 0x2,
  kChiComp = 0x4,
  kChiCompDbidResp = 0x5,
  kChiDbidResp = 0x6,
};

enum ChiDatOpcode : uint32_t {
  kChiCompData = 0x4,
};

// DAT flit geometry for this DUT: 256-bit data flits carrying a 64-byte
// cache line as two beats (dataID 0 = first 32B, dataID 1 = second 32B).
constexpr size_t kChiDataBytes = 32;
constexpr size_t kChiLineBytes = 64;

struct ChiReq {
  uint32_t opcode = 0;
  uint32_t txn_id = 0;
  uint32_t size = 0;  // log2(bytes transferred)
  uint64_t addr = 0;
};

struct ChiRsp {
  uint32_t opcode = 0;
  uint32_t txn_id = 0;
  uint32_t db_id = 0;
};

struct ChiDat {
  uint32_t opcode = 0;
  uint32_t txn_id = 0;
  uint32_t data_id = 0;  // 0/1: which 32B half of the 64B line
  uint32_t be = 0;       // byte enable, bit i covers data[i]
  uint32_t data_check = 0;  // odd parity per byte, bit i covers data[i]
  std::array<uint8_t, kChiDataBytes> data{};
};

// Cycle-driven functional model of the CHI Subordinate Node (the memory
// side) for the NoSnoop subset the CUTE L2 issues downstream. Pure logic:
// no Verilator dependency, so it can be unit-tested standalone and later
// bound to DUT ports by a thin signal-mapping layer.
//
// Per-cycle usage:
//   observe:  on_req_fire() / on_dat_fire() / on_rsp_fire() on TX handshakes
//   drive:    rsp_out()/dat_out() (+valid) to the RX channels,
//             rsp_out_consume()/dat_out_consume() on RX handshakes
//   then:     tick()
class ChiSnCore {
 public:
  explicit ChiSnCore(uint64_t response_latency = 4);

  // Backdoor memory access used for memory_init and memory_expect.
  void load(uint64_t address, const std::vector<uint8_t>& bytes);
  std::vector<uint8_t> read(uint64_t address, size_t size) const;

  // TX side (DUT -> SN).
  void on_req_fire(const ChiReq& req);
  void on_dat_fire(const ChiDat& dat);
  void on_rsp_fire(const ChiRsp& rsp);

  // RX side (SN -> DUT): present output flits.
  bool rsp_out_valid() const { return rsp_out_.valid; }
  const ChiRsp& rsp_out() const { return rsp_out_.bits; }
  bool dat_out_valid() const { return dat_out_.valid; }
  const ChiDat& dat_out() const { return dat_out_.bits; }
  void rsp_out_consume();
  void dat_out_consume();

  void tick();

  // Configuration knobs (kept minimal for the first integration; the JSON
  // transport layer exposes them later).
  void set_response_latency(uint64_t cycles) { response_latency_ = cycles; }
  // true: write grants use one merged CompDBIDResp instead of DBIDResp
  // followed by Comp after the data beats.
  void set_compdbid_merged(bool merged) { compdbid_merged_ = merged; }

  // Protocol violations observed by the model itself.
  const std::string& error() const { return error_; }

  // Watchdog support: a human-readable snapshot of what the SN is still
  // holding, oldest transaction first. Appended by the runners to
  // completion-timeout messages so a stalled case points at the exact
  // txnID / address / wait-state instead of only a release-mask mismatch.
  // Non-fatal by design: an old transaction is a symptom, not a verdict.
  std::string outstanding_summary() const;

  // Counters (used by self-test assertions and later checkers).
  uint64_t compack_count() const { return compack_count_; }
  uint64_t accepted_reads() const { return accepted_reads_; }
  uint64_t accepted_writes() const { return accepted_writes_; }

  // Completion-class counters for the progress watchdog: rx rsp/dat flits
  // actually taken by the DUT and accepted TXDAT write beats.  Request-side
  // counters (accepted_reads/writes) are deliberately excluded -- issuing
  // requests is churn, not completion (see watchdog.h).
  uint64_t rsp_consumed_count() const { return rsp_consumed_count_; }
  uint64_t dat_consumed_count() const { return dat_consumed_count_; }
  uint64_t dat_tx_fire_count() const { return dat_tx_fire_count_; }
  uint64_t milestone_stamp() const {
    return compack_count_ + rsp_consumed_count_ + dat_consumed_count_
        + dat_tx_fire_count_;
  }

 private:
  template <typename T>
  struct Flit {
    // The two-arg constructor keeps `= {valid, bits}` assignments legal under
    // the remote VCS host's C++11 gcc 4.8, where NSDMIs make this a
    // non-aggregate; a brace NSDMI on the dependent type trips the same
    // compiler, so the default constructor value-initializes instead.
    Flit() : valid(false), bits() {}
    Flit(bool is_valid, const T& flit_bits) : valid(is_valid), bits(flit_bits) {}

    bool valid;
    T bits;
  };

  struct PendingRead {
    uint32_t txn_id = 0;
    uint64_t addr = 0;
    uint32_t total_beats = 0;     // beats in the whole line response
    uint32_t beats_left = 0;      // remaining CompData beats to emit
    std::array<uint8_t, kChiLineBytes> line{};
    uint64_t ready_cycle = 0;
    uint64_t fired_cycle = 0;     // REQ handshake cycle, for age reporting
  };

  struct PendingWrite {
    uint32_t req_txn_id = 0;   // txnID the request arrived with
    uint32_t db_id = 0;        // granted in DBIDResp; DAT beats carry it
    uint64_t addr = 0;
    uint32_t beats_expected = 0;
    uint32_t beats_received = 0;
    uint32_t beats_mask = 0;   // dataID bits already seen (dup detection)
    uint32_t last_data_id = 0; // beats must arrive in ascending dataID order
    bool partial = false;      // WriteNoSnpPtl honours the DAT be field
    bool merged = false;       // grant uses CompDBIDResp (write-back class)
    bool dbid_assigned = false;   // a DBID was taken from the pool
    bool dbid_sent = false;
    bool dbid_delivered = false;  // grant flit consumed by the requester
    bool data_complete = false;
    bool comp_sent = false;
    uint64_t ready_cycle = 0;  // earliest cycle the DBIDResp may be sent
    uint64_t fired_cycle = 0;
  };

  // Permission / CMO requests (MakeUnique, CleanShared, ...) need a plain
  // Comp on rx_rsp: no data beats and no DBID grant.
  struct PendingGrant {
    uint32_t txn_id = 0;
    uint64_t addr = 0;
    uint64_t ready_cycle = 0;
    uint64_t fired_cycle = 0;
  };

  void fail(const std::string& message);
  bool allocate_dbid(uint32_t* db_id);
  void release_dbid(uint32_t db_id);
  void present_next_flits();

  // Sparse 64B-line memory.
  std::map<uint64_t, std::array<uint8_t, kChiLineBytes>> memory_;

  std::deque<PendingRead> reads_;
  std::vector<PendingWrite> writes_;
  std::deque<PendingGrant> grants_;
  std::vector<uint32_t> free_dbids_;

  // txnIDs whose CompData has been fully delivered and may still be CompAcked.
  // Bounded so a requester that never acks cannot grow it without limit.
  std::deque<uint32_t> compack_pending_;
  // Total delivered Comp-class responses (CompData lines plus permission
  // grant Comps): the volume bound backing the CompAck check when the
  // requester keys its acks by a hardwired txnID of 0.
  uint64_t delivered_lines_ = 0;

  Flit<ChiRsp> rsp_out_;
  Flit<ChiDat> dat_out_;

  uint64_t cycle_ = 0;
  uint64_t response_latency_ = 4;
  bool compdbid_merged_ = false;

  uint64_t compack_count_ = 0;
  uint64_t accepted_reads_ = 0;
  uint64_t accepted_writes_ = 0;
  uint64_t rsp_consumed_count_ = 0;
  uint64_t dat_consumed_count_ = 0;
  uint64_t dat_tx_fire_count_ = 0;

  std::string error_;
};

}  // namespace verification
}  // namespace cute
