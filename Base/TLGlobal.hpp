#pragma once

#ifndef TLC_TEST_TLCONFIG_H
#define TLC_TEST_TLCONFIG_H


#define TLTP_LOG_GLOBAL(str_proc) \
    { std::cout << Gravity::StringAppender().Append("[tl-test-new] ", __FILE__, ": ").str_proc.ToString(); }
    

struct TLGlobalConfiguration {

    bool                    verbose;
    bool                    verbose_detailed_dpi;
    bool                    verbose_data_full;
};

struct TLGlobalContext {
};

struct TLGlobal {

    TLGlobalConfiguration   cfg;
    TLGlobalContext         ctx;
};


extern TLGlobal glbl;


#endif //TLC_TEST_TLCONFIG_H
