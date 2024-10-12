//
// Created by wkf on 2021/11/2.
//

#include <cstring>
#include <memory>

#include "BaseAgent.h"
#include "Bundle.h"
#include "CAgent.h"


/*
* 0 = Non-inclusive System
* 1 = Inclusive System
*/
#define CAGENT_INCLUSIVE_SYSTEM         1
//

#ifndef CAGENT_INCLUSIVE_SYSTEM
#   define CAGENT_INCLUSIVE_SYSTEM      0
#endif


// debug purpuse, never remove for TileLink spec
#define CAGENT_NO_ALIAS_ACQUIRE         1
#define CAGENT_NO_ALIAS_RELEASE         1


#ifndef CAGENT_NO_ALIAS_ACQUIRE
#   define CAGENT_NO_ALIAS_ACQUIRE      0
#endif

#ifndef CAGENT_NO_ALIAS_RELEASE
#   define CAGENT_NO_ALIAS_RELEASE      0
#endif


//
#ifndef CAGENT_TRAIN_PREFETCH
#   define CAGENT_TRAIN_PREFETCH        1
#endif


namespace tl_agent {

    TLPermission capGenPrivByProbe(TLLocalContext* ctx, TLParamProbe param) {
        switch (param) {
            case TLParamProbe::toT:     return TLPermission::TIP;
            case TLParamProbe::toB:     return TLPermission::BRANCH;
            case TLParamProbe::toN:     return TLPermission::INVALID;
            default:
                tlc_assert(false, ctx, "Invalid param!");
        }
    }

    TLPermission capGenPrivByGrant(TLLocalContext* ctx, TLParamGrant param) {
        switch (param) {
            case TLParamGrant::toT:     return TLPermission::TIP;
            case TLParamGrant::toB:     return TLPermission::BRANCH;
            case TLParamGrant::toN:     return TLPermission::INVALID;
            default:
                tlc_assert(false, ctx, "Invalid param!");
        }
    }

    TLPermission capGenPrivByGrantData(TLLocalContext* ctx, TLParamGrantData param) {
        switch (param) {
            case TLParamGrantData::toT: return TLPermission::TIP;
            case TLParamGrantData::toB: return TLPermission::BRANCH;
            case TLParamGrantData::toN: return TLPermission::INVALID;
            default:
                tlc_assert(false, ctx, "Invalid param!");
        }
    }

    TLPermission shrinkGenPrivByProbeAck(TLLocalContext* ctx, TLParamProbeAck param) {
        switch (param) {
            case TLParamProbeAck::TtoT: return TLPermission::TIP;
            case TLParamProbeAck::BtoB:
            case TLParamProbeAck::TtoB: return TLPermission::BRANCH;
            case TLParamProbeAck::TtoN:
            case TLParamProbeAck::BtoN:
            case TLParamProbeAck::NtoN: return TLPermission::INVALID;
            default:
                tlc_assert(false, ctx, "Invalid param!");
        }
    }

    TLPermission shrinkGenPrivByRelease(TLLocalContext* ctx, TLParamRelease param) {
        switch (param) {
            case TLParamRelease::TtoT:  return TLPermission::TIP;
            case TLParamRelease::BtoB:
            case TLParamRelease::TtoB:  return TLPermission::BRANCH;
            case TLParamRelease::TtoN:
            case TLParamRelease::BtoN:
            case TLParamRelease::NtoN:  return TLPermission::INVALID;
            default:
                tlc_assert(false, ctx, "Invalid param!");
        }
    }

    CAgent::CAgent(TLLocalConfig* cfg, GlobalBoard<paddr_t>* gb, UncachedBoard<paddr_t>* ubs, int sysId, unsigned int seed, uint64_t *cycles) noexcept :
        BaseAgent(cfg, sysId, seed), pendingA(), pendingB(), pendingC(), pendingD(), pendingE(), probeIDpool(NR_SOURCEID, NR_SOURCEID+1)
    {
        this->globalBoard = gb;
        this->uncachedBoards = ubs;
        this->cycles = cycles;
        this->localBoard = new LocalScoreBoard();
        this->idMap = new IDMapScoreBoard();
        this->acquirePermBoard = new AcquirePermScoreBoard();
    }

    CAgent::~CAgent() noexcept
    {
        delete this->localBoard;
        delete this->idMap;
        delete this->acquirePermBoard;
    }

    uint64_t CAgent::cycle() const noexcept
    {
        return *cycles;
    }

    Resp CAgent::send_a(std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>> &a) {
        /*
        * *NOTICE: The AcquirePermBoard is only needed to be updated in send_a() procedure,
        *          since AcquirePerm NtoB is never issued by L1 and the transition of NtoB -> toT
        *          is not currently possible.
        */

        switch (TLOpcodeA(a->opcode)) {
            case TLOpcodeA::AcquireBlock: {
                auto idmap_entry = std::make_shared<C_IDEntry>(a->address, a->alias);
                idMap->update(this, a->source, idmap_entry);

                if (localBoard->haskey(a->address)) {
                    localBoard->query(this, a->address)->update_status(this, S_SENDING_A, a->alias);
                } else {
                    int             statuses[4]   = {S_INVALID};
                    TLPermission    privileges[4] = {TLPermission::INVALID};
                    for (int i = 0; i < 4; i++) {
                        if (a->alias == i) {
                            statuses[i] = S_SENDING_A;
                        }
                    }
                    // Set pending as INVALID
                    auto entry = std::make_shared<C_SBEntry>(this, statuses, privileges);
                    localBoard->update(this, a->address, entry);
                }

                if (acquirePermBoard->haskey(a->address))
                    acquirePermBoard->query(this, a->address)->valid[a->alias] = false;

                break;
            }
            case TLOpcodeA::AcquirePerm: {
                auto idmap_entry = std::make_shared<C_IDEntry>(a->address, a->alias);
                idMap->update(this, a->source, idmap_entry);
                if (localBoard->haskey(a->address)) {
                    localBoard->query(this, a->address)->update_status(this, S_SENDING_A, a->alias);
                } else {
                    int             statuses[4]   = {S_INVALID};
                    TLPermission    privileges[4] = {TLPermission::INVALID};
                    for (int i = 0; i < 4; i++) {
                        if (a->alias == i) {
                          statuses[i] = S_SENDING_A;
                        }
                    }
                    // Set pending as INVALID
                    auto entry = std::make_shared<C_SBEntry>(this, statuses, privileges);
                    localBoard->update(this, a->address, entry);
                }

                std::shared_ptr<C_AcquirePermEntry> acquirePermRecord;
                if (!acquirePermBoard->haskey(a->address))
                    acquirePermBoard->update(this, a->address, acquirePermRecord = std::make_shared<C_AcquirePermEntry>());
                else
                    acquirePermRecord = acquirePermBoard->query(this, a->address);

                acquirePermRecord->valid[a->alias] = true;

                break;
            }
            default:
                tlc_assert(false, this, "Unknown opcode for channel A!");
        }
        this->port->a.opcode   = a->opcode;
        this->port->a.address  = a->address;
        this->port->a.size     = a->size;
        this->port->a.param    = a->param;
        this->port->a.mask     = a->mask;
        this->port->a.source   = a->source;
        this->port->a.needHint = a->needHint;
        this->port->a.vaddr    = a->vaddr;
        this->port->a.alias    = a->alias;
        this->port->a.valid    = true;
        return OK;
    }

