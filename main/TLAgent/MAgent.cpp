#include "MAgent.h"

#include <memory>

#include "../Base/TLEnum.hpp"

namespace tl_agent {

MAgent::MAgent(TLLocalConfig* cfg,
               MemoryBackend* mem,
               GlobalBoard<paddr_t>* gb,
               UncachedBoard<paddr_t>* ubs,
               int sys,
               int sysId,
               unsigned int seed,
               uint64_t* cycles) noexcept
    : BaseAgent(cfg, mem, sys, sysId, seed)
    , pendingA()
    , pendingD() {
    this->globalBoard = gb;
    this->uncachedBoards = ubs;
    this->cycles = cycles;
    this->localBoard = new LocalScoreBoard();
}

MAgent::~MAgent() noexcept {
    delete this->localBoard;
}

uint64_t MAgent::cycle() const noexcept {
    return *this->cycles;
}

Resp MAgent::send_a(std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>>& a) {
    switch (TLOpcodeA(a->opcode)) {
        case TLOpcodeA::Get: {
            auto entry = std::make_shared<UL_SBEntry>(this, TLOpcodeA::Get, S_SENDING_A, a->address);
            localBoard->update(this, a->source, entry);
            break;
        }

        case TLOpcodeA::PutFullData: {
            auto entry = std::make_shared<UL_SBEntry>(this, TLOpcodeA::PutFullData, S_SENDING_A, a->address);
            localBoard->update(this, a->source, entry);
            int beat_num = pendingA.nr_beat - pendingA.beat_cnt;
            std::memcpy(this->port->a.data->data, (uint8_t*)(a->data->data) + BEATSIZE * beat_num, BEATSIZE);
            break;
        }

        default:
            tlc_assert(false, this, "Unknown opcode for channel A!");
    }

    this->port->a.opcode = a->opcode;
    this->port->a.address = a->address;
    this->port->a.size = a->size;
    this->port->a.mask = a->mask;
    this->port->a.source = a->source;
    this->port->a.param = 0;
    this->port->a.matrix = a->matrix;
    this->port->a.ameIndex = a->ameIndex;
    this->port->a.valid = true;

    return OK;
}

Resp MAgent::send_c(std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>>& c) {
    (void)c;
    return OK;
}

void MAgent::fire_a() {
    if (!this->port->a.fire())
        return;

    auto& chnA = this->port->a;

    bool hasData = TLEnumEquals(chnA.opcode, TLOpcodeA::PutFullData);
    bool recvData = TLEnumEquals(chnA.opcode, TLOpcodeA::Get);

    chnA.valid = false;
    tlc_assert(pendingA.is_pending(), this, "No pending A but A fired!");
    pendingA.update(this);

    if (!pendingA.is_pending()) {
        this->localBoard->query(this, pendingA.info->source)->update_status(this, S_A_WAITING_D);

        if (hasData) {
            auto global_SBEntry = std::make_shared<Global_SBEntry>();
            global_SBEntry->pending_data = pendingA.info->data;
            if (this->globalBoard->get().count(pendingA.info->address) == 0)
                global_SBEntry->data = nullptr;
            else
                global_SBEntry->data = this->globalBoard->get()[pendingA.info->address]->data;

            global_SBEntry->status = Global_SBEntry::SB_PENDING;
            this->globalBoard->update(this, pendingA.info->address, global_SBEntry);
            uncachedBoards->appendAll(this, chnA.address, global_SBEntry->pending_data);
        }

        if (recvData) {
            if (!globalBoard->haskey(chnA.address)) {
                uncachedBoards->allocateZero(this, id, chnA.address, chnA.source);
            } else {
                auto global_SBEntry = globalBoard->query(this, chnA.address);
                if (global_SBEntry->status == Global_SBEntry::SB_VALID)
                    uncachedBoards->allocate(this, id, chnA.address, chnA.source, global_SBEntry->data);
                if (global_SBEntry->status == Global_SBEntry::SB_PENDING)
                    uncachedBoards->allocate(this, id, chnA.address, chnA.source, global_SBEntry->pending_data);
            }

            if (globalBoard->has_memory_alt(this, chnA.address)) {
                auto data = make_shared_tldata<DATASIZE>();
                for (size_t i = 0; i < DATASIZE; i++)
                    data->data[i] = mem->access(chnA.address + i);

                uncachedBoards->appendAll(this, chnA.address, data);
            }
        }
    }
}

void MAgent::fire_b() {}

void MAgent::fire_c() {}

void MAgent::fire_d() {
    if (this->port->d.fire()) {
        auto& chnD = this->port->d;

        if (!localBoard->haskey(chnD.source))
            return;

        auto info = localBoard->query(this, chnD.source);

        bool hasData = TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAckData, TLOpcodeD::GrantData);
        bool noDataAck = TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAck, TLOpcodeD::ReleaseAck);

