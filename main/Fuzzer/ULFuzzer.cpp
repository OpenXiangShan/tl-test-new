//
// Created by wkf on 2021/10/29.
//

#include "Fuzzer.h"
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


ULFuzzer::ULFuzzer(tl_agent::ULAgent *ulAgent) noexcept {
    this->ulAgent = ulAgent;

    this->memoryStart                   = ulAgent->config().memoryStart;
    this->memoryEnd                     = ulAgent->config().memoryEnd;

    this->fuzzARIRangeIndex             = 0;
    this->fuzzARIRangeIterationInterval = ulAgent->config().fuzzARIInterval;
    this->fuzzARIRangeIterationTarget   = ulAgent->config().fuzzARITarget;

    this->fuzzARIRangeIterationCount    = 0;
    this->fuzzARIRangeIterationTime     = ulAgent->config().fuzzARIInterval;

    this->fuzzStreamOffset              = 0;
    this->fuzzStreamStepTime            = ulAgent->config().fuzzStreamStep;
    this->fuzzStreamEnded               = false;
    this->fuzzStreamStep                = ulAgent->config().fuzzStreamStep;
    this->fuzzStreamInterval            = ulAgent->config().fuzzStreamInterval;
    this->fuzzStreamStart               = ulAgent->config().fuzzStreamStart;
    this->fuzzStreamEnd                 = ulAgent->config().fuzzStreamEnd;

    decltype(ulAgent->config().sequenceModes.end()) modeInMap;
    if ((modeInMap = ulAgent->config().sequenceModes.find(ulAgent->sysId()))
            != ulAgent->config().sequenceModes.end())
        this->mode = modeInMap->second;
    else
        this->mode = TLSequenceMode::FUZZ_ARI;

    if (this->mode == TLSequenceMode::FUZZ_ARI)
    {
        LogInfo(this->ulAgent->cycle(), Append("ULFuzzer [", ulAgent->sysId(), "] in FUZZ_ARI mode").EndLine());

        for (size_t i = 0; i < FUZZ_ARI_RANGES.size(); i++)
            this->fuzzARIRangeOrdinal.push_back(i);

        size_t loop = ulAgent->sysSeed() % fact(fuzzARIRangeOrdinal.size());
        for (size_t i = 0; i < loop; i++)
            std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());

        LogInfo(this->ulAgent->cycle(), Append("Initial Fuzz Set: index = ", this->fuzzARIRangeIndex, ", permutation: "));
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
        LogInfo(this->ulAgent->cycle(), Append("ULFuzzer [", ulAgent->sysId(), "] in FUZZ_STREAM mode").EndLine());
        LogInfo(this->ulAgent->cycle(), Append("ULFuzzer [", ulAgent->sysId(), "] stream steps ")
            .Hex().ShowBase().Append(this->fuzzStreamStep).EndLine());
        LogInfo(this->ulAgent->cycle(), Append("ULFuzzer [", ulAgent->sysId(), "] stream starts at ")
            .Hex().ShowBase().Append(this->fuzzStreamStart).EndLine());
        LogInfo(this->ulAgent->cycle(), Append("ULFuzzer [", ulAgent->sysId(), "] stream ends at ")
            .Hex().ShowBase().Append(this->fuzzStreamEnd).EndLine());
    }
}