    void CAgent::handle_b(std::shared_ptr<BundleChannelB> &b) {
        if (pendingC.is_pending()) {
            Log(this, Append("B wanna pendingC\n"));
            return;
        }
        if (this->probeIDpool.full()) {
            Log(this, Append("[info] id pool full\n"));
            return;
        }

        tlc_assert(localBoard->haskey(b->address), this, "Probe an non-exist block!");

        auto info = localBoard->query(this, b->address);
        auto exact_status = info->status;
        auto exact_privilege = info->privilege[b->alias];
        tlc_assert(exact_status[b->alias] != S_SENDING_C, this, "handle_b should be mutual exclusive with pendingC!");
        for (int i = 0; i < 4; i++)
        {
            if (exact_status[i] == S_C_WAITING_D) {
                // Probe waits for releaseAck
                return;
            }
        }
        auto req_c = std::make_shared<BundleChannelC<ReqField, EchoField, DATASIZE>>();
        req_c->address  = b->address;
        req_c->size     = b->size;
        req_c->source   = this->probeIDpool.getid();
        req_c->dirty    = 1;
        req_c->alias    = b->alias;
        // Log("== id == handleB %d\n", *req_c->source);
        Log(this, ShowBase().Hex().Append("Accepting over Probe to ProbeAck: ", uint64_t(b->source), " -> ", uint64_t(req_c->source)).EndLine());
        if (exact_status[b->alias] == S_SENDING_A || exact_status[b->alias] == S_INVALID || exact_status[b->alias] == S_A_WAITING_D) {
            req_c->opcode   = uint8_t(TLOpcodeC::ProbeAck);
            req_c->param    = uint8_t(TLParamProbeAck::NtoN);
            pendingC.init(req_c, 1);

            if (glbl.cfg.verbose_agent_debug)
            {
                Debug(this, Append("[CAgent] handle_b(): probed an non-exist block, status: ", StatusToString(exact_status[b->alias])).EndLine());
            }
        } else {
            int dirty = (exact_privilege == TLPermission::TIP) && (info->dirty[b->alias] || CAGENT_RAND64(this, "CAgent") % 3);
            // When should we probeAck with data? request need_data or dirty itself
            req_c->opcode = uint8_t((dirty || b->needdata) ? TLOpcodeC::ProbeAckData : TLOpcodeC::ProbeAck);
            if (TLEnumEquals(b->param, TLParamProbe::toB)) {
                switch (exact_privilege) {
                    case TLPermission::TIP:    req_c->param = uint8_t(TLParamProbeAck::TtoB); break;
                    case TLPermission::BRANCH: req_c->param = uint8_t(TLParamProbeAck::BtoB); break;
                    default: tlc_assert(false, this, "Try to probe toB an invalid block!");
                }
            } else if (TLEnumEquals(b->param, TLParamProbe::toN)) {
                switch (exact_privilege) {
                    case TLPermission::TIP:    req_c->param = uint8_t(TLParamProbeAck::TtoN); break;
                    case TLPermission::BRANCH: req_c->param = uint8_t(TLParamProbeAck::BtoN); break;
                    default: tlc_assert(false, this, "Try to probe toB an invalid block!");
                }
            }
            if (!globalBoard->haskey(b->address)) {
                // want to probe an all-zero block which does not exist in global board
                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Append("[CAgent] handle_b(): probed data all-zero").EndLine());
                }
                std::memset((req_c->data = make_shared_tldata<DATASIZE>())->data, 0, DATASIZE);
            } else {
                if (TLEnumEquals(req_c->opcode, TLOpcodeC::ProbeAckData)
                 && TLEnumEquals(req_c->param, TLParamProbeAck::TtoT, 
                                                  TLParamProbeAck::TtoB, 
                                                  TLParamProbeAck::TtoN))
                {
                    req_c->data = make_shared_tldata<DATASIZE>();
                    for (int i = 0; i < DATASIZE; i++) {
                      req_c->data->data[i] = (uint8_t)CAGENT_RAND64(this, "CAgent");
                    }

                    if (glbl.cfg.verbose_agent_debug)
                    {
                        Debug(this, Append("[CAgent] handle_b(): randomized data: "));
                        DebugEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                        DebugEx(std::cout << std::endl);
                    }

                } else {
                    std::memcpy(
                        (req_c->data = make_shared_tldata<DATASIZE>())->data, 
                        globalBoard->query(this, b->address)->data->data, 
                        DATASIZE);

                    if (glbl.cfg.verbose_agent_debug)
                    {
                        Debug(this, Append("[CAgent] handle_b(): fetched scoreboard data: "));
                        DebugEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                        DebugEx(std::cout << std::endl);
                    }
                }
            }
            if (TLEnumEquals(req_c->opcode, TLOpcodeC::ProbeAckData)) {
                pendingC.init(req_c, DATASIZE / BEATSIZE);
            } else {
                pendingC.init(req_c, 1);
            }

