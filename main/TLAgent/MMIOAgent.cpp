#include "MMIOAgent.h"

#include <bitset>
#include <cmath>
#include <cstring>
#include <memory>

#include "BaseAgent.h"
#include "../Base/TLEnum.hpp"

//
namespace tl_agent::details {

    void data_dump(const uint8_t* data, uint8_t size, uint8_t mask)
    {
        std::bitset<8> maskBits(mask);

        Gravity::StringAppender sa;
        sa.Append("[ ").Hex().Fill('0');

        for (int i = 0; i < size; i++)
            if (maskBits[i])
                sa.NextWidth(2).Append(uint64_t(data[i]), " ");
            else
                sa.Append("?? ");
        
        sa.Append("]");

        std::cout << sa.ToString();
    }

    void data_dump_embedded(const uint8_t* data, uint8_t size, uint8_t mask)
    {
        if (glbl.cfg.verbose_data_full)
            std::cout << std::endl;

        data_dump(data, size, mask);
    }

    void data_dump_on_verify(const uint8_t* dut, const uint8_t* ref, uint8_t size, uint8_t mask)
    {
        std::cout << std::endl;

        std::cout << "dut: ";
        data_dump(dut, size, mask);
        std::cout << std::endl;

        std::cout << "ref: ";
        data_dump(ref, size, mask);
        std::cout << std::endl;
    }
}


// Implementation of: class InflightMMIO
namespace tl_agent {
    /*
    bool                                dataCheck;
    paddr_t                             address;
    TLOpcodeA                           opcode;
    uint8_t                             size;
    uint8_t                             mask;
    uint64_t                            timeStamp;
    int                                 status;
    shared_tldata_t<DATASIZE_MMIO>      data;
    */

    InflightMMIO::InflightMMIO(const TLLocalContext*    ctx,
                               bool                     dataCheck,
                               TLOpcodeA                opcode,
                               int                      status,
                               paddr_t                  address,
                               uint8_t                  size,
                               uint8_t                  mask) noexcept
        : dataCheck     (dataCheck)
        , address       (address)
        , opcode        (opcode)
        , size          (size)
        , mask          (mask)
        , timeStamp     (ctx->cycle())
        , status        (status)
        , data          ()
    { }

    void InflightMMIO::updateStatus(const TLLocalContext* ctx, int status) noexcept
    {
        this->timeStamp = ctx->cycle();
        this->status = status;
    }
}


// Implementation of: class MMIOGlobalStatus
namespace tl_agent {
    /*
    std::unordered_set<paddr_t> inflightAddress;
    uint8_t*                    data;
    size_t                      dataSize;
    TLLocalConfig*              tlcfg;
    */

    MMIOGlobalStatus::MMIOGlobalStatus(const TLLocalConfig* tlcfg) noexcept
    {
        this->tlcfg = tlcfg;
        this->dataSize = tlcfg->mmioEnd - tlcfg->mmioStart + 1;
        this->data = new uint8_t[dataSize];

        std::memset(this->data, 0, dataSize);
    }

    MMIOGlobalStatus::~MMIOGlobalStatus() noexcept
    {
        delete[] this->data;
    }

    void MMIOGlobalStatus::setInflight(const TLLocalContext* ctx, paddr_t address)
    {
        tlc_assert(inflightAddress.insert(address).second, ctx,
            Gravity::StringAppender().Hex().ShowBase()
                .Append("MMIO already in-flight at ", address)
            .ToString());
    }

    void MMIOGlobalStatus::freeInflight(const TLLocalContext* ctx, paddr_t address)
    {
        tlc_assert(inflightAddress.erase(address), ctx,
            Gravity::StringAppender().Hex().ShowBase()
                .Append("MMIO not in-flight at ", address)
            .ToString());
    }

    bool MMIOGlobalStatus::isInflight(const TLLocalContext* ctx, paddr_t address) const noexcept
    {
        return inflightAddress.count(address);
    }

    void MMIOGlobalStatus::updateData(const TLLocalContext* ctx, paddr_t address, uint8_t size, uint8_t mask, uint8_t* data)
    {
        address &= ~paddr_t(0x07);

        std::bitset<8> maskBits(mask);

        tlc_assert(maskBits.count() <= size, ctx, "update data size mask mismatch");

        for (int i = 0; i < DATASIZE_MMIO; i++)
        {
            if (maskBits[i])
                this->data[address - tlcfg->mmioStart + i] = data[i];
        }
    }

