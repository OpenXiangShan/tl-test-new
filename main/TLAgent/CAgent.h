//
// Created by wkf on 2021/11/2.
//

#ifndef TLC_TEST_CAGENT_H
#define TLC_TEST_CAGENT_H

#include "BaseAgent.h"

#include "../Base/TLEnum.hpp"
#include "../Utils/Common.h"
#include "../Utils/ScoreBoard.h"
#include "../Utils/gravity_eventbus.hpp"

#include "Bundle.h"


namespace tl_agent {

    class GrantEvent : public Gravity::Event<GrantEvent> {
    public:
        uint64_t        sysId;
        paddr_t         address;
        TLPermission    finalPerm;

    public:
        inline GrantEvent(uint64_t sysId, paddr_t address, TLPermission finalPerm) noexcept
            : sysId     (sysId)
            , address   (address)
            , finalPerm (finalPerm)
        { }
    };

    class C_SBEntry {
    public:
        uint64_t        time_stamp;
        int             status[4];
        TLPermission    privilege[4];
        TLPermission    pending_privilege[4];
        int             dirty[4];

        inline C_SBEntry(const TLLocalContext* ctx, const int status[], const TLPermission privilege[])
        {
            this->time_stamp = ctx->cycle();
            for(int i = 0; i<4; i++){
              this->privilege[i] = privilege[i];
              this->status[i] = status[i];
            }
        }

        inline void update_status(const TLLocalContext* ctx, int status, int alias)
        {
#           if SB_DEBUG == 1
                Debug(ctx, Append("TL-C local scoreboard update (status): ")
                    .ShowBase()
                    .Dec().Append("[alias = ", alias, "]")
                    .Hex().Append(" status = ", StatusToString(this->status[alias]), " -> ", StatusToString(status))
                    .EndLine());
#           endif

            this->status[alias] = status;
            this->time_stamp = ctx->cycle();
        }

        inline void update_priviledge(const TLLocalContext* ctx, TLPermission priv, int alias)
        {
            this->privilege[alias] = priv;
            this->time_stamp = ctx->cycle();

#           if SB_DEBUG == 1
                Debug(ctx, Append("TL-C local scoreboard update (privilege): ")
                    .ShowBase()
                    .Dec().Append("[alias = ", alias, "]")
                    .Hex().Append(" privilege = ", PrivilegeToString(priv))
                    .EndLine());
#           endif
        }

        inline void update_pending_priviledge(const TLLocalContext* ctx, TLPermission priv, int alias)
        {
            this->pending_privilege[alias] = priv;
            this->time_stamp = ctx->cycle();

#           if SB_DEBUG == 1
                Debug(ctx, Append("TL-C local scoreboard update (pending_privilege): ")
                    .ShowBase()
                    .Dec().Append("[alias = ", alias, "] ")
                    .Hex().Append(" pending_privilege = ", PrivilegeToString(priv))
                    .EndLine());
#           endif
        }

        inline void unpending_priviledge(const TLLocalContext* ctx, int alias)
        {
            this->privilege[alias] = this->pending_privilege[alias];
            this->pending_privilege[alias] = TLPermission::NIL;
            this->time_stamp = ctx->cycle();

#           if SB_DEBUG == 1
                Debug(ctx, Append("TL-C local scoreboard update (unpending_privilege): ")
                    .ShowBase()
                    .Dec().Append("[alias = ", alias, "]")
                    .Hex().Append(" pending_privilege = ", PrivilegeToString(this->pending_privilege[alias]))
                    .EndLine());
#           endif
        }

        inline void update_dirty(const TLLocalContext* ctx, int dirty, int alias)
        {
            this->dirty[alias] = dirty;

#           if SB_DEBUG == 1
                Debug(ctx, Append("TL-C local scoreboard update (dirty): ")
                    .ShowBase()
                    .Dec().Append("[alias = ", alias, "]")
                    .Hex().Append(" dirty = ", dirty)
                    .EndLine());
#           endif
        }
    };

    class C_IDEntry {
    public:
        paddr_t address;
        int alias;

        inline C_IDEntry(paddr_t &addr, uint8_t &alias) 
        {
            this->address = addr;
            this->alias = alias;
        }
    };

    class C_AcquirePermEntry {
    public:
        bool valid[4];
    
    public:
        inline C_AcquirePermEntry()
        { 
            std::memset(valid, false, 4 * sizeof(bool));
        }
    };


    template<typename Tk>
    struct ScoreBoardUpdateCallbackCSBEntry : public ScoreBoardUpdateCallback<Tk, tl_agent::C_SBEntry>
    { 
        inline void update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<tl_agent::C_SBEntry>& data)
        {
#           if SB_DEBUG == 1
                Gravity::StringAppender strapp;

                strapp.Append("TL-C local scoreboard update: ")
                    .ShowBase()
                    .Hex().Append("key = ", uint64_t(key))
                //  .Dec().Append(", present = ", mapping.count(key))
                    .ToString();

                strapp.Append(", type = C_SBEntry");

                strapp.Append(", timestamp = ", data->time_stamp);

                strapp.EndLine();

                Debug(ctx, Append(strapp.ToString()));

                for (int i = 0; i < 4; i++)
                {
                    std::cout << Gravity::StringAppender("[", i, "] ")
                        .ShowBase()
                        .Append("status = ", tl_agent::StatusToString(data->status[i]))
                        .Append(", privilege = ", PrivilegeToString(data->privilege[i]))
                        .Append(", pending_privilege = ", PrivilegeToString(data->pending_privilege[i]))
                        .Append(", dirty = ", data->dirty[i])
                        .EndLine()
                        .ToString();
                }
#           endif 
        }
    };

    template<typename Tk>
    struct ScoreBoardUpdateCallbackCIDEntry : public ScoreBoardUpdateCallback<Tk, tl_agent::C_IDEntry>
    {
        inline void update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<tl_agent::C_IDEntry>& data)
        {
#           if SB_DEBUG == 1
                Debug(ctx, Append("TL-C local scoreboard update: ")
                    .ShowBase()
                    .Hex().Append("key = ", uint64_t(key))
                    .Dec().Append(", type = C_IDEntry")
                    .Hex().Append(", address = ", data->address)
                    .Hex().Append(", alias = ", data->alias)
                    .EndLine());
#           endif
        }
    };


