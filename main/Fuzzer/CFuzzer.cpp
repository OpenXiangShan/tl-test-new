//
// Created by wkf on 2021/10/29.
//

#include "../Base/TLEnum.hpp"

#include "../Events/TLSystemEvent.hpp"

#include <algorithm>
#include <iostream>


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


CFuzzer::CFuzzer(tl_agent::CAgent *cAgent) noexcept {
    this->cAgent = cAgent;

    this->memoryStart                   = cAgent->config().memoryStart;
    this->memoryEnd                     = cAgent->config().memoryEnd;

    this->cmoStart                      = cAgent->config().cmoStart;
    this->cmoEnd                        = cAgent->config().cmoEnd;

    this->fuzzARIRangeIndex             = 0;
    this->fuzzARIRangeIterationInterval = cAgent->config().fuzzARIInterval;
    this->fuzzARIRangeIterationTarget   = cAgent->config().fuzzARITarget;

    this->fuzzARIRangeIterationCount    = 0;
    this->fuzzARIRangeIterationTime     = cAgent->config().fuzzARIInterval;

    this->fuzzStreamOffset              = 0;
    this->fuzzStreamStepTime            = cAgent->config().fuzzStreamStep;
    this->fuzzStreamEnded               = false;
    this->fuzzStreamStep                = cAgent->config().fuzzStreamStep;
    this->fuzzStreamInterval            = cAgent->config().fuzzStreamInterval;
    this->fuzzStreamStart               = cAgent->config().fuzzStreamStart;
    this->fuzzStreamEnd                 = cAgent->config().fuzzStreamEnd;

    decltype(cAgent->config().sequenceModes.end()) modeInMap;
    if ((modeInMap = cAgent->config().sequenceModes.find(cAgent->sysId()))
            != cAgent->config().sequenceModes.end())
        this->mode = modeInMap->second;
    else
        this->mode = TLSequenceMode::FUZZ_ARI;

    if (this->mode == TLSequenceMode::FUZZ_ARI)
    {
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] in FUZZ_ARI mode").EndLine());

        for (size_t i = 0; i < FUZZ_ARI_RANGES.size(); i++)
            this->fuzzARIRangeOrdinal.push_back(i);

        size_t loop = cAgent->sysSeed() % fact(fuzzARIRangeOrdinal.size());
        for (size_t i = 0; i < loop; i++)
            std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());

        LogInfo(this->cAgent->cycle(), Append("Initial Fuzz Set: index = ", this->fuzzARIRangeIndex, ", permutation: "));
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
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] in FUZZ_STREAM mode").EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream steps ")
            .Hex().ShowBase().Append(this->fuzzStreamStep).EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream starts at ")
            .Hex().ShowBase().Append(this->fuzzStreamStart).EndLine());
        LogInfo(this->cAgent->cycle(), Append("CFuzzer [", cAgent->sysId(), "] stream ends at ")
            .Hex().ShowBase().Append(this->fuzzStreamEnd).EndLine());
    }
}

