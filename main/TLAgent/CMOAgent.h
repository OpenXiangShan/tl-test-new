#ifndef TLC_TEST_CMOAGENT_H
#define TLC_TEST_CMOAGENT_H

#include <random>
#include <cstdint>
#include <unordered_map>

#include "Bundle.h"
#include "BaseAgent.h"
#include "../Utils/Common.h"
#include "../Utils/gravity_eventbus.hpp"


#ifndef CMOAGENT_RAND64
#   define CMOAGENT_RAND64(agent, source) (agent->rand64())
#endif


namespace tl_agent {

    typedef uint8_t cmoop_t;

    static constexpr cmoop_t    CBO_CLEAN = 0;
    static constexpr cmoop_t    CBO_FLUSH = 1;
    static constexpr cmoop_t    CBO_INVAL = 2;

    class InflightCMO {
    public:
        uint64_t        timeStamp;
        paddr_t         address;
        cmoop_t         opcode;
        bool            fired;

    public:
        InflightCMO() noexcept = default;
        InflightCMO(uint64_t timeStamp, paddr_t address, cmoop_t opcode) noexcept;
    };

    class CMOLocalStatus {
    private:
        std::unordered_map<paddr_t, InflightCMO>    inflight;

    public:
        void            setInflight(const TLLocalContext* ctx, paddr_t address, cmoop_t opcode);
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

    class CMOResponseEvent : public Gravity::Event<CMOResponseEvent> {
    public:
        const int               id;
        const cmoop_t           opcode;
        const paddr_t           address;

    public:
        CMOResponseEvent(int id, cmoop_t opcode, paddr_t address) noexcept;
    };

    class CMOAgent : public TLLocalContext {
    public:
        using cmoport_t = BundleCMO;

    protected:
        TLLocalConfig*          cfg;
        const int               id;
        const unsigned int      seed;
        cmoport_t*              port;
        CMOLocalStatus*         localStatus;
        uint64_t*               cycles;

    private:
        std::mt19937_64         rand;

        PendingTrans<BundleCMOReq>  pendingReq;
        PendingTrans<BundleCMOResp> pendingResp;

    public:
        CMOAgent(TLLocalConfig* cfg, int sysId, unsigned int seed, uint64_t* cycles) noexcept;
        virtual ~CMOAgent() noexcept;

    public:
        int                     sys() const noexcept override;
        int                     sysId() const noexcept override;
        unsigned int            sysSeed() const noexcept override;

        TLLocalConfig&          config() noexcept override;
        const TLLocalConfig&    config() const noexcept override;

        uint64_t                cycle() const noexcept override;

        CMOLocalStatus*         local() noexcept;
        const CMOLocalStatus*   local() const noexcept;

        bool                    mainSys() const noexcept override;

        Resp                    send_req(std::shared_ptr<BundleCMOReq> req);
        void                    fire_req();
        void                    fire_resp();
        void                    handle_channel();
        void                    update_signal();

        bool                    do_cbo_clean(paddr_t address);
        bool                    do_cbo_flush(paddr_t address);
        bool                    do_cbo_inval(paddr_t address);

        void                    connect(cmoport_t* port) noexcept;
        uint64_t                rand64() noexcept;

        void                    timeout_check();
    };
}

#endif // TLC_TEST_CMOAGENT_H
