#include "CMOAgent.h"
#include "BaseAgent.h"


// Implementation of: class InflightCMO
namespace tl_agent {
    /*
    uint64_t        timeStamp;
    paddr_t         address;
    cmoop_t         opcode;
    bool            fired;
    */

    InflightCMO::InflightCMO(uint64_t timeStamp, paddr_t address, cmoop_t opcode) noexcept
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

    void CMOLocalStatus::setInflight(const TLLocalContext* ctx, paddr_t address, cmoop_t opcode)
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
                tlc_assert(false, ctx, Gravity::StringAppender().Hex().ShowBase()
                    .Append("Multiple CMO in-flight on set")
                    .ToString());
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


// Implementation of: class CMOResponseEvent
namespace tl_agent {
    /*
    const int               id;
    const cmoop_t           opcode;
    const paddr_t           address;
    */

    CMOResponseEvent::CMOResponseEvent(int id, cmoop_t opcode, paddr_t address) noexcept
        : id        (id)
        , opcode    (opcode)
        , address   (address)
    { }
}


// Implementation of: class CMOAgent
namespace tl_agent {
    /*
    TLLocalConfig*          cfg;
    const int               id;
    const unsigned int      seed;
    cmoport_t*              port;
    CMOLocalStatus*         localStatus;

    std::mt19937_64         rand;
    */

    CMOAgent::CMOAgent(TLLocalConfig* cfg, int sysId, unsigned int seed, uint64_t* cycles) noexcept
        : cfg           (cfg)
        , id            (sysId)
        , seed          (seed)
        , port          (nullptr)
        , localStatus   (new CMOLocalStatus)
        , rand          (sysId + seed)
        , cycles        (cycles)
        , pendingReq    ()
        , pendingResp   ()
    { }

    CMOAgent::~CMOAgent() noexcept
    {
        delete localStatus;
    }
    
    int CMOAgent::sys() const noexcept
    {
        return id;
    }

    int CMOAgent::sysId() const noexcept
    {
        return id;
    }

    unsigned int CMOAgent::sysSeed() const noexcept
    {
        return seed;
    }

    TLLocalConfig& CMOAgent::config() noexcept
    {
        return *cfg;
    }

    const TLLocalConfig& CMOAgent::config() const noexcept
    {
        return *cfg;
    }

    uint64_t CMOAgent::cycle() const noexcept
    {
        return *cycles;
    }

    CMOLocalStatus* CMOAgent::local() noexcept
    {
        return localStatus;
    }

    const CMOLocalStatus* CMOAgent::local() const noexcept
    {
        return localStatus;
    }

    Resp CMOAgent::send_req(std::shared_ptr<BundleCMOReq> req)
    {
        if (localStatus->hasInflight())
            return FAIL;

        switch (req->opcode)
        {
            case CBO_CLEAN:
            case CBO_FLUSH:
            case CBO_INVAL:
                localStatus->setInflight(this, req->address, req->opcode);
                break;

            default:
                tlc_assert(false, this,
                    Gravity::StringAppender().Hex().ShowBase()
                        .Append("Unknown opcode for sending CMO: ", uint64_t(req->opcode))
                    .ToString());
        }
        this->port->req.opcode  = req->opcode;
        this->port->req.address = req->address;
        this->port->req.valid   = true;

        return OK;
    }

    void CMOAgent::fire_req()
    {
        if (this->port->req.fire())
        {
            auto& req = this->port->req;

            switch (req.opcode)
            {
                case CBO_CLEAN:
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire req] [cbo.clean] ")
                            .Append("addr: ", uint64_t(req.address))
                            .EndLine());
                    }
                    break;