#   ifdef false
#       define CAGENT_RAND64(agent, source) ( \
            agent->aux_rand_value = agent->rand64(), \
            Debug(this,  \
                Append("rand64() called: from ", source, ", counter = ", agent->aux_rand_counter++) \
                .Hex().ShowBase() \
                .Append(", seed = ").Append(agent->sysSeed()) \
                .Append(", value = ").Append(agent->aux_rand_value).EndLine() \
            ), \
            agent->aux_rand_value)
#   endif

#   ifndef CAGENT_RAND64
#       define CAGENT_RAND64(agent, source) (agent->rand64())
#   endif


    class InflightCMO {
    public:
        uint64_t        timeStamp;
        paddr_t         address;
        TLOpcodeA       opcode;
        bool            fired;

    public:
        InflightCMO() noexcept = default;
        InflightCMO(uint64_t timeStamp, paddr_t address, TLOpcodeA opcode) noexcept;
    };

    class CMOLocalStatus {
    private:
        std::unordered_map<paddr_t, InflightCMO>    inflight;

    public:
        void            setInflight(const TLLocalContext* ctx, paddr_t address, TLOpcodeA opcode);
        void            freeInflight(const TLLocalContext* ctx, paddr_t address);
        bool            isInflight(const TLLocalContext* ctx, paddr_t address) const noexcept;
        bool            hasInflight() const noexcept;
        InflightCMO     firstInflight() const noexcept;

        size_t          inflightCount() const noexcept;

        void            setFired(const TLLocalContext* ctx, paddr_t address);

        InflightCMO     query(const TLLocalContext* ctx, paddr_t address) const;
        void            update(const TLLocalContext* ctx, paddr_t address, InflightCMO entry);

        void            checkTimeout(const TLLocalContext* ctx);
    };


    class CAgent : public BaseAgent<> {
    public:
        using LocalScoreBoard       = ScoreBoard<paddr_t, C_SBEntry, ScoreBoardUpdateCallbackCSBEntry<paddr_t>>;
        using IDMapScoreBoard       = ScoreBoard<paddr_t, C_IDEntry, ScoreBoardUpdateCallbackCIDEntry<paddr_t>>;
        using AcquirePermScoreBoard = ScoreBoard<paddr_t, C_AcquirePermEntry>;

    private:
        uint64_t *cycles;
        PendingTrans<BundleChannelA<ReqField, EchoField, DATASIZE>> pendingA;
        PendingTrans<BundleChannelB> pendingB;
        PendingTrans<BundleChannelC<ReqField, EchoField, DATASIZE>> pendingC;
        PendingTrans<BundleChannelD<RespField, EchoField, DATASIZE>> pendingD;
        PendingTrans<BundleChannelE> pendingE;
        /* Here we need a scoreboard called localBoard maintaining address->info
         * For convenience, an idMap(id->addr) is also maintained
         */
        LocalScoreBoard*        localBoard;
        IDMapScoreBoard*        idMap;
        CMOLocalStatus*         localCMOStatus;
        /*
        * *NOTICE:
        *   L1D infers that all following Release must obtain dirty data (must be ReleaseData)
        *   after the AcquirePerm with the same address.
        *   Besides, this is not straight-forward for inclusive system simulation, so the
        *   AcquirePermScoreBoard must be maintained.
        */
        bool                    lastProbeAfterRelease;
        paddr_t                 lastProbeAfterReleaseAddress;
        AcquirePermScoreBoard*  acquirePermBoard;
        IDPool probeIDpool;
        void timeout_check() override;

    public:
        CAgent(TLLocalConfig* cfg, MemoryBackend* mem, GlobalBoard<paddr_t>* gb, UncachedBoard<paddr_t>* ub, int sys, int id, unsigned int seed, uint64_t* cycles) noexcept;
        virtual ~CAgent() noexcept;

        uint64_t    cycle() const noexcept override;

        Resp send_a     (std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>>&    a) override;
        void handle_b   (std::shared_ptr<BundleChannelB>&                                   b) override;
        Resp send_c     (std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>>&    c) override;
        Resp send_e     (std::shared_ptr<BundleChannelE>&                                   e);
        void fire_a() override;
        void fire_b() override;
        void fire_c() override;
        void fire_d() override;
        void fire_e() override;
        void handle_channel() override;
        void update_signal() override;

        void onGrant(GrantEvent& event);

        bool do_acquireBlock(paddr_t address, TLParamAcquire param, int alias);
        bool do_acquirePerm(paddr_t address, TLParamAcquire param, int alias);
        bool do_releaseData(paddr_t address, TLParamRelease param, shared_tldata_t<DATASIZE> data, int alias);
        bool do_releaseDataAuto(paddr_t address, int alias, bool dirty, bool forced);

        bool do_cbo(TLOpcodeA opcode, paddr_t address, bool alwaysHit);
        bool do_cbo_clean(paddr_t address, bool alwaysHit);
        bool do_cbo_flush(paddr_t address, bool alwaysHit);
        bool do_cbo_inval(paddr_t address, bool alwaysHit);

        LocalScoreBoard*        local() noexcept;
        const LocalScoreBoard*  local() const noexcept;
    };
}


#endif //TLC_TEST_CAGENT_H
