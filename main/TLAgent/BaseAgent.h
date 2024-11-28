//
// Created by ljw on 10/21/21.
//

#ifndef TLC_TEST_BASEAGENT_H
#define TLC_TEST_BASEAGENT_H

#include <set>
#include <random>
#include <array>
#include "Bundle.h"
#include "../Utils/Common.h"
#include "../Utils/ScoreBoard.h"
#include "../Utils/ULScoreBoard.h"
#include "../Base/TLLocal.hpp"


namespace tl_agent {

    enum Resp {OK, FAIL};

    enum {
        S_INVALID = 0,
        S_VALID,
        S_SENDING_A,                            // ready to send A request actively
        S_SENDING_C,                            // ready to send C request actively
        S_SENDING_A_NESTED_SENDING_C,           // nested ProbeAck/ProbeAckData on sending AcquirePerm/AcquireBlock
        S_A_WAITING_D_NESTED_SENDING_C,         // nested ProbeAck/ProbeAckData on waiting for Grant
    //  S_SENDING_E_NESTED_SENDING_C,           // ** prohibited ** Grant not allowed on pending ProbeAck/ProbeAckData
        S_C_WAITING_D,                          // C wait for D response
        S_A_WAITING_D,                          // A wait for D response
        S_A_WAITING_D_INTR,                     // A wait for D response while probe interrupted
        S_SENDING_E,                            // ready to send E request actively
        S_CBO_SENDING_A,
        S_CBO_A_WAITING_D,
        S_CBO_SENDING_A_NESTED_SENDING_C,
        S_CBO_SENDING_A_NESTED_C_WAITING_D,
        S_CBO_A_WAITING_D_NESTED_SENDING_C,
        S_CBO_A_WAITING_D_NESTED_C_WAITING_D
    };

    inline std::string StatusToString(int status) noexcept
    {
        switch (status)
        {
            case S_INVALID:                             return "S_INVALID";
            case S_VALID:                               return "S_VALID";
            case S_SENDING_A:                           return "S_SENDING_A";
            case S_SENDING_C:                           return "S_SENDING_C";
            case S_C_WAITING_D:                         return "S_C_WAITING_D";
            case S_A_WAITING_D:                         return "S_A_WAITING_D";
            case S_A_WAITING_D_INTR:                    return "S_A_WAITING_D_INTR";
            case S_SENDING_E:                           return "S_SENDING_E";
            case S_CBO_SENDING_A:                       return "S_CBO_SENDING_A";
            case S_CBO_A_WAITING_D:                     return "S_CBO_A_WAITING_D";
            case S_CBO_SENDING_A_NESTED_SENDING_C:      return "S_CBO_SENDING_A_NESTED_SENDING_C";
            case S_CBO_SENDING_A_NESTED_C_WAITING_D:    return "S_CBO_SENDING_A_NESTED_C_WAITING_D";
            case S_CBO_A_WAITING_D_NESTED_SENDING_C:    return "S_CBO_A_WAITING_D_NESTED_SENDING_C";
            case S_CBO_A_WAITING_D_NESTED_C_WAITING_D:  return "S_CBO_A_WAITING_D_NESTED_C_WAITING_D";
            default:                                    return Gravity::StringAppender("<unknown_status:", status, ">").ToString();
        }
    }

    inline std::string StatusToDescription(int status) noexcept
    {
        switch(status)
        {
            case S_INVALID:                             return "invalid";
            case S_VALID:                               return "valid";
            case S_SENDING_A:                           return "ready to send A request actively";
            case S_SENDING_C:                           return "ready to send C request actively";
            case S_C_WAITING_D:                         return "C wait for D response";
            case S_A_WAITING_D:                         return "A wait for D response";
            case S_A_WAITING_D_INTR:                    return "A wait for D response while probe interrupted";
            case S_SENDING_E:                           return "ready to send E request actively";
            case S_CBO_SENDING_A:                       return "ready to send A CBO request actively";
            case S_CBO_A_WAITING_D:                     return "A CBO waiting for D CBO response";
            case S_CBO_SENDING_A_NESTED_SENDING_C:      return "ready to send A CBO nested ready to send C request actively";
            case S_CBO_SENDING_A_NESTED_C_WAITING_D:    return "ready to send A CBO nested C wait for D response";
            case S_CBO_A_WAITING_D_NESTED_SENDING_C:    return "A CBO waiting for D nested ready to send C request actively";
            case S_CBO_A_WAITING_D_NESTED_C_WAITING_D:  return "A CBO waiting for D nested C wait for D response";
            default:                                    return "";
        }
    }

