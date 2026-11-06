#include "cute_verification/chi_sn_core.h"

#include <algorithm>
#include <sstream>

namespace cute {
namespace verification {

namespace {

constexpr uint64_t kDbidPoolSize = 8;
// txnIDs with delivered CompData kept for CompAck matching. Generous versus
// the 8-entry DBID pool; a requester that stalls simply stops acking and the
// deque rotates its old tail out.
constexpr size_t kCompackTrackLimit = 64;

uint64_t line_base(uint64_t address) { return address & ~(kChiLineBytes - 1); }

bool is_read_opcode(uint32_t opcode) {
  switch (opcode) {
    case kChiReadShared:
    case kChiReadClean:
    case kChiReadOnce:
    case kChiReadNoSnp:
    case kChiReadUnique:
    case kChiReadNotSharedDirty:
      return true;
    default:
      return false;
  }
}

bool is_write_opcode(uint32_t opcode) {
  switch (opcode) {
    case kChiWriteEvictFull:
    case kChiWriteCleanFull:
    case kChiWriteUniquePtl:
    case kChiWriteUniqueFull:
    case kChiWriteBackPtl:
    case kChiWriteBackFull:
    case kChiWriteNoSnpPtl:
    case kChiWriteNoSnpFull:
    case kChiWriteEvictOrEvict:
      return true;
    default:
      return false;
  }
}

// Permission / CMO requests: the L2 (coupledL2 MSHR TL->CHI mapping) issues
// these for PutFull/AcquirePerm (MakeUnique) and cache op hints; they are
// answered with a plain Comp on rx_rsp, no data and no DBID.
bool is_grant_opcode(uint32_t opcode) {
  switch (opcode) {
    case kChiMakeUnique:
    case kChiCleanShared:
    case kChiCleanInvalid:
    case kChiMakeInvalid:
      return true;
    default:
      return false;
  }
}

// Write-back class requests are answered with one merged CompDBIDResp per
// the MSHR response table (Release -> WriteBackFull -> CompDBID); the
// no-snoop/unique class uses DBIDResp followed by Comp after the data.
bool is_writeback_opcode(uint32_t opcode) {
  switch (opcode) {
    case kChiWriteEvictFull:
    case kChiWriteCleanFull:
    case kChiWriteBackPtl:
    case kChiWriteBackFull:
    case kChiWriteEvictOrEvict:
      return true;
    default:
      return false;
  }
}

}  // namespace

ChiSnCore::ChiSnCore(uint64_t response_latency)
    : response_latency_(response_latency) {
  for (uint32_t id = 0; id < kDbidPoolSize; ++id) free_dbids_.push_back(id);
}

void ChiSnCore::load(uint64_t address, const std::vector<uint8_t>& bytes) {
  for (size_t i = 0; i < bytes.size(); ++i) {
    const uint64_t addr = address + i;
    auto& line = memory_[line_base(addr)];
    line[addr % kChiLineBytes] = bytes[i];
  }
}

std::vector<uint8_t> ChiSnCore::read(uint64_t address, size_t size) const {
  std::vector<uint8_t> bytes(size, 0);
  for (size_t i = 0; i < size; ++i) {
    const uint64_t addr = address + i;
    const auto it = memory_.find(line_base(addr));
    if (it != memory_.end()) bytes[i] = it->second[addr % kChiLineBytes];
  }
  return bytes;
}

void ChiSnCore::on_req_fire(const ChiReq& req) {
  if (req.size > 6) {
    std::ostringstream message;
    message << "CHI SN: unsupported size " << req.size
            << " (only up to 64B lines are modelled)";
    fail(message.str());
    return;
  }
  const uint32_t bytes = 1u << req.size;
  if (req.opcode == kChiPcrdReturn) {
    // Credit return carries no response requirement.
    return;
  }
  // A txnID must stay unique while its transaction is outstanding (reads,
  // permission grants, and writes until the DBID grant is delivered); reuse
  // would corrupt the CompData and pre-grant keying above. For writes the
  // CHI rule ends at the grant: after DBIDResp / CompDBIDResp the requester
  // may reuse the TxnID, and the remaining beats and Comp are keyed by the
  // granted DBID alone.
  for (const auto& read : reads_) {
    if (read.txn_id == req.txn_id) {
      std::ostringstream message;
      message << "CHI SN: txnID " << req.txn_id
              << " reused while read still outstanding (addr 0x" << std::hex
              << read.addr << ")";
      fail(message.str());
      return;
    }
  }
  for (const auto& write : writes_) {
    if (write.dbid_delivered) continue;
    if (write.req_txn_id == req.txn_id) {
      std::ostringstream message;
      message << "CHI SN: txnID " << req.txn_id
              << " reused while write grant undelivered (addr 0x" << std::hex
              << write.addr << std::dec << ")";
      fail(message.str());
      return;
    }
  }
  for (const auto& grant : grants_) {
    if (grant.txn_id == req.txn_id) {
      std::ostringstream message;
      message << "CHI SN: txnID " << req.txn_id
              << " reused while grant still outstanding";
      fail(message.str());
      return;
    }
  }
  if (is_grant_opcode(req.opcode)) {
    PendingGrant grant;
    grant.txn_id = req.txn_id;
    grant.addr = req.addr;
    grant.ready_cycle = cycle_ + response_latency_;
    grant.fired_cycle = cycle_;
    grants_.push_back(grant);
    ++accepted_writes_;  // counted as state-changing (non-read) traffic
    return;
  }
  if (is_read_opcode(req.opcode)) {
    PendingRead job;
    job.txn_id = req.txn_id;
    job.addr = req.addr;
    job.total_beats = bytes / kChiDataBytes;
    job.beats_left = job.total_beats;
    job.ready_cycle = cycle_ + response_latency_;
    job.fired_cycle = cycle_;
    const std::vector<uint8_t> line = read(req.addr, bytes);
    std::copy(line.begin(), line.end(), job.line.begin());
    reads_.push_back(job);
    ++accepted_reads_;
    return;
  }
  if (is_write_opcode(req.opcode)) {
    if (bytes < kChiDataBytes) {
      // beats_expected would be zero: no DAT could ever complete the write,
      // the DBID would leak and the case would hang without a reason.
      std::ostringstream message;
      message << "CHI SN: write size " << req.size
              << " below one 32B beat is not modelled";
      fail(message.str());
      return;
    }
    // A full DBID pool defers the grant instead of failing the case: a real
    // SN withholds DBIDResp until a DBID recycles, back-pressuring the
    // requester. The grant loop below retries the allocation.
    uint32_t db_id = 0;
    const bool dbid_assigned = allocate_dbid(&db_id);
    PendingWrite job;
    job.req_txn_id = req.txn_id;
    job.db_id = db_id;
    job.dbid_assigned = dbid_assigned;
    job.addr = req.addr;
    job.beats_expected = bytes / kChiDataBytes;
    job.partial = req.opcode == kChiWriteNoSnpPtl ||
                  req.opcode == kChiWriteUniquePtl ||
                  req.opcode == kChiWriteBackPtl;
    job.merged = compdbid_merged_ || is_writeback_opcode(req.opcode);
    job.ready_cycle = cycle_ + response_latency_;
    job.fired_cycle = cycle_;
    writes_.push_back(job);
    ++accepted_writes_;
    return;
  }
  std::ostringstream message;
  message << "CHI SN: unexpected REQ opcode 0x" << std::hex << req.opcode;
  fail(message.str());
}

void ChiSnCore::on_dat_fire(const ChiDat& dat) {
  // After the DBIDResp the DUT carries the granted dbID in txnID; match
  // pending writes on that key. Data arriving before the grant is tolerated
  // as long as the dbID matches.
  PendingWrite* job = nullptr;
  for (auto& write : writes_) {
    // Merged grants set comp_sent before the data arrives, so match any
    // not-yet-complete write on its dbID. Unassigned writes (grant deferred
    // on an empty pool) have no valid dbID to match yet.
    if (write.dbid_assigned && write.db_id == dat.txn_id &&
        write.beats_received < write.beats_expected) {
      job = &write;
      break;
    }
  }
  if (job == nullptr) {
    std::ostringstream message;
    message << "CHI SN: DAT beat with unknown/retired txnID " << dat.txn_id;
    fail(message.str());
    return;
  }
  if (dat.data_id >= 2) {
    std::ostringstream message;
    message << "CHI SN: DAT dataID " << dat.data_id
            << " outside the modelled 64B line";
    fail(message.str());
    return;
  }
  // Beats of one write must arrive exactly once each and in ascending
  // dataID order (the L2 TXDAT emits them streamed in line order).
  if (job->beats_mask & (1u << dat.data_id)) {
    std::ostringstream message;
    message << "CHI SN: duplicate DAT dataID " << dat.data_id
            << " for dbID " << dat.txn_id;
    fail(message.str());
    return;
  }
  if (job->beats_received > 0 && dat.data_id <= job->last_data_id) {
    std::ostringstream message;
    message << "CHI SN: DAT dataID " << dat.data_id << " after "
            << job->last_data_id << " (beats out of order, dbID "
            << dat.txn_id << ")";
    fail(message.str());
    return;
  }
  // L2Param dataCheck = "oddparity": check bit i complements byte i so the
  // data+check pair has odd weight -- TXDAT generates it, RXDAT expects it.
  for (size_t i = 0; i < kChiDataBytes; ++i) {
    const bool bit_set = ((dat.data_check >> i) & 1u) != 0;
    const bool want_set = (__builtin_popcount(dat.data[i]) & 1) == 0;
    if (bit_set != want_set) {
      std::ostringstream message;
      message << "CHI SN: DAT dataCheck parity wrong on byte " << i
              << " (dataID " << dat.data_id << ", dbID " << dat.txn_id
              << ", data 0x" << std::hex << static_cast<uint32_t>(dat.data[i])
              << ", check bit " << std::dec << bit_set << ")";
      fail(message.str());
      return;
    }
  }
  job->beats_mask |= 1u << dat.data_id;
  job->last_data_id = dat.data_id;
  const uint64_t beat_addr = line_base(job->addr) + dat.data_id * kChiDataBytes;
  for (size_t i = 0; i < kChiDataBytes; ++i) {
    if (job->partial && ((dat.be >> i) & 1u) == 0) continue;
    auto& line = memory_[line_base(beat_addr)];
    line[beat_addr % kChiLineBytes + i] = dat.data[i];
  }
  if (++job->beats_received >= job->beats_expected) job->data_complete = true;
  ++dat_tx_fire_count_;  // watchdog: an accepted write beat is a completion
}

void ChiSnCore::on_rsp_fire(const ChiRsp& rsp) {
  if (rsp.opcode != kChiCompAck) return;
  ++compack_count_;
  // CompAck must reference a read whose CompData was fully delivered, and
  // each delivered line may be acked exactly once. The L2 TXRSP hardwires
  // TxnID=0 on CompAck (a NoSnoop-subordinate simplification), so a zero
  // id cannot be key-matched: fall back to a volume bound instead.
  if (rsp.txn_id == 0) {
    if (compack_count_ > delivered_lines_) {
      std::ostringstream message;
      message << "CHI SN: " << compack_count_
              << " CompAcks exceed the " << delivered_lines_
              << " delivered CompData line(s)";
      fail(message.str());
    }
    return;
  }
  for (auto it = compack_pending_.begin(); it != compack_pending_.end(); ++it) {
    if (*it == rsp.txn_id) {
      compack_pending_.erase(it);
      return;
    }
  }
  std::ostringstream message;
  message << "CHI SN: CompAck for txnID " << rsp.txn_id
          << " without a fully delivered CompData line (duplicate, unknown "
             "or stale ack)";
  fail(message.str());
}

void ChiSnCore::rsp_out_consume() {
  if (rsp_out_.valid) {
    ++rsp_consumed_count_;
    // Receiving DBIDResp / CompDBIDResp releases the requester's TxnID
    // (CHI): the remaining beats and the Comp are keyed by the granted
    // DBID alone, so the write leaves the TxnID uniqueness set here.
    if (rsp_out_.bits.opcode == kChiDbidResp ||
        rsp_out_.bits.opcode == kChiCompDbidResp) {
      for (auto& write : writes_) {
        if (write.dbid_assigned && write.db_id == rsp_out_.bits.db_id) {
          write.dbid_delivered = true;
          break;
        }
      }
    }
  }
  rsp_out_.valid = false;
}

void ChiSnCore::dat_out_consume() {
  if (dat_out_.valid) ++dat_consumed_count_;
  dat_out_.valid = false;
}

void ChiSnCore::tick() {
  ++cycle_;
  present_next_flits();
}

bool ChiSnCore::allocate_dbid(uint32_t* db_id) {
  if (free_dbids_.empty()) return false;
  *db_id = free_dbids_.front();
  free_dbids_.erase(free_dbids_.begin());
  return true;
}

void ChiSnCore::release_dbid(uint32_t db_id) { free_dbids_.push_back(db_id); }

void ChiSnCore::present_next_flits() {
  // One RSP and one DAT flit may be presented per cycle. Write-side grants
  // (DBIDResp / CompDBIDResp), write completions (Comp) and permission
  // grants (Comp) take the RSP slot; read data takes the DAT slot.
  if (!rsp_out_.valid) {
    for (auto& write : writes_) {
      if (write.comp_sent) continue;
      if (!write.dbid_sent) {
        if (write.ready_cycle > cycle_) continue;
        if (!write.dbid_assigned) {
          // Grant deferred on an empty pool: retry once a DBID recycles.
          uint32_t db_id = 0;
          if (!allocate_dbid(&db_id)) continue;
          write.db_id = db_id;
          write.dbid_assigned = true;
        }
        ChiRsp rsp;
        rsp.txn_id = write.req_txn_id;
        rsp.db_id = write.db_id;
        if (write.merged) {
          rsp.opcode = kChiCompDbidResp;
          write.comp_sent = true;  // the merged flit is also the completion
        } else {
          rsp.opcode = kChiDbidResp;
        }
        write.dbid_sent = true;
        rsp_out_ = {true, rsp};
        break;
      }
      if (!write.merged && write.data_complete) {
        ChiRsp rsp;
        rsp.txn_id = write.db_id;
        rsp.db_id = write.db_id;
        rsp.opcode = kChiComp;
        write.comp_sent = true;
        rsp_out_ = {true, rsp};
        break;
      }
    }
    if (!rsp_out_.valid && !grants_.empty() &&
        grants_.front().ready_cycle <= cycle_) {
      ChiRsp rsp;
      rsp.txn_id = grants_.front().txn_id;
      rsp.opcode = kChiComp;
      rsp_out_ = {true, rsp};
      grants_.pop_front();
      // CompAck answers Comp-class responses generally, not only CompData:
      // the L2 also acks permission grants (MakeUnique & co).
      ++delivered_lines_;
    }
  }

  // Recycle writes whose grant, optional completion and data are all done;
  // merged grants must survive until the data beats have arrived.
  for (auto it = writes_.begin(); it != writes_.end();) {
    if (it->dbid_sent && it->comp_sent &&
        it->beats_received >= it->beats_expected) {
      release_dbid(it->db_id);
      it = writes_.erase(it);
    } else {
      ++it;
    }
  }

  if (!dat_out_.valid && !reads_.empty()) {
    PendingRead& job = reads_.front();
    if (job.ready_cycle <= cycle_) {
      const uint32_t beat_index = job.total_beats - job.beats_left;
      ChiDat dat;
      dat.opcode = kChiCompData;
      dat.txn_id = job.txn_id;
      dat.data_id = beat_index;  // beats emitted in dataID order 0,1
      const size_t offset = beat_index * kChiDataBytes;
      std::copy(job.line.begin() + offset,
                job.line.begin() + offset + kChiDataBytes, dat.data.begin());
      dat_out_ = {true, dat};
      if (--job.beats_left == 0) {
        compack_pending_.push_back(job.txn_id);
        if (compack_pending_.size() > kCompackTrackLimit) {
          compack_pending_.pop_front();
        }
        ++delivered_lines_;
        reads_.pop_front();
      }
    }
  }
}

std::string ChiSnCore::outstanding_summary() const {
  std::ostringstream out;
  out << "SN outstanding: " << reads_.size() << " read(s), "
      << writes_.size() << " write(s), " << grants_.size() << " grant(s), "
      << (compack_pending_.size() >= kCompackTrackLimit ? ">= " : "")
      << compack_pending_.size() << " unacked CompData line(s)";
  if (rsp_out_.valid || dat_out_.valid) {
    out << "; presented and waiting DUT ready:";
    if (rsp_out_.valid) {
      out << " RSP op=0x" << std::hex << rsp_out_.bits.opcode << " txn "
          << rsp_out_.bits.txn_id << std::dec;
    }
    if (dat_out_.valid) {
      out << " CompData txn " << dat_out_.bits.txn_id << " dataID "
          << dat_out_.bits.data_id;
    }
  }
  // Oldest entries first: the first stuck transaction is usually the story.
  struct Entry {
    uint64_t age;
    std::string line;
  };
  std::vector<Entry> entries;
  for (const auto& read : reads_) {
    std::ostringstream line;
    line << "read txn " << read.txn_id << " addr 0x" << std::hex
         << read.addr << std::dec << " waiting CompData (" << read.beats_left
         << "/" << read.total_beats << " beats left)";
    entries.push_back({cycle_ - read.fired_cycle, line.str()});
  }
  for (const auto& write : writes_) {
    std::ostringstream line;
    line << "write txn " << write.req_txn_id;
    if (write.dbid_assigned) line << " (dbID " << write.db_id << ")";
    line << " addr 0x" << std::hex << write.addr << std::dec;
    if (!write.dbid_assigned) {
      line << " waiting DBID (pool empty)";
    } else if (!write.dbid_sent) {
      line << " waiting DBIDResp grant";
    } else {
      line << " waiting DAT beats (" << write.beats_received << "/"
           << write.beats_expected;
      if (!write.comp_sent) line << ", then Comp";
      line << ")";
    }
    entries.push_back({cycle_ - write.fired_cycle, line.str()});
  }
  for (const auto& grant : grants_) {
    std::ostringstream line;
    line << "grant txn " << grant.txn_id << " addr 0x" << std::hex
         << grant.addr << std::dec << " waiting Comp";
    entries.push_back({cycle_ - grant.fired_cycle, line.str()});
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& a, const Entry& b) { return a.age > b.age; });
  const size_t shown = entries.size() < 4 ? entries.size() : 4;
  for (size_t i = 0; i < shown; ++i) {
    out << "; [" << entries[i].age << " cycles] " << entries[i].line;
  }
  if (entries.size() > shown) out << "; (+ " << (entries.size() - shown) << " more)";
  return out.str();
}

void ChiSnCore::fail(const std::string& message) {
  if (error_.empty()) error_ = message;
}

}  // namespace verification
}  // namespace cute