    void MMIOGlobalStatus::verifyData(const TLLocalContext* ctx, paddr_t address, uint8_t size, uint8_t mask, uint8_t* data) const
    {
        std::bitset<8> maskBits(mask);

        tlc_assert(maskBits.count() <= size, ctx, "verify data size mask mismatch");

        for (int i = 0; i < DATASIZE_MMIO; i++)
        {
            if (maskBits[i] && this->data[address - tlcfg->mmioStart + i] != data[i])
            {
                details::data_dump_on_verify(
                    data,
                    this->data + (address - tlcfg->mmioStart),
                    size,
                    mask);

                tlc_assert(false, ctx,
                    Gravity::StringAppender().Hex().ShowBase()
                        .Append("Data mismatch from MMIO operation at ", address)
                    .ToString());
            }
        }
    }
}


// Implementation of: class MMIOLocalStatus
namespace tl_agent {
    /*
    std::unordered_map<uint64_t, std::shared_ptr<InflightMMIO>> inflight;
    */

    void MMIOLocalStatus::update(const TLLocalContext* ctx, uint64_t source, std::shared_ptr<InflightMMIO> mmio) noexcept
    {
        inflight[source] = mmio;
    }

    std::shared_ptr<InflightMMIO> MMIOLocalStatus::query(const TLLocalContext* ctx, uint64_t source) const
    {
        auto iter = inflight.find(source);

        tlc_assert(iter != inflight.end(), ctx,
            Gravity::StringAppender().Hex().ShowBase()
                .Append("query: No in-flight MMIO on source: ", uint64_t(source))
            .ToString());

        return iter->second;
    }

    bool MMIOLocalStatus::contains(const TLLocalContext* ctx, uint64_t source) const noexcept
    {
        return inflight.find(source) != inflight.end();
    }

    void MMIOLocalStatus::erase(const TLLocalContext* ctx, uint64_t source)
    {
        tlc_assert(inflight.erase(source) == 1, ctx, 
            Gravity::StringAppender().Hex().ShowBase()
                .Append("erase: No in-flight MMIO on source: ", uint64_t(source))
            .ToString());
    }

    void MMIOLocalStatus::clear() noexcept
    {
        inflight.clear();
    }
}


// Implementation of: class MMIOAgent
namespace tl_agent {

    MMIOAgent::MMIOAgent(TLLocalConfig* cfg, MMIOGlobalStatus* globalStatus, int sysId, unsigned int seed, uint64_t* cycles) noexcept
        : BaseAgent (cfg, sysId, sysId, seed, NR_SOURCEID_MMIO)
        , pendingA  ()
        , pendingD  ()
    {
        this->globalBoard = nullptr;
        this->uncachedBoards = nullptr;

        this->cycles = cycles;
        this->globalStatus = globalStatus;
        this->localStatus = new MMIOLocalStatus;
    }

    MMIOAgent::~MMIOAgent() noexcept
    {
        delete this->localStatus;
    }

    uint64_t MMIOAgent::cycle() const noexcept
    {
        return *this->cycles;
    }

    Resp MMIOAgent::send_a(std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE_MMIO>>& a)
    {
        bool hitInflight = globalStatus->isInflight(this, a->address);

        if (!hitInflight)
            globalStatus->setInflight(this, a->address);

        switch (TLOpcodeA(a->opcode))
        {
            case TLOpcodeA::Get:
            {
                auto entry = std::make_shared<InflightMMIO>(
                    this, !hitInflight, TLOpcodeA::Get, S_SENDING_A, a->address, a->size, a->mask);

                localStatus->update(this, a->source, entry);

                break;
            }

            case TLOpcodeA::PutFullData:
            {
                if (hitInflight)
                    return FAIL;

                auto entry = std::make_shared<InflightMMIO>(
                    this, true, TLOpcodeA::PutFullData, S_SENDING_A, a->address, a->size, a->mask);
                entry->data = a->data;

                localStatus->update(this, a->source, entry);

                std::memcpy(this->port->a.data->data, (uint8_t*)(a->data->data), DATASIZE_MMIO);
                break;
            }

            case TLOpcodeA::PutPartialData:
            {
                if (hitInflight)
                    return FAIL;

                auto entry = std::make_shared<InflightMMIO>(
                    this, true, TLOpcodeA::PutPartialData, S_SENDING_A, a->address, a->size, a->mask);
                entry->data = a->data;

                localStatus->update(this, a->source, entry);

                std::memcpy(this->port->a.data->data, (uint8_t*)(a->data->data), DATASIZE_MMIO);
                break;
            }

            default:
                tlc_assert(false, this, 
                    Gravity::StringAppender().Hex().ShowBase()
                        .Append("Unknown opcode for channel A: ", uint64_t(a->opcode))
                    .ToString());
        }
        this->port->a.opcode    = a->opcode;
        this->port->a.address   = a->address;
        this->port->a.size      = a->size;
        this->port->a.mask      = a->mask;
        this->port->a.source    = a->source;
        this->port->a.valid     = true;
        
        return OK;
    }

