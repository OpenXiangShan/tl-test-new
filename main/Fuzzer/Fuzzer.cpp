#include "Fuzzer.h"

void Fuzzer::read_trace(const std::string& tracefile_path) {
    printf("#### Fuzzer %d mode: %d, init state: %d\n", index, (int)mode, state);
    printf("####        Reading tracefile %s\n", tracefile_path.c_str());

    blkCountLimit = 0;
    blkCountLimitTrace = 0;

    std::ifstream tracefile(tracefile_path);
    if (!tracefile.is_open()) {
        throw std::runtime_error("Failed to open tracefile: " + tracefile_path);
    }

    for (std::string line; std::getline(tracefile, line); ) {
        std::istringstream iss(line);
        std::string op;
        uint64_t address;
        int agent_id;

        // trace format: <op> <address> <agent>   (op is two-letter code, check addr.py in NEMU-Matrix)
        if (!(iss >> op >> std::hex >> address >> agent_id)) {
            continue; // Skip malformed lines
        }

        if (agent_id == index) { // 0: cAgent, 1: ulAgent, 2~: mAgent
            // Map new two-letter ops into existing traceOp enums
            if (op == "LD" || op == "IF" || op == "MR") {
                traceAddrs.push(std::make_pair(traceOp::READ, address));
                blkCountLimitTrace++;
            }
            else if (op == "CR") {
                traceAddrs.push(std::make_pair(traceOp::MODIFY, address));
                blkCountLimitTrace++;
            }
            else if (op == "ST" || op == "MW") {
                traceAddrs.push(std::make_pair(traceOp::WRITE, address));
                blkCountLimitTrace++;
            } 
            else if (op == "EV") {
                traceAddrs.push(std::make_pair(traceOp::EVICT, address));
                blkCountLimitTrace++;
            } 
            else if (op == "FE") {
                traceAddrs.push(std::make_pair(traceOp::FENCE, address));
                // Fence does not count towards blkCountLimitTrace
            } 
            else {
                printf("Unknown operation: %s\n", op.c_str());
                continue; // Skip unknown operations
            }
        }
    }

    printf("####        filledAddrs size: %d, traceAddrs size: %d\n", blkCountLimit, blkCountLimitTrace);
    tracefile.close();

    // no preload address, directly start acquire2 process
    if (blkCountLimit == 0) {
        state = bwTestState::acquire2;
    }
}

// TODO-AI: fix this for new fuzzer framework later
void Fuzzer::traceTestWithPrefill() {
  /*
  //复用fuzzStream Code
  paddr_t addr;
  int     alias;
  bool do_alias = false;

  // dont alias
  alias = (do_alias) ? (CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxAlias) : 0;

  // ============================================================
  // |               Preload data into L2 Cache                 |
  // ============================================================
  if (state == bwTestState::acquire) {
    if (!mAgent->config().memoryEnable)
      return;

    addr = filledAddrs.front();
    // addr = remap_memory_address(addr);
    if(mAgent->do_getAuto(addr)){
        LogX("%ld IIIIacq try do_getAuto(0x%lx) at agent %d\n", *cycles, addr, index);
        blkSent++;
        filledAddrs.push(addr); // push equals push_back
        filledAddrs.pop();
    };

    if (blkSent == blkCountLimit) {
      printf("#### MFuzzer %d state: wait_acquire\n", index);
      state = bwTestState::wait_acquire;
      blkSent = 0;
    }
  }

  if (state == bwTestState::wait_acquire || state == bwTestState::acquire) {
    // wait channel M to fire
    if (mAgent->is_m_fired()) {
      blkReceived++;
    }
    if (blkReceived == blkCountLimit) {
      printf("#### MFuzzer %d state: releasing\n", index);
      state = bwTestState::releasing;
      blkReceived = 0;
    }
  }
  // FIXME 重地址冲突(M1和M3相邻时间对同一地址读写)
  if (state == bwTestState::releasing) {
    addr = filledAddrs.front();

    // randomize putdata
    auto putdata = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE; i++) {
      putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "CFuzzer");
    }

    if(mAgent->do_putfulldata(addr, putdata)){
        LogX("%ld IIIIrel try do_putFull(0x%lx) at agent %d\n", *cycles, addr, index);
        blkSent++;
        filledAddrs.pop();
    }

    if (blkSent == blkCountLimit) {
      printf("#### MFuzzer %d state: wait_release\n", index);
      state = bwTestState::wait_release;
      blkSent = 0;
    }
  }

  if (state == bwTestState::releasing||state == bwTestState::wait_release) {
    // wait channel D to fire (AccessAck)
    if (mAgent->is_d_fired()) {
      blkReceived++;
    }
    if (blkReceived == blkCountLimit) {
      printf("#### MFuzzer %d state: acquire2\n", index);
      state = bwTestState::acquire2;
      perfCycleStart=this->mAgent->cycle();
      blkReceived = 0;
    }
  }

  // ============================================================
  // |                Trace r/w accessing L2                    |
  // ============================================================
  if (state == bwTestState::acquire2) {
    auto [opcode2, addr2] = traceAddrs.front();

    bool success = false;
    switch (opcode2) {
      case traceOp::READ:
        if(mAgent->do_getAuto(addr2)){
          success = true;
          LogX("%ld IIIIacq2 try do_GetAuto(0x%lx) at agent %d\n", *cycles, addr2, index);
        };
        break;

      case traceOp::WRITE:
        auto putdata = make_shared_tldata<DATASIZE>();
        for (int i = 0; i < DATASIZE; i++) {
          putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "CFuzzer");
        }
        if(mAgent->do_putfulldata(addr2, putdata)){
          success = true;
          LogX("%ld IIIIacq2 try do_putFull(0x%lx) at agent %d\n", *cycles, addr2, index);
        }
        break;
    }

    if (success) {
      blkSent++;
      traceAddrs.pop();

      if (blkSent % 10000 == 0) {
        printf("#### MFuzzer %d processed %d blocks\n", index, blkSent);
      }
    }


    if (blkSent == blkCountLimitTrace) {
      printf("#### MFuzzer %d state: wait_acquire2\n", index);
      state = bwTestState::wait_acquire2;
      blkSent = 0;
    }
  }

  if (state == bwTestState::acquire2 || state == bwTestState::wait_acquire2) {
    // M_fire and D_fire might happen at the same cycle
    if (mAgent->is_m_fired()) {
      blkReceived++;
    }
    if (mAgent->is_d_fired()) {
      blkReceived++;
    }

    if (blkReceived == blkCountLimitTrace) {
      state = exit_fuzzer;
      blkReceived = 0;
      perfCycleEnd = this->mAgent->cycle(); // maybe just *cycles
      // perfCycleEnd=(this->mAgent->cycle()-perfCycleStart)/2;
      // TLSystemFinishEvent().Fire();// stop
      std::cout << "perf debug : "<< blkCountLimitTrace*64/((this->mAgent->cycle()-perfCycleStart)/2)<< "B/Cycle"<<std::endl;
    }
  }
    */
}

