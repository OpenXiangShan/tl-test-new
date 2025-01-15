//
// Created by ljw on 10/21/21.
//

#ifndef TLC_TEST_MAGENT_H
#define TLC_TEST_MAGENT_H

#include "BaseAgent.h"
#include "../Utils/Common.h"
#include "../Utils/ScoreBoard.h"
#include "Bundle.h"
#include "CAgent.h"
#include "ULAgent.h"

namespace tl_agent {

    class MAgent : public BaseAgent<> {
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
        MAgent(TLLocalConfig* cfg, GlobalBoard<paddr_t>* gb, UncachedBoard<paddr_t>* ub, int sys, int id, unsigned int seed, uint64_t* cycles) noexcept;
        virtual ~MAgent() noexcept;

        uint64_t    cycle() const noexcept override;

        Resp send_a     (std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>>&    a) override;
        void handle_b   (std::shared_ptr<BundleChannelB>&                                   b) override;
        Resp send_c     (std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>>&    c) override;
        void fire_a() override;
        void fire_b() override;
        void fire_c() override;
        bool is_d_fired() override;
        void fire_d() override;
        void fire_e() override;
        void handle_channel() override;
        void update_signal() override;
        bool do_getAuto         (paddr_t address);
        bool do_get             (paddr_t address, uint8_t size, uint32_t mask);
        bool do_putfulldata     (paddr_t address, shared_tldata_t<DATASIZE> data);
        bool do_putpartialdata  (paddr_t address, uint8_t size, uint32_t mask, shared_tldata_t<DATASIZE> data);
    };

}

#endif //TLC_TEST_MAGENT_H