            tlc_assert(TLEnumEquals(req_c->param,
                    TLParamProbeAck::TtoN,
                    TLParamProbeAck::TtoB,
                    TLParamProbeAck::NtoN,
                    TLParamProbeAck::BtoN,
                    TLParamProbeAck::BtoB),
                this, 
                Gravity::StringAppender("Not permitted req_c param: ", 
                    ProbeAckParamToString(TLParamProbeAck(req_c->param))).ToString());
        }
        pendingB.update(this);
    }

    Resp CAgent::send_c(std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>> &c) 
    {
        switch (TLOpcodeC(c->opcode)) 
        {
            case TLOpcodeC::ReleaseData:
            {
                std::shared_ptr<C_IDEntry> idmap_entry(new C_IDEntry(c->address, c->alias));
                idMap->update(this, c->source, idmap_entry);

                if (localBoard->haskey(c->address)) {
                    localBoard->query(this, c->address)->update_status(this, S_SENDING_C, c->alias);
                } else {
                    tlc_assert(false, this, "Localboard key not found!");
                }
                int beat_num = pendingC.nr_beat - pendingC.beat_cnt;
                /*
                for (int i = BEATSIZE * beat_num; i < BEATSIZE * (beat_num + 1); i++) {
                    this->port->c.data[i - BEATSIZE * beat_num] = c->data[i];
                }
                */
                std::memcpy(this->port->c.data->data, (uint8_t*)(c->data->data) + (BEATSIZE * beat_num), BEATSIZE);

                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Hex().ShowBase()
                        .Append("[CAgent] channel C presenting: ReleaseData: address = ", c->address)
                        .Append(", data: "));
                    DebugEx(data_dump_embedded<BEATSIZE>(this->port->c.data->data));
                    DebugEx(std::cout << std::endl);
                }

                break;
            }

            case TLOpcodeC::Release:
            {
                std::shared_ptr<C_IDEntry> idmap_entry(new C_IDEntry(c->address, c->alias));
                idMap->update(this, c->source, idmap_entry);

                if (localBoard->haskey(c->address))
                    localBoard->query(this, c->address)->update_status(this, S_SENDING_C, c->alias);
                else 
                    tlc_assert(false, this, "Localboard key not found!");

                break;
            }

            case TLOpcodeC::ProbeAckData:
            {
                std::shared_ptr<C_IDEntry> idmap_entry(new C_IDEntry(c->address, c->alias));
                idMap->update(this, c->source, idmap_entry);

                if (localBoard->haskey(c->address)) {
                    // TODO: What if this is an interrupted probe?
                    localBoard->query(this, c->address)->update_status(this, S_SENDING_C, c->alias);
                } else {
                    tlc_assert(false, this, "Localboard key not found!");
                }
                int beat_num = pendingC.nr_beat - pendingC.beat_cnt;
                /*
                for (int i = BEATSIZE * beat_num; i < BEATSIZE * (beat_num + 1); i++) {
                    this->port->c.data[i - BEATSIZE * beat_num] = c->data[i];
                }
                */
                std::memcpy(this->port->c.data->data, (uint8_t*)(c->data->data) + (BEATSIZE * beat_num), BEATSIZE);

                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Hex().ShowBase()
                        .Append("[CAgent] channel C presenting: ProbeAckData: address = ", c->address)
                        .Append(", data: "));
                    DebugEx(data_dump_embedded<BEATSIZE>(this->port->c.data->data));
                    DebugEx(std::cout << std::endl);
                }

                break;
            }

            case TLOpcodeC::ProbeAck:
            {
                std::shared_ptr<C_IDEntry> idmap_entry(new C_IDEntry(c->address, c->alias));
                idMap->update(this, c->source, idmap_entry);
                // tlc_assert(*c->param == NtoN, "Now probeAck only supports NtoN");
                if (localBoard->haskey(c->address)) {
                    auto item = localBoard->query(this, c->address);
                    if (item->status[c->alias] == S_C_WAITING_D) {
                        item->update_status(this, S_C_WAITING_D_INTR, c->alias);
                    } else if (item->status[c->alias] == S_A_WAITING_D) {
                        item->update_status(this, S_A_WAITING_D_INTR, c->alias);
                    } else {
                        item->update_status(this, S_SENDING_C, c->alias);
                    }
                } else {
                    tlc_assert(false, this, "Localboard key not found!");
                }
                break;
            }
            default:
                tlc_assert(false, this, "Unknown opcode for channel C!");
        }
        this->port->c.opcode   = c->opcode;
        this->port->c.param    = c->param;
        this->port->c.address  = c->address;
        this->port->c.size     = c->size;
        this->port->c.source   = c->source;
        this->port->c.alias    = 0;
        // this->port->c.dirty = c->dirty;
        this->port->c.valid    = true;
        return OK;
    }

    Resp CAgent::send_e(std::shared_ptr<BundleChannelE> &e) {
        this->port->e.sink = e->sink;
        this->port->e.valid = true;
        return OK;
    }

    void CAgent::fire_a() {
        if (this->port->a.fire()) {
            auto& chnA = this->port->a;

            switch (TLOpcodeA(chnA.opcode))
            {
                case TLOpcodeA::AcquireBlock:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire A] [AcquireBlock ", AcquireParamToString(TLParamAcquire(chnA.param)), "] ")
                            .Append("source: ",     uint64_t(chnA.source))
                            .Append(", addr: ",     uint64_t(chnA.address))
                            .Append(", alias: ",    uint64_t(chnA.alias))
                            .EndLine());
                    }
                    break;

                case TLOpcodeA::AcquirePerm:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire A] [AcquirePerm ", AcquireParamToString(TLParamAcquire(chnA.param)) , "] ")
                            .Append("source: ",     uint64_t(chnA.source))
                            .Append(", addr: ",     uint64_t(chnA.address))
                            .Append(", alias: ",    uint64_t(chnA.alias))
                            .EndLine());
                    }
                    break;

                default:
                    tlc_assert(false, this, Gravity::StringAppender()
                        .Hex().ShowBase()
                        .Append("[fire A] unknown opcode: ", uint64_t(chnA.opcode))
                        .EndLine().ToString());
            }

            chnA.valid = false;
            tlc_assert(pendingA.is_pending(), this, "No pending A but A fired!");
            pendingA.update(this);
            if (!pendingA.is_pending()) { // req A finished
                this->localBoard->query(this, pendingA.info->address)->update_status(this, S_A_WAITING_D, pendingA.info->alias);
            }
        }
    }

    void CAgent::fire_b() {
        if (this->port->b.fire()) {
            auto& chnB = this->port->b;

            switch (TLOpcodeB(chnB.opcode))
            {
                case TLOpcodeB::ProbeBlock:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire B] [ProbeBlock ", 
                                ProbeParamToString(TLParamProbe(chnB.param)), "] ")
                            .Append("source: ",     uint64_t(chnB.source))
                            .Append(", addr: ",     uint64_t(chnB.address))
                            .Append(", alias: ",    uint64_t((chnB.alias) >> 1))
                            .EndLine());
                    }
                    break;

                /*
                case TLOpcodeB::ProbePerm:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire B] [ProbePerm ", ProbeParamToString(chnB.param), "] ")
                            .Append("source: ",     uint64_t(chnB.source))
                            .Append(", addr: ",     uint64_t(chnB.address))
                            .Append(", alias: ",    uint64_t((chnB.alias) >> 1))
                            .EndLine());
                    }
                    break;
                */

                default:
                    tlc_assert(false, this, Gravity::StringAppender()
                        .Hex().ShowBase()
                        .Append("[fire B] unknown opcode: ", uint64_t(chnB.opcode))
                        .EndLine().ToString());
            }

            auto req_b = std::make_shared<BundleChannelB>();
            req_b->opcode   = chnB.opcode;
            req_b->address  = chnB.address;
            req_b->param    = chnB.param;
            req_b->size     = chnB.size;
            req_b->source   = chnB.source;
            req_b->alias    = (chnB.alias) >> 1;
            req_b->needdata = (chnB.alias) & 0x1;
            pendingB.init(req_b, 1);
        }
    }

    void CAgent::fire_c() {
        if (this->port->c.fire()) {
            auto& chnC = this->port->c;

            switch (TLOpcodeC(chnC.opcode))
            {
                case TLOpcodeC::Release:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire C] [Release ", 
                                ReleaseParamToString(TLParamRelease(chnC.param)), "] ")
                            .Append("source: ",     uint64_t(chnC.source))
                            .Append(", addr: ",     uint64_t(chnC.address))
                            .Append(", alias: ",    uint64_t(chnC.alias))
                            .EndLine());
                    }
                    break;

                case TLOpcodeC::ReleaseData:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire C] [ReleaseData ", 
                                ReleaseParamToString(TLParamRelease(chnC.param)), "] ")
                            .Append("source: ",     uint64_t(chnC.source))
                            .Append(", addr: ",     uint64_t(chnC.address))
                            .Append(", alias: ",    uint64_t(chnC.alias))
                            .Append(", data: "));
                        LogEx(data_dump_embedded<BEATSIZE>(chnC.data->data));
                        LogEx(std::cout << std::endl);
                    }
                    break;

                case TLOpcodeC::ProbeAck:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire C] [ProbeAck ", 
                                ProbeAckParamToString(TLParamProbeAck(chnC.param)), "] ")
                            .Append("source: ",     uint64_t(chnC.source))
                            .Append(", addr: ",     uint64_t(chnC.address))
                            .Append(", alias: ",    uint64_t(chnC.alias))
                            .EndLine());
                    }
                    break;

                case TLOpcodeC::ProbeAckData:

                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[fire C] [ProbeAckData ", 
                                ProbeAckParamToString(TLParamProbeAck(chnC.param)), "] ")
                            .Append("source: ",     uint64_t(chnC.source))
                            .Append(", addr: ",     uint64_t(chnC.address))
                            .Append(", alias: ",    uint64_t(chnC.alias))
                            .Append(", data: "));
                        LogEx(data_dump_embedded<BEATSIZE>(chnC.data->data));
                        LogEx(std::cout << std::endl);
                    }
                    break;

                default:
                    tlc_assert(false, this, Gravity::StringAppender()
                        .Hex().ShowBase()
                        .Append("[fire C] unknown opcode: ", uint64_t(chnC.opcode))
                        .EndLine().ToString());
            }

            bool releaseHasData = TLEnumEquals(chnC.opcode, TLOpcodeC::ReleaseData);
            bool needAck = TLEnumEquals(chnC.opcode, TLOpcodeC::ReleaseData, TLOpcodeC::Release);
            bool probeAckDataToB = TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAckData) 
                                && TLEnumEquals(chnC.param, TLParamProbeAck::TtoB, TLParamProbeAck::BtoB);
            bool probeAckToB     = TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAck)
                                && TLEnumEquals(chnC.param, TLParamProbeAck::TtoB, TLParamProbeAck::BtoB);
            tlc_assert(pendingC.is_pending(), this, "No pending C but C fired!");
            pendingC.update(this);
            if (!pendingC.is_pending()) { // req C finished

                if (glbl.cfg.verbose_xact_data_complete)
                {
                    if (TLEnumEquals(chnC.opcode, TLOpcodeC::ReleaseData))
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[data complete C] [ReleaseData ", 
                                ProbeAckParamToString(TLParamProbeAck(chnC.param)), "] ")
                            .Append("source: ",     uint64_t(chnC.source))
                            .Append(", addr: ",     uint64_t(chnC.address))
                            .Append(", alias: ",    uint64_t(chnC.alias))
                            .Append(", data: "));
                        LogEx(data_dump_embedded<DATASIZE>(pendingC.info->data->data));
                        LogEx(std::cout << std::endl);
                    }
                    else if (TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAckData))
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[data complete C] [ProbeAckData ", 
                                ProbeAckParamToString(TLParamProbeAck(chnC.param)), "] ")
                            .Append("source: ",     uint64_t(chnC.source))
                            .Append(", addr: ",     uint64_t(chnC.address))
                            .Append(", alias: ",    uint64_t(chnC.alias))
                            .Append(", data: "));
                        LogEx(data_dump_embedded<DATASIZE>(pendingC.info->data->data));
                        LogEx(std::cout << std::endl);
                    }
                }

                chnC.valid = false;

                // Log("[%ld] [C fire] addr: %hx opcode: %hx\n", *cycles, *chnC.address, *chnC.opcode);
                auto info = this->localBoard->query(this, pendingC.info->address);
                auto exact_status = info->status[pendingC.info->alias];
                if (needAck) {
                    info->update_status(this, S_C_WAITING_D, pendingC.info->alias);
                } else {
                    if (exact_status == S_C_WAITING_D_INTR) {
                        info->update_status(this, S_C_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_A_WAITING_D_INTR || exact_status == S_A_WAITING_D) {
                        info->update_status(this, S_A_WAITING_D, pendingC.info->alias);
                    } else {
                        if (probeAckDataToB || probeAckToB) {
                            info->update_status(this, S_VALID, pendingC.info->alias);
                        } else {
                            info->update_status(this, S_INVALID, pendingC.info->alias);
                        }
                    }
                }
                if (releaseHasData) {
                    auto global_SBEntry = std::make_shared<Global_SBEntry>();
                    global_SBEntry->pending_data = pendingC.info->data;
                    if (this->globalBoard->get().count(pendingC.info->address) == 0) {
                        global_SBEntry->data = nullptr;
                    } else {
                        global_SBEntry->data = this->globalBoard->get()[pendingC.info->address]->data;
                    }
                    global_SBEntry->status = Global_SBEntry::SB_PENDING;
                    this->globalBoard->update(this, pendingC.info->address, global_SBEntry);

                    uncachedBoards->appendAll(this, pendingC.info->address, global_SBEntry->pending_data);
                }
                if (TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAckData)) {
                    auto global_SBEntry = std::make_shared<Global_SBEntry>();
                    global_SBEntry->data = pendingC.info->data;
                    global_SBEntry->status = Global_SBEntry::SB_VALID;
                    this->globalBoard->update(this, pendingC.info->address, global_SBEntry);

                    uncachedBoards->appendAll(this, pendingC.info->address, global_SBEntry->data);
                }
                if (TLEnumEquals(chnC.opcode, TLOpcodeC::ReleaseData, TLOpcodeC::Release)) {
                    info->update_pending_priviledge(
                        this, 
                        shrinkGenPrivByRelease(this, TLParamRelease(pendingC.info->param)), 
                        pendingC.info->alias);
                } else {
                    if (TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAck, TLOpcodeC::ProbeAckData)) {
                      info->update_priviledge(
                        this, 
                        shrinkGenPrivByProbeAck(this, TLParamProbeAck(pendingC.info->param)), 
                        pendingC.info->alias);
                    }
                    // Log("== free == fireC %d\n", *chnC.source);
                    this->probeIDpool.freeid(chnC.source);
                }

            }
        }
    }

    void CAgent::fire_d() {
        if (this->port->d.fire()) {
            auto& chnD = this->port->d;

            auto addr = idMap->query(this, chnD.source)->address;
            auto alias = idMap->query(this, chnD.source)->alias;

            if (TLEnumEquals(chnD.opcode, TLOpcodeD::Grant))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[fire D] [Grant ", 
                            GrantParamToString(TLParamGrant(chnD.param)), "] ")
                        .Append("source: ",     uint64_t(chnD.source))
                        .Append(", addr: ",     uint64_t(addr))
                        .Append(", alias: ",    uint64_t(alias))
                        .EndLine());
                }
            }
            else if (TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[fire D] [GrantData ", 
                            GrantDataParamToString(TLParamGrantData(chnD.param)), "] ")
                        .Append("source: ",     uint64_t(chnD.source))
                        .Append(", addr: ",     uint64_t(addr))
                        .Append(", alias: ",    uint64_t(alias))
                        .Append(", data: "));
                    LogEx(data_dump_embedded<BEATSIZE>(chnD.data->data));
                    LogEx(std::cout << std::endl);
                }
            }
            else if (TLEnumEquals(chnD.opcode, TLOpcodeD::ReleaseAck))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[fire D] [ReleaseAck] ")
                        .Append("source: ",     uint64_t(chnD.source))
                        .Append(", addr: ",     uint64_t(addr))
                        .Append(", alias: ",    uint64_t(alias))
                        .EndLine());
                }
            }
            else
            {
                tlc_assert(false, this, Gravity::StringAppender()
                    .Hex().ShowBase()
                    .Append("[fire D] unknown opcode: ", uint64_t(chnD.opcode))
                    .EndLine().ToString());
            }

            bool hasData = TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData);
            bool grant = TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData, TLOpcodeD::Grant);

            auto info = localBoard->query(this, addr);
            auto exact_status = info->status[alias];
            if (!(exact_status == S_C_WAITING_D || exact_status == S_A_WAITING_D || exact_status == S_C_WAITING_D_INTR || exact_status == S_A_WAITING_D_INTR || exact_status == S_INVALID)) {
              Log(this, Append("fire_d: status of localboard is ", exact_status).EndLine());
              Log(this, Hex().ShowBase().Append("addr: ", addr).EndLine());
              tlc_assert(false, this, Gravity::StringAppender("Status error! Not expected to received from channel D.").EndLine()
                    .Append("current status: ", StatusToString(exact_status)).EndLine()
                    .Append("description: ", StatusToDescription(exact_status)).EndLine()
                .ToString());
            }
            if (pendingD.is_pending()) { // following beats
                tlc_assert(chnD.opcode == pendingD.info->opcode, this, "Opcode mismatch among beats!");
                tlc_assert(chnD.param  == pendingD.info->param,  this, "Param mismatch among beats!");
                tlc_assert(chnD.source == pendingD.info->source, this, "Source mismatch among beats!");
                pendingD.update(this);
            } else { // new D resp
                auto resp_d = std::make_shared<BundleChannelD<RespField, EchoField, DATASIZE>>();
                resp_d->opcode  = chnD.opcode;
                resp_d->param   = chnD.param;
                resp_d->source  = chnD.source;
                resp_d->data    = grant ? make_shared_tldata<DATASIZE>() : nullptr;
                int nr_beat = TLEnumEquals(chnD.opcode, TLOpcodeD::Grant, TLOpcodeD::ReleaseAck) ? 0 : 1; // TODO: parameterize it
                pendingD.init(resp_d, nr_beat);
            }
            if (hasData) {
                int beat_num = pendingD.nr_beat - pendingD.beat_cnt;
                /*
                for (int i = BEATSIZE * beat_num; i < BEATSIZE * (beat_num + 1); i++) {
                    pendingD.info->data[i] = chnD.data[i - BEATSIZE * beat_num];
                }
                */
                std::memcpy((uint8_t*)(pendingD.info->data->data) + BEATSIZE * beat_num, chnD.data->data, BEATSIZE);

                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Append("[CAgent] channel D receiving data: "));
                    DebugEx(data_dump_embedded<BEATSIZE>(chnD.data->data));
                    DebugEx(std::cout << std::endl);
                }
            }
            if (!pendingD.is_pending()) {
                switch (TLOpcodeD(chnD.opcode)) {
                    case TLOpcodeD::GrantData: {

                        if (glbl.cfg.verbose_xact_data_complete)
                        {
                            Log(this, Append("[data complete D] [GrantData ", 
                                    GrantDataParamToString(TLParamGrantData(chnD.param)), "] ")
                                .Hex().ShowBase().Append("source: ", uint64_t(chnD.source), ", addr: ", addr, ", alias: ", alias, ", data: "));
                            LogEx(data_dump_embedded<DATASIZE>(pendingD.info->data->data));
                            LogEx(std::cout << std::endl);
                        }
                        
                        this->globalBoard->verify(this, addr, pendingD.info->data);
                        // info->update_dirty(*chnD.dirty, alias);
                        break;
                    }
                    case TLOpcodeD::Grant: {
                        // Always set dirty in AcquirePerm toT txns
                        info->update_dirty(this, true, alias);
                        break;
                    }
                    case TLOpcodeD::ReleaseAck: {
                        if (exact_status == S_C_WAITING_D) {
                            info->update_status(this, S_INVALID, alias);
                            info->update_dirty(this, 0, alias);
                        } else {
                            tlc_assert(exact_status == S_C_WAITING_D_INTR, this, 
                                Gravity::StringAppender("Status error! ReleaseAck not expected.").EndLine()
                                    .Append("current status: ", StatusToString(exact_status)).EndLine()
                                    .Append("description: ", StatusToDescription(exact_status)).EndLine()
                                .ToString());
                            info->update_status(this, S_SENDING_C, alias);
                        }
                        info->unpending_priviledge(this, alias);
                        if (this->globalBoard->haskey(addr))
                            this->globalBoard->unpending(this, addr); // ReleaseData
                        break;
                    }
                    default:
                        tlc_assert(false, this, "Unknown opcode in channel D!");
                }

                // Send E
                if (grant) {
                    tlc_assert(exact_status != S_A_WAITING_D_INTR, this, "TODO: check this Ridiculous probe!");
                    auto req_e = std::make_shared<BundleChannelE>();
                    req_e->sink     = chnD.sink;
                    req_e->addr     = addr;
                    req_e->alias    = alias;
                    if (pendingE.is_pending()) {
                        tlc_assert(false, this, "E is pending!");
                    }
                    pendingE.init(req_e, 1);
                    info->update_status(this, S_SENDING_E, alias);

                    if (TLEnumEquals(chnD.opcode, TLOpcodeD::Grant))
                    {
                        info->update_priviledge(
                            this,
                            capGenPrivByGrant(this, TLParamGrant(chnD.param)),
                            alias);
                    }
                    else if (TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData))
                    {
                        info->update_priviledge(
                            this,
                            capGenPrivByGrantData(this, TLParamGrantData(chnD.param)),
                            alias);
                    }
                }
                idMap->erase(this, chnD.source);
                // Log("== free == fireD %d\n", *chnD.source);
                this->idpool.freeid(chnD.source);
            }
        }
    }

    void CAgent::fire_e() {
        if (this->port->e.fire()) {
            auto& chnE = this->port->e;

            if (glbl.cfg.verbose_xact_fired)
            {
                Log(this, 
                    Append("[fire E] GrantAck")
                    .EndLine());
            }

            chnE.valid = false;
            tlc_assert(pendingE.is_pending(), this, "No pending E but E fired!");
            auto info = localBoard->query(this, pendingE.info->addr);
            info->update_status(this, S_VALID, pendingE.info->alias);
            pendingE.update(this);
        }
    }

    void CAgent::handle_channel() {
        // Constraint: fire_e > fire_d, otherwise concurrent D/E requests will disturb the pendingE
        fire_a();
        fire_b();
        fire_c();
        fire_e();
        fire_d();
    }

    void CAgent::update_signal() {
        this->port->d.ready = true; // TODO: do random here
        this->port->b.ready = !(pendingB.is_pending());

        if (pendingA.is_pending()) {
            // TODO: do delay here
            send_a(pendingA.info);
        }
        if (pendingB.is_pending()) {
            handle_b(pendingB.info);
        }
        if (pendingC.is_pending()) {
            send_c(pendingC.info);
        }
        if (pendingE.is_pending()) {
            send_e(pendingE.info);
        }
        // do timeout check lazily
        if (*this->cycles % TIMEOUT_INTERVAL == 0) {
            this->timeout_check();
        }
        idpool.update(this);
        probeIDpool.update(this);
    }

    bool CAgent::do_acquireBlock(paddr_t address, TLParamAcquire param, int alias) {
        if (pendingA.is_pending() || pendingB.is_pending() || idpool.full())
            return false;
        if (localBoard->haskey(address)) { // check whether this transaction is legal
            auto entry = localBoard->query(this, address);

#           if CAGENT_NO_ALIAS_ACQUIRE == 1
            {
                for (int i = 0; i < 4; i++) 
                {
                    if (i == alias)
                        continue;

                    if (entry->status[i] != S_VALID && entry->status[i] != S_INVALID)
                        return false;
                }
            }
#           endif

            auto perm = entry->privilege[alias];
            auto status = entry->status[alias];
            
            if (status != S_VALID && status != S_INVALID)
                return false;

            if (status == S_VALID)
            {
                if (TLEnumEquals(perm, TLPermission::TIP))
                    return false;
                if (TLEnumEquals(perm, TLPermission::BRANCH) && !TLEnumEquals(param, TLParamAcquire::BtoT))
                    return false;
                if (TLEnumEquals(perm, TLPermission::INVALID) && TLEnumEquals(param, TLParamAcquire::BtoT))
                    return false;
            }
        }
        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = uint8_t(TLOpcodeA::AcquireBlock);
        req_a->address  = address;
        req_a->param    = uint8_t(param);
        req_a->size     = ceil(log2((double)DATASIZE));
        req_a->mask     = (0xffffffffUL);
        req_a->source   = this->idpool.getid();
        req_a->alias    = alias;
        req_a->vaddr    = address;
#if CAGENT_TRAIN_PREFETCH
        req_a->needHint = 1;
#else
        req_a->needHint = 0;
#endif
        // Log("== id == acquire %d\n", *req_a->source);
        pendingA.init(req_a, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced A] [AcquireBlock ", 
                    AcquireParamToString(TLParamAcquire(param)), "] ")
                .Append("source: ",     uint64_t(req_a->source))
                .Append(", addr: ",     uint64_t(address))
                .Append(", alias: ",    uint64_t(alias)).EndLine());
        }

        return true;
    }

    bool CAgent::do_acquirePerm(paddr_t address, TLParamAcquire param, int alias) {
        /*
        * *NOTICE: Only AcquirePerm NtoT & BtoT were possible to be issued,
        *          currently NtoB not utilized in L1.
        */

        if (pendingA.is_pending() || pendingB.is_pending() || idpool.full())
            return false;
        if (localBoard->haskey(address)) {
            auto entry = localBoard->query(this, address);

#           if CAGENT_NO_ALIAS_ACQUIRE == 1
            {
                for (int i = 0; i < 4; i++) 
                {
                    if (i == alias)
                        continue;

                    if (entry->status[i] != S_VALID && entry->status[i] != S_INVALID)
                        return false;
                }
            }
#           endif

            auto perm = entry->privilege[alias];
            auto status = entry->status[alias];

            if (status != S_VALID && status != S_INVALID)
                return false;

            if (status == S_VALID)
            {
                if (TLEnumEquals(perm, TLPermission::TIP))
                    return false;
                if (TLEnumEquals(perm, TLPermission::BRANCH) && !TLEnumEquals(param, TLParamAcquire::BtoT))
                    param = TLParamAcquire::BtoT;
                if (TLEnumEquals(perm, TLPermission::INVALID) && TLEnumEquals(param, TLParamAcquire::BtoT))
                    return false;
            }
        }
        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = uint8_t(TLOpcodeA::AcquirePerm);
        req_a->address  = address;
        req_a->param    = uint8_t(param);
        req_a->size     = ceil(log2((double)DATASIZE));
        req_a->mask     = (0xffffffffUL);
        req_a->source   = this->idpool.getid();
        req_a->alias    = alias;
        req_a->vaddr    = address;
#if CAGENT_TRAIN_PREFETCH
        req_a->needHint = 1;
#else
        req_a->needHint = 0;
#endif
        // Log("== id == acquire %d\n", *req_a->source);
        pendingA.init(req_a, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced A] [AcquirePerm ", 
                    AcquireParamToString(TLParamAcquire(param)) , "] ")
                .Append("source: ",     uint64_t(req_a->source))
                .Append(", addr: ",     uint64_t(address))
                .Append(", alias: ",    uint64_t(alias))
                .EndLine());
        }
        
        return true;
    }

    bool CAgent::do_releaseData(paddr_t address, TLParamRelease param, shared_tldata_t<DATASIZE> data, int alias) {
        if (pendingC.is_pending() || pendingB.is_pending() || idpool.full() || !localBoard->haskey(address))
            return false;
        // ** DEPRECATED **
        // TODO: checkout pendingA
        // TODO: checkout pendingB - give way?
        auto entry = localBoard->query(this, address);
        auto perm = entry->privilege[alias];
        auto status = entry->status[alias];
        if (status != S_VALID) {
            return false;
        }

        if (TLEnumEquals(perm, TLPermission::INVALID)) return false;
        if (TLEnumEquals(perm, TLPermission::BRANCH) && !TLEnumEquals(param, TLParamRelease::BtoN)) return false;
        if (TLEnumEquals(perm, TLPermission::TIP) && TLEnumEquals(perm, TLParamRelease::BtoN)) return false;

        auto req_c = std::make_shared<BundleChannelC<ReqField, EchoField, DATASIZE>>();
        req_c->opcode   = uint8_t(TLOpcodeC::ReleaseData);
        req_c->address  = address;
        req_c->param    = uint8_t(param);
        req_c->size     = ceil(log2((double)DATASIZE));
        req_c->source   = this->idpool.getid();
        req_c->dirty    = 1;
        // Log("== id == release %d\n", *req_c->source);
        req_c->data     = data;
        req_c->alias    = alias;
        pendingC.init(req_c, DATASIZE / BEATSIZE);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced C] [ReleaseData ", 
                    ReleaseParamToString(TLParamRelease(param)), "] ")
                .Append("source: ",     uint64_t(req_c->source))
                .Append(", addr: ",     uint64_t(address))
                .Append(", alias: ",    uint64_t(alias))
                .Append(", data: "));
            LogEx(data_dump_embedded<DATASIZE>(data->data));
            LogEx(std::cout << std::endl);
        }
        
        return true;
    }

    bool CAgent::do_releaseDataAuto(paddr_t address, int alias, bool dirty, bool forced)
    {
        if (forced)
        {
            auto& localMap = localBoard->get();
            if (localMap.empty())
                return false;

            size_t picked = CAGENT_RAND64(this, "CAgent") % localMap.size();

            auto iter = localMap.begin();
            for (size_t pi = 0; pi < picked; pi++, iter++);
    
            address = iter->first;
        }

        if (pendingC.is_pending() || pendingB.is_pending() || idpool.full() || !localBoard->haskey(address))
            return false;
        // TODO: checkout pendingB - give way?
        auto entry = localBoard->query(this, address);
        auto perm = entry->privilege[alias];

        TLPermDemotion param;
        switch (perm) 
        {
            case TLPermission::INVALID:
                return false;

            case TLPermission::BRANCH:
                param = TLPermDemotion::BtoN;
                break;

            case TLPermission::TIP:
                param = TLPermDemotion::TtoN;
                break;

            default:
                tlc_assert(false, this, "Invalid priviledge detected!");
        }

        auto status = entry->status;
        if (status[alias] != S_VALID) 
            return false;
#if     CAGENT_NO_ALIAS_RELEASE == 1
        for (int i = 0; i < 4; i++) {
#else
        int i = alias; {
#endif
            // never send Release/ReleaseData when there is an pending Acquire with same address
            if (status[i] == S_A_WAITING_D || status[i] == S_A_WAITING_D_INTR)
                return false;
        }

        auto req_c = std::make_shared<BundleChannelC<ReqField, EchoField, DATASIZE>>();
        if (TLEnumEquals(param, TLPermDemotion::BtoN))
        {
            req_c->address  = address;
            req_c->size     = ceil(log2((double)DATASIZE));
            req_c->source   = this->idpool.getid();
            req_c->dirty    = 0;
            req_c->alias    = alias;

#           if CAGENT_INCLUSIVE_SYSTEM == 1
            {
                req_c->opcode   = uint8_t(TLOpcodeC::Release);
                req_c->param    = uint8_t(TLParamRelease(param));
                req_c->data     = make_shared_tldata_zero<DATASIZE>();

                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Append("[CAgent] do_releaseDataAuto(): BtoN release without data").EndLine());
                }

                pendingC.init(req_c, 1);

                if (glbl.cfg.verbose_xact_sequenced)
                {
                    Log(this, Append("[sequenced C] [Release ", ReleaseParamToString(param), "] ")
                        .Hex().ShowBase().Append("source: ", uint64_t(req_c->source), ", addr: ", address, ", alias: ", alias).EndLine());
                }
            }
#           else
            {
                req_c->opcode   = ReleaseData;
                req_c->param    = uint8_t(TLParamRelease(param));

                if (globalBoard->haskey(address))
                    req_c->data = globalBoard->query(this, address)->data;
                else
                    req_c->data = make_shared_tldata_zero<DATASIZE>();

                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Append("[CAgent] do_releaseDataAuto(): BtoN release with non-dirty data: "));
                    DebugEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                    DebugEx(std::cout << std::endl);
                }

                pendingC.init(req_c, DATASIZE / BEATSIZE);

                if (glbl.cfg.verbose_xact_sequenced)
                {
                    Log(this, Append("[sequenced C] [ReleaseData ", ReleaseParamToString(param), "] ")
                        .Hex().ShowBase().Append("source: ", uint64_t(req_c->source), ", addr: ", address, ", alias: ", alias, ", data: "));
                    LogEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                    LogEx(std::cout << std::endl);
                }
            }
