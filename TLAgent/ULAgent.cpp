//
// Created by ljw on 10/21/21.
//

#include "Bundle.h"
#include "TLEnum.h"
#include <memory>
#include "ULAgent.h"

namespace tl_agent {

    ULAgent::ULAgent(GlobalBoard<paddr_t> *gb, int sysId, unsigned int seed, uint64_t* cycles) noexcept :
            BaseAgent(sysId, seed), pendingA(), pendingD()
    {
        this->globalBoard = gb;
        this->cycles = cycles;
        this->localBoard = new ScoreBoard<int, UL_SBEntry>();
    }

    ULAgent::~ULAgent() noexcept
    {
        delete this->localBoard;
    }

    uint64_t ULAgent::cycle() const noexcept 
    {
        return *this->cycles;
    }

    Resp ULAgent::send_a(std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>> &a) {
        switch (a->opcode) {
            case Get: {
                auto entry 
                    = std::make_shared<UL_SBEntry>(Get, S_SENDING_A, a->address, *this->cycles);
                localBoard->update(a->source, entry);
                break;
            }
            case PutFullData: {
                auto entry
                    = std::make_shared<UL_SBEntry>(PutFullData, S_SENDING_A, a->address, *this->cycles);
                localBoard->update(a->source, entry);
                int beat_num = pendingA.nr_beat - pendingA.beat_cnt;
                /*
                for (int i = BEATSIZE * beat_num; i < BEATSIZE * (beat_num + 1); i++) {
                    this->port->a.data[i - BEATSIZE * beat_num] = a->data[i];
                }
                */
                std::memcpy(this->port->a.data->data, (uint8_t*)(a->data->data) + BEATSIZE * beat_num, BEATSIZE);
                break;
            }
            case PutPartialData: {
                auto entry 
                    = std::make_shared<UL_SBEntry>(PutPartialData, S_SENDING_A, a->address, *this->cycles);
                localBoard->update(a->source, entry);
                int beat_num = pendingA.nr_beat - pendingA.beat_cnt;
                /*
                for (int i = BEATSIZE * beat_num; i < BEATSIZE * (beat_num + 1); i++) {
                    this->port->a.data[i - BEATSIZE * beat_num] = a->data[i];
                }
                */
                std::memcpy(this->port->a.data->data, (uint8_t*)(a->data->data) + BEATSIZE * beat_num, BEATSIZE);
                break;
            }
            default:
                tlc_assert(false, this, "Unknown opcode for channel A!");
        }
        this->port->a.opcode   = a->opcode;
        this->port->a.address  = a->address;
        this->port->a.size     = a->size;
        this->port->a.mask     = a->mask;
        this->port->a.source   = a->source;
        this->port->a.valid    = true;
        return OK;
    }

    Resp ULAgent::send_c(std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>> &c) {
        return OK;
    }

    void ULAgent::fire_a() {
        if (this->port->a.fire()) {
            auto& chnA = this->port->a;
            bool hasData = chnA.opcode == PutFullData || chnA.opcode == PutPartialData;
            chnA.valid = false;
            tlc_assert(pendingA.is_pending(), this, "No pending A but A fired!");
            pendingA.update(this);
            if (!pendingA.is_pending()) { // req A finished
                this->localBoard->query(this, pendingA.info->source)->update_status(S_A_WAITING_D, *cycles);
                if (hasData) {
                    auto global_SBEntry = std::make_shared<Global_SBEntry>();
                    global_SBEntry->pending_data = pendingA.info->data;
                    if (this->globalBoard->get().count(pendingA.info->address) == 0) {
                        global_SBEntry->data = nullptr;
                    } else {
                        global_SBEntry->data = this->globalBoard->get()[pendingA.info->address]->data;
                    }
                    global_SBEntry->status = Global_SBEntry::SB_PENDING;
                    this->globalBoard->update(pendingA.info->address, global_SBEntry);
                }
            }
        }
    }

    void ULAgent::fire_b() {

    }

    void ULAgent::fire_c() {

    }