void Fuzzer::traceTestWithFence() {
  //复用fuzzStream Code
  paddr_t addr;

  // require no prefill since no such handling logic
  assert(filledAddrs.size() == 0);

  // ============================================================
  // |                Trace r/w accessing L2                    |
  // ============================================================
  if (state == bwTestState::acquire2) {
    auto [opcode2, addr2] = traceAddrs.front();

    bool success = false;
    switch (opcode2) {
      case traceOp::FENCE:
        printf("#### MFuzzer %d state: wait_fence\n", index);
        LogX("%ld Agent %d try Fence(0x%lx)\n", *cycles/2, index, addr2);
        state = bwTestState::wait_fence;
        traceAddrs.pop();
        break;

      case traceOp::READ:
        if (do_read(addr2)) {
          success = true;
          LogX("%ld Agent %d try do_read(0x%lx)\n", *cycles/2, index, addr2);
        };
        break;

      case traceOp::MODIFY:
        if (do_read_modify(addr2)) {
          success = true;
          LogX("%ld Agent %d try do_read_modify(0x%lx)\n", *cycles/2, index, addr2);
        };
        break;

      case traceOp::WRITE: {
        auto putdata = make_shared_tldata<DATASIZE>();
        for (int i = 0; i < DATASIZE; i++) {
          putdata->data[i] = (uint8_t)rand();
        }
        if(do_write(addr2, putdata)){
          success = true;
          LogX("%ld Agent %d try do_write(0x%lx)\n", *cycles/2, index, addr2);
        }
        break;
      }

      case traceOp::EVICT:
        if (do_evict(addr2)) {
          success = true;
          LogX("%ld Agent %d try do_evict(0x%lx)\n", *cycles/2, index, addr2);
        }
        break;

      default:
        fprintf(stderr, "Error: Unhandled opcode 0x%x at agent %d\n", (unsigned int)opcode2, index);
        break;
    }

    if (success) {
      blkSentFence ++;
      blkSent ++;
      traceAddrs.pop();

      if (blkSent % 10000 == 0) {
        printf("#### MFuzzer %d processed %d blocks\n", index, blkSent);
      }
    }

    if (blkSent == blkCountLimitTrace) {
      printf("#### MFuzzer %d state: wait_acquire2\n", index);
      state = bwTestState::wait_acquire2;
      blkSent = 0;
    }
  }

  if (read_ack()) {
    blkReceived ++;
    blkReceivedFence ++;
  }
  if (write_ack()) {
    blkReceived ++;
    blkReceivedFence ++;
  }
  if (evict_ack()) {
    blkReceived ++;
    blkReceivedFence ++;
  }

  if (state == bwTestState::wait_fence) {
    if (blkSentFence == blkReceivedFence) {
      blkSentFence = 0;
      blkReceivedFence = 0;
      state = bwTestState::acquire2;
      printf("#### MFuzzer %d state: acquire2\n", index);
    }
  }

  // === count all responses to determine the end of test ===
  // M_fire and D_fire might happen at the same cycle
  if (blkReceived == blkCountLimitTrace) {
    state = exit_fuzzer;
    blkReceived = 0;
    /*
    perfCycleEnd = this->mAgent->cycle(); // maybe just *cycles
    // TLSystemFinishEvent().Fire();// stop
    std::cout << "perf debug : [!Needs FIX]"<< blkCountLimitTrace*64/((this->mAgent->cycle()-perfCycleStart)/2)<< "B/Cycle"<<std::endl;
    */
  }
}

bool Fuzzer::issue_trace_entry(uint8_t op, uint64_t addr)
{
    switch (op)
    {
        case traceOp::READ:
            return do_read(addr);

        case traceOp::MODIFY:
            return do_read_modify(addr);

        case traceOp::WRITE: {
            auto putdata = make_shared_tldata<DATASIZE>();
            for (int i = 0; i < DATASIZE; i++)
                putdata->data[i] = (uint8_t)rand();
            return do_write(addr, putdata);
        }

        case traceOp::EVICT:
            return do_evict(addr);

        default:
            assert(false && "Unhandled trace operation");
    }
    return false;
}