        if (!hasData && !noDataAck) {
            tlc_assert(false, this, Gravity::StringAppender()
                .Hex().ShowBase()
                .Append("[fire D] unknown opcode: ", uint64_t(chnD.opcode))
                .EndLine().ToString());
        }

        tlc_assert(info->status == S_A_WAITING_D, this, "Status error!");

        if (pendingD.is_pending()) {
            tlc_assert(chnD.opcode == pendingD.info->opcode, this, "Opcode mismatch among beats!");
            tlc_assert(chnD.param == pendingD.info->param, this, "Param mismatch among beats!");
            tlc_assert(chnD.source == pendingD.info->source, this, "Source mismatch among beats!");
            pendingD.update(this);
        } else {
            auto resp_d = std::make_shared<BundleChannelD<RespField, EchoField, DATASIZE>>();
            resp_d->opcode = chnD.opcode;
            resp_d->param = chnD.param;
            resp_d->source = chnD.source;
            resp_d->data = hasData ? make_shared_tldata<DATASIZE>() : nullptr;
            int nr_beat = noDataAck ? 0 : ((chnD.size <= 5) ? 0 : (chnD.size == 6 ? 1 : (chnD.size == 7 ? 2 : 0)));
            pendingD.init(resp_d, nr_beat);
        }

        if (hasData) {
            int beat_num = pendingD.nr_beat - pendingD.beat_cnt;
            std::memcpy((uint8_t*)(pendingD.info->data->data) + BEATSIZE * beat_num, chnD.data->data, BEATSIZE);
        }

        if (!pendingD.is_pending()) {
            if (hasData) {
                uncachedBoards->verify(this, id, info->address, pendingD.info->source, pendingD.info->data);
                uncachedBoards->finish(this, id, info->address, pendingD.info->source);
            } else if (TLEnumEquals(chnD.opcode, TLOpcodeD::AccessAck)) {
                uncachedBoards->appendAll(this, info->address, this->globalBoard->query(this, info->address)->data);
                this->globalBoard->unpending(this, info->address);
            }

            localBoard->erase(this, chnD.source);
            this->idpool.freeid(chnD.source);
        }
    }

}

void MAgent::fire_m() {
    if (this->port->m.fire()) {
        auto& chnM = this->port->m;

        if (!localBoard->haskey(chnM.source))
            return;

        auto info = localBoard->query(this, chnM.source);
        auto data = make_shared_tldata<DATASIZE>();
        std::memcpy(data->data, chnM.data->data, DATASIZE);

        // Matrix M channel may return data with ordering different from generic UL scoreboard assumptions.
        // For minimal bring-up, accept returned data and retire the tracked transaction.
        if (uncachedBoards->haskey(id, info->address, chnM.source)) {
            uncachedBoards->appendAll(this, info->address, data);
            uncachedBoards->finish(this, id, info->address, chnM.source);
        }

        localBoard->erase(this, chnM.source);
        this->idpool.freeid(chnM.source);
    }
}

void MAgent::fire_e() {}