                case CBO_FLUSH:
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire req] [cbo.flush] ")
                            .Append("addr: ", uint64_t(req.address))
                            .EndLine());
                    }
                    break;

                case CBO_INVAL:
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire req] [cbo.inval] ")
                            .Append("addr: ", uint64_t(req.address))
                            .EndLine());
                    }
                    break;

                default:
                    tlc_assert(false, this,
                        Gravity::StringAppender().Hex().ShowBase()
                            .Append("Unknown opcode for firing CMO req: ", uint64_t(req.opcode))
                        .ToString());
            }

            localStatus->setFired(this, req.address);

            req.valid = false;
            pendingReq.update(this);
        }
    }

    void CMOAgent::fire_resp()
    {
        if (this->port->resp.fire())
        {
            auto& resp = this->port->resp;

            if (localStatus->inflightCount() != 1)
                tlc_assert(false, this, "zero or multiple in-flight CMO");

            auto status = localStatus->firstInflight();

            switch (status.opcode)
            {
                case CBO_CLEAN:
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire resp] [cbo.clean] ")
                            .Append("addr: ", uint64_t(status.address))
                            .EndLine());
                    }
                    break;

                case CBO_FLUSH:
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire resp] [cbo.flush] ")
                            .Append("addr: ", uint64_t(status.address))
                            .EndLine());
                    }
                    break;

                case CBO_INVAL:
                    if (glbl.cfg.verbose_xact_fired)
                    {
                        Log(this, Hex().ShowBase()
                            .Append("[CMO] [fire resp] [cbo.inval] ")
                            .Append("addr: ", uint64_t(status.address))
                            .EndLine());
                    }
                    break;

                default:
                    tlc_assert(false, this,
                        Gravity::StringAppender().Hex().ShowBase()
                            .Append("Unknown opcode for firing CMO resp: ", uint64_t(status.opcode))
                        .ToString());
            }

            if (!status.fired)
            {
                tlc_assert(false, this,
                    Gravity::StringAppender().Hex().ShowBase()
                        .Append("in-flight CMO not fired yet")
                    .ToString());
            }

            // fire events to notify all other TL-C Agents
            CMOResponseEvent(id, status.opcode, status.address).Fire();

            localStatus->freeInflight(this, status.address);
        }
    }

    void CMOAgent::handle_channel()
    {
        fire_req();
        fire_resp();
    }

    void CMOAgent::update_signal()
    {
        this->port->resp.ready = true;

        if (pendingReq.is_pending())
            send_req(pendingReq.info);
        else
            this->port->req.valid = false;

        if (*this->cycles % TIMEOUT_INTERVAL == 0)
            this->timeout_check();
    }

    bool CMOAgent::do_cbo_clean(paddr_t address)
    {
        if (pendingReq.is_pending())
            return false;

        if (localStatus->hasInflight())
            return false;

        auto req = std::make_shared<BundleCMOReq>();
        req->opcode     = CBO_CLEAN;
        req->address    = address;

        pendingReq.init(req, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[CMO] [sequenced req] [cbo.clean] ")
                .Append("addr: ", uint64_t(req->address))
                .EndLine());
        }

        return true;
    }

    bool CMOAgent::do_cbo_flush(paddr_t address)
    {
        if (pendingReq.is_pending())
            return false;

        if (localStatus->hasInflight())
            return false;

        auto req = std::make_shared<BundleCMOReq>();
        req->opcode     = CBO_FLUSH;
        req->address    = address;

        pendingReq.init(req, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[CMO] [sequenced req] [cbo.flush] ")
                .Append("addr: ", uint64_t(req->address))
                .EndLine());
        }

        return true;
    }

    bool CMOAgent::do_cbo_inval(paddr_t address)
    {
        if (pendingReq.is_pending())
            return false;

        if (localStatus->hasInflight())
            return false;

        auto req = std::make_shared<BundleCMOReq>();
        req->opcode     = CBO_INVAL;
        req->address    = address;

        pendingReq.init(req, 1);

        if (glbl.cfg.verbose_xact_sequenced)
        {
            Log(this, Hex().ShowBase()
                .Append("[CMO] [sequenced req] [cbo.inval] ")
                .Append("addr: ", uint64_t(req->address))
                .EndLine());
        }

        return true;
    }

    void CMOAgent::connect(cmoport_t* port) noexcept
    {
        this->port = port;
    }

    uint64_t CMOAgent::rand64() noexcept
    {
        return rand();
    }

    void CMOAgent::timeout_check()
    {
        // don't do timeout check for now
    }

    bool CMOAgent::mainSys() const noexcept
    {
        return false;
    }
}