void CFuzzer::randomTest(bool do_alias) {
    paddr_t addr;
    int     alias;
    if (this->mode == TLSequenceMode::FUZZ_ARI || this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        if (this->mode == TLSequenceMode::FUZZ_ARI)
        {
            // Tag + Set + Offset
            addr  = ((CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxTag) << 13) 
                  + ((CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxSet) << 6);
            alias = (do_alias) ? (CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxAlias) : 0;
        }
        else // FUZZ_STREAM
        {
            addr  = ((CAGENT_RAND64(cAgent, "CFUZZER") % FUZZ_STREAM_RANGE.maxTag) << 13)
                  + ((CAGENT_RAND64(cAgent, "CFUZZER") % FUZZ_STREAM_RANGE.maxSet) << 6)
                  + this->fuzzStreamOffset
                  + this->fuzzStreamStart;
            alias = (do_alias) ? (CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_STREAM_RANGE.maxAlias) : 0;

            if (addr >= this->fuzzStreamEnd)
            {
                this->fuzzStreamEnded = true;
                return;
            }
        }

        if (CAGENT_RAND64(cAgent, "CFuzzer") % 4)
        {
            if (!cAgent->config().memoryEnable)
                return;

            addr = remap_memory_address(addr);

            if (CAGENT_RAND64(cAgent, "CFuzzer") % 2) {
                if (CAGENT_RAND64(cAgent, "CFuzzer") % 3) {
                    if (CAGENT_RAND64(cAgent, "CFuzzer") % 2) {
                        cAgent->do_acquireBlock(addr, TLParamAcquire::NtoT, alias); // AcquireBlock NtoT
                    } else {
                        cAgent->do_acquireBlock(addr, TLParamAcquire::NtoB, alias); // AcquireBlock NtoB
                    }
                } else {
                    cAgent->do_acquirePerm(addr, TLParamAcquire::NtoT, alias); // AcquirePerm
                }
            } else {
                /*
                uint8_t* putdata = new uint8_t[DATASIZE];
                for (int i = 0; i < DATASIZE; i++) {
                    putdata[i] = (uint8_t)CAGENT_RAND64(cAgent, "CFuzzer");
                }
                cAgent->do_releaseData(addr, tl_agent::TtoN, putdata); // ReleaseData
                */
                cAgent->do_releaseDataAuto(addr, alias, 
                    CAGENT_RAND64(cAgent, "CFuzzer") & 0x1,
                    CAGENT_RAND64(cAgent, "CFuzzer") & 0x1); // feel free to releaseData according to its priv
            }
        }
        else if (cAgent->config().cmoEnable)
        {
            addr = remap_cmo_address(addr);

            bool alwaysHit = (CAGENT_RAND64(cAgent, "CFuzzer") % 8) == 0;

            switch (CAGENT_RAND64(cAgent, "CFuzzer") % 3)
            {
                case 0: // cbo.clean
                    if (cAgent->config().cmoEnableCBOClean)
                        cAgent->do_cbo_clean(addr, alwaysHit);
                    break;

                case 1: // cbo.flush
                    if (cAgent->config().cmoEnableCBOFlush)
                        cAgent->do_cbo_flush(addr, alwaysHit);
                    break;

                case 2: // cbo.inval
                    if (cAgent->config().cmoEnableCBOInval)
                        cAgent->do_cbo_inval(addr, alwaysHit);
                    break;

                default:
                    break;
            }
        }
    }
}

