//
// Created by wkf on 2021/11/2.
//

#include <cstring>
#include <memory>
#include <unordered_map>

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
#ifndef CAGENT_TRAIN_PREFETCH_A
#   define CAGENT_TRAIN_PREFETCH_A      1
#endif

#ifndef CAGENT_TRAIN_PREFETCH_C
#   define CAGENT_TRAIN_PREFETCH_C      1
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

    CAgent::CAgent(TLLocalConfig* cfg, MemoryBackend* mem, GlobalBoard<paddr_t>* gb, UncachedBoard<paddr_t>* ubs, int sys, int sysId, unsigned int seed, uint64_t *cycles) noexcept :
        BaseAgent(cfg, mem, sys, sysId, seed), pendingA(), pendingB(), pendingC(), pendingD(), pendingE(), probeIDpool(NR_SOURCEID, NR_SOURCEID+1)
    {
        this->globalBoard = gb;
        this->uncachedBoards = ubs;
        this->cycles = cycles;
        this->localBoard = new LocalScoreBoard();
        this->idMap = new IDMapScoreBoard();
        this->acquirePermBoard = new AcquirePermScoreBoard();
        this->localCMOStatus = new CMOLocalStatus();
        this->lastProbeAfterRelease = false;
        this->lastProbeAfterReleaseAddress = 0;

        Gravity::RegisterListener(
            Gravity::MakeListener(
                Gravity::StringAppender("CAgent.onGrant[", sysId, "]").ToString(),
                0,
                &CAgent::onGrant,
                this
            )
        );
    }

    CAgent::~CAgent() noexcept
    {
        delete this->localBoard;
        delete this->idMap;
        delete this->acquirePermBoard;
        delete this->localCMOStatus;

        Gravity::UnregisterListener<GrantEvent>(Gravity::StringAppender("CAgent.onGrant[", sysId(), "]").ToString());
    }

    uint64_t CAgent::cycle() const noexcept
    {
        return *cycles;
    }

    uint64_t CAgent::map_latency(paddr_t addr)
    {
        auto iterTimeA = inflightTimeStampsA.find(addr);

        if (iterTimeA != inflightTimeStampsA.end())
        {
            uint64_t latency = (cycle() - iterTimeA->second.time) / cfg->profileCycleUnit;
            auto iterLatencyMap = latencyMapA[int(iterTimeA->second.opcode)].find(latency / 10);
            if (iterLatencyMap == latencyMapA[int(iterTimeA->second.opcode)].end())
                latencyMapA[int(iterTimeA->second.opcode)][latency / 10] = 1;
            else
                iterLatencyMap->second++;

            inflightTimeStampsA.erase(iterTimeA);

            return latency;
        }
        else
        {
            auto iterTimeC = inflightTimeStampsC.find(addr);

            if (iterTimeC != inflightTimeStampsC.end())
            {
                uint64_t latency = cycle() - iterTimeC->second.time;
                auto iterLatencyMap = latencyMapA[int(iterTimeC->second.opcode)].find(latency / 10);
                if (iterLatencyMap == latencyMapA[int(iterTimeC->second.opcode)].end())
                    latencyMapA[int(iterTimeC->second.opcode)][latency / 10] = 1;
                else
                    iterLatencyMap->second++;

                inflightTimeStampsC.erase(iterTimeC);

                return latency;
            }
            else
                tlc_assert(false, this, "in-accurate inflight time count");
        }

        return 0;
    }

    Resp CAgent::send_a(std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>> &a) {
        /*
        * *NOTICE: The AcquirePermBoard is only needed to be updated in send_a() procedure,
        *          since AcquirePerm NtoB is never issued by L1 and the transition of NtoB -> toT
        *          is not currently possible.
        */

        switch (TLOpcodeA(a->opcode)) {

            case TLOpcodeA::AcquireBlock:
            {
                auto idmap_entry = std::make_shared<C_IDEntry>(a->address, a->alias);
                idMap->update(this, a->source, idmap_entry);

                if (localBoard->haskey(a->address)) {
                    auto entry = localBoard->query(this, a->address);
                    auto status = entry->status[a->alias];
                    if (status == S_SENDING_C)
                        entry->update_status(this, S_SENDING_A_NESTED_SENDING_C, a->alias);
                    else if (status == S_VALID || status == S_INVALID)
                        entry->update_status(this, S_SENDING_A, a->alias);
                    else if (status == S_SENDING_A)
                        entry->update_status(this, S_SENDING_A, a->alias);
                    else if (status == S_SENDING_A_NESTED_SENDING_C)
                        entry->update_status(this, S_SENDING_A_NESTED_SENDING_C, a->alias);
                    else
                    {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unexpected AcquireBlock on ", StatusToString(status))
                            .Append(" at ", a->address)
                            .ToString());
                    }
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

            case TLOpcodeA::AcquirePerm:
            {
                auto idmap_entry = std::make_shared<C_IDEntry>(a->address, a->alias);
                idMap->update(this, a->source, idmap_entry);
                if (localBoard->haskey(a->address)) {
                    auto entry = localBoard->query(this, a->address);
                    auto status = entry->status[a->alias];
                    if (status == S_SENDING_C)
                        entry->update_status(this, S_SENDING_A_NESTED_SENDING_C, a->alias);
                    else if (status == S_VALID || status == S_INVALID)
                        entry->update_status(this, S_SENDING_A, a->alias);
                    else if (status == S_SENDING_A)
                        entry->update_status(this, S_SENDING_A, a->alias);
                    else if (status == S_SENDING_A_NESTED_SENDING_C)
                        entry->update_status(this, S_SENDING_A_NESTED_SENDING_C, a->alias);
                    else
                    {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unexpected AcquirePerm on ", StatusToString(status))
                            .Append(" at ", a->address)
                            .ToString());
                    }
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

            case TLOpcodeA::CBOClean:
            case TLOpcodeA::CBOFlush:
            case TLOpcodeA::CBOInval:
            {
                auto idmap_entry = std::make_shared<C_IDEntry>(a->address, a->alias);
                idMap->update(this, a->source, idmap_entry);

                if (localBoard->haskey(a->address))
                {
                    auto entry = localBoard->query(this, a->address);
                    for (int i = 0; i < 4; i++)
                        if (entry->status[i] == S_CBO_SENDING_A
                         || entry->status[i] == S_CBO_SENDING_A_NESTED_SENDING_C
                         || entry->status[i] == S_CBO_SENDING_A_NESTED_C_WAITING_D) {
                            // keep the old state
                        } else if (entry->status[i] == S_VALID || entry->status[i] == S_INVALID) {
                            entry->update_status(this, S_CBO_SENDING_A, i);
                        } else if (entry->status[i] == S_SENDING_C) {
                            entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, i);
                        } else if (entry->status[i] == S_C_WAITING_D) {
                            entry->update_status(this, S_CBO_SENDING_A_NESTED_C_WAITING_D, i);
                        } else {
                            tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                .Append("not allowed to send CBO on ", StatusToString(entry->status[i]))
                                .Append(" at ", a->address)
                                .ToString());
                        }
                }
                else
                {
                    int          statuses   [4] = { S_INVALID };
                    TLPermission perms      [4] = { TLPermission::INVALID };
                    for (int i = 0; i < 4; i++)
                        statuses[i] = S_CBO_SENDING_A;
                    
                    auto entry = std::make_shared<C_SBEntry>(this, statuses, perms);
                    localBoard->update(this, a->address, entry);
                }

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

        tlc_assert(localBoard->haskey(b->address), this, Gravity::StringAppender().Hex().ShowBase()
            .Append("Probe an non-exist block: ", b->address)
            .ToString());

        auto info = localBoard->query(this, b->address);
        auto exact_status = info->status;
        auto exact_privilege = info->privilege[b->alias];
        tlc_assert(exact_status[b->alias] != S_SENDING_C
                && exact_status[b->alias] != S_CBO_SENDING_A_NESTED_SENDING_C
                && exact_status[b->alias] != S_CBO_A_WAITING_D_NESTED_SENDING_C,
            this, 
            "handle_b should be mutual exclusive with pendingC!");
        for (int i = 0; i < 4; i++)
        {
            if (exact_status[i] == S_C_WAITING_D
             || exact_status[i] == S_CBO_SENDING_A_NESTED_C_WAITING_D
             || exact_status[i] == S_CBO_A_WAITING_D_NESTED_C_WAITING_D) {
                // Probe waits for releaseAck
                lastProbeAfterRelease = true;
                lastProbeAfterReleaseAddress = b->address;
                return;
            }
        }
        auto req_c = std::make_shared<BundleChannelC<ReqField, EchoField, DATASIZE>>();
        req_c->address  = b->address;
        req_c->size     = b->size;
        req_c->source   = this->probeIDpool.getid();
        req_c->dirty    = 1;
        req_c->alias    = b->alias;
        req_c->vaddr    = b->address;
#if CAGENT_TRAIN_PREFETCH_C
        req_c->needHint = 1;
#else
        req_c->needHint = 0;
#endif
        // Log("== id == handleB %d\n", *req_c->source);
        Log(this, ShowBase().Hex()
            .Append("Accepting over Probe to ProbeAck ", uint64_t(b->source), " -> ", uint64_t(req_c->source))
            .Append(" at ", b->address).EndLine());
        if (exact_status[b->alias] == S_SENDING_A || exact_status[b->alias] == S_A_WAITING_D)
        {
            // Probe under AcquirePerm/AcquireBlock
            req_c->opcode = uint8_t(TLOpcodeC::ProbeAck);

            // *NOTICE: This is the inevitable non-filtered snoop, causing by nested self-Probe
            //          with simultaneous Probe from other cores.
            /*
            tlc_assert(TLEnumEquals(b->param, TLParamProbe::toN), this,
                Gravity::StringAppender().Hex().ShowBase()
                .Append("unexpected non-filtered Probe ", ProbeParamToString(TLParamProbe(b->param)))
                .Append(" on ", PrivilegeToString(exact_privilege))
                .Append(" at ", b->address)
                .Append(", check inclusive directory")
                .ToString());
            */

            switch (exact_privilege) {
                case TLPermission::INVALID: req_c->param = uint8_t(TLParamProbeAck::NtoN); break;
                case TLPermission::BRANCH:  req_c->param = uint8_t(TLParamProbeAck::BtoN); break;
                default:
                    tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("Acquire happened on T block, touched by Probe at ", b->address).ToString());
            }

            pendingC.init(req_c, 1);
        }
        else if (exact_status[b->alias] == S_INVALID)
        {
            // Probe after Release/ReleaseData
            req_c->opcode = uint8_t(TLOpcodeC::ProbeAck);

            // *NOTICE: This is the inevitable non-filtered snoop, causing by nested self-Probe
            //          with simultaneous Probe from other cores.
            /*
            tlc_assert(lastProbeAfterRelease && lastProbeAfterReleaseAddress == b->address, this,
                Gravity::StringAppender().Hex().ShowBase()
                .Append("unexpected non-filtered Probe ", ProbeParamToString(TLParamProbe(b->param)))
                .Append(" on ", PrivilegeToString(exact_privilege))
                .Append(" at ", b->address)
                .Append(", check inclusive directory")
                .ToString());
            */

            req_c->param = uint8_t(TLParamProbeAck::NtoN);

            if (TLEnumEquals(b->param, TLParamProbe::toB))
            {
                if (glbl.cfg.verbose)
                    Log(this, Append("down-graded Probe toB to NtoN, due to previous Release").EndLine());
            }
            else if (TLEnumEquals(b->param, TLParamProbe::toT))
            {
                if (glbl.cfg.verbose)
                    Log(this, Append("down-graded Probe toT to NtoN, due to previous Release").EndLine());
            }

            pendingC.init(req_c, 1);
        }
        else 
        {
            int dirty = (exact_privilege == TLPermission::TIP) && (info->dirty[b->alias] || CAGENT_RAND64(this, "CAgent") % 3);
            // When should we probeAck with data? request need_data or dirty itself
            req_c->opcode = uint8_t((dirty || b->needdata) ? TLOpcodeC::ProbeAckData : TLOpcodeC::ProbeAck);
            if (TLEnumEquals(b->param, TLParamProbe::toT)) {
                switch (exact_privilege) {
                    case TLPermission::TIP:    req_c->param = uint8_t(TLParamProbeAck::TtoT); break;
                    case TLPermission::BRANCH: req_c->param = uint8_t(TLParamProbeAck::BtoB); break;
                    case TLPermission::INVALID:req_c->param = uint8_t(TLParamProbeAck::NtoN); break;
                    default: tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("[Internal] Try to probe toT an unknown permission block: ", b->address).ToString());
                }
            } else if (TLEnumEquals(b->param, TLParamProbe::toB)) {
                switch (exact_privilege) {
                    case TLPermission::TIP:    req_c->param = uint8_t(TLParamProbeAck::TtoB); break;
                    case TLPermission::BRANCH: req_c->param = uint8_t(TLParamProbeAck::BtoB); break;
                    case TLPermission::INVALID:req_c->param = uint8_t(TLParamProbeAck::NtoN); break;
                    default: tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("[Internal] Try to probe toB an unknown permission block: ", b->address).ToString());
                }
            } else if (TLEnumEquals(b->param, TLParamProbe::toN)) {
                switch (exact_privilege) {
                    case TLPermission::TIP:    req_c->param = uint8_t(TLParamProbeAck::TtoN); break;
                    case TLPermission::BRANCH: req_c->param = uint8_t(TLParamProbeAck::BtoN); break;
                    case TLPermission::INVALID:req_c->param = uint8_t(TLParamProbeAck::NtoN); break;
                    default: tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                        .Append("[Internal] Try to probe toN an unknown permission block: ", b->address).ToString());
                }
            } else {
                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                    .Append("unknown Probe param: ", uint64_t(b->param), " at ", b->address).ToString());
            }

            if (localCMOStatus->isInflight(this, b->address))
            {
                if (glbl.cfg.verbose)
                    Log(this, Append("CMO nested probe detected and marked at ")
                        .Hex().ShowBase().Append(b->address).EndLine());
                
                info->cmoProbed = true;
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
                    TLParamProbeAck::TtoT,
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
        lastProbeAfterRelease = false;
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
                    auto entry = localBoard->query(this, c->address);
                    if (entry->status[c->alias] == S_CBO_SENDING_A) {
                        entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_A_WAITING_D) {
                        entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_SENDING_A_NESTED_SENDING_C) {
                        entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_A_WAITING_D_NESTED_SENDING_C) {
                        entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else {
                        entry->update_status(this, S_SENDING_C, c->alias);
                    }
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

                if (localBoard->haskey(c->address)) {
                    auto entry = localBoard->query(this, c->address);
                    if (entry->status[c->alias] == S_CBO_SENDING_A) {
                        entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_A_WAITING_D) {
                        entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_SENDING_A_NESTED_SENDING_C) {
                        entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_A_WAITING_D_NESTED_SENDING_C) {
                        entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else {
                        entry->update_status(this, S_SENDING_C, c->alias);
                    }
                } else { 
                    tlc_assert(false, this, "Localboard key not found!");
                }

                break;
            }

            case TLOpcodeC::ProbeAckData:
            {
                std::shared_ptr<C_IDEntry> idmap_entry(new C_IDEntry(c->address, c->alias));
                idMap->update(this, c->source, idmap_entry);

                if (localBoard->haskey(c->address)) {
                    auto entry = localBoard->query(this, c->address);
                    if (entry->status[c->alias] == S_CBO_SENDING_A) {
                        entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_A_WAITING_D) {
                        entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_SENDING_A_NESTED_SENDING_C) {
                        entry->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_CBO_A_WAITING_D_NESTED_SENDING_C) {
                        entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_SENDING_A) {
                        entry->update_status(this, S_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_A_WAITING_D) {
                        entry->update_status(this, S_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_VALID || entry->status[c->alias] == S_INVALID) {
                        entry->update_status(this, S_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_SENDING_C) {
                        entry->update_status(this, S_SENDING_C, c->alias);
                    } else if (entry->status[c->alias] == S_SENDING_A_NESTED_SENDING_C) {
                        entry->update_status(this, S_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unexpected ProbeAckData on ", StatusToString(entry->status[c->alias]))
                            .Append(" at ", c->address)
                            .ToString());
                    }
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
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("ProbeAck ran over ReleaseAck: ", c->address)
                            .ToString());
                    } else if (item->status[c->alias] == S_A_WAITING_D) {
                        item->update_status(this, S_A_WAITING_D_INTR, c->alias);
                    } else if (item->status[c->alias] == S_CBO_SENDING_A) {
                        item->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_CBO_A_WAITING_D) {
                        item->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_CBO_SENDING_A_NESTED_SENDING_C) {
                        item->update_status(this, S_CBO_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_CBO_A_WAITING_D_NESTED_SENDING_C) {
                        item->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_SENDING_A) {
                        item->update_status(this, S_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_A_WAITING_D) {
                        item->update_status(this, S_A_WAITING_D_NESTED_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_VALID || item->status[c->alias] == S_INVALID) {
                        item->update_status(this, S_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_SENDING_C) {
                        item->update_status(this, S_SENDING_C, c->alias);
                    } else if (item->status[c->alias] == S_SENDING_A_NESTED_SENDING_C) {
                        item->update_status(this, S_SENDING_A_NESTED_SENDING_C, c->alias);
                    } else {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unexpected ProbeAck on ", StatusToString(item->status[c->alias]))
                            .Append(" at ", c->address)
                            .ToString());
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
            bool cbo = false;
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
                    inflightTimeStampsA[chnA.address] = { .time = cycle(), .opcode = TLOpcodeA::AcquireBlock };
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
                    inflightTimeStampsA[chnA.address] = { .time = cycle(), .opcode = TLOpcodeA::AcquirePerm };
                    break;

                case TLOpcodeA::CBOClean:
                    cbo = true;
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire req] [cbo.clean] ")
                            .Append("source: ",     uint64_t(chnA.source))
                            .Append(", addr: ",     uint64_t(chnA.address))
                            .EndLine());
                    }
                    localCMOStatus->setFired(this, chnA.address);
                    inflightTimeStampsA[chnA.address] = { .time = cycle(), .opcode = TLOpcodeA::CBOClean };
                    break;

                case TLOpcodeA::CBOFlush:
                    cbo = true;
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire req] [cbo.flush] ")
                            .Append("source: ",     uint64_t(chnA.source))
                            .Append(", addr: ",     uint64_t(chnA.address))
                            .EndLine());
                    }
                    localCMOStatus->setFired(this, chnA.address);
                    inflightTimeStampsA[chnA.address] = { .time = cycle(), .opcode = TLOpcodeA::CBOFlush };
                    break;

                case TLOpcodeA::CBOInval:
                    cbo = true;
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire req] [cbo.inval] ")
                            .Append("source: ",     uint64_t(chnA.source))
                            .Append(", addr: ",     uint64_t(chnA.address))
                            .EndLine());
                    }
                    localCMOStatus->setFired(this, chnA.address);
                    inflightTimeStampsA[chnA.address] = { .time = cycle(), .opcode = TLOpcodeA::CBOInval };

                    if (!cfg->memorySyncStrict)
                        globalBoard->grant_memory_alt(this, chnA.address);

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
                auto entry = this->localBoard->query(this, pendingA.info->address);
                if (cbo)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        int status = entry->status[i];
                        switch (status)
                        {
                            case S_CBO_SENDING_A:
                                entry->update_status(this, S_CBO_A_WAITING_D, i);
                                break;

                            case S_CBO_SENDING_A_NESTED_SENDING_C:
                                entry->update_status(this, S_CBO_A_WAITING_D_NESTED_SENDING_C, i);
                                break;

                            case S_CBO_SENDING_A_NESTED_C_WAITING_D:
                                entry->update_status(this, S_CBO_A_WAITING_D_NESTED_C_WAITING_D, i);
                                break;

                            default:
                                tlc_assert(false, this,
                                    Gravity::StringAppender().Hex().ShowBase()
                                    .Append("Not S_CBO_SENDING_A or nested on ", TLOpcodeAToString(TLOpcodeA(chnA.opcode)))
                                    .Append(" fired with ", StatusToString(status))
                                    .Append(" at ", pendingA.info->address)
                                    .ToString());
                        }
                    }
                }
                else
                {
                    int status = entry->status[pendingA.info->alias];
                    tlc_assert(status == S_SENDING_A || status == S_SENDING_A_NESTED_SENDING_C, this,
                        Gravity::StringAppender().Hex().ShowBase()
                        .Append("Not S_SENDING_A or nested on ", TLOpcodeAToString(TLOpcodeA(chnA.opcode)))
                        .Append(" fired with ", StatusToString(status))
                        .Append(" at ", pendingA.info->address)
                        .ToString());

                    if (status == S_SENDING_A)
                        entry->update_status(this, S_A_WAITING_D, pendingA.info->alias);
                    else if (status == S_SENDING_A_NESTED_SENDING_C)
                        entry->update_status(this, S_A_WAITING_D_NESTED_SENDING_C, pendingA.info->alias);
                    else {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("Not S_SENDING_A or nested on ", TLOpcodeAToString(TLOpcodeA(chnA.opcode)))
                            .Append(" fired with ", StatusToString(status))
                            .Append(" at ", pendingA.info->address)
                            .ToString());
                    }
                }
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
                    inflightTimeStampsC[chnC.address] = { .time = cycle(), .opcode = TLOpcodeC::Release };
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
                    inflightTimeStampsC[chnC.address] = { .time = cycle(), .opcode = TLOpcodeC::ReleaseData };
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
            bool probeAckDataToT = TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAckData) 
                                && TLEnumEquals(chnC.param, TLParamProbeAck::TtoT);
            bool probeAckToT     = TLEnumEquals(chnC.opcode, TLOpcodeC::ProbeAck)
                                && TLEnumEquals(chnC.param, TLParamProbeAck::TtoT);
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
                    if (exact_status == S_SENDING_C) {
                        info->update_status(this, S_C_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_CBO_SENDING_A_NESTED_SENDING_C) {
                        info->update_status(this, S_CBO_SENDING_A_NESTED_C_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_CBO_A_WAITING_D_NESTED_SENDING_C) {
                        info->update_status(this, S_CBO_A_WAITING_D_NESTED_C_WAITING_D, pendingC.info->alias);
                    } else {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unexpected fire_c() with state ", exact_status)
                            .Append(" at ", pendingC.info->address)
                            .ToString());
                    }
                } else {
                    if (exact_status == S_A_WAITING_D_INTR || exact_status == S_A_WAITING_D) {
                        info->update_status(this, S_A_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_CBO_A_WAITING_D) {
                        info->update_status(this, S_CBO_A_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_CBO_SENDING_A_NESTED_SENDING_C) {
                        info->update_status(this, S_CBO_SENDING_A, pendingC.info->alias);
                    } else if (exact_status == S_CBO_A_WAITING_D_NESTED_SENDING_C) {
                        info->update_status(this, S_CBO_A_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_SENDING_A_NESTED_SENDING_C) {
                        info->update_status(this, S_SENDING_A, pendingC.info->alias);
                    } else if (exact_status == S_A_WAITING_D_NESTED_SENDING_C) {
                        info->update_status(this, S_A_WAITING_D, pendingC.info->alias);
                    } else if (exact_status == S_SENDING_C) {
                        if (probeAckDataToB || probeAckToB || probeAckDataToT || probeAckToT) {
                            info->update_status(this, S_VALID, pendingC.info->alias);
                        } else {
                            info->update_status(this, S_INVALID, pendingC.info->alias);
                        }
                    } else {
                        tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                            .Append("unexpected fire_c() with state ", exact_status)
                            .Append(" at ", pendingC.info->address)
                            .ToString());
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
                    info->update_dirty(this, 0, pendingC.info->alias);
                    info->update_priviledge(
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

    bool CAgent::is_d_fired() {
      return this->port->d.fire();
    }

    void CAgent::fire_d() {
        if (this->port->d.fire()) {
            auto& chnD = this->port->d;

            auto addr = idMap->query(this, chnD.source)->address;
            auto alias = idMap->query(this, chnD.source)->alias;

            uint64_t latency;

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
            else if (TLEnumEquals(chnD.opcode, TLOpcodeD::CBOAck))
            {
                if (glbl.cfg.verbose_xact_fired)
                {
                    Log(this, Hex().ShowBase()
                        .Append("[fire D] [CBOAck] ")
                        .Append("source: ",     uint64_t(chnD.source))
                        .Append(", addr: ",     uint64_t(addr))
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

            if (TLEnumEquals(chnD.opcode, TLOpcodeD::CBOAck))
            {
                if (localCMOStatus->inflightCount() == 0)
                    tlc_assert(false, this, "zero in-flight CMO");

                auto status = localCMOStatus->query(this, addr);
                auto entry = localBoard->query(this, status.address);

                for (int i = 0; i < 4; i++)
                {
                    if (entry->status[i] != S_CBO_A_WAITING_D
                     && entry->status[i] != S_CBO_A_WAITING_D_NESTED_SENDING_C
                     && entry->status[i] != S_CBO_A_WAITING_D_NESTED_C_WAITING_D)
                    {
                        Log(this, Append("fire_d: status of localboard is ", StatusToString(entry->status[i]), " at ", i).EndLine());
                        Log(this, Hex().ShowBase().Append("addr: ", addr).EndLine());
                        tlc_assert(false, this,
                            Gravity::StringAppender().Hex().ShowBase()
                                .Append("unexpected to receive CBOAck from channel D")
                                .Append(" at ", addr).EndLine()
                                .Append("current status: ", StatusToString(entry->status[i])).EndLine()
                                .Append("description: ", StatusToDescription(entry->status[i])).EndLine()
                            .ToString());
                    }
                }

                switch (status.opcode)
                {
                    case TLOpcodeA::CBOClean:
                        latency = map_latency(addr);
                        for (int i = 0; i < 4; i++)
                        {
                            int          status = entry->status[i];
                            TLPermission perm   = entry->privilege[i];
                            if (TLEnumEquals(perm, TLPermission::TIP)
                            ||  TLEnumEquals(perm, TLPermission::BRANCH)
                            ||  TLEnumEquals(perm, TLPermission::INVALID))
                            {
                                if (glbl.cfg.verbose)
                                {
                                    Log(this, Append("[CBOAck] [cbo.clean]")
                                        .Append(" checked CMO final state on system #", sysId(), ": ")
                                        .Append(PrivilegeToString(perm)).EndLine());

                                    Log(this, Append("[CBOAck] [cbo.clean] A -> D latency: ")
                                        .Append(latency)
                                        .EndLine());
                                }

                                if (TLEnumEquals(perm, TLPermission::TIP))
                                {
                                    tlc_assert(entry->cmoProbed, this, Gravity::StringAppender().Hex().ShowBase()
                                        .Append("[CBOAck] [cbo.clean] ended up with TIP without nested probe")
                                        .ToString());
                                }

                                if (status == S_CBO_A_WAITING_D_NESTED_SENDING_C)
                                    entry->update_status(this, S_SENDING_C, i);
                                else if (status == S_CBO_A_WAITING_D_NESTED_C_WAITING_D)
                                    entry->update_status(this, S_C_WAITING_D, i);
                                else
                                {
                                    if (TLEnumEquals(perm, TLPermission::BRANCH))
                                        entry->update_status(this, S_VALID, i);
                                    else
                                        entry->update_status(this, S_INVALID, i);
                                }
                            }
                            else
                            {
                                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                    .Append("[CBOAck] [cbo.clean] ended up with ")
                                    .Append(PrivilegeToString(perm))
                                    .ToString());
                            }
                        }
                        break;

                    case TLOpcodeA::CBOFlush:
                        latency = map_latency(addr);
                        for (int i = 0; i < 4; i++)
                        {
                            int          status = entry->status[i];
                            TLPermission perm   = entry->privilege[i];
                            if (TLEnumEquals(perm, TLPermission::INVALID))
                            {
                                if (glbl.cfg.verbose)
                                {
                                    Log(this, Append("[CBOAck] [cbo.flush] checked CMO final state on system #", sysId(), ": ")
                                        .Append(PrivilegeToString(perm)).EndLine());

                                    Log(this, Append("[CBOAck] [cbo.flush] A -> D latency: ")
                                        .Append(latency)
                                        .EndLine());
                                }

                                if (status == S_CBO_A_WAITING_D_NESTED_SENDING_C)
                                    entry->update_status(this, S_SENDING_C, i);
                                else if (status == S_CBO_A_WAITING_D_NESTED_C_WAITING_D)
                                    entry->update_status(this, S_C_WAITING_D, i);
                                else
                                    entry->update_status(this, S_INVALID, i);
                            }
                            else
                            {
                                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                    .Append("[CBOAck] [cbo.flush] ended up with ")
                                    .Append(PrivilegeToString(perm))
                                    .ToString());
                            }
                        }
                        break;

                    case TLOpcodeA::CBOInval:
                        latency = map_latency(addr);
                        for (int i = 0; i < 4; i++)
                        {
                            int          status = entry->status[i];
                            TLPermission perm   = entry->privilege[i];
                            if (TLEnumEquals(perm, TLPermission::INVALID))
                            {
                                if (glbl.cfg.verbose)
                                {
                                    Log(this, Append("[CBOAck] [cbo.inval] checked CMO final state on system #", sysId(), ": ")
                                        .Append(PrivilegeToString(perm)).EndLine());

                                    Log(this, Append("[CBOAck] [cbo.inval] A -> D latency: ")
                                        .Append(latency)
                                        .EndLine());
                                }

                                if (status == S_CBO_A_WAITING_D_NESTED_SENDING_C)
                                    entry->update_status(this, S_SENDING_C, i);
                                else if (status == S_CBO_A_WAITING_D_NESTED_C_WAITING_D)
                                    entry->update_status(this, S_C_WAITING_D, i);
                                else
                                    entry->update_status(this, S_INVALID, i);
                            }
                            else
                            {
                                tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                    .Append("[CBOAck] [cbo.inval] ended up with ")
                                    .Append(PrivilegeToString(perm))
                                    .ToString());
                            }
                        }

                        if (!cfg->memorySyncStrict)
                        {
                            globalBoard->reclaim_memory_alt(this, addr);
                        }
                        else
                        {
                            if (globalBoard->haskey(addr))
                            {
                                auto globalEntry = globalBoard->query(this, addr);

                                if (globalEntry->status == Global_SBEntry::SB_PENDING)
                                {
                                    tlc_assert(false, this, Gravity::StringAppender().Hex().ShowBase()
                                        .Append("unsafe strict mode data sync override from memory at ", addr)
                                        .ToString());
                                }

                                if (globalEntry->status == Global_SBEntry::SB_VALID)
                                {
                                    auto oldData = globalEntry->data;

                                    globalEntry->data = make_shared_tldata<DATASIZE>();
                                    for (size_t i = 0; i < DATASIZE; i++)
                                        globalEntry->data->data[i] = mem->access(addr + i);

                                    if (glbl.cfg.verbose)
                                    {
                                        Log(this, Hex().ShowBase()
                                            .Append("[strict data sync] override data from memory at ", addr));
                                        LogEx(data_dump_embedded<DATASIZE>(oldData->data));
                                        LogEx(std::cout << " to ");
                                        LogEx(data_dump_embedded<DATASIZE>(globalEntry->data->data));
                                    }
                                }
                            }
                        }

                        break;

                    default:
                        tlc_assert(false, this,
                            Gravity::StringAppender().Hex().ShowBase()
                                .Append("Unknown opcode for firing CMOAck: ", uint64_t(status.opcode))
                            .ToString());
                }

                localCMOStatus->freeInflight(this, status.address);

                idMap->erase(this, chnD.source);
                this->idpool.freeid(chnD.source);
            }
            else
            {
                bool hasData = TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData);
                bool grant = TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData, TLOpcodeD::Grant);

                auto info = localBoard->query(this, addr);
                auto exact_status = info->status[alias];
                if (exact_status == S_A_WAITING_D_NESTED_SENDING_C) {
                    tlc_assert(false, this, Gravity::StringAppender()
                            .Append("received Grant on S_A_WAITING_D_NESTED_SENDING_C (Grant not allowed on pending ProbeAck/ProbeAckData)")
                        .ToString());
                }
                if (!(exact_status == S_C_WAITING_D 
                   || exact_status == S_A_WAITING_D
                   || exact_status == S_A_WAITING_D_INTR
                   || exact_status == S_CBO_SENDING_A_NESTED_C_WAITING_D
                   || exact_status == S_CBO_A_WAITING_D_NESTED_C_WAITING_D
                   || exact_status == S_INVALID)) {
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
                            if (exact_status != S_A_WAITING_D && exact_status != S_A_WAITING_D_INTR)
                                tlc_assert(false, this, Gravity::StringAppender()
                                    .Append("GrantData not expected on ", StatusToString(exact_status))
                                    .Append(" at ", addr)
                                    .ToString());

                            latency = map_latency(addr);
                            
                            if (glbl.cfg.verbose_xact_data_complete)
                            {
                                Log(this, Append("[data complete D] [GrantData ", 
                                        GrantDataParamToString(TLParamGrantData(chnD.param)), "] ")
                                    .Hex().ShowBase().Append("source: ", uint64_t(chnD.source), ", addr: ", addr, ", alias: ", alias, ", data: "));
                                LogEx(data_dump_embedded<DATASIZE>(pendingD.info->data->data));
                                LogEx(std::cout << std::endl);

                                Log(this, Append("[data complete D] [GrantData ",
                                        GrantDataParamToString(TLParamGrantData(chnD.param)), "] ")
                                    .Append("A -> D latency: ", latency)
                                    .EndLine());
                            }
                            
                            bool memoryAlted = false;

                            this->globalBoard->verify(this, mem, addr, pendingD.info->data, &memoryAlted);
                            
                            if (glbl.cfg.verbose)
                            {
                                if (memoryAlted)
                                {
                                    Log(this, Hex().ShowBase()
                                        .Append("[relax data sync] memory alternative at ", addr, " with "));
                                    LogEx(data_dump_embedded<DATASIZE>(pendingD.info->data->data));
                                    LogEx(std::cout << std::endl);
                                }
                            }

                            // info->update_dirty(*chnD.dirty, alias);
                            break;
                        }
                        case TLOpcodeD::Grant: {
                            if (exact_status != S_A_WAITING_D && exact_status != S_A_WAITING_D_INTR)
                                tlc_assert(false, this, Gravity::StringAppender()
                                    .Append("Grant not expected on ", StatusToString(exact_status))
                                    .Append(" at ", addr)
                                    .ToString());
                            // Always set dirty in AcquirePerm toT txns
                            info->update_dirty(this, true, alias);

                            latency = map_latency(addr);
                            Log(this, Append("[fire D] [Grant ", 
                                    GrantParamToString(TLParamGrant(chnD.param)), "] ")
                                .Append("A -> D latency: ", latency)
                                .EndLine());

                            break;
                        }
                        case TLOpcodeD::ReleaseAck: {
                            // Release toN possible only
                            if (exact_status == S_C_WAITING_D) {
                                info->update_status(this, S_INVALID, alias);
                            } else if (exact_status == S_CBO_SENDING_A_NESTED_C_WAITING_D) {
                                info->update_status(this, S_CBO_SENDING_A, alias);
                            } else if (exact_status == S_CBO_A_WAITING_D_NESTED_C_WAITING_D) {
                                info->update_status(this, S_CBO_A_WAITING_D, alias);
                            } else {
                                tlc_assert(false, this, 
                                    Gravity::StringAppender("Status error! ReleaseAck not expected.").EndLine()
                                        .Append("current status: ", StatusToString(exact_status)).EndLine()
                                        .Append("description: ", StatusToDescription(exact_status)).EndLine()
                                    .ToString());
                            }
                            if (this->globalBoard->haskey(addr))
                                this->globalBoard->unpending(this, addr); // ReleaseData

                            latency = map_latency(addr);
                            Log(this, Append("[fire D] [ReleaseAck] ")
                                .Append("C -> D latency: ", latency)
                                .EndLine());

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

                            GrantEvent(sysId(), addr, info->privilege[alias]).Fire();
                        }
                        else if (TLEnumEquals(chnD.opcode, TLOpcodeD::GrantData))
                        {
                            info->update_priviledge(
                                this,
                                capGenPrivByGrantData(this, TLParamGrantData(chnD.param)),
                                alias);

                            GrantEvent(sysId(), addr, info->privilege[alias]).Fire();
                        }
                    }

                    idMap->erase(this, chnD.source);
                    this->idpool.freeid(chnD.source);
                }
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
            printf("Is HADLE_B\n");
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

    void CAgent::onGrant(GrantEvent& event)
    {
        if (event.sysId != sysId())
        {
            if (event.finalPerm != TLPermission::TIP)
                return;

            if (!localBoard->haskey(event.address))
                return;

            auto entry = localBoard->query(this, event.address);

            for (int i = 0; i < 4; i++)
                if (entry->privilege[i] != TLPermission::INVALID)
                {
                    tlc_assert(false, this, Gravity::StringAppender()
                        .Append("one CAgent #", event.sysId, " got T ")
                        .Hex().ShowBase()
                        .Append("on non-INVALID in other agents at ", event.address)
                        .ToString());
                }
        }
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
#if CAGENT_TRAIN_PREFETCH_A
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
#if CAGENT_TRAIN_PREFETCH_A
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
        req_c->vaddr    = address;
#if CAGENT_TRAIN_PREFETCH_C
        req_c->needHint = 1;
#else
        req_c->needHint = 0;
#endif
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
            req_c->vaddr    = address;
#if CAGENT_TRAIN_PREFETCH_C
            req_c->needHint = 1;
#else
            req_c->needHint = 0;
#endif

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
            req_c->vaddr    = address;
#if CAGENT_TRAIN_PREFETCH_C
            req_c->needHint = 1;
#else
            req_c->needHint = 0;
#endif

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

    bool CAgent::do_cbo(TLOpcodeA opcode, paddr_t address, bool alwaysHit)
    {
        if (localCMOStatus->isInflight(this, address))
            return false;

        if (cfg->cmoParallelDepth && localCMOStatus->inflightCount() >= cfg->cmoParallelDepth)
            return false;

        if (pendingA.is_pending() || idpool.full())
            return false;

        if (alwaysHit)
        {
            std::unordered_map<paddr_t, std::shared_ptr<C_SBEntry>> hitTable;

            if (hitTable.empty())
                return false;

            for (auto& iter : localBoard->get())
                for (int i = 0; i < 4; i++)
                    if (iter.second->status[i] == S_VALID)
                        hitTable[iter.first] = iter.second;

            size_t index = CAGENT_RAND64(this, "CAgent") % hitTable.size();

            auto iter = hitTable.begin();
            for (size_t i = 0; i < index; i++)
                iter++;

            address = iter->first;
        }

        if (localBoard->haskey(address))
        {
            // check whether this transaction is legal
            auto entry = localBoard->query(this, address);

            for (int i = 0; i < 4; i++)
                if (entry->status[i] != S_VALID 
                 && entry->status[i] != S_INVALID
                 && entry->status[i] != S_SENDING_C
                 && entry->status[i] != S_C_WAITING_D)
                    return false;
        }

        auto req_a = std::make_shared<BundleChannelA<ReqField, EchoField, DATASIZE>>();
        req_a->opcode   = uint8_t(opcode);
        req_a->address  = address;
        req_a->param    = 0;
        req_a->size     = 0;
        req_a->mask     = 0;
        req_a->source   = this->idpool.getid();
        req_a->alias    = 0;
        req_a->vaddr    = 0;
        req_a->needHint = 0;

        pendingA.init(req_a, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[sequenced A] [CBOClean] ")
                .Append("source: ", uint64_t(req_a->source))
                .Append(", addr: ", uint64_t(address)).EndLine());
        }

        localCMOStatus->setInflight(this, address, opcode);

        return true;
    }

    bool CAgent::do_cbo_clean(paddr_t address, bool alwaysHit)
    {
        return do_cbo(TLOpcodeA::CBOClean, address, alwaysHit);
    }

    bool CAgent::do_cbo_flush(paddr_t address, bool alwaysHit)
    {
        return do_cbo(TLOpcodeA::CBOFlush, address, alwaysHit);
    }

    bool CAgent::do_cbo_inval(paddr_t address, bool alwaysHit)
    {
        return do_cbo(TLOpcodeA::CBOInval, address, alwaysHit);
    }

    void CAgent::timeout_check() {
        return;
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
                        .Dec().Append("Status[%d]:   ", StatusToString(value->status[i])).EndLine()
                    .ToString();

                    tlc_assert(false, this, "Transaction time out");
                }
              }
            }
        }
    }

    CAgent::LocalScoreBoard* CAgent::local() noexcept
    {
        return localBoard;
    }

    const CAgent::LocalScoreBoard* CAgent::local() const noexcept
    {
        return localBoard;
    }
}


