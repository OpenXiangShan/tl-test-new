#ifndef TLC_TEST_MAGENT_H
#define TLC_TEST_MAGENT_H

#include "BaseAgent.h"
#include "Bundle.h"
#include "CAgent.h"
#include "ULAgent.h"

namespace tl_agent {

class MAgent : public BaseAgent<> {
public:
    using LocalScoreBoard = ScoreBoard<int, UL_SBEntry, ScoreBoardUpdateCallbackULSBEntry<int>>;

private:
    uint64_t* cycles;
    uint32_t readAckBeatCnt;
    PendingTrans<BundleChannelA<ReqField, EchoField, DATASIZE>> pendingA;
    PendingTrans<BundleChannelD<RespField, EchoField, DATASIZE>> pendingD;
    LocalScoreBoard* localBoard;

    void timeout_check() override;

public:
    MAgent(TLLocalConfig* cfg,
           MemoryBackend* mem,
           GlobalBoard<paddr_t>* gb,
           UncachedBoard<paddr_t>* ub,
           int sys,
           int id,
           unsigned int seed,
           uint64_t* cycles) noexcept;
    virtual ~MAgent() noexcept;

    uint64_t cycle() const noexcept override;

    Resp send_a(std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE>>& a) override;
    void handle_b(std::shared_ptr<BundleChannelB>& b) override;
    Resp send_c(std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE>>& c) override;
    void fire_a() override;
    void fire_b() override;
    void fire_c() override;
    void fire_d() override;
    void fire_m();
    void fire_e() override;
    void handle_channel() override;
    void update_signal() override;

    bool is_d_fired();
    bool is_m_fired();
    bool recv_readAck();
    bool recv_writeAck();

    ActionDenialEnum do_getAuto(paddr_t address, bool modify = false);
    ActionDenialEnum do_get(paddr_t address, uint8_t size, uint32_t mask);
    ActionDenialEnum do_putfulldata(paddr_t address, shared_tldata_t<DATASIZE> data);
};

} // namespace tl_agent

#endif // TLC_TEST_MAGENT_H
