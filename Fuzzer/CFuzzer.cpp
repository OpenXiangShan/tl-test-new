//
// Created by wkf on 2021/10/29.
//

#include "Fuzzer.h"

CFuzzer::CFuzzer(tl_agent::CAgent *cAgent) {
    this->cAgent = cAgent;
    srand((unsigned)time(0));
}

void CFuzzer::randomTest() {
    paddr_t addr = (rand() % 0x100) * 0x100;
    if (rand() % 2) {  // AcquireBlock
        cAgent->do_acquireBlock(addr, tl_agent::NtoT);
    } else { // ReleaseData
        uint8_t* putdata = new uint8_t[DATASIZE];
        for (int i = 0; i < DATASIZE; i++) {
            putdata[i] = (uint8_t)rand();
        }
        cAgent->do_releaseData(addr, tl_agent::TtoN, putdata);
    }
}

void CFuzzer::caseTest() {
    if (*cycles == 100) {
        this->cAgent->do_acquireBlock(0x1000, tl_agent::NtoT);
    }
    if (*cycles == 300) {
        uint8_t* putdata = new uint8_t[DATASIZE];
        for (int i = 0; i < DATASIZE; i++) {
            putdata[i] = (uint8_t)rand();
        }
        this->cAgent->do_releaseData(0x1000, tl_agent::TtoN, putdata);
    }
}

void CFuzzer::tick() {
    this->randomTest();
//    this->caseTest();
}