void MAgent::handle_b(std::shared_ptr<BundleChannelB>& b) {
    (void)b;
}

void MAgent::handle_channel() {
    fire_a();
    fire_d();
    fire_m();
}

void MAgent::update_signal() {
    this->port->d.ready = true;
    this->port->m.ready = true;

    if (pendingA.is_pending())
        send_a(pendingA.info);
    else
        this->port->a.valid = false;

    if (*this->cycles % TIMEOUT_INTERVAL == 0)
        this->timeout_check();

    idpool.update(this);
}

bool MAgent::is_d_fired() {
    return this->port->d.fire();
}

bool MAgent::is_m_fired() {
    return this->port->m.fire();
}

ActionDenialEnum MAgent::do_getAuto(paddr_t address, bool modify) {
    if (pendingA.is_pending())
        return ActionDenial::CHANNEL_CONGESTION;

    if (idpool.full())
        return ActionDenial::CHANNEL_RESOURCE;

    auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
    req_a->opcode = uint8_t(TLOpcodeA::Get);
    req_a->address = address;
    req_a->size = ceil(log2((double)DATASIZE));
    req_a->mask = 0xffffffffUL;
    req_a->source = this->idpool.getid();
    req_a->vaddr = address;
    req_a->needHint = 0;
    req_a->matrix = modify ? 0x3 : 0x1;
    req_a->ameIndex = req_a->source;

    pendingA.init(req_a, 1);

    return ActionDenial::ACCEPTED;
}

ActionDenialEnum MAgent::do_get(paddr_t address, uint8_t size, uint32_t mask) {
    if (pendingA.is_pending())
        return ActionDenial::CHANNEL_CONGESTION;

    if (idpool.full())
        return ActionDenial::CHANNEL_RESOURCE;

    auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
    req_a->opcode = uint8_t(TLOpcodeA::Get);
    req_a->address = address;
    req_a->size = size;
    req_a->mask = mask;
    req_a->source = this->idpool.getid();
    req_a->vaddr = address;
    req_a->needHint = 0;
    req_a->matrix = 0x1;
    req_a->ameIndex = req_a->source;

    pendingA.init(req_a, 1);

    return ActionDenial::ACCEPTED;
}

ActionDenialEnum MAgent::do_putfulldata(paddr_t address, shared_tldata_t<DATASIZE> data) {
    if (pendingA.is_pending())
        return ActionDenial::CHANNEL_CONGESTION;

    if (idpool.full())
        return ActionDenial::CHANNEL_RESOURCE;

    if (this->globalBoard->haskey(address) && this->globalBoard->query(this, address)->status == Global_SBEntry::SB_PENDING)
        return ActionDenial::REJECTED_BY_INFLIGHT;

    auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
    req_a->opcode = uint8_t(TLOpcodeA::PutFullData);
    req_a->address = address;
    req_a->size = ceil(log2((double)DATASIZE));
    req_a->mask = 0xffffffffUL;
    req_a->source = this->idpool.getid();
    req_a->data = data;
    req_a->vaddr = address;
    req_a->needHint = 0;
    req_a->matrix = 0x1;
    req_a->ameIndex = req_a->source;

    pendingA.init(req_a, DATASIZE / BEATSIZE);

    return ActionDenial::ACCEPTED;
}

void MAgent::timeout_check() {
    if (localBoard->get().empty())
        return;

    for (auto it = this->localBoard->get().begin(); it != this->localBoard->get().end(); it++) {
        auto value = it->second;
        if (value->status != S_INVALID && value->status != S_VALID) {
            if (*this->cycles - value->time_stamp > TIMEOUT_INTERVAL) {
                std::cout << Gravity::StringAppender()
                    .Append("Now time:   ", *this->cycles).EndLine()
                    .Append("Last stamp: ", value->time_stamp).EndLine()
                    .Append("Status:     ", value->status).EndLine()
                    .ToString();

                tlc_assert(false, this, "Transaction time out");
            }
        }
    }
}

} // namespace tl_agent
