#ifndef TLC_TEST_MMIOAGENT_H
#define TLC_TEST_MMIOAGENT_H

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "BaseAgent.h"
#include "ULAgent.h"
#include "Bundle.h"
#include "../Utils/Common.h"


namespace tl_agent {

    class InflightMMIO {
    public:
        bool                                dataCheck;  // timely first in-flight
        paddr_t                             address;
        TLOpcodeA                           opcode;
        uint8_t                             size;
        uint8_t                             mask;
        uint64_t                            timeStamp;
        int                                 status;
        shared_tldata_t<DATASIZE_MMIO>      data;

    public:
        InflightMMIO(const TLLocalContext*  ctx,
                     bool                   dataCheck,
                     TLOpcodeA              opcode,
                     int                    status,
                     paddr_t                address,
                     uint8_t                size,
                     uint8_t                mask) noexcept;

        void updateStatus(const TLLocalContext* ctx, int status) noexcept;
    };

    class MMIOGlobalStatus {
    private:
        std::unordered_set<paddr_t> inflightAddress;
        uint8_t*                    data;
        size_t                      dataSize;
        const TLLocalConfig*        tlcfg;

    public:
        MMIOGlobalStatus(const TLLocalConfig* tlcfg) noexcept;
        ~MMIOGlobalStatus() noexcept;

    public:
        void            setInflight(const TLLocalContext* ctx, paddr_t address);
        void            freeInflight(const TLLocalContext* ctx, paddr_t address);
        bool            isInflight(const TLLocalContext* ctx, paddr_t address) const noexcept;
        bool            isInflightGet(const TLLocalContext* ctx, paddr_t address) const noexcept;
        bool            isInflightPut(const TLLocalContext* ctx, paddr_t address) const noexcept;

        void            updateData(const TLLocalContext* ctx, paddr_t address, uint8_t size, uint8_t mask, uint8_t* data);
        void            verifyData(const TLLocalContext* ctx, paddr_t address, uint8_t size, uint8_t mask, uint8_t* data) const;
    };

    class MMIOLocalStatus {
    private:
        std::unordered_map<uint64_t, std::shared_ptr<InflightMMIO>> inflight;
    
    public:
        void                            update(const TLLocalContext* ctx, uint64_t source, std::shared_ptr<InflightMMIO> mmio) noexcept;
        std::shared_ptr<InflightMMIO>   query(const TLLocalContext* ctx, uint64_t source) const;
        bool                            contains(const TLLocalContext* ctx, uint64_t source) const noexcept;
        void                            erase(const TLLocalContext* ctx, uint64_t source);
        void                            clear() noexcept;
    };

    class MMIOAgent : public BaseAgent<DATASIZE_MMIO, DATASIZE_MMIO> {
    private:
        uint64_t* cycles;
        PendingTrans<BundleChannelA<ReqField, EchoField, DATASIZE_MMIO>> pendingA;
        PendingTrans<BundleChannelD<RespField, EchoField, DATASIZE_MMIO>> pendingD;
        MMIOGlobalStatus* globalStatus;
        MMIOLocalStatus* localStatus;

    public:
        MMIOAgent(TLLocalConfig* cfg, MMIOGlobalStatus* globalStatus, int id, unsigned int seed, uint64_t* cycles) noexcept;
        virtual ~MMIOAgent() noexcept;

        uint64_t    cycle() const noexcept override;

        Resp send_a     (std::shared_ptr<BundleChannelA<ReqField, EchoField, DATASIZE_MMIO>>&   a) override;
        void handle_b   (std::shared_ptr<BundleChannelB>&                                       b) override;
        Resp send_c     (std::shared_ptr<BundleChannelC<ReqField, EchoField, DATASIZE_MMIO>>&   c) override;
        void fire_a() override;
        void fire_b() override;
        void fire_c() override;
        void fire_d() override;
        void fire_e() override;
        void handle_channel() override;
        void update_signal() override;
        bool do_get             (paddr_t address, uint8_t size, uint8_t mask);
        bool do_putfulldata     (paddr_t address, shared_tldata_t<DATASIZE_MMIO> data);
        bool do_putpartialdata  (paddr_t address, uint8_t size, uint8_t mask, shared_tldata_t<DATASIZE_MMIO> data);
    
        void timeout_check() override;
        bool mainSys() const noexcept override;
    };
}

#endif // TLC_TEST_MMIOAGENT_H
