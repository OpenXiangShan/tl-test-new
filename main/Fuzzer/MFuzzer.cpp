// Matrix Fuzzer

//
// Created by wkf on 2021/10/29.
//

// #include "MFuzzer.h"
#include "../Events/TLSystemEvent.hpp"

static std::vector<CFuzzRange> FUZZ_ARI_RANGES = {
    { .ordinal = 0, .maxTag = CFUZZER_RAND_RANGE_TAG,     .maxSet = CFUZZER_RAND_RANGE_SET,   .maxAlias = CFUZZER_RAND_RANGE_ALIAS    },
    { .ordinal = 1, .maxTag = 0x1,                        .maxSet = 0x10,                     .maxAlias = 0x4                         },
    { .ordinal = 2, .maxTag = 0x10,                       .maxSet = 0x1,                      .maxAlias = 0x4                         }
};

static CFuzzRange FUZZ_STREAM_RANGE = {
      .ordinal = 0, .maxTag = 0x10,                        .maxSet = 0x10,                      .maxAlias = CFUZZER_RAND_RANGE_ALIAS
};

static inline size_t fact(size_t n) noexcept
{
    size_t r = 1;
    for (size_t i = 1; i <= n; i++)
        r *= i;
    return r;
}


MFuzzer::MFuzzer(tl_agent::MAgent *mAgent) noexcept {
    this->mAgent = mAgent;

    this->memoryStart                   = mAgent->config().memoryStart;
    this->memoryEnd                     = mAgent->config().memoryEnd;

    this->fuzzARIRangeIndex             = 0;
    this->fuzzARIRangeIterationInterval = mAgent->config().fuzzARIInterval;
    this->fuzzARIRangeIterationTarget   = mAgent->config().fuzzARITarget;

    this->fuzzARIRangeIterationCount    = 0;
    this->fuzzARIRangeIterationTime     = mAgent->config().fuzzARIInterval;

    this->fuzzStreamOffset              = 0;
    this->fuzzStreamStepTime            = mAgent->config().fuzzStreamStep;
    this->fuzzStreamEnded               = false;
    this->fuzzStreamStep                = mAgent->config().fuzzStreamStep;
    this->fuzzStreamInterval            = mAgent->config().fuzzStreamInterval;
    this->fuzzStreamStart               = mAgent->config().fuzzStreamStart;
    this->fuzzStreamEnd                 = mAgent->config().fuzzStreamEnd;

    decltype(mAgent->config().sequenceModes.end()) modeInMap;
    if ((modeInMap = mAgent->config().sequenceModes.find(mAgent->sysId()))
            != mAgent->config().sequenceModes.end())
        this->mode = modeInMap->second;
    else
        this->mode = TLSequenceMode::FUZZ_ARI;

    if (this->mode == TLSequenceMode::FUZZ_ARI)
    {
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] in FUZZ_ARI mode").EndLine());

        for (size_t i = 0; i < FUZZ_ARI_RANGES.size(); i++)
            this->fuzzARIRangeOrdinal.push_back(i);

        size_t loop = mAgent->sysSeed() % fact(fuzzARIRangeOrdinal.size());
        for (size_t i = 0; i < loop; i++)
            std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());

        LogInfo(this->mAgent->cycle(), Append("Initial Fuzz Set: index = ", this->fuzzARIRangeIndex, ", permutation: "));
        LogEx(
            std::cout << "[ ";
            for (size_t i = 0; i < fuzzARIRangeOrdinal.size(); i++)
                std::cout << fuzzARIRangeOrdinal[i] << " ";
            std::cout << "]";
        );
        LogEx(std::cout << std::endl);
    }
    else if (this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] in FUZZ_STREAM mode").EndLine());
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] stream steps ")
            .Hex().ShowBase().Append(this->fuzzStreamStep).EndLine());
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] stream starts at ")
            .Hex().ShowBase().Append(this->fuzzStreamStart).EndLine());
        LogInfo(this->mAgent->cycle(), Append("MFuzzer [", mAgent->sysId(), "] stream ends at ")
            .Hex().ShowBase().Append(this->fuzzStreamEnd).EndLine());
    }
}