#           endif
        }
        else
        {
            tlc_assert(TLEnumEquals(param, TLPermDemotion::TtoN), this, "Wrong execution path!");

            req_c->address  = address;
            req_c->size     = ceil(log2((double)DATASIZE));
            req_c->source   = this->idpool.getid();
            req_c->alias    = alias;

            bool acquirePermPrev = false;
            if (acquirePermBoard->haskey(address))
                acquirePermPrev = acquirePermBoard->query(this, address)->valid[alias];

            if (dirty || acquirePermPrev)
            {
                /*
                * *NOTICE: always dirty with previous AcquirePerm
                */
                req_c->opcode   = uint8_t(TLOpcodeC::ReleaseData);
                req_c->param    = uint8_t(TLParamRelease(param));
                req_c->dirty    = 1;

                req_c->data = make_shared_tldata<DATASIZE>();
                for (int i = 0; i < DATASIZE; i++) {
                    req_c->data->data[i] = (uint8_t)CAGENT_RAND64(this, "CAgent");
                }

                if (glbl.cfg.verbose_agent_debug)
                {
                    Debug(this, Append("[CAgent] do_releaseDataAuto(): TtoN randomized dirty data: "));
                    DebugEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                    DebugEx(std::cout << std::endl);
                }

                pendingC.init(req_c, DATASIZE / BEATSIZE);

                if (glbl.cfg.verbose_xact_sequenced)
                {
                    Log(this, Append("[sequenced C] [ReleaseData ", ReleaseParamToString(param), "] ")
                        .Hex().ShowBase().Append("source: ", uint64_t(req_c->source), ", addr: ", address, ", alias: ", alias, ", data: "));
                    LogEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                    LogEx(std::cout << std::endl);
                }
            }
            else
            {
#               if CAGENT_INCLUSIVE_SYSTEM == 1
                {
                    req_c->opcode   = uint8_t(TLOpcodeC::Release);
                    req_c->param    = uint8_t(TLParamRelease(param));
                    req_c->dirty    = 0;
                    req_c->data     = make_shared_tldata_zero<DATASIZE>();

                    if (glbl.cfg.verbose_agent_debug)
                    {
                        Debug(this, Append("[CAgent] do_releaseDataAuto(): TtoN release without data").EndLine());
                    }

                    pendingC.init(req_c, 1);

                    if (glbl.cfg.verbose_xact_sequenced)
                    {
                        Log(this, Append("[sequenced C] [Release ", ReleaseParamToString(param), "] ")
                            .Hex().ShowBase().Append("source: ", uint64_t(req_c->source), ", addr: ", address, ", alias: ", alias).EndLine());
                    }
                }
#               else
                {
                    req_c->opcode   = uint8_t(TLOpcodeC::ReleaseData);
                    req_c->param    = uint8_t(TLParamRelease(param));
                    req_c->dirty    = 0;
                    
                    if (globalBoard->haskey(address))
                        req_c->data = globalBoard->query(this, address)->data;
                    else
                        req_c->data = make_shared_tldata_zero<DATASIZE>();

                    if (glbl.cfg.verbose_agent_debug)
                    {
                        Debug(this, Append("[CAgent] do_releaseDataAuto(): TtoN release with non-dirty data: "));
                        DebugEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                        DebugEx(std::cout << std::endl);
                    }

                    pendingC.init(req_c, DATASIZE / BEATSIZE);

                    if (glbl.cfg.verbose_xact_sequenced)
                    {
                        Log(this, Append("[sequenced A] [ReleaseData ", ReleaseParamToString(param), "] ")
                            .Hex().ShowBase().Append("source: ", uint64_t(req_c->source), ", addr: ", address, ", alias: ", alias, ", data: "));
                        LogEx(data_dump_embedded<DATASIZE>(req_c->data->data));
                        LogEx(std::cout << std::endl);
                    }
                }
#               endif
            }
        }

        return true;
    }

    void CAgent::timeout_check() {
        if (localBoard->get().empty()) {
            return;
        }
        for (auto it = this->localBoard->get().begin(); it != this->localBoard->get().end(); it++) {
            auto addr = it->first;
            auto value = it->second;
            for(int i = 0; i < 4; i++){
              if (value->status[i] != S_INVALID && value->status[i] != S_VALID) {
                if (*this->cycles - value->time_stamp > TIMEOUT_INTERVAL) {

                    std::cout << Gravity::StringAppender().ShowBase()
                        .Hex().Append("Address:     ", addr).EndLine()
                        .Dec().Append("Now time:    ", *this->cycles).EndLine()
                        .Dec().Append("Last stamp:  ", value->time_stamp).EndLine()
                        .Dec().Append("Status[0]:   ", StatusToString(value->status[0])).EndLine()
                    .ToString();

                    tlc_assert(false, this, "Transaction time out");
                }
              }
            }
        }
    }
}