void ULFuzzer::randomTest(bool put) {
    if (this->mode == TLSequenceMode::PASSIVE)
    {
        return;
    }
    else if (this->mode == TLSequenceMode::FUZZ_ARI || this->mode == TLSequenceMode::FUZZ_STREAM)
    {
        paddr_t addr = (CAGENT_RAND64(ulAgent, "ULFuzzer") % 0x400) << 6;

        if (this->mode == TLSequenceMode::FUZZ_ARI)
        {
            // Tag + Set + Offset
            addr  = ((CAGENT_RAND64(ulAgent, "ULFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxTag) << 13) 
                  + ((CAGENT_RAND64(ulAgent, "ULFuzzer") % FUZZ_ARI_RANGES[fuzzARIRangeOrdinal[fuzzARIRangeIndex]].maxSet) << 6);
            
            addr  = remap_memory_address(addr);
        }
        else // FUZZ_STREAM
        {
            addr  = ((CAGENT_RAND64(ulAgent, "ULFuzzer") % FUZZ_STREAM_RANGE.maxTag) << 13)
                  + ((CAGENT_RAND64(ulAgent, "ULFuzzer") % FUZZ_STREAM_RANGE.maxSet) << 6)
                  + this->fuzzStreamOffset
                  + this->fuzzStreamStart;
        }

        if (!put || CAGENT_RAND64(ulAgent, "ULFuzzer") % 2) {  // Get
            ulAgent->do_getAuto(addr);
        } else { // Put
            auto putdata = make_shared_tldata<DATASIZE>();
            for (int i = 0; i < DATASIZE; i++) {
                putdata->data[i] = (uint8_t)CAGENT_RAND64(ulAgent, "ULFuzzer");
            }
            ulAgent->do_putfulldata(addr, putdata);
        }
    }
}

void ULFuzzer::caseTest() {
    if (*cycles == 500) {
        auto putdata = make_shared_tldata<DATASIZE>();
        for (int i = 0; i < DATASIZE/2; i++) {
            putdata->data[i] = (uint8_t)CAGENT_RAND64(ulAgent, "ULFuzzer");
        }
        for (int i = DATASIZE/2; i < DATASIZE; i++) {
            putdata->data[i] = putdata->data[i-DATASIZE/2];
        }
        ulAgent->do_putpartialdata(0x1070, 2, 0xf0000, putdata);
    }
    if (*cycles == 600) {
      ulAgent->do_getAuto(0x1040);
    }
}

void ULFuzzer::caseTest2() {
  if (*cycles == 100) {
    auto putdata = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE/2; i++) {
      putdata->data[i] = (uint8_t)CAGENT_RAND64(ulAgent, "ULFuzzer");
    }
    for (int i = DATASIZE/2; i < DATASIZE; i++) {
      putdata->data[i] = putdata->data[i-DATASIZE/2];
    }
    ulAgent->do_putpartialdata(0x1000, 2, 0xf, putdata);
  }
  if (*cycles == 500) {
    ulAgent->do_get(0x1000, 2, 0xf);
  }
}

void ULFuzzer::caseTest3() {
  //复用fuzzStream Code
  paddr_t addr;
  int     alias;
  bool do_alias = false;

  // dont alias
  alias = (do_alias) ? (CAGENT_RAND64(ulAgent, "ULFuzzer") % FUZZ_STREAM_RANGE.maxAlias) : 0;
  // Read And NtoT
  // Write 
  if (state == bwTestState::aquire) {
    // this->fuzzStreamOffset   += this->fuzzStreamStep;
    // addr =  this->fuzzStreamStart + 0x100 * blkProcessed + this->fuzzStreamStep;//0x040, 0x140, 0x240...
    addr =  this->fuzzStreamStart + (blkProcessed+index) * this->fuzzStreamStep;            //0x00, 0x40, 0x80, 0xC0...
    if (!ulAgent->config().memoryEnable)
      return;

    // addr = remap_memory_address(addr);
    if(ulAgent->do_getAuto(addr)){
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
    if (ulAgent->is_d_fired()) {
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
    printf("Debug RRRRRR 0x%08lx\n",addr);
    
    auto putdata = make_shared_tldata<DATASIZE>();
    for (int i = 0; i < DATASIZE; i++) {
      putdata->data[i] = (uint8_t)CAGENT_RAND64(ulAgent, "CFuzzer");
    }
    if(ulAgent->do_putfulldata(addr, putdata)){
        printf("test TTTTTTTTT\n");
        blkProcessed++;
        filledAddrs.push(addr);
        filledAddrs.pop();
    }
    // else{
    //     ulAgent->do_acquireBlock(addr, TLParamAcquire::NtoT, alias);
    //     printf("HAVE RWRWRWRWRWRW\n");
    // }; // ReleaseData
    
    if (blkProcessed == blkCountLimit) {
      state = bwTestState::wait_release;
      blkProcessed = 0;
    }
  }

  if (state == bwTestState::releasing||state == bwTestState::wait_release) {
    // wait channel D to fire
    if (ulAgent->is_d_fired()) {
      // How many cycle will A channel hold the data?
      blkFired++;//0x180
    }
    if (blkFired == blkCountLimit+1) {
      state = bwTestState::aquire2;
      perfCycleStart=this->ulAgent->cycle();
      blkFired = 0;
      printf("#################Test\n\n");
    }
  }

    // Read 2 0x00,0x40,0x80,0xc0
  if (state == bwTestState::aquire2) {
    addr = filledAddrs.front();
    
    if(ulAgent->do_getAuto(addr)){
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
    if (ulAgent->is_d_fired()) {
      // How many cycle will D channel hold the data?
      blkFired++;
    }
    if (blkFired == blkCountLimit*2) {
      state = bwTestState::aquire;
      blkFired = 0;
      filledAddrs.pop();
      perfCycleEnd=(this->ulAgent->cycle()-perfCycleStart)/2;
      
      TLSystemFinishEvent().Fire();// stop
      std::cout<<"perf debug : "<<blkCountLimit*64/perfCycleEnd<<"B/Cycle"<<std::endl;
    }
  }
}
void ULFuzzer::tick() {

    // if (this->mode == TLSequenceMode::FUZZ_ARI)
    // {
    //     this->randomTest(false);

    //     if (this->ulAgent->cycle() >= this->fuzzARIRangeIterationTime)
    //     {
    //         this->fuzzARIRangeIterationTime += this->fuzzARIRangeIterationInterval;
    //         this->fuzzARIRangeIndex++;

    //         if (this->fuzzARIRangeIndex == fuzzARIRangeOrdinal.size())
    //         {
    //             this->fuzzARIRangeIndex = 0;
    //             this->fuzzARIRangeIterationCount++;

    //             std::next_permutation(fuzzARIRangeOrdinal.begin(), fuzzARIRangeOrdinal.end());
    //         }

    //         LogInfo(this->ulAgent->cycle(), Append("Fuzz Set switched: index = ", this->fuzzARIRangeIndex, ", permutation: "));
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

    //     if (this->ulAgent->cycle() >= this->fuzzStreamStepTime)
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
    if(true){
        this->caseTest3();

    if (this->ulAgent->cycle() >= this->fuzzStreamStepTime)
    {
        this->fuzzStreamStepTime += this->fuzzStreamInterval;
    }

    if (this->fuzzStreamEnded)
    {
        // TLSystemFinishEvent().Fire();
    }
}
}
