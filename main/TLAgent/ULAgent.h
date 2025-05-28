//
// Created by ljw on 10/21/21.
//

#ifndef TLC_TEST_ULAGENT_H
#define TLC_TEST_ULAGENT_H

#include "BaseAgent.h"
#include "../Utils/Common.h"
#include "../Utils/ScoreBoard.h"
#include "Bundle.h"
#include "CAgent.h"

namespace tl_agent {

    class UL_SBEntry {
    public:
        paddr_t address;
        TLOpcodeA req_type;
        int status;
        uint64_t time_stamp;
        std::array<uint8_t, DATASIZE> data;
        UL_SBEntry(const TLLocalContext* ctx, TLOpcodeA req_type, int status, paddr_t address) {
            this->req_type = req_type;
            this->status = status;
            this->address = address;
            this->time_stamp = ctx->cycle();
        }
        UL_SBEntry(const TLLocalContext* ctx, TLOpcodeA req_type, int status, paddr_t address, std::array<uint8_t, DATASIZE> data) {
            this->req_type = req_type;
            this->status = status;
            this->address = address;
            this->time_stamp = ctx->cycle();
            this->data = data;
        }
        void update_status(const TLLocalContext* ctx, int status) {
            this->status = status;
            this->time_stamp = ctx->cycle();
        }
    };

    template<typename Tk>
    struct ScoreBoardUpdateCallbackULSBEntry : public ScoreBoardUpdateCallback<Tk, tl_agent::UL_SBEntry>
    {
        void update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<tl_agent::UL_SBEntry>& data)
        {
#           if SB_DEBUG == 1

#           endif
        }
    };


    class ULAgent : public BaseAgent<> {
    public:
        using LocalScoreBoard = ScoreBoard<int, UL_SBEntry, ScoreBoardUpdateCallbackULSBEntry<int>>;

    private:
        uint64_t* cycles;
        PendingTrans<BundleChannelA<ReqField, EchoField, DATASIZE>> pendingA;
        PendingTrans<BundleChannelD<RespField, EchoField, DATASIZE>> pendingD;
        /* We only need a localBoard recording SourceID -> UL_SBEntry
         * because UL agent needn't store data.
         */
        LocalScoreBoard*    localBoard; // SourceID -> UL_SBEntry
        void timeout_check() override;

    public:
        ULAgent(TLLocalConfig* cfg, MemoryBackend* mem, GlobalBoard<paddr_t>* gb, UncachedBoard<paddr_t>* ub, int sys, int id, unsigned int seed, uint64_t* cycles) noexcept;
        virtual ~ULAgent() noexcept;

        uint64_t    cycle() const noexcept override;

        Resp send_a     (std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>>&    a) override;
        void handle_b   (std::shared_ptr<BundleChannelB>&                                   b) override;
        Resp send_c     (std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>>&    c) override;
        void fire_a() override;
        void fire_b() override;
        void fire_c() override;
        void fire_d() override;
        void fire_e() override;
        void handle_channel() override;
        void update_signal() override;
        ActionDenialEnum do_getAuto         (paddr_t address);
        ActionDenialEnum do_get             (paddr_t address, uint8_t size, uint32_t mask);
        ActionDenialEnum do_putfulldata     (paddr_t address, shared_tldata_t<DATASIZE> data);
        ActionDenialEnum do_putpartialdata  (paddr_t address, uint8_t size, uint32_t mask, shared_tldata_t<DATASIZE> data);
    };

}

#endif //TLC_TEST_ULAGENT_H