void MFuzzer::randomTest(bool put) {
    if (this->mode == TLSequenceMode::PASSIVE)
    {
        return;
    }
    else if (this->mode == TLSequenceMode::FUZZ_ARI || this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        paddr_t addr = (CAGENT_RAND64(mAgent, "MFuzzer") % 0x400) << 6;

        if (this->mode == TLSequenceMode::FUZZ_ARI)
        {
            // Tag + Set + Offset
            addr  = ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxTag) << 13) 
                  + ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxSet) << 6);
            
            addr  = remap_memory_address(addr);
        }
        else // FUZZ_STREAM
        {
            addr  = ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxTag) << 13)
                  + ((CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxSet) << 6)
                  + this->fuzzStreamOffset
                  + this->fuzzStreamStart;
        }

        if (!put || CAGENT_RAND64(mAgent, "MFuzzer") % 2) {  // Get
            mAgent->do_getAuto(addr);
        } else { // Put
            auto putdata = make_shared_tldata<DATASIZE>();
            for (int i = 0; i < DATASIZE; i++) {
                putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "MFuzzer");
            }
            mAgent->do_putfulldata(addr, putdata);
        }
    }
}

void MFuzzer::caseTest() {
    if (*cycles == 500) {
        auto putdata = make_shared_tldata<DATASIZE>();
        for (int i = 0; i < DATASIZE/2; i++) {
            putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "MFuzzer");
        }
        for (int i = DATASIZE/2; i < DATASIZE; i++) {
            putdata->data[i] = putdata->data[i-DATASIZE/2];
        }
        mAgent->do_putpartialdata(0x1070, 2, 0xf0000, putdata);
    }
    if (*cycles == 600) {
      mAgent->do_getAuto(0x1040);
    }
}

void MFuzzer::caseTest2() {
  if (*cycles == 100) {
    auto putdata = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE/2; i++) {
      putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "MFuzzer");
    }
    for (int i = DATASIZE/2; i < DATASIZE; i++) {
      putdata->data[i] = putdata->data[i-DATASIZE/2];
    }
    mAgent->do_putpartialdata(0x1000, 2, 0xf, putdata);
  }
  if (*cycles == 500) {
    mAgent->do_get(0x1000, 2, 0xf);
  }
}

