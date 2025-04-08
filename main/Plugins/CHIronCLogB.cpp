#include "CHIronCLogB.hpp"

#include "../CHIron/clogdpi_b.hpp"
#include "../Utils/gravity_utility.hpp"
#include "../Utils/Common.h"

//
static constexpr const char* SHARED_NAME[] = {
    "l3top"
};

//
bool CHIron::CLogB::Init(const char *file)
{
    for (int i = 0; i < (sizeof(SHARED_NAME) / sizeof(SHARED_NAME[0])); i++)
    {
        uint32_t status;
        void* handle = CLogB_OpenFile(Gravity::StringAppender()
            .Append(file, ".", SHARED_NAME[i] , ".b").ToString().c_str(), &status);

        if (status)
            return false;
        
        CLogB_ShareHandle(SHARED_NAME[i], handle);
    }

    return true;
}

void CHIron::CLogB::Close()
{
    for (int i = 0; i < (sizeof(SHARED_NAME) / sizeof(SHARED_NAME[0])); i++)
    {
        void* handle = CLogB_GetSharedHandle(SHARED_NAME[i]);

        if (handle)
        {
            CLogB_CloseFile(handle);
            CLogB_UnshareHandle(SHARED_NAME[i]);
        }
    }
}


// Implementation of: class PluginInstance
namespace CHIron::CLogB {

    PluginInstance::PluginInstance() noexcept
        : Plugin    ("chiron.clog.b")
    { }

    std::string PluginInstance::GetDisplayName() const noexcept
    {
        return "CHIron::CLogB";
    }

    std::string PluginInstance::GetDescription() const noexcept
    {
        return "CHIron CLog.B Compatibility Plugin for TL-Test New";
    }

    std::string PluginInstance::GetVersion() const noexcept
    {
        return "Kunming Lake";
    }

    void PluginInstance::OnEnable()
    {
        LogInfo("CHIron::CLogB", Append("Enabled").EndLine());

        Init("clog");

        LogInfo("CHIron::CLogB", Append("Initialized").EndLine());
    }

    void PluginInstance::OnDisable()
    {
        LogInfo("CHIron::CLogB", Append("Saving CLog.B to: clog.*.b").EndLine());

        Close();

        LogInfo("CHIron::CLogB", Append("Disabled").EndLine());
    }
}
