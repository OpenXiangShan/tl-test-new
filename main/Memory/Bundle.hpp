#ifndef TLC_TEST_AXI_BUNDLE_H
#define TLC_TEST_AXI_BUNDLE_H

#include "../Utils/Common.h"


namespace axi_agent {

    //
    class DecoupledBundle {
    public:
        uint8_t     valid;
        uint8_t     ready;

        bool fire() const {
            return valid && ready;
        }
    };

    //
    class BundleChannelAW : public DecoupledBundle {
    public:
        uint32_t            id;
        uint64_t            addr;
        uint8_t             len;
        uint8_t             size;
        uint8_t             burst;
    };

    template<std::size_t N>
    class BundleChannelW : public DecoupledBundle {
    public:
        shared_tldata_t<N>  data;
        uint64_t            strb;
        uint8_t             last;
    };

    class BundleChannelB : public DecoupledBundle {
    public:
        uint32_t            id;
        uint8_t             resp;
    };

    //
    class BundleChannelAR : public DecoupledBundle {
    public:
        uint32_t            id;
        uint64_t            addr;
        uint8_t             len;
        uint8_t             size;
        uint8_t             burst;
    };

    template<std::size_t N>
    class BundleChannelR : public DecoupledBundle {
    public:
        uint32_t            id;
        shared_tldata_t<N>  data;
        uint8_t             resp;
        uint8_t             last;
    };

    //
    template<std::size_t N>
    class Bundle {
    public:
        BundleChannelAW     aw;
        BundleChannelW<N>   w;
        BundleChannelB      b;
        BundleChannelAR     ar;
        BundleChannelR<N>   r;
    };
}

#endif // TLC_TEST_AXI_BUNDLE_H
