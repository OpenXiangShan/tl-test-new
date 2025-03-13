#include "TLSystem.hpp"

#include <algorithm>
#include <cctype>

#include "../Utils/inicpp.hpp"

#include "../Plugins/ChiselDB.hpp"


void TLInitialize(TLSequencer** tltest, PluginManager** plugins, std::function<void(TLLocalConfig&)> tlcfgInit)
{
    // internal event handlers
    Gravity::RegisterListener(
        Gravity::MakeListener(
            "tltest.logger.plugins.enable", 0,
            (std::function<void(PluginEvent::PostEnable&)>)
            [] (PluginEvent::PostEnable& event) -> void {
                LogInfo("PIM", Append("---PluginManager------------------------------------------------").EndLine());
                LogInfo("PIM", Append("Enabled Plugin:").EndLine());
                LogInfo("PIM", Append(" - name          : ", event.GetPlugin()->GetName()).EndLine());
                LogInfo("PIM", Append(" - display_name  : ", event.GetPlugin()->GetDisplayName()).EndLine());
                LogInfo("PIM", Append(" - version       : ", event.GetPlugin()->GetVersion()).EndLine());
                LogInfo("PIM", Append(" - description   : ", event.GetPlugin()->GetDescription()).EndLine());
                LogInfo("PIM", Append("----------------------------------------------------------------").EndLine());
            }
        )
    );

    Gravity::RegisterListener(
        Gravity::MakeListener(
            "tltest.logger.plugins.disable", 0,
            (std::function<void(PluginEvent::PostDisable&)>)
            [] (PluginEvent::PostDisable& event) -> void {
                LogInfo("PIM", Append("---PluginManager------------------------------------------------").EndLine());
                LogInfo("PIM", Append("Disabled Plugin:").EndLine());
                LogInfo("PIM", Append(" - name          : ", event.GetPlugin()->GetName()).EndLine());
                LogInfo("PIM", Append(" - display_name  : ", event.GetPlugin()->GetDisplayName()).EndLine());
                LogInfo("PIM", Append(" - version       : ", event.GetPlugin()->GetVersion()).EndLine());
                LogInfo("PIM", Append(" - description   : ", event.GetPlugin()->GetDescription()).EndLine());
                LogInfo("PIM", Append("----------------------------------------------------------------").EndLine());
            }
        )
    ); 

    // system initialization
    TLLocalConfig tlcfg;
    tlcfg.seed                          = TLTEST_DEFAULT_SEED;
    tlcfg.coreCount                     = TLTEST_DEFAULT_CORE_COUNT;
    tlcfg.masterCountPerCoreTLC         = TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLC;
    tlcfg.masterCountPerCoreTLUL        = TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLUL;

    tlcfg.startupCycle                  = TLTEST_DEFAULT_STARTUP_CYCLE;

    tlcfg.memoryEnable                  = TLTEST_DEFAULT_MEMORY_ENABLE;
    tlcfg.memoryStart                   = TLTEST_DEFAULT_MEMORY_START;
    tlcfg.memoryEnd                     = TLTEST_DEFAULT_MEMORY_END;
    tlcfg.memoryOOOR                    = TLTEST_DEFAULT_MEMORY_OOOR;
    tlcfg.memorySyncStrict              = TLTEST_DEFAULT_MEMORY_SYNC_STRICT;

    tlcfg.mmioEnable                    = TLTEST_DEFAULT_MMIO_ENABLE;
    tlcfg.mmioStart                     = TLTEST_DEFAULT_MMIO_START;
    tlcfg.mmioEnd                       = TLTEST_DEFAULT_MMIO_END;

    tlcfg.cmoEnable                     = TLTEST_DEFAULT_CMO_ENABLE;
    tlcfg.cmoEnableCBOClean             = TLTEST_DEFAULT_CMO_ENABLE_CBO_CLEAN;
    tlcfg.cmoEnableCBOFlush             = TLTEST_DEFAULT_CMO_ENABLE_CBO_FLUSH;
    tlcfg.cmoEnableCBOInval             = TLTEST_DEFAULT_CMO_ENABLE_CBO_INVAL;
    tlcfg.cmoStart                      = TLTEST_DEFAULT_CMO_START;
    tlcfg.cmoEnd                        = TLTEST_DEFAULT_CMO_END;
    tlcfg.cmoParallelDepth              = TLTEST_DEFAULT_CMO_PARALLEL_DEPTH;

    tlcfg.fuzzARIInterval               = CFUZZER_RANGE_ITERATE_INTERVAL;
    tlcfg.fuzzARITarget                 = CFUZZER_RANGE_ITERATE_TARGET;

    tlcfg.fuzzStreamInterval            = CFUZZER_FUZZ_STREAM_INTERVAL;
    tlcfg.fuzzStreamStep                = CFUZZER_FUZZ_STREAM_STEP;
    tlcfg.fuzzStreamStart               = CFUZZER_FUZZ_STREAM_START;
    tlcfg.fuzzStreamEnd                 = CFUZZER_FUZZ_STREAM_END;

    tlcfgInit(tlcfg);

    glbl.cfg.verbose                    = false;
    glbl.cfg.verbose_xact_fired         = false;
    glbl.cfg.verbose_xact_sequenced     = false;
    glbl.cfg.verbose_xact_data_complete = false;
    glbl.cfg.verbose_memory_axi_write   = false;
    glbl.cfg.verbose_memory_axi_read    = false;
    glbl.cfg.verbose_data_full          = false;
    glbl.cfg.verbose_agent_debug        = false;

    // read configuration override
    inicpp::IniManager ini("tltest.ini");

#   define INI_OVERRIDE_INT(section_name, key, target) \
    { \
        auto section = ini[section_name]; \
        if (section.isKeyExist(key)) \
        { \
            target = section.toInt(key); \
            LogInfo("INI", \
                Append("Configuration \'" #target "\' overrided by " section_name ":" key " = ", uint64_t(target), ".").EndLine()); \
        } \
    } \

    INI_OVERRIDE_INT("tltest.logger", "verbose",                    glbl.cfg.verbose);
    INI_OVERRIDE_INT("tltest.logger", "verbose.xact_fired",         glbl.cfg.verbose_xact_fired);
    INI_OVERRIDE_INT("tltest.logger", "verbose.xact_sequenced",     glbl.cfg.verbose_xact_sequenced);
    INI_OVERRIDE_INT("tltest.logger", "verbose.xact_data_complete", glbl.cfg.verbose_xact_data_complete);
    INI_OVERRIDE_INT("tltest.logger", "verbose.memory.axi_write",   glbl.cfg.verbose_memory_axi_write);
    INI_OVERRIDE_INT("tltest.logger", "verbose.memory.axi_read",    glbl.cfg.verbose_memory_axi_read);
    INI_OVERRIDE_INT("tltest.logger", "verbose.data_full",          glbl.cfg.verbose_data_full);
    INI_OVERRIDE_INT("tltest.logger", "verbose.agent_debug",        glbl.cfg.verbose_agent_debug);

    INI_OVERRIDE_INT("tltest.config", "core",                       tlcfg.coreCount);
    INI_OVERRIDE_INT("tltest.config", "core.tl_c",                  tlcfg.masterCountPerCoreTLC);
    INI_OVERRIDE_INT("tltest.config", "core.tl_ul",                 tlcfg.masterCountPerCoreTLUL);

    INI_OVERRIDE_INT("tltest.config", "startup.cycle",              tlcfg.startupCycle);

    INI_OVERRIDE_INT("tltest.config", "memory.enable",              tlcfg.memoryEnable);
    INI_OVERRIDE_INT("tltest.config", "memory.start",               tlcfg.memoryStart);
    INI_OVERRIDE_INT("tltest.config", "memory.end",                 tlcfg.memoryEnd);
    INI_OVERRIDE_INT("tltest.config", "memory.backend.ooor",        tlcfg.memoryOOOR);
    INI_OVERRIDE_INT("tltest.config", "memory.sync.strict",         tlcfg.memorySyncStrict);

    INI_OVERRIDE_INT("tltest.config", "mmio.enable",                tlcfg.mmioEnable);
    INI_OVERRIDE_INT("tltest.config", "mmio.start",                 tlcfg.mmioStart);
    INI_OVERRIDE_INT("tltest.config", "mmio.end",                   tlcfg.mmioEnd);

    INI_OVERRIDE_INT("tltest.config", "cmo.enable",                 tlcfg.cmoEnable);
    INI_OVERRIDE_INT("tltest.config", "cmo.enable.cbo.clean",       tlcfg.cmoEnableCBOClean);
    INI_OVERRIDE_INT("tltest.config", "cmo.enable.cbo.flush",       tlcfg.cmoEnableCBOFlush);
    INI_OVERRIDE_INT("tltest.config", "cmo.enable.cbo.inval",       tlcfg.cmoEnableCBOInval);
    INI_OVERRIDE_INT("tltest.config", "cmo.start",                  tlcfg.cmoStart);
    INI_OVERRIDE_INT("tltest.config", "cmo.end",                    tlcfg.cmoEnd);
    INI_OVERRIDE_INT("tltest.config", "cmo.parallel.depth",         tlcfg.cmoParallelDepth);

    INI_OVERRIDE_INT("tltest.fuzzer", "seed",                       tlcfg.seed);
    INI_OVERRIDE_INT("tltest.fuzzer", "ari.interval",               tlcfg.fuzzARIInterval);
    INI_OVERRIDE_INT("tltest.fuzzer", "ari.target",                 tlcfg.fuzzARITarget);
    INI_OVERRIDE_INT("tltest.fuzzer", "stream.interval",            tlcfg.fuzzStreamInterval);
    INI_OVERRIDE_INT("tltest.fuzzer", "stream.step",                tlcfg.fuzzStreamStep);
    INI_OVERRIDE_INT("tltest.fuzzer", "stream.start",               tlcfg.fuzzStreamStart);
    INI_OVERRIDE_INT("tltest.fuzzer", "stream.end",                 tlcfg.fuzzStreamEnd);

#   undef INI_OVERRIDE_INT

    // read sequence mode configurations
    {
        auto section = ini["tltest.sequence"];

        // *NOTICE: (*TODO*) This would just do its job with limitation of maximum
        //          agent count of 2048. :)
        //          Better implementation might be welcomed, but for now with INI
        //          this works just fine.
        for (int i = 0; i < 2048; i++)
        {
            std::string key = Gravity::StringAppender("mode.", i).ToString();
            if (section.isKeyExist(key))
            {
                std::string mode = section.toString(key);

                std::transform(mode.begin(), mode.end(), mode.begin(),
                    [](unsigned char c) { return std::tolower(c); });

                if (mode.compare("passive") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::PASSIVE;
                else if (mode.compare("fuzz_ari") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::FUZZ_ARI;
                else if (mode.compare("fuzz_stream") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::FUZZ_STREAM;
                else if (mode.compare("fuzz_counter") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::FUZZ_COUNTER;
                else if (mode.compare("stream_copy2") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::STREAM_COPY2;
                else if (mode.compare("stream_multi") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::STREAM_MULTI;
                else if (mode.compare("bwprof_stream_stride_read") == 0)
                    tlcfg.sequenceModes[i] = TLSequenceMode::BWPROF_STREAM_STRIDE_READ;
                else
                {
                    LogInfo("INI", 
                        Append("Unknown sequence mode \"", mode, "\" for ", key, ", ignored.")
                        .EndLine());

                    continue;
                }

                LogInfo("INI",
                    Append("Overrided sequence mode: ", key, " = ", mode)
                    .EndLine());
            }
        }
    }

    //
    (*tltest) = new TLSequencer;
    (*tltest)->Initialize(tlcfg);

    (*plugins) = new PluginManager;
    (*plugins)->EnablePlugin(new ChiselDB::PluginInstance);
}

void TLFinalize(TLSequencer** tltest, PluginManager** plugins)
{
    (*plugins)->DisableAll([](auto plugin) -> void { delete plugin; });
    delete *plugins;
    
    (*tltest)->Finalize();
    delete *tltest;
}