void MFuzzer::caseTest3() {
  //复用fuzzStream Code
  paddr_t addr;
  int     alias;
  bool do_alias = false;

  // dont alias
  alias = (do_alias) ? (CAGENT_RAND64(mAgent, "MFuzzer") % FUZZ_STREAM_RANGE.maxAlias) : 0;
  // Read And NtoT
  // Write 
  if (state == bwTestState::aquire) {
    // this->fuzzStreamOffset   += this->fuzzStreamStep;
    // addr =  this->fuzzStreamStart + 0x100 * blkProcessed + this->fuzzStreamStep;//0x040, 0x140, 0x240...
    addr =  this->fuzzStreamStart + (blkProcessed*8+index) * this->fuzzStreamStep;            //0x00, 0x40, 0x80, 0xC0...
    #if DEBUG_ADDR_ERROR
    addr = (addr+0x40*(16)-1) & ~(0x40*(16)-1)+blkProcessed%8;//FIXME BUG, key not find
    #endif
    if (!mAgent->config().memoryEnable)
      return;

    // addr = remap_memory_address(addr);
    if(mAgent->do_getAuto(addr)){
        blkProcessed++;
        filledAddrs.push(addr);
        // printf("size1 %lu\n",filledAddrs.size());
    };  // AcquireBlock NtoT
        // Chan A

    if (blkProcessed == blkCountLimit) {
      state = bwTestState::wait_aquire;
      blkProcessed = 0;
    }
  }

  if (state == bwTestState::wait_aquire || state == bwTestState::aquire) {
    // wait channel A to fire
    if (mAgent->is_m_fired()) {
      // How many cycle will D channel hold the data?
      blkFired++;
    }
    if (blkFired == blkCountLimit) {// Notice! 64B need 2 fired.
      state = bwTestState::releasing;
      blkFired = 0;
    }
  }
// FIXME 重地址冲突(M1和M3相邻时间对同一地址读写)
  if (state == bwTestState::releasing) {

    addr = filledAddrs.front();
    auto putdata = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE; i++) {
      putdata->data[i] = (uint8_t)CAGENT_RAND64(mAgent, "CFuzzer");
    }
    // printf("will do_putfulldata(%lx)\n",addr);
    if(mAgent->do_putfulldata(addr, putdata)){
        blkProcessed++;
        filledAddrs.push(addr);
        filledAddrs.pop();
        // printf("size2 %lu\n",filledAddrs.size());
        // printf("fine do_putfulldata(%lx)\n",addr);
    }

    if (blkProcessed == blkCountLimit) {
      state = bwTestState::wait_release;
      blkProcessed = 0;
    }
  }

  if (state == bwTestState::releasing||state == bwTestState::wait_release) {
    // wait channel D to fire
    if (mAgent->is_d_fired()) {
      // How many cycle will A channel hold the data?
      blkFired++;//0x180
    }
    // is AccessAck
    if (blkFired == blkCountLimit) {
      state = bwTestState::aquire2;
      perfCycleStart=this->mAgent->cycle();
      blkFired = 0;
      printf("#################Test\n\n");
    }
  }

    // Read 2 0x00,0x40,0x80,0xc0
  if (state == bwTestState::aquire2) {
    addr = filledAddrs.front();

    if(mAgent->do_getAuto(addr)){
        blkProcessed++;
        filledAddrs.pop();
    };
    if (blkProcessed == blkCountLimit) {
      state = bwTestState::wait_aquire2;
      blkProcessed = 0;
    }
  }

  if (state == bwTestState::aquire2||state == bwTestState::wait_aquire2) {
    // wait channel A to fire
    if (mAgent->is_m_fired()) {
      // How many cycle will D channel hold the data?
      blkFired++;
    }
    if (blkFired == blkCountLimit) {
      state = exit_fuzzer;
      blkFired = 0;
      filledAddrs.pop();
      perfCycleEnd=this->mAgent->cycle();
      // perfCycleEnd=(this->mAgent->cycle()-perfCycleStart)/2;
      // TLSystemFinishEvent().Fire();// stop
      std::cout<<"perf debug : "<<blkCountLimit*64/((this->mAgent->cycle()-perfCycleStart)/2)<<"B/Cycle"<<std::endl;
    }
  }
}
void MFuzzer::tick() {

    // if (this->mode == TLSequenceMode::FUZZ_ARI)
    // {
    //     this->randomTest(false);

    //     if (this->mAgent->cycle() >= this->fuzzARIRangeIterationTime)
    //     {
    //         this->fuzzARIRangeIterationTime += this->fuzzARIRangeIterationInterval;
    //         this->fuzzARIRangeIndex++;

    //         if (this->fuzzARIRangeIndex == fuzzARIRangeOrdinal.size())
    //         {
    //             this->fuzzARIRangeIndex = 0;
    //             this->fuzzARIRangeIterationCount++;

    //             std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());
    //         }

    //         LogInfo(this->mAgent->cycle(), Append("Fuzz Set switched: index = ", this->fuzzARIRangeIndex, ", permutation: "));
    //         LogEx(
    //             std::cout << "[ ";
    //             for (size_t i = 0; i < fuzzARIRangeOrdinal.size(); i++)
    //                 std::cout << fuzzARIRangeOrdinal[i] << " ";
    //             std::cout << "]";
    //         );
    //         LogEx(std::cout << std::endl);
    //     }

    //     if (this->fuzzARIRangeIterationCount == this->fuzzARIRangeIterationTarget)
    //     {
    //         // TLSystemFinishEvent().Fire();
    //     }
    // }
    // else if (this->mode == TLSequenceMode::FUZZ_STREAM)
    // {
    //     this->randomTest(false);

    //     if (this->mAgent->cycle() >= this->fuzzStreamStepTime)
    //     {
    //         this->fuzzStreamStepTime += this->fuzzStreamInterval;
    //         this->fuzzStreamOffset   += this->fuzzStreamStep;
    //     }

    //     if (this->fuzzStreamEnded)
    //     {
    //         // TLSystemFinishEvent().Fire();
    //     }
    // }
    // else 
    // if (this->mode == TLSequenceMode::FUZZ_STREAM_GS) {
    if(perfCycleEnd==0){
      this->caseTest3();

      if (this->mAgent->cycle() >= this->fuzzStreamStepTime)
      {
          this->fuzzStreamStepTime += this->fuzzStreamInterval;
      }

      if (this->fuzzStreamEnded)
      {
          // TLSystemFinishEvent().Fire();
      }
    }
}
