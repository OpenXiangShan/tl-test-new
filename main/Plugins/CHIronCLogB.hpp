#pragma once
//
//
//

#ifndef TLC_TEST_PLUGINS_CHIRON_CLOG_B
#define TLC_TEST_PLUGINS_CHIRON_CLOG_B

#include "PluginManager.hpp"


namespace CHIron::CLogB {

    //
    bool Init(const char* file);
    void Close();

    //
    class PluginInstance : public Plugin {
    public:
        PluginInstance() noexcept;

    public:
        std::string     GetDisplayName() const noexcept override;
        std::string     GetDescription() const noexcept override;
        std::string     GetVersion() const noexcept override;

        void            OnEnable() override;
        void            OnDisable() override;
    };
}



#endif // TLC_TEST_PLUGINS_CHIRON_CLOG_B
