#include "ChiselDB.hpp"

#include "../Utils/autoinclude.h"
#include "../Utils/Common.h"

#include AUTOINCLUDE_CHISELDB(chisel_db.cpp)


//
void ChiselDB::InitDB()
{
    init_db(true, false, "");
}

//
void ChiselDB::SaveDB(const char* file)
{
    save_db(file);
}


// Implementation of: class PluginInstance
namespace ChiselDB {

    PluginInstance::PluginInstance() noexcept
        : Plugin    ("chiseldb")
    { }

    std::string PluginInstance::GetDisplayName() const noexcept
    {
        return "ChiselDB";
    }

    std::string PluginInstance::GetDescription() const noexcept
    {
        return "ChiselDB Compatibility Plugin for TL-Test New";
    }

    std::string PluginInstance::GetVersion() const noexcept
    {
        return "Kunming Lake";
    }

    void PluginInstance::OnEnable()
    {
        LogInfo("ChiselDB", Append("Enabled").EndLine());

        InitDB();

        LogInfo("ChiselDB", Append("DB Initialized").EndLine());
    }

    void PluginInstance::OnDisable()
    {
        const char* file = "logs/chiseldb.db";

        LogInfo("ChiselDB", Append("Saving DB to: ", file).EndLine());

        SaveDB(file);

        LogInfo("ChiselDB", Append("Saved DB").EndLine());
        LogInfo("ChiselDB", Append("Disabled").EndLine());
    }
}
