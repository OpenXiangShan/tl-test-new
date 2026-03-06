#include "ChiselDB.hpp"

#include "../Utils/autoinclude.h"
#include "../Utils/Common.h"

#include AUTOINCLUDE_CHISELDB(chisel_db.cpp)
#include AUTOINCLUDE_CHISELDB(perfCCT.cpp)

extern "C" {
#define DEFINE_ROLLING_DPI_STUB(name)     void name(long long, long long, long long, const char*) {}

DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracyBOP_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracyPBOP_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracySMS_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracyStream_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracyStride_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracyTP_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchAccuracy_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoverageBOP_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoveragePBOP_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoverageSMS_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoverageStream_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoverageStride_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoverageTP_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchCoverage_rolling_0_write)
DEFINE_ROLLING_DPI_STUB(L2PrefetchLate_rolling_0_write)

#undef DEFINE_ROLLING_DPI_STUB
}


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
        const char* file = "chiseldb.db";

        LogInfo("ChiselDB", Append("Saving DB to: ", file).EndLine());

        SaveDB(file);

        LogInfo("ChiselDB", Append("Saved DB").EndLine());
        LogInfo("ChiselDB", Append("Disabled").EndLine());
    }
}
