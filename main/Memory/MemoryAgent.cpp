#include "MemoryAgent.hpp"

#include <bitset>
#include <cstring>


//
namespace axi_agent::details {

    void data_dump(const uint8_t* data, uint8_t size, uint64_t mask)
    {
        std::bitset<64> maskBits(mask);

        Gravity::StringAppender sa;
        sa.Append("[ ").Hex().Fill('0');

        if (glbl.cfg.verbose_data_full)
        {
            for (int i = 0; i < size; i++)
                if (maskBits[i])
                    sa.NextWidth(2).Append(uint64_t(data[i]), " ");
                else
                    sa.Append("?? ");
        }
        else
        {
            bool skip = false;
            for (std::size_t j = 0; j < size;)
            {
                sa.NextWidth(2).Append(uint64_t(data[j]), " ");

                if (skip)
                {
                    j = (j / size + 1) * size;
                    sa.Append("... ");
                }
                else 
                    j++;

                skip = !skip;
            }
        }
        
        sa.Append("]");

        std::cout << sa.ToString();
    }

    void data_dump_embedded(const uint8_t* data, uint8_t size, uint64_t mask)
    {
        if (glbl.cfg.verbose_data_full)
            std::cout << std::endl;

        data_dump(data, size, mask);
    }
}


// Implementation of: class AXIWriteTransaction
namespace axi_agent {

    template<size_t BusWidth>
    AXIWriteTransaction<BusWidth>::AXIWriteTransaction() noexcept
        : request   ()
        , data      ()
        , response  ()
        , dataDone  (false)
    { }

    template<size_t BusWidth>
    AXIWriteTransaction<BusWidth>::AXIWriteTransaction(const FiredBundleAW& request) noexcept
        : request   (request)
        , data      ()
        , response  ()
        , dataDone  (false)
    { }
}


// Implementation of: class AXIReadTransaction
namespace axi_agent {

    template<size_t BusWidth>
    AXIReadTransaction<BusWidth>::AXIReadTransaction() noexcept
        : request       ()
        , data          ()
        , dataPending   (0)
        , dataSending   (0)
    { }

    template<size_t BusWidth>
    AXIReadTransaction<BusWidth>::AXIReadTransaction(const FiredBundleAR& request) noexcept
        : request       (request)
        , data          ()
        , dataPending   (0)
        , dataSending   (0)
    { }
}


// Implementation of: class MemoryAgent
namespace axi_agent {

    MemoryAgent::MemoryAgent(TLLocalConfig* cfg, unsigned int seed, uint64_t* cycles) noexcept
        : seed  (seed)
    {
        this->cfg = cfg;
        this->pmem = new uint8_t[size()];
        this->cycles = cycles;
        this->port = nullptr;

        std::memset(pmem, 0, size());
    }

    MemoryAgent::~MemoryAgent() noexcept
    {
        delete[] pmem;
    }

    size_t MemoryAgent::cycle() const noexcept
    {
        return *cycles;
    }

    int MemoryAgent::sys() const noexcept
    {
        return 0;
    }

    int MemoryAgent::sysId() const noexcept
    {
        return 0;
    }

    unsigned int MemoryAgent::sysSeed() const noexcept
    {
        return 0;
    }

    const TLLocalConfig& MemoryAgent::config() const noexcept
    {
        return *cfg;
    }

    TLLocalConfig& MemoryAgent::config() noexcept
    {
        return *cfg;
    }

    bool MemoryAgent::mainSys() const noexcept
    {
        return false;
    }

    uint8_t* MemoryAgent::memory() noexcept
    {
        return pmem;
    }

    const uint8_t* MemoryAgent::memory() const noexcept
    {
        return pmem;
    }

    uint8_t& MemoryAgent::access(paddr_t addr) noexcept
    {
        return pmem[addr - cfg->memoryStart];
    }

    uint8_t MemoryAgent::access(paddr_t addr) const noexcept
    {
        return pmem[addr - cfg->memoryStart];
    }

    bool MemoryAgent::accessible(paddr_t addr) const noexcept
    {
        return addr >= cfg->memoryStart && addr < cfg->memoryEnd;
    }

    size_t MemoryAgent::size() const noexcept
    {
        return cfg->memoryEnd - cfg->memoryStart;
    }

    bool MemoryAgent::is_w_active(uint32_t wid) const noexcept
    {
        for (auto activeWrite : activeWrites)
            if (activeWrite->request.bundle.id == wid)
                return true;

        return false;
    }

