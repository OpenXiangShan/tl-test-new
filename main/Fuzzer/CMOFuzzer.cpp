#include "Fuzzer.h"


CMOFuzzer::CMOFuzzer(tl_agent::CMOAgent* cmoAgent) noexcept
{
    this->cmoAgent = cmoAgent;

    this->memoryStart   = cmoAgent->config().memoryStart;
    this->memoryEnd     = cmoAgent->config().memoryEnd;
}

void CMOFuzzer::randomTest(bool clean, bool flush, bool inval)
{
    if (cmoAgent->local()->hasInflight())
        return;

    paddr_t addr = (CMOAGENT_RAND64(cmoAgent, "CMOFuzzer"));

    // TODO: addr with narrow region or ARI
    addr &= 0xFFFFUL;
    addr &= ~0x3FUL;

    addr = remap_memory_address(addr);

    switch (CMOAGENT_RAND64(cmoAgent, "CMOFuzzer") % 4)
    {
        case 0: // cbo.clean
            if (!clean)
                return;
            cmoAgent->do_cbo_clean(addr);
            break;

        case 1: // cbo.flush
            if (!flush)
                return;
            cmoAgent->do_cbo_flush(addr);
            break;

        case 2: // cbo.inval
            if (!inval)
                return;
            cmoAgent->do_cbo_inval(addr);
            break;

        default:
            return;
    }
}

void CMOFuzzer::tick()
{
    this->randomTest(true, true, true);
}