    void MMIOAgent::handle_b(std::shared_ptr<BundleChannelB>& b)
    { }

    Resp MMIOAgent::send_c(std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE_MMIO>>& c)
    {
        return FAIL;
    }

    void MMIOAgent::fire_a()
    {
        if (this->port->a.fire())
        {
            auto& chnA = this->port->a;

            if (TLEnumEquals(chnA.opcode, TLOpcodeA::Get))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[MMIO] [fire A] [Get] ")
                        .Append("source: ", uint64_t(chnA.source))
                        .Append(", addr: ", uint64_t(chnA.address))
                        .Append(", size: ", uint64_t(chnA.size))
                        .EndLine());
                }
            }
            else if (TLEnumEquals(chnA.opcode, TLOpcodeA::PutFullData))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[MMIO] [fire A] [PutFullData] ")
                        .Append("source: ", uint64_t(chnA.source))
                        .Append(", addr: ", uint64_t(chnA.address))
                        .Append(", size: ", uint64_t(chnA.size))
                        .Append(", mask: ").NextWidth(2).Append(uint64_t(chnA.mask))
                        .Append(", data: "));
                    LogEx(details::data_dump_embedded(chnA.data->data, DATASIZE_MMIO, chnA.mask));
                    LogEx(std::cout << std::endl);
                }
            }
            else if (TLEnumEquals(chnA.opcode, TLOpcodeA::PutPartialData))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[MMIO] [fire A] [PutPartialData] ")
                        .Append("source: ", uint64_t(chnA.source))
                        .Append(", addr: ", uint64_t(chnA.address))
                        .Append(", size: ", uint64_t(chnA.size))
                        .Append(", mask: ").NextWidth(2).Append(uint64_t(chnA.mask))
                        .Append(", data: "));
                    LogEx(details::data_dump_embedded(chnA.data->data, DATASIZE_MMIO, chnA.mask));
                    LogEx(std::cout << std::endl);
                }
            }
            else
            {
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("[MMIO] [fire A] unknown opcode: ", uint64_t(chnA.opcode))
                    .EndLine().ToString());
            }

            bool hasData  = TLEnumEquals(chnA.opcode, TLOpcodeA::PutFullData, TLOpcodeA::PutPartialData);
            bool recvData = TLEnumEquals(chnA.opcode, TLOpcodeA::Get);
            chnA.valid = false;
            tlc_assert(pendingA.is_pending(), this, "[MMIO] No pending A but A fired!");
            pendingA.update(this);

            if (!pendingA.is_pending())
            {
                if (glbl.cfg.verbose_xact_data_complete)
                {
                    if (TLEnumEquals(chnA.opcode, TLOpcodeA::PutFullData))
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[MMIO] [data complete A] [PutFullData] ")
                            .Append("addr: ", uint64_t(chnA.address))
                            .Append(", data: "));
                        LogEx(details::data_dump_embedded(pendingA.info->data->data, DATASIZE_MMIO, chnA.mask));
                        LogEx(std::cout << std::endl);
                    }
                    else if (TLEnumEquals(chnA.opcode, TLOpcodeA::PutPartialData))
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[MMIO] [data complete A] [PutPartialData] ")
                            .Append("addr: ", uint64_t(chnA.address))
                            .Append(", size: ", uint64_t(chnA.size))
                            .Append(", mask: ").NextWidth(2).Append(uint64_t(chnA.mask))
                            .Append(", data: "));
                        LogEx(details::data_dump_embedded(pendingA.info->data->data, DATASIZE_MMIO, chnA.mask));
                        LogEx(std::cout << std::endl);
                    }
                }

                auto status = this->localStatus->query(this, pendingA.info->source);
                status->updateStatus(this, S_A_WAITING_D);
            
                if (hasData)
                {
                    // nothing to be done here for now
                }
                else if (recvData)
                {
                    // nothing to be done here for now
                }
            }
        }
    }

    void MMIOAgent::fire_b()
    { }

    void MMIOAgent::fire_c()
    { }

    void MMIOAgent::fire_d()
    {
        if (this->port->d.fire())
        {
            auto& chnD = this->port->d;
            auto status = localStatus->query(this, chnD.source);

            if (TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAck))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[MMIO] [fire D] [AccessAck] ")
                        .Append("source: ", uint64_t(chnD.source))
                        .Append(", addr: ", status->address)
                        .EndLine());
                }
            }
            else if (TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAckData))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[MMIO] [fire D] [AccessAckData] ")
                        .Append("source: ", uint64_t(chnD.source))
                        .Append(", addr: ", status->address, ", data: "));
                    LogEx(details::data_dump_embedded(chnD.data->data, DATASIZE_MMIO, status->mask));
                    LogEx(std::cout << std::endl);
                }
            }
            else
            {
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("[MMIO] [fire D] unknown opcode: ", uint64_t(chnD.opcode))
                    .EndLine().ToString());
            }

            bool hasData = TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAckData);
            tlc_assert(status->status == S_A_WAITING_D, this, "[MMIO] Status error!");

            // Store and verify data
            if (TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAckData))
            {
                Log(this, Hex().ShowBase()
                    .Append("[MMIO] [data complete D] [AccessAckData] ")
                    .Append("addr: ", status->address, ", data: "));
                LogEx(details::data_dump_embedded(chnD.data->data, DATASIZE_MMIO, status->mask));
                LogEx(std::cout << std::endl);
            }

            if (hasData && status->dataCheck)
                this->globalStatus->verifyData(this, status->address, 1 << int(status->size), status->mask, chnD.data->data);
            else if (TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAck))
            {
                // sync data into global status
                this->globalStatus->updateData(this, status->address, 1 << int(status->size), status->mask, status->data->data);
            }

            if (status->dataCheck)
                this->globalStatus->freeInflight(this, status->address);

            this->localStatus->erase(this, chnD.source);
            this->idpool.freeid(chnD.source);
        }
    }

    void MMIOAgent::fire_e()
    { }

    void MMIOAgent::handle_channel()
    {
        fire_a();
        fire_d();
    }

    void MMIOAgent::update_signal()
    {
        this->port->d.ready = true; // TODO: maybe do random here
        
        if (pendingA.is_pending())
            send_a(pendingA.info);
        else
            this->port->a.valid = false;

        if (*this->cycles % TIMEOUT_INTERVAL == 0)
            this->timeout_check();
        
        this->idpool.update(this);
    }

    bool MMIOAgent::do_get(paddr_t address, uint8_t size, uint8_t mask)
    {
        if (pendingA.is_pending() || idpool.full())
            return false;

        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE_MMIO>>();
        req_a->opcode   = uint8_t(TLOpcodeA::Get);
        req_a->address  = address;
        req_a->size     = size;
        req_a->mask     = mask;
        req_a->source   = this->idpool.getid();

        pendingA.init(req_a, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced A] [MMIO] [Get] ")
                .Append("addr: ", uint64_t(req_a->address))
                .Append(", size: ", uint64_t(req_a->size))
                .Append(", mask: ").NextWidth(2).Append(uint64_t(req_a->mask))
                .EndLine());
        }

        return true;
    }

    bool MMIOAgent::do_putfulldata(paddr_t address, shared_tldata_t<DATASIZE_MMIO> data)
    {
        if (pendingA.is_pending() || idpool.full())
            return false;

        if (this->globalStatus->isInflight(this, address))
            return false;

        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE_MMIO>>();
        req_a->opcode   = uint8_t(TLOpcodeA::PutFullData);
        req_a->address  = address;
        req_a->size     = ceil(log2((double)DATASIZE_MMIO));
        req_a->mask     = 0xff;
        req_a->source   = this->idpool.getid();
        req_a->data     = data;

        pendingA.init(req_a, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced A] [MMIO] [PutFullData] ")
                .Append("addr: ", uint64_t(req_a->address))
                .Append(", data: "));
            LogEx(details::data_dump_embedded(req_a->data->data, DATASIZE_MMIO, 0xff));
            LogEx(std::cout << std::endl);
        }

        return true;
    }

    bool MMIOAgent::do_putpartialdata(paddr_t address, uint8_t size, uint8_t mask, shared_tldata_t<DATASIZE_MMIO> data)
    {
        if (pendingA.is_pending() || idpool.full())
            return false;

        if (this->globalStatus->isInflight(this, address))
            return false;

        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE_MMIO>>();
        req_a->opcode   = uint8_t(TLOpcodeA::PutPartialData);
        req_a->address  = address;
        req_a->size     = size;
        req_a->mask     = mask;
        req_a->source   = this->idpool.getid();
        req_a->data     = data;

        pendingA.init(req_a, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced A] [PutPartialData] ")
                .Append("addr: ", uint64_t(req_a->address))
                .Append(", size: ", uint64_t(req_a->size))
                .Append(", mask: ").NextWidth(2).Append(uint64_t(req_a->mask))
                .Append(", data: "));
            LogEx(details::data_dump_embedded(req_a->data->data, DATASIZE_MMIO, req_a->mask));
            LogEx(std::cout << std::endl);
        }

        return true;
    }

    void MMIOAgent::timeout_check()
    {
        // don't do timeout check currently
    }

    bool MMIOAgent::mainSys() const noexcept
    {
        return false;
    }
}