// Implementation of: class InflightCMO
namespace tl_agent {
    /*
    uint64_t        timeStamp;
    paddr_t         address;
    TLOpcodeA       opcode;
    bool            fired;
    */

    InflightCMO::InflightCMO(uint64_t timeStamp, paddr_t address, TLOpcodeA opcode) noexcept
        : timeStamp (timeStamp)
        , address   (address)
        , opcode    (opcode)
        , fired     (false)
    { }
}


// Implementation of: class CMOLocalStatus
namespace tl_agent {
    /*
    std::unordered_map<paddr_t, InflightCMO>    inflight;
    */

    void CMOLocalStatus::setInflight(const TLLocalContext* ctx, paddr_t address, TLOpcodeA opcode)
    {
        if (!inflight.empty())
        {
            if (inflight.count(address))
            {
                tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                    .Append("CMO already in flight: address: ", uint64_t(address))
                    .ToString());
            }
            else
            {
                /*
                tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                    .Append("Multiple CMO in-flight on set")
                    .ToString());
                */
            }
        }

        inflight[address] = InflightCMO(ctx->cycle(), address, opcode);
    }

    void CMOLocalStatus::freeInflight(const TLLocalContext* ctx, paddr_t address)
    {
        auto iter = inflight.find(address);

        if (iter == inflight.end())
        {
            tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                .Append("CMO not in flight: address: ", uint64_t(address))
                .ToString());
        }

        inflight.erase(iter);
    }

    bool CMOLocalStatus::isInflight(const TLLocalContext* ctx, paddr_t address) const noexcept
    {
        return inflight.count(address) != 0;
    }

    bool CMOLocalStatus::hasInflight() const noexcept
    {
        return !inflight.empty();
    }

    InflightCMO CMOLocalStatus::firstInflight() const noexcept
    {
        return inflight.begin()->second;
    }
    
    size_t CMOLocalStatus::inflightCount() const noexcept
    {
        return inflight.size();
    }

    void CMOLocalStatus::setFired(const TLLocalContext* ctx, paddr_t address)
    {
        auto iter = inflight.find(address);

        if (iter == inflight.end())
        {
            tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                .Append("CMO not in flight to set fired: address: ", uint64_t(address))
                .ToString());
        }

        if (iter->second.fired)
        {
            tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                .Append("In-flight CMO already fired: address: ", uint64_t(address))
                .ToString());
        }

        iter->second.fired = true;
    }

    InflightCMO CMOLocalStatus::query(const TLLocalContext* ctx, paddr_t address) const
    {
        auto iter = inflight.find(address);

        if (iter == inflight.end())
        {
            tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                .Append("In-flight CMO not found: address: ", uint64_t(address))
                .ToString());
        }

        return iter->second;
    }

    void CMOLocalStatus::update(const TLLocalContext* ctx, paddr_t address, InflightCMO entry)
    {
        inflight[address] = entry;

        if (inflight.size() != 1)
        {
            tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                .Append("Multiple CMO in-flight after update")
                .ToString());
        }
    }

    void CMOLocalStatus::checkTimeout(const TLLocalContext* ctx)
    {
        // don't do timeout check currently
    }
}