    class ReqField {
    public:
        uint8_t value;
    };

    class RespField {
    public:
        uint8_t value;
    };

    class EchoField {
    public:
        uint8_t value;
    };

    template<typename T>
    class PendingTrans {
    public:
        int beat_cnt;
        int nr_beat;
        std::shared_ptr<T> info;

        PendingTrans() {
            nr_beat = 0;
            beat_cnt = 0;
        }
        ~PendingTrans() = default;

        bool is_multiBeat() { return (this->nr_beat != 1); };
        bool is_pending() { return (beat_cnt != 0); }
        void init(std::shared_ptr<T> &info, int nr_beat) {
            this->info = info;
            this->nr_beat = nr_beat;
            beat_cnt = nr_beat;
        }
        void update(TLLocalContext* ctx) {
            beat_cnt--;
            tlc_assert(beat_cnt >= 0, ctx, "More beats received than expected!");
        }
    };

    class IDPool {
    private:
        std::set<int> *idle_ids;
        std::set<int> *used_ids;
        int pending_freeid;
    public:
        IDPool(int start, int end) {
            idle_ids = new std::set<int>();
            used_ids = new std::set<int>();
            for (int i = start; i < end; i++) {
                idle_ids->insert(i);
            }
            used_ids->clear();
            pending_freeid = -1;
        }
        ~IDPool() {
            delete idle_ids;
            delete used_ids;
        }
        int getid() {
            if (idle_ids->size() == 0)
                return -1;
            int ret = *idle_ids->begin();
            used_ids->insert(ret);
            idle_ids->erase(ret);
            return ret;
        }
        void freeid(int id) {
            this->pending_freeid = id;
        }
        void update(TLLocalContext* ctx) {
            if (pending_freeid != -1) {
                tlc_assert(used_ids->count(pending_freeid) > 0, ctx, "Try to free unused SourceID!");
                used_ids->erase(pending_freeid);
                idle_ids->insert(pending_freeid);
                pending_freeid = -1;
            }
        }
        bool full() {
            return idle_ids->empty();
        }
    };

    template<size_t BeatSize = BEATSIZE, size_t DataSize = DATASIZE> 
    class BaseAgent : public TLLocalContext {
    public:
        using tlport_t = Bundle<ReqField, RespField, EchoField, BeatSize>;

    protected:
        TLLocalConfig*              cfg;
        const int                   core;
        const int                   id;
        tlport_t*                   port;
        GlobalBoard<paddr_t>*       globalBoard;
        UncachedBoard<paddr_t>*     uncachedBoards;
        IDPool                      idpool;
        virtual void timeout_check() = 0;

        const unsigned int      seed;

    private:
        std::mt19937_64         rand;

    public:
#   if AGENT_DEBUG == 1
        uint64_t                aux_rand_counter = 0;
        uint64_t                aux_rand_value;
#   endif

    public:
        inline int                  sys() const noexcept override { return core; }
        inline int                  sysId() const noexcept override { return id; }
        inline unsigned int         sysSeed() const noexcept override { return seed; }

        inline TLLocalConfig&       config() noexcept override { return *cfg; }
        inline const TLLocalConfig& config() const noexcept override { return *cfg; }

        virtual Resp send_a     (std::shared_ptr<BundleChannelA<ReqField, EchoField, DataSize>>&    a) = 0;
        virtual void handle_b   (std::shared_ptr<BundleChannelB>&                                   b) = 0;
        virtual Resp send_c     (std::shared_ptr<BundleChannelC<ReqField, EchoField, DataSize>>&    c) = 0;
        virtual void fire_a() = 0;
        virtual void fire_b() = 0;
        virtual void fire_c() = 0;
        virtual void fire_d() = 0;
        virtual void fire_e() = 0;
        virtual void handle_channel() = 0;
        virtual void update_signal() = 0;
        BaseAgent(TLLocalConfig* cfg, int sys, int sysId, unsigned int seed, unsigned int limitSource): cfg(cfg), core(sys), id(sysId), idpool(0, limitSource), seed(seed), rand(sysId + seed) {};
        BaseAgent(TLLocalConfig* cfg, int sys, int sysId, unsigned int seed): cfg(cfg), core(sys), id(sysId), idpool(0, NR_SOURCEID), seed(seed), rand(sysId + seed) {};
        virtual ~BaseAgent() = default;

        inline void  connect(tlport_t* p){ this->port = p; }

        inline uint64_t rand64() noexcept { return rand(); }
    };

}

#endif //TLC_TEST_BASEAGENT_H
