#pragma once

#ifndef TLC_TEST_MEMORY_AGENT_H
#define TLC_TEST_MEMORY_AGENT_H

#include <cstdint>
#include <vector>
#include <list>
#include <random>

#include "../Base/TLLocal.hpp"

#include "Bundle.hpp"


namespace axi_agent {

    //
    inline constexpr uint8_t BURST_FIXED    = 0b00;
    inline constexpr uint8_t BURST_INCR     = 0b01;
    inline constexpr uint8_t BURST_WRAP     = 0b10;

    //
    template<class TBundle>
    class FiredBundle {
    public:
        uint64_t    time;
        TBundle     bundle;
    };

    using FiredBundleAW     = FiredBundle<BundleChannelAW>;
    using FiredBundleAR     = FiredBundle<BundleChannelAR>;
    using FiredBundleB      = FiredBundle<BundleChannelB>;

    template<std::size_t N>
    using FiredBundleW      = FiredBundle<BundleChannelW<N>>;

    template<std::size_t N>
    using FiredBundleR      = FiredBundle<BundleChannelR<N>>;

    //
    template<size_t BusWidth = BEATSIZE_MEMORY>
    class AXIWriteTransaction {
    public:
        FiredBundleAW                       request;
        std::vector<FiredBundleW<BusWidth>> data;
        FiredBundleB                        response;
    
    public:
        bool                                dataDone;

    public:
        AXIWriteTransaction() noexcept;
        AXIWriteTransaction(const FiredBundleAW& request) noexcept;
    };

    template<size_t BusWidth = BEATSIZE_MEMORY>
    class AXIReadTransaction {
    public:
        FiredBundleAR                       request;
        std::vector<FiredBundleR<BusWidth>> data;

    public:
        size_t                              dataPending;
        size_t                              dataSending;

    public:
        AXIReadTransaction() noexcept;
        AXIReadTransaction(const FiredBundleAR& request) noexcept;
    };

    //
    class MemoryAgent : public TLLocalContext, public MemoryBackend {
    public:
        using axi_port  = Bundle<BEATSIZE_MEMORY>;
        using axiport_t = axi_port;
    
    protected:
        TLLocalConfig*      cfg;

        uint8_t*            pmem;
        bool                pmemExternal;

        uint64_t*           cycles;
        axi_port*           port;

        const unsigned int  seed;
        const int           id;

    private:
        std::mt19937_64     rand;

    protected:
        // *NOTICE: There is no pendingWrites mechanism because all AXI writes
        //          were handled in-order regardless of ID match.
        std::list<FiredBundleAR>                                            pendingReadRequests;
        std::list<std::shared_ptr<AXIReadTransaction<BEATSIZE_MEMORY>>>     pendingReadResponses;

        std::list<std::shared_ptr<AXIWriteTransaction<BEATSIZE_MEMORY>>>    activeWrites;
        std::vector<std::shared_ptr<AXIReadTransaction<BEATSIZE_MEMORY>>>   activeReads;

        std::list<std::shared_ptr<AXIWriteTransaction<BEATSIZE_MEMORY>>>    finishedWrites;

    public:
        MemoryAgent(TLLocalConfig* cfg, unsigned int seed, uint64_t* cycles, int id) noexcept;
        MemoryAgent(TLLocalConfig* cfg, unsigned int seed, uint64_t* cycles, int id, uint8_t* pmem) noexcept;
        virtual ~MemoryAgent() noexcept;

    public:
        virtual uint64_t                cycle() const noexcept override;
        virtual int                     sys() const noexcept override;
        virtual int                     sysId() const noexcept override;
        virtual unsigned int            sysSeed() const noexcept override;

        virtual const TLLocalConfig&    config() const noexcept override;
        virtual TLLocalConfig&          config() noexcept override;

        virtual bool                    mainSys() const noexcept override;

    public:
        uint8_t*            memory() noexcept;
        const uint8_t*      memory() const noexcept;

        virtual uint8_t&    access(paddr_t addr) noexcept override;
        virtual uint8_t     access(paddr_t addr) const noexcept override;

        virtual bool        accessible(paddr_t addr) const noexcept override;

        size_t              size() const noexcept;

        bool                is_w_active(uint32_t wid) const noexcept;
        bool                is_r_active(uint32_t rid) const noexcept;

        void                handle_aw();
        void                handle_w();
        void                send_b();
        void                handle_ar();
        void                send_r();

        void                fire_aw();
        void                fire_w();
        void                fire_b();
        void                fire_ar();
        void                fire_r();

        void                handle_channel();
        void                update_signal();

    public:
        inline void     connect(axiport_t* p) { this->port = p; }
        inline uint64_t rand64() noexcept     { return rand(); }
    };
}

#endif // TLC_TEST_MEMORY_AGENT_H