    void ULAgent::fire_d() {
        if (this->port->d.fire()) {
            auto& chnD = this->port->d;
            auto info = localBoard->query(this, chnD.source);
            bool hasData = chnD.opcode == GrantData || chnD.opcode == AccessAckData;
            tlc_assert(info->status == S_A_WAITING_D, this, "Status error!");
            if (pendingD.is_pending()) { // following beats
                // TODO: wrap the following assertions into a function
                tlc_assert(chnD.opcode == pendingD.info->opcode, this, "Opcode mismatch among beats!");
                tlc_assert(chnD.param  == pendingD.info->param,  this, "Param mismatch among beats!");
                tlc_assert(chnD.source == pendingD.info->source, this, "Source mismatch among beats!");
                pendingD.update(this);
            } else { // new D resp
                auto resp_d = std::make_shared<BundleChannelD<RespField, EchoField, DATASIZE>>();
                resp_d->opcode  = chnD.opcode;
                resp_d->param   = chnD.param;
                resp_d->source  = chnD.source;
                resp_d->data    = hasData ? make_shared_tldata<DATASIZE>() : nullptr;
                int nr_beat     = (chnD.opcode == Grant || chnD.opcode == AccessAck || chnD.size <= 5) ? 0 : 1; // TODO: parameterize it
                pendingD.init(resp_d, nr_beat);
            }
            // Store data to pendingD
            if (hasData) {
                int beat_num = pendingD.nr_beat - pendingD.beat_cnt;
                /*
                for (int i = BEATSIZE * beat_num; i < BEATSIZE * (beat_num + 1); i++) {
                    pendingD.info->data[i] = chnD.data[i - BEATSIZE * beat_num];
                }
                */
                std::memcpy((uint8_t*)(pendingD.info->data->data) + BEATSIZE * beat_num, chnD.data->data, BEATSIZE);
            }
            if (!pendingD.is_pending()) {
                // ULAgent needn't care about endurance
                if (hasData) {
                    Log(this, Append("[", *cycles, "] [AccessAckData] ")
                        .Hex().ShowBase().Append("addr: ", info->address, ", data: "));
                    for(int i = 0; i < DATASIZE; i++) {
                        Dump(Hex().NextWidth(2).Fill('0').Append(pendingD.info->data->data[i]));
                    }
                    Dump(EndLine());
                    this->globalBoard->verify(this, info->address, pendingD.info->data);
                } else if (chnD.opcode == AccessAck) { // finish pending status in GlobalBoard
                    Log(this, Append("[", *cycles, "] [AccessAck] ")
                        .Hex().ShowBase().Append("addr: ", info->address).EndLine());
                    this->globalBoard->unpending(this, info->address);
                }
                localBoard->erase(this, chnD.source);
                this->idpool.freeid(chnD.source);
            }
        }
    }
    
    void ULAgent::fire_e() {
    }

    void ULAgent::handle_b(std::shared_ptr<BundleChannelB> &b) {
    }
    
    void ULAgent::handle_channel() {
        fire_a();
        fire_d();
    }

    void ULAgent::update_signal() {
        this->port->d.ready = true; // TODO: do random here
        if (pendingA.is_pending()) {
            // TODO: do delay here
            send_a(pendingA.info);
        } else {
            this->port->a.valid = false;
        }
        // do timeout check lazily
        if (*this->cycles % TIMEOUT_INTERVAL == 0) {
            this->timeout_check();
        }
        idpool.update(this);
    }
    
    bool ULAgent::do_getAuto(paddr_t address) {
        if (pendingA.is_pending() || idpool.full())
            return false;
        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = Get;
        req_a->address  = address;
        req_a->size     = ceil(log2((double)DATASIZE));
        req_a->mask     = 0xffffffffUL;
        req_a->source   = this->idpool.getid();
        pendingA.init(req_a, 1);
        Log(this, Append("[", *cycles, "] [Get] ")
            .Hex().ShowBase().Append("addr: ", address).EndLine());
        return true;
    }

    bool ULAgent::do_get(paddr_t address, uint8_t size, uint32_t mask) {
        if (pendingA.is_pending() || idpool.full())
            return false;
        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = Get;
        req_a->address  = address;
        req_a->size     = size;
        req_a->mask     = mask;
        req_a->source   = this->idpool.getid();
        pendingA.init(req_a, 1);
        Log(this, Append("[", *cycles, "] [Get] ")
            .Hex().ShowBase().Append("addr: ", address, ", size: ", size).EndLine());
        return true;
    }
    
    bool ULAgent::do_putfulldata(paddr_t address, shared_tldata_t<DATASIZE> data) {
        if (pendingA.is_pending() || idpool.full())
            return false;
        if (this->globalBoard->haskey(address) && this->globalBoard->query(this, address)->status == Global_SBEntry::SB_PENDING) {
            return false;
        }
        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = PutFullData;
        req_a->address  = address;
        req_a->size     = ceil(log2((double)DATASIZE));
        req_a->mask     = 0xffffffffUL;
        req_a->source   = this->idpool.getid();
        req_a->data     = data;
        pendingA.init(req_a, DATASIZE / BEATSIZE);
        Log(this, Append("[", *cycles, "] [PutFullData] ")
            .Hex().ShowBase().Append("addr: ", address, ", data: "));
        for(int i = 0; i < DATASIZE; i++) {
            Dump(Hex().NextWidth(2).Fill('0').Append(data->data[i]));
        }
        Dump(EndLine());
        return true;
    }

    bool ULAgent::do_putpartialdata(paddr_t address, uint8_t size, uint32_t mask, shared_tldata_t<DATASIZE> data) {
        if (pendingA.is_pending() || idpool.full())
            return false;
        if (this->globalBoard->haskey(address) && this->globalBoard->query(this, address)->status == Global_SBEntry::SB_PENDING)
            return false;
        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = PutPartialData;
        req_a->address  = address;
        req_a->size     = size;
        req_a->mask     = mask;
        req_a->source   = this->idpool.getid();
        req_a->data     = data;
        int nrBeat = ceil((float)pow(2, size) / (float)BEATSIZE);
        pendingA.init(req_a, nrBeat);
        Log(this, Append("[", *cycles, "] [PutPartialData] ")
            .Hex().ShowBase().Append("addr: ", address, ", data: "));
        for(int i = 0; i < DATASIZE; i++) {
            Dump(Hex().NextWidth(2).Fill('0').Append(data->data[i]));
        }
        Dump(EndLine());
        return true;
    }
    
    void ULAgent::timeout_check() {
        if (localBoard->get().empty()) {
            return;
        }
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
}