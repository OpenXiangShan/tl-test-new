#include "Fuzzer.h"


MMIOFuzzer::MMIOFuzzer(tl_agent::MMIOAgent* mmioAgent) noexcept
{
    this->mmioAgent = mmioAgent;

    this->mmioStart     = mmioAgent->config().mmioStart;
    this->mmioEnd       = mmioAgent->config().mmioEnd;
}

void MMIOFuzzer::randomTest(bool put)
{
    paddr_t addr = (CAGENT_RAND64(mmioAgent, "MMIOFuzzer"));

    addr = remap_mmio_address(addr);

    if (!put || CAGENT_RAND64(mmioAgent, "MMIOFuzzer") % 2) // Get
        mmioAgent->do_get(addr &= ~paddr_t(0x07), 3, 0xff);
    else
    {
        // Put
        auto data = make_shared_tldata<DATASIZE_MMIO>();
        for (int i = 0; i < DATASIZE_MMIO; i++)
            data->data[i] = (uint8_t)CAGENT_RAND64(mmioAgent, "MMIOFuzzer");

        if (CAGENT_RAND64(mmioAgent, "MMIOFuzzer") % 2) // PutPartialData
        {
            uint8_t size = (uint8_t)(CAGENT_RAND64(mmioAgent, "MMIOFuzzer") % 4);
            uint8_t mask;

            if (size == 3)
            {
                mask = 0xff;
                addr &= ~paddr_t(0x07);
            }
            else if (size == 2)
            {
                mask = 0xf << (addr & 0x04);
                addr &= ~paddr_t(0x03);
            }
            else if (size == 1)
            {
                mask = 0x3 << (addr & 0x06);
                addr &= ~paddr_t(0x01);
            }
            else if (size == 0)
            {
                mask = 0x1 << (addr & 0x07);
            }

            mmioAgent->do_putpartialdata(addr, size, mask, data);

        }
        else // PutFullData
            mmioAgent->do_putfulldata(addr &= ~paddr_t(0x07), data);
    }
}

void MMIOFuzzer::tick()
{
    this->randomTest(true);
}