    bool MemoryAgent::is_r_active(uint32_t rid) const noexcept
    {
        for (auto activeRead : activeReads)
            if (activeRead->request.bundle.id == rid)
                return true;

        return false;
    }

    void MemoryAgent::handle_aw()
    {
        port->aw.ready = true;
    }

    void MemoryAgent::handle_w()
    {
        port->w.ready = true;
    }

    void MemoryAgent::send_b()
    {
        if (finishedWrites.empty())
            port->b.valid = false;
        else
        {
            auto finishedWrite = finishedWrites.front();

            port->b.valid = true;
            port->b.id    = finishedWrite->response.bundle.id;
            port->b.resp  = finishedWrite->response.bundle.resp;
        }
    }

    void MemoryAgent::handle_ar()
    {
        port->ar.ready = true;
    }

    void MemoryAgent::send_r()
    {
        if (!port->r.fire() && port->r.valid)
            return;

        std::shared_ptr<AXIReadTransaction<BEATSIZE_MEMORY>> picked = nullptr;

        auto pick_r = [&]() -> bool {

            if (activeReads.empty())
                return false;

            if (cfg->memoryOOOR)
            {
                auto randPicked = activeReads[rand64() % activeReads.size()];

                if (randPicked->dataPending == randPicked->data.size())
                    return false;

                picked = randPicked;
            }
            else
            {
                for (auto seqPicked : activeReads)
                {
                    if (seqPicked->dataPending == seqPicked->data.size())
                        continue;

                    picked = seqPicked;

                    break;
                }
            }

            if (picked == nullptr)
                return false;

            pendingReadResponses.push_back(picked);

            return true;
        };

        if (pick_r())
        {
            auto& bundle = picked->data[picked->dataPending++].bundle;

            port->r.valid = true;
            port->r.id    = bundle.id;
            port->r.data  = bundle.data;
            port->r.resp  = bundle.resp;
            port->r.last  = bundle.last;
        }
        else
            port->r.valid = false;
    }

    void MemoryAgent::fire_aw()
    {
        auto& chnAw = port->aw;
        if (chnAw.fire())
        {
            if (glbl.cfg.verbose_memory_axi_write)
            {
                Log(this, Hex().ShowBase()
                    .Append("[memory.axi] [fire AW] ")
                    .Append("id: ",         uint64_t(chnAw.id))
                    .Append(", addr: ",     uint64_t(chnAw.addr))
                    .Append(", size: ",     uint64_t(chnAw.size))
                    .Append(", burst: ",    uint64_t(chnAw.burst))
                    .Append(", len: ",      uint64_t(chnAw.len))
                    .EndLine());
            }

            if ((1 << chnAw.size) > BEATSIZE_MEMORY)
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                    .Append("AXI AW size overflow")
                    .ToString());

            switch (chnAw.burst)
            {
                case BURST_FIXED:
                case BURST_INCR:
                case BURST_WRAP:
                    break;

                default:
                    tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("unknown AXI AW burst type ", uint64_t(chnAw.burst))
                        .Append(" at addr=",    uint64_t(chnAw.addr))
                        .Append(" id=",         uint64_t(chnAw.id))
                        .ToString());
            }

