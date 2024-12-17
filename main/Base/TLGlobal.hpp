#pragma once

#ifndef TLC_TEST_TLCONFIG_H
#define TLC_TEST_TLCONFIG_H


#define TLTP_LOG_GLOBAL(str_proc) \
    { std::cout << Gravity::StringAppender().Append("[tl-test-new] ", __FILE__, ": ").str_proc.ToString(); }
    

struct TLGlobalConfiguration {

    bool                    verbose;
    bool                    verbose_xact_fired;
    bool                    verbose_xact_sequenced;
    bool                    verbose_xact_data_complete;
    bool                    verbose_memory_axi_write;
    bool                    verbose_memory_axi_read;
    bool                    verbose_memory_data_full;
    bool                    verbose_data_full;
    bool                    verbose_agent_debug;
};

struct TLGlobalContext {
};

struct TLGlobal {

    TLGlobalConfiguration   cfg;
    TLGlobalContext         ctx;
};


extern TLGlobal glbl;


#endif //TLC_TEST_TLCONFIG_H
