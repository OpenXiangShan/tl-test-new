//
// Created by Kaifan Wang on 2021/10/27.
//
// Modified by Haonan Ding on 2024/04/11
//

#ifndef TLC_TEST_COMMON_H
#define TLC_TEST_COMMON_H

#include <memory>
#include <cmath>
#include <string>
#include <cstring>
#include <iostream>
#include <assert.h>
#include <unistd.h>
#include <execinfo.h>

#include "../Base/TLGlobal.hpp"
#include "../Base/TLLocal.hpp"

#include "TLDefault.h"
#include "gravity_utility.hpp"
#include "assert.hpp"


enum {
    DATASIZE = 64, // Cache line is 64B
    BEATSIZE = 32,
    NR_SOURCEID = 16,
    TIMEOUT_INTERVAL = 50000
};

//
template<std::size_t N>
using tldata_t          = uint8_t[N];

template<std::size_t N>
class wrapped_tldata_t {
    /* NOTICE: It's disappointing that GCC-11 still doen't obtain full C++20 library support.
    *          And C++20 support is still not that general on today's machine. :(
    *          So we need this wrapper to pseudoly implement C++20 shared_ptr feature.
    */
public:
    tldata_t<N> data;

public:
    operator uint8_t*() noexcept { return data; }
    operator const uint8_t*() const noexcept { return data; };
    operator tldata_t<N>&() noexcept { return data; }
    operator const tldata_t<N>&() const noexcept { return data; }
    uint8_t& operator[](size_t index) noexcept { return data[index]; };
    uint8_t  operator[](size_t index) const noexcept { return data[index]; };
};

template<std::size_t N>
using shared_tldata_t   = std::shared_ptr<wrapped_tldata_t<N>>;

template<std::size_t N>
inline shared_tldata_t<N> make_shared_tldata() noexcept
{
    return std::make_shared<wrapped_tldata_t<N>>();
}

template<std::size_t N>
inline shared_tldata_t<N> make_shared_tldata_zero() noexcept
{
    auto data = make_shared_tldata<N>();
    std::memset(data->data, 0, DATASIZE);
    return data;
}

//

typedef uint64_t paddr_t;


inline std::string GetDeviceName(const TLLocalContext* ctx)
{
    /* Cache the result of GetDeviceName for 64 cores (with 1 TL-C and 2 TL-UL) */
    static constexpr int DEVICE_NAME_CACHE_SIZE = 192;
    static std::string cached[DEVICE_NAME_CACHE_SIZE];

    if (ctx->sysId() < DEVICE_NAME_CACHE_SIZE)
        if (!cached[ctx->sysId()].empty())
            return cached[ctx->sysId()];

    Gravity::StringAppender strapp;

    int64_t coreId  = int64_t(ctx->sysId() / (ctx->config().masterCountPerCoreTLC + ctx->config().masterCountPerCoreTLUL));
    int64_t tlcId   = int64_t(ctx->sysId() % (ctx->config().masterCountPerCoreTLC + ctx->config().masterCountPerCoreTLUL));
    int64_t tlulId  = int64_t(ctx->sysId() % (ctx->config().masterCountPerCoreTLC + ctx->config().masterCountPerCoreTLUL)) - ctx->config().masterCountPerCoreTLC;

    strapp.Append("#", ctx->sysId(), " L2[", coreId, "].");
        
    if (tlulId < 0)
        strapp.Append("C[", tlcId, "]");
    else
        strapp.Append("UL[", tlulId, "]");

    if (ctx->sysId() < DEVICE_NAME_CACHE_SIZE)
        cached[ctx->sysId()] = strapp.ToString();

    return strapp.ToString();
}


