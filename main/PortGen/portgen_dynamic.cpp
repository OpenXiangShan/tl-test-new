#include "portgen_dynamic.hpp"

#include "portgen_static.hpp"

#include <fstream>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>

#include <dlfcn.h>


#ifndef CXX_COMPILER
#   define CXX_COMPILER "g++"
#endif


namespace V3::PortGen {

    //
    typedef void        (*LoadedPortGen)(VTestTop* verilated, TLSequencer* tltest);
    typedef uint64_t    (*LoadedPortInfo)();

    static LoadedPortInfo   loadedGetCoreCount;
    static LoadedPortInfo   loadedGetULPortCountPerCore;
    static LoadedPortInfo   loadedGetMPortCountPerCore;

    static LoadedPortGen    loadedPushChannelA;
    static LoadedPortGen    loadedPullChannelA;
    static LoadedPortGen    loadedPushChannelB;
    static LoadedPortGen    loadedPullChannelB;
    static LoadedPortGen    loadedPushChannelC;
    static LoadedPortGen    loadedPullChannelC;
    static LoadedPortGen    loadedPushChannelD;
    static LoadedPortGen    loadedPullChannelD;
    static LoadedPortGen    loadedPushChannelE;
    static LoadedPortGen    loadedPullChannelE;

    void LoadDynamic(std::vector<std::string> includePaths, int coreCount, int tlULPerCore, int tlMPerCore)
    {
        //
        Gravity::StringAppender cpp_file_name("/tmp/tltest_portgen");

        auto time = std::chrono::system_clock::now().time_since_epoch();
        auto time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(time);

        cpp_file_name.Append("-").Append(time_ns.count());
        
        //
        std::ofstream fout(cpp_file_name.Append(".cpp").ToString());

        if (!fout.is_open())
        {
            tlsys_assert(false, 
                Gravity::StringAppender("Failed to create temporary file for TileLink PortGen: ")
                    .Append(cpp_file_name.ToString())
                    .EndLine()
                    .ToString());
            
            return; //
        }

        fout << Generate(coreCount, tlULPerCore, tlMPerCore);
        fout.flush();
        fout.close();

        //
        Gravity::StringAppender cc_command;
        cc_command
            .Append(CXX_COMPILER, " ")
            .Append("-fPIC ")
            .Append("-shared ")
            .Append(cpp_file_name.ToString(), " ")
            .Append("-o ", cpp_file_name.Append(".so").ToString(), " ");

        for (auto iter = includePaths.begin(); iter != includePaths.end(); iter++)
            cc_command.Append("-I\"", *iter, "\" ");

        LogInfo("PortGen", Append("CXX ", cc_command.ToString()).EndLine());

        int cc = system(cc_command.ToString().c_str());

        if (WEXITSTATUS(cc) != EXIT_SUCCESS)
        {
            tlsys_assert(false, 
                Gravity::StringAppender("Failed to compile TileLink PortGen: ")
                    .Append(cpp_file_name.ToString())
                    .EndLine()
                    .ToString());
            
            return; //
        }

        //
        void* dlib = dlopen(cpp_file_name.ToString().c_str(), RTLD_NOW);
        if (!dlib)
        {
            tlsys_assert(false, 
                Gravity::StringAppender("Failed to load dynamic TileLink PortGen: ")
                    .Append(cpp_file_name.ToString())
                    .Append(": ")
                    .Append(dlerror())
                    .EndLine()
                    .ToString());
            
            return; //
        }

        //
#       define LOAD_PORTGEN_SYM(type, target, name) \
        { \
            void* sym = dlsym(dlib, name); \
            const char* dlsym_error = dlerror(); \
            if (dlsym_error != NULL) \
            { \
                tlsys_assert(false, \
                    Gravity::StringAppender("Failed to load dynamic TileLink PortGen: ") \
                        .Append("symbol=", name) \
                        .EndLine() \
                        .ToString()); \
                return; \
            } \
            target = reinterpret_cast<type>(sym); \
        }

        LOAD_PORTGEN_SYM(LoadedPortInfo, loadedGetCoreCount, "GetCoreCount");
        LOAD_PORTGEN_SYM(LoadedPortInfo, loadedGetULPortCountPerCore, "GetULPortCountPerCore");
        LOAD_PORTGEN_SYM(LoadedPortInfo, loadedGetMPortCountPerCore, "GetMPortCountPerCore");

        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelA, "PushChannelA");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelA, "PullChannelA");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelB, "PushChannelB");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelB, "PullChannelB");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelC, "PushChannelC");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelC, "PullChannelC");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelD, "PushChannelD");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelD, "PullChannelD");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelE, "PushChannelE");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelE, "PullChannelE");