            activeWrites.push_back(std::make_shared<AXIWriteTransaction<BEATSIZE_MEMORY>>(
                FiredBundleAW { .time = cycle(), .bundle = chnAw }));
        }
    }

    void MemoryAgent::fire_w()
    {
        auto& chnW = port->w;
        if (chnW.fire())
        {
            if (activeWrites.empty())
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                    .Append("[memory.axi] unexpected W fire before AW")
                    .ToString());

            auto trans = activeWrites.front();

            if (glbl.cfg.verbose_memory_axi_write)
            {
                Log(this, Hex().ShowBase()
                    .Append("[memory.axi] [fire W] ")
                    .Append("(id: ",        uint64_t(trans->request.bundle.id))
                    .Append(", addr: ",     uint64_t(trans->request.bundle.addr), ")")
                    .Append(", last: ",     uint64_t(chnW.last))
                    .Append(", data: ")); 
                LogEx(details::data_dump_embedded(
                    chnW.data->data, 1 << trans->request.bundle.size, chnW.strb));
                LogEx(std::cout << std::endl);
            }

            BundleChannelW<BEATSIZE_MEMORY> writeBundle = chnW;
            trans->data.push_back(FiredBundleW<BEATSIZE_MEMORY> { .time = cycle(), .bundle = writeBundle });

            chnW.data = make_shared_tldata<BEATSIZE_MEMORY>();

            if (chnW.last)
            {
                trans->dataDone = true;

                //
                uint64_t address = trans->request.bundle.addr;
                uint8_t  burst   = trans->request.bundle.burst;

                size_t   numBytes = 1ULL << trans->request.bundle.size;
                size_t   burstLen = trans->request.bundle.len + 1;

                switch (burst)
                {
                    case BURST_FIXED:
                        break;

                    case BURST_INCR:
                    case BURST_WRAP:
                        address = address & ~(numBytes - 1);
                        break;

                    default:
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unknown AXI AW burst type ", uint64_t(burst))
                            .Append(" at addr=",    uint64_t(trans->request.bundle.addr))
                            .Append(" id=",         uint64_t(trans->request.bundle.id))
                            .ToString());
                }

                // confirm memory write
                for (auto& data : trans->data)
                {
                    size_t paddr = (address & ~size_t(BEATSIZE_MEMORY - 1)) - cfg->memoryStart;
                    
                    std::bitset<64> strb(data.bundle.strb);
                    for (int i = 0; i < BEATSIZE_MEMORY; i++)
                    {
                        if (strb[i])
                        {
                            size_t bytePAddr = paddr + i;

                            if (bytePAddr >= size())
                                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                    .Append("memory write out of bound at ", uint64_t(bytePAddr))
                                    .Append(" backing (addr=",  uint64_t(trans->request.bundle.addr))
                                    .Append(", size=",          uint64_t(trans->request.bundle.size))
                                    .Append(", burst=",         uint64_t(trans->request.bundle.burst))
                                    .Append(", len=",           uint64_t(trans->request.bundle.len))
                                    .Append(") at ",            uint64_t(trans->request.time))
                                    .Append(" with (")
                                    .Append(", strb=",          uint64_t(data.bundle.strb))
                                    .Append(", last=",          uint64_t(data.bundle.last))
                                    .Append(") at ",            uint64_t(data.time))
                                    .ToString());
                            
                            pmem[bytePAddr] = data.bundle.data->data[i];
                        }
                    }

                    switch (burst)
                    {
                        case BURST_FIXED: 
                            break;

                        case BURST_INCR:
                            address = address + numBytes;
                            break;

                        case BURST_WRAP:
                            address = address + numBytes;
                            if (address == ((address & ~(numBytes * burstLen - 1))
                                                       + numBytes * burstLen))
                                address = address & ~(numBytes * burstLen - 1);
                            break;

                        default:
                            break;
                    }
                }

                //
                trans->response.bundle.id   = trans->request.bundle.id;
                trans->response.bundle.resp = 0;

                finishedWrites.push_back(trans);
                activeWrites.pop_front();
            }
        }
    }

    void MemoryAgent::fire_b()
    {
        auto& chnB = port->b;
        if (chnB.fire())
        {
            if (finishedWrites.empty())
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                    .Append("[memory.axi] unexpected B fire before write finish")
                    .ToString());
            
            auto finishedWrite = finishedWrites.front();
            finishedWrites.pop_front();

            finishedWrite->response.time = cycle();

            // call event here for record purpose

            chnB.valid = false;
        }
    }

    void MemoryAgent::fire_ar()
    {
        auto& chnAr = port->ar;
        if (chnAr.fire())
        {
            if (is_r_active(chnAr.id))
            {
                if (glbl.cfg.verbose_memory_axi_read)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[memory.axi] [fire AR] pending read with same active RID")
                        .Append(", id: ",       uint64_t(chnAr.id))
                        .Append(", addr: ",     uint64_t(chnAr.addr))
                        .Append(", burst: ",    uint64_t(chnAr.burst))
                        .Append(", size: ",     uint64_t(chnAr.size))
                        .Append(", len: ",      uint64_t(chnAr.len))
                        .EndLine());
                }

                pendingReadRequests.push_back(FiredBundleAR {
                    .time = cycle(), .bundle = chnAr });
            }
            else
            {
                if (glbl.cfg.verbose_memory_axi_read)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[memory.axi] [fire AR] ")
                        .Append("id: ",         uint64_t(chnAr.id))
                        .Append(", addr: ",     uint64_t(chnAr.addr))
                        .Append(", burst: ",    uint64_t(chnAr.burst))
                        .Append(", size: ",     uint64_t(chnAr.size))
                        .Append(", len: ",      uint64_t(chnAr.len))
                        .EndLine());
                }
                
                auto trans = std::make_shared<AXIReadTransaction<BEATSIZE_MEMORY>>(
                    FiredBundleAR { .time = cycle(), .bundle = chnAr });

                activeReads.push_back(trans);

                // confirm read
                uint64_t address = chnAr.addr;
                uint8_t  burst   = chnAr.burst;

                size_t   numBytes = 1ULL << chnAr.size;
                size_t   burstLen = chnAr.len + 1;

                if ((1 << chnAr.size) > BEATSIZE_MEMORY)
                    tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("AXI AR size overflow")
                        .ToString());

                switch (burst)
                {
                    case BURST_FIXED:
                        break;

                    case BURST_INCR:
                    case BURST_WRAP:
                        address = address & ~(numBytes - 1);
                        break;

                    default:
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unknown AXI AR burst type ", uint64_t(burst))
                            .Append(" at addr=",    uint64_t(chnAr.addr))
                            .Append(" id=",         uint64_t(chnAr.id))
                            .ToString());
                }

                int offset = 0;
                for (int i = 0; i < burstLen; i++)
                {
                    auto& dataR = trans->data.emplace_back();

                    dataR.bundle.id     = chnAr.id;
                    dataR.bundle.resp   = 0;
                    dataR.bundle.last   = (i + 1) == burstLen;
                    dataR.bundle.data   = make_shared_tldata_zero<BEATSIZE_MEMORY>();

                    size_t paddr = (address & ~size_t(BEATSIZE_MEMORY - 1)) - cfg->memoryStart;

                    for (int j = 0; j < numBytes; j++, offset++)
                    {
                        if (offset == numBytes)
                            offset = 0;

                        size_t bytePAddr = paddr + j;

                        if (bytePAddr >= size())
                            tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                .Append("memory read out of bound at ", uint64_t(bytePAddr))
                                .Append(" backing (addr=",  uint64_t(trans->request.bundle.addr))
                                .Append(", size=",          uint64_t(trans->request.bundle.size))
                                .Append(", burst=",         uint64_t(trans->request.bundle.burst))
                                .Append(", len=",           uint64_t(trans->request.bundle.len))
                                .Append(") at ",            uint64_t(trans->request.time))
                                .Append(" with (")
                                .Append(", last=",          uint64_t(dataR.bundle.last))
                                .Append(")")
                                .ToString());
                        
                        dataR.bundle.data->data[offset] = pmem[bytePAddr];
                    }

                    switch (burst)
                    {
                        case BURST_FIXED: 
                            break;

                        case BURST_INCR:
                            address = address + numBytes;
                            break;

                        case BURST_WRAP:
                            address = address + numBytes;
                            if (address == ((address & ~(numBytes * burstLen - 1))
                                                       + numBytes * burstLen))
                                address = address & ~(numBytes * burstLen - 1);
                            break;

                        default:
                            break;
                    }
                }
            }
        }
    }

    void MemoryAgent::fire_r()
    {
        auto& chnR = port->r;
        if (chnR.fire())
        {
            if (pendingReadResponses.empty())
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                    .Append("[memory.axi] unexpected R fire with no pending read responses")
                    .ToString());

            // call event here for record purpose

            auto trans = pendingReadResponses.front();
            pendingReadResponses.pop_front();

            if (trans->dataSending >= trans->data.size())
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                    .Append("over-pending read responses from no longer active request: ")
                    .Append(trans->dataSending, " of ", trans->data.size())
                    .ToString());

            trans->data[trans->dataSending++].time = cycle();

            if (glbl.cfg.verbose_memory_axi_read)
            {
                Log(this, Hex().ShowBase()
                    .Append("[memory.axi] [fire R] ")
                    .Append("id: ",         uint64_t(chnR.id))
                    .Append(", (addr: ",    uint64_t(trans->request.bundle.addr), ")")
                    .Append(", resp: ",     uint64_t(chnR.resp))
                    .Append(", last: ",     uint64_t(chnR.last))
                    .Append(", data: "));
                LogEx(details::data_dump_embedded(chnR.data->data, BEATSIZE_MEMORY, 0xFFFFFFFF));
                LogEx(std::cout << std::endl);
            }
            
            if (chnR.last)
            {
                for (auto iter = activeReads.begin(); iter != activeReads.end(); iter++)
                {
                    if (iter->get() == trans.get())
                    {
                        activeReads.erase(iter);
                        break;
                    }
                }
            }

            chnR.valid = false;
        }
    }

    void MemoryAgent::handle_channel()
    {
        fire_aw();
        fire_w();
        fire_b();
        fire_ar();
        fire_r();
    }

    void MemoryAgent::update_signal()
    {
        handle_aw();
        handle_w();
        send_b();
        handle_ar();
        send_r();
    }
}