#define Log(ctx, str_app) \
    do { \
        if (glbl.cfg.verbose) { \
            const TLLocalContext* __tlc_assert__ctx = ctx; /* suppress the warning: -Wnonnull-compare */  \
            if (__tlc_assert__ctx) \
                std::cout << "[" << ctx->cycle() << "] [tl-test-new-INFO] " << GetDeviceName(ctx) << " "; \
            std::cout << (Gravity::StringAppender().str_app.ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)

#define LogEx(action) \
    do { \
        if (glbl.cfg.verbose) { \
            action; \
        } \
    } while(0)


#define LogInfo(time, str_app) \
    do { \
        if (glbl.cfg.verbose) { \
            std::cout << "[" << time << "] [tl-test-new-INFO] "; \
            std::cout << (Gravity::StringAppender().str_app.ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)

#define LogFinal(time, str_app) \
    do { \
        { \
            std::cout << "[" << time << "] [tl-test-new-FINAL] "; \
            std::cout << (Gravity::StringAppender().str_app.ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)

#define LogError(time, str_app) \
    do { \
        { \
            std::cout << "[" << time << "]\033[31m [tl-test-new-ERROR] \033[1;31m"; \
            std::cout << (Gravity::StringAppender().str_app.Append("\033[0m").ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)

#define LogFatal(time, str_app) \
    do { \
        { \
            std::cout << "[" << time << "]\033[31m [tl-test-new-FATAL] \033[0m"; \
            std::cout << (Gravity::StringAppender().str_app.ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)


#define tlc_assert(cond, ctx, info) \
    do { \
        if (!(cond)) { \
            const TLLocalContext* __tlc_assert__ctx = ctx; /* suppress the warning: -Wnonnull-compare */ \
            if (__tlc_assert__ctx) \
            { \
                LogError(ctx->cycle(), Append("[tlc_assert failure at ", __PRETTY_FUNCTION__, ":", __LINE__, "]").EndLine()); \
                LogError(ctx->cycle(), Append("[tlc_assert failure from system #", ctx->sysId(), "]").EndLine()); \
            } \
            LogError(ctx->cycle(), Append("info: ", info).EndLine()); \
            LogError(ctx->cycle(), Append("\033[0m", "stack backtrace: ").EndLine()); \
            { \
                size_t bktr_i, bktr_size; \
                void* array[1024]; \
                bktr_size = backtrace(array, 1024); \
                char** bktr_strings = backtrace_symbols(array, bktr_size); \
                for (bktr_i = 0; bktr_i < bktr_size; bktr_i++) \
                { \
                    size_t pos; \
                    std::string bktr_str(bktr_strings[bktr_i]); \
                    if ((pos = bktr_str.find_last_of('/')) != std::string::npos) \
                        bktr_str = bktr_str.substr(pos); \
                    if ((pos = bktr_str.find_last_of('\\')) != std::string::npos) \
                        bktr_str = bktr_str.substr(pos); \
                    LogError(ctx->cycle(), Append("\033[0m", "#", bktr_i, " ", bktr_str).EndLine()); \
                } \
                free(bktr_strings); \
            } \
            fflush(stdout); \
            fflush(stderr); \
            TLAssertFailureEvent assert_event(__PRETTY_FUNCTION__, info); \
            assert_event.Fire(); \
            throw TLAssertFailureException(assert_event); \
            assert(false); \
        } \
    } while (0)

#define tlsys_assert(cond, info) \
    do { \
        if (!(cond)) { \
            LogError("tlsys_assert", Append("tlsys_assert() failure at ", __PRETTY_FUNCTION__, ":", __LINE__, " ").EndLine()); \
            LogError("tlsys_assert", Append(info).EndLine()); \
            fflush(stdout); \
            fflush(stderr); \
            TLAssertFailureEvent assert_event(__PRETTY_FUNCTION__, info); \
            assert_event.Fire(); \
            throw TLAssertFailureException(assert_event); \
            assert(false); \
        } \
    } while (0)


#define Debug(ctx, str_app) \
    do { \
        if (glbl.cfg.verbose) { \
            const TLLocalContext* __tlc_assert__ctx = ctx; /* suppress the warning: -Wnonnull-compare */  \
            if (__tlc_assert__ctx) \
                std::cout << "[" << ctx->cycle() << "] [tl-test-new-DEBUG] #" << ctx->sysId() << " "; \
            std::cout << (Gravity::StringAppender().str_app.ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)

#define DebugEx(action) \
    do { \
        if (glbl.cfg.verbose) { \
            action; \
        } \
    } while(0)


#define Dump(str_app) \
    do { \
        if (glbl.cfg.verbose) { \
            std::cout << (Gravity::StringAppender().str_app.ToString()); \
            fflush(stdout); \
            fflush(stderr); \
        } \
    } while(0)

#define DumpEx(action) \
    do { \
        if (glbl.cfg.verbose) { \
            action; \
        } \
    } while(0)

/*
#define Log(...) \
    do {} while(0)

#define Dump(...) \
    do {} while(0)
*/

#endif //TLC_TEST_COMMON_H