void CFuzzer::caseTest() {
  //复用fuzzStream Code
  paddr_t addr;
  int     alias;
  bool do_alias = false;

  // dont alias
  alias = (do_alias) ? (CAGENT_RAND64(cAgent, "CFuzzer") % FUZZ_STREAM_RANGE.maxAlias) : 0;
  // Read And NtoT
  // Write 
  if (state == bwTestState::aquire) {
    // this->fuzzStreamOffset   += this->fuzzStreamStep;
    // addr =  this->fuzzStreamStart + 0x100 * blkProcessed + this->fuzzStreamStep;//0x040, 0x140, 0x240...
    addr =  this->fuzzStreamStart + blkProcessed * this->fuzzStreamStep;            //0x00, 0x40, 0x80, 0xC0...
    // addr = (addr+0x40*(16)-1) & ~(0x40*(16)-1); // BUG!
    if (!cAgent->config().memoryEnable)
      return;

    addr = remap_memory_address(addr);
    if(cAgent->do_acquireBlock(addr, TLParamAcquire::NtoT, alias)){
        blkProcessed++;
        filledAddrs.push(addr);
    };  // AcquireBlock NtoT
        // Chan A

    if (blkProcessed == blkCountLimit) {
      state = bwTestState::wait_aquire;
      blkProcessed = 0;
    }
  }

  if (state == bwTestState::wait_aquire || state == bwTestState::aquire) {
    // wait channel A to fire
    if (cAgent->is_d_fired()) {
      // How many cycle will D channel hold the data?
      blkFired++;
    }
    if (blkFired == blkCountLimit*2) {// Notice! 64B need 2 fired.
      state = bwTestState::releasing;
      blkFired = 0;
    }
  }

  if (state == bwTestState::releasing) {

    addr = filledAddrs.front();
    // printf("Debug RRRRRR 0x%08lx\n",addr);
    
    auto putdata = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE; i++) {
      putdata->data[i] = (uint8_t)CAGENT_RAND64(cAgent, "CFuzzer");
    }
    if(cAgent->do_releaseData(addr, TLParamRelease::TtoN, putdata,0)){
        // printf("test TTTTTTTTT\n");
        blkProcessed++;
        filledAddrs.push(addr);
        filledAddrs.pop();
    }
    // else{
    //     cAgent->do_acquireBlock(addr, TLParamAcquire::NtoT, alias);
    //     printf("HAVE RWRWRWRWRWRW\n");
    // }; // ReleaseData
    
    if (blkProcessed == blkCountLimit) {
      state = bwTestState::wait_release;
      blkProcessed = 0;
    }
  }

  if (state == bwTestState::releasing||state == bwTestState::wait_release) {
    // wait channel D to fire
    if (cAgent->is_d_fired()) {
      // How many cycle will A channel hold the data?
      blkFired++;//0x180
    }
    if (blkFired == blkCountLimit+1) {
      state = bwTestState::aquire2;
      perfCycleStart=this->cAgent->cycle();
      blkFired = 0;
      printf("#################Test\n\n");
    }
  }

    // Read 2 0x00,0x40,0x80,0xc0
  if (state == bwTestState::aquire2) {
    addr = filledAddrs.front();
    
    if(cAgent->do_acquireBlock(addr, TLParamAcquire::NtoT, alias)){
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
    if (cAgent->is_d_fired()) {
      // How many cycle will D channel hold the data?
      blkFired++;
    }
    if (blkFired == blkCountLimit*2) {
      state = bwTestState::aquire;
      blkFired = 0;
      filledAddrs.pop();
      perfCycleEnd=(this->cAgent->cycle()-perfCycleStart)/2;
      TLSystemFinishEvent().Fire();// stop
      std::cout<<"perf debug : "<<blkCountLimit*64/perfCycleEnd<<"B/Cycle"<<std::endl;
    }
  }
}

void CFuzzer::tick() {
#ifndef TLC_TEST
    return;
#endif
    if (this->mode == TLSequenceMode::FUZZ_ARI)
    {
        this->randomTest(true);

        if (this->cAgent->cycle() >= this->fuzzARIRangeIterationTime)
        {
            this->fuzzARIRangeIterationTime += this->fuzzARIRangeIterationInterval;
            this->fuzzARIRangeIndex++;

            if (this->fuzzARIRangeIndex == fuzzARIRangeOrdinal.size())
            {
                this->fuzzARIRangeIndex = 0;
                this->fuzzARIRangeIterationCount++;

                std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());
            }

            LogInfo(this->cAgent->cycle(), Append("Fuzz Set switched: index = ", this->fuzzARIRangeIndex, ", permutation: "));
            LogEx(
                std::cout << "[ ";
                for (size_t i = 0; i < fuzzARIRangeOrdinal.size(); i++)
                    std::cout << fuzzARIRangeOrdinal[i] << " ";
                std::cout << "]";
            );
            LogEx(std::cout << std::endl);
        }

        if (this->fuzzARIRangeIterationCount == this->fuzzARIRangeIterationTarget)
        {
            TLSystemFinishEvent().Fire();
        }
    }
    else if (this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        this->randomTest(true);

        if (this->cAgent->cycle() >= this->fuzzStreamStepTime)
        {
            this->fuzzStreamStepTime += this->fuzzStreamInterval;
            this->fuzzStreamOffset   += this->fuzzStreamStep;
        }

        if (this->fuzzStreamEnded)
        {
            TLSystemFinishEvent().Fire();
        }
    }
    else if (this->mode == TLSequenceMode::FUZZ_STREAM_GS) {
        this->caseTest();
        if (this->cAgent->cycle() >= this->fuzzStreamStepTime)
        {
            this->fuzzStreamStepTime += this->fuzzStreamInterval;
        }

        if (this->fuzzStreamEnded)
        {
            // TLSystemFinishEvent().Fire();
        }
    }
}