#       undef LOAD_PORTGEN_SYM

        //
        LogInfo("PortGen", 
            Append("Dynamic TileLink PortGen module loaded: ", cpp_file_name.ToString()).EndLine());
    }

    void LoadStatic()
    {
        static constexpr char dlname[] = "tltest_portgen.so";

        //
        std::string abs_dlname = Gravity::StringAppender()
            .Append(std::filesystem::current_path().generic_string(), "/")
            .Append(dlname)
            .ToString();

        //
        void* dlib = dlopen(abs_dlname.c_str(), RTLD_NOW);
        if (!dlib)
        {
            tlsys_assert(false, 
                Gravity::StringAppender("Failed to load dynamic TileLink PortGen: ")
                    .Append(abs_dlname)
                    .Append(": ")
                    .Append(dlerror())
                    .EndLine()
                    .ToString());
            
            return; //
        }

        //
#       define LOAD_PORTGEN_SYM(type, target, name) \
        { \
            void* sym = dlsym(dlib, name); \
            const char* dlsym_error = dlerror(); \
            if (dlsym_error != NULL) \
            { \
                tlsys_assert(false, \
                    Gravity::StringAppender("Failed to load dynamic TileLink PortGen: ") \
                        .Append("symbol=", name) \
                        .EndLine() \
                        .ToString()); \
                return; \
            } \
            target = reinterpret_cast<type>(sym); \
        }

        LOAD_PORTGEN_SYM(LoadedPortInfo, loadedGetCoreCount, "GetCoreCount");
        LOAD_PORTGEN_SYM(LoadedPortInfo, loadedGetULPortCountPerCore, "GetULPortCountPerCore");

        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelA, "PushChannelA");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelA, "PullChannelA");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelB, "PushChannelB");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelB, "PullChannelB");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelC, "PushChannelC");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelC, "PullChannelC");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelD, "PushChannelD");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelD, "PullChannelD");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPushChannelE, "PushChannelE");
        LOAD_PORTGEN_SYM(LoadedPortGen, loadedPullChannelE, "PullChannelE");

#       undef LOAD_PORTGEN_SYM

        //
        LogInfo("PortGen", 
            Append("Static TileLink PortGen module loaded: ", abs_dlname).EndLine());
    }
}

uint64_t V3::PortGen::GetCoreCount()
{
    tlsys_assert(V3::PortGen::loadedGetCoreCount,
        "PortGen not available");

    return V3::PortGen::loadedGetCoreCount();
}

uint64_t V3::PortGen::GetULPortCountPerCore()
{
    tlsys_assert(V3::PortGen::loadedGetULPortCountPerCore,
        "PortGen not available");
    
    return V3::PortGen::loadedGetULPortCountPerCore();
}

uint64_t V3::PortGen::GetMPortCountPerCore()
{
    tlsys_assert(V3::PortGen::loadedGetMPortCountPerCore,
        "PortGen not available");
    
    return V3::PortGen::loadedGetULPortCountPerCore();
}

void V3::PortGen::PushChannelA(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPushChannelA, 
        "PortGen not available");
    
    V3::PortGen::loadedPushChannelA(verilated, tltest);
}

void V3::PortGen::PullChannelA(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPullChannelA, 
        "PortGen not available");

    V3::PortGen::loadedPullChannelA(verilated, tltest);
}

void V3::PortGen::PushChannelB(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPushChannelB, 
        "PortGen not available");
    
    V3::PortGen::loadedPushChannelB(verilated, tltest);
}

void V3::PortGen::PullChannelB(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPullChannelB, 
        "PortGen not available");

    V3::PortGen::loadedPullChannelB(verilated, tltest);
}

void V3::PortGen::PushChannelC(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPushChannelC, 
        "PortGen not available");

    V3::PortGen::loadedPushChannelC(verilated, tltest);
}

void V3::PortGen::PullChannelC(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPullChannelC, 
        "PortGen not available");

    V3::PortGen::loadedPullChannelC(verilated, tltest);
}

void V3::PortGen::PushChannelD(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPushChannelD, 
        "PortGen not available");

    V3::PortGen::loadedPushChannelD(verilated, tltest);
}

void V3::PortGen::PullChannelD(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPullChannelD, 
        "PortGen not available");

    V3::PortGen::loadedPullChannelD(verilated, tltest);
}

void V3::PortGen::PushChannelE(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPushChannelE, 
        "PortGen not available");

    V3::PortGen::loadedPushChannelE(verilated, tltest);
}

void V3::PortGen::PullChannelE(VTestTop* verilated, TLSequencer* tltest)
{
    tlsys_assert(V3::PortGen::loadedPullChannelE, 
        "PortGen not available");

    V3::PortGen::loadedPullChannelE(verilated, tltest);
}

