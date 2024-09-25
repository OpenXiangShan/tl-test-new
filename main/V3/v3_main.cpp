#include <cstdint>
#include <chrono>

#include "../Utils/autoinclude.h"

#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"
#include "../Plugins/PluginManager.hpp"
#include "../System/TLSystem.hpp"
#include "../PortGen/portgen_dynamic.hpp"

#include <iomanip>
#include <verilated_fst_c.h>


static bool wave_enable     = true;
static uint64_t wave_begin  = 0;
static uint64_t wave_end    = 0;

static VerilatedFstC*   fst;
static VTestTop*        top;


inline static bool IsInWaveTime(uint64_t time)
{
    if (wave_begin > wave_end)
        return false;

    if (wave_begin == 0 && wave_end == 0)
        return true;

    return time >= wave_begin && time <= wave_end;
}

inline static std::string GetFstFileName()
{
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    return Gravity::StringAppender()
        .Append("wave-")
        .Append(std::put_time(
            std::localtime(&t),
            "%Y%m%d-%H-%M-%S"))
        .Append(".fst")
        .ToString();
}

inline static void V3Reset(uint64_t& time, VTestTop* top, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++)
    {
        top->reset = 1;

        top->clock = 0;
        top->eval(); 
        if (wave_enable && IsInWaveTime(time))
            fst->dump(time);
        time++;

        top->clock = 1;
        top->eval(); 
        if (wave_enable && IsInWaveTime(time))
            fst->dump(time);
        time++;
    }

    top->reset = 0;
}

inline static void V3EvalNegedge(uint64_t& time, VTestTop* top)
{
    top->clock = 0;
    top->eval();

    if (wave_enable && IsInWaveTime(time))
        fst->dump(time);

    time++;
}

inline static void V3EvalPosedge(uint64_t& time, VTestTop* top)
{
    top->clock = 1;
    top->eval();

    if (wave_enable && IsInWaveTime(time))
        fst->dump(time);

    time++;
}


int main(int argc, char **argv)
{
    uint64_t time       = 0;
    //

    //
    Verilated::commandArgs(argc, argv);

    // initialize TL-Test subsystem
    TLSequencer*    tltest;
    PluginManager*  plugins;

    TLInitialize(&tltest, &plugins, [](TLLocalConfig&) -> void {});

    // load PortGen component
#   ifdef TLTEST_PORTGEN_DYNAMIC
        V3::PortGen::LoadDynamic(
            {
                CURRENT_PATH,
                VERILATED_PATH,
                VERILATOR_INCLUDE
            },
            tltest->GetLocalConfig().coreCount,
            tltest->GetLocalConfig().masterCountPerCoreTLUL
        );
#   else
        V3::PortGen::LoadStatic();
#   endif

    // check PortGen component consistency
    tlsys_assert(V3::PortGen::GetCoreCount() == tltest->GetLocalConfig().coreCount,
        Gravity::StringAppender()
            .Append("PortGen inconsistent: PortGen.coreCount(",
                V3::PortGen::GetCoreCount(),
            ") != ",
            "LocalConfig.coreCount(",
                tltest->GetLocalConfig().coreCount
            ,"), "
            "please re-compile PortGen components or check dynamic generation")
            .ToString());

    tlsys_assert(V3::PortGen::GetULPortCountPerCore() == tltest->GetLocalConfig().masterCountPerCoreTLUL,
        Gravity::StringAppender()
            .Append("PortGen inconsistent: PortGen.ulPortCountPerCore(",
                V3::PortGen::GetULPortCountPerCore(),
            ") != ",
            "LocalConfig.ulPortCountPerCore(",
                tltest->GetLocalConfig().masterCountPerCoreTLUL
            ,"), "
            "please re-compile PortGen components or check dynamic generation")
            .ToString());

    //
    fst = nullptr;
    top = new VTestTop;

    if (wave_enable)
    {
        Verilated::traceEverOn(true);
        fst = new VerilatedFstC;
        top->trace(fst, 99);
        fst->open(GetFstFileName().c_str());
    }

    V3Reset(time, top, 10);

    while (tltest->IsAlive())
    {
        // Tick
        tltest->Tick(time);

        // Push
        V3::PortGen::PushChannelA(top, tltest);
        V3::PortGen::PushChannelB(top, tltest);
        V3::PortGen::PushChannelC(top, tltest);
        V3::PortGen::PushChannelE(top, tltest);
        V3::PortGen::PushChannelD(top, tltest);

        // Tock
        tltest->Tock();

        // Pull
        V3::PortGen::PullChannelA(top, tltest);
        V3::PortGen::PullChannelC(top, tltest);
        V3::PortGen::PullChannelE(top, tltest);

        //
        V3EvalNegedge(time, top);

        // Pull 'ready's
        V3::PortGen::PullChannelB(top, tltest);
        V3::PortGen::PullChannelD(top, tltest);

        //
        V3EvalPosedge(time, top);

        //
        if (!(time % 10000))
            std::cout << "[" << time << "] [tl-test-new] Simulation time elapsed: " << time << "ps" << std::endl;
    }

    //
    int error = 0;

    //
    if (tltest->IsFinished())
    {
        LogInfo("test_top", 
            Append("TL-Test TileLink subsystem FINISHED.")
            .EndLine());
    }
    else if (tltest->IsFailed())
    {
        LogInfo("test_top",
            Append("TL-Test TileLink subsystem FAILED.")
            .EndLine());

        error = 1;
    }
    else 
    {
        LogInfo("test_top", 
            Append("TL-Test TileLink subsystem no longer ALIVE.")
            .EndLine());

        error = 1;
    }

    if (wave_enable)
        fst->close();

    //
    TLFinalize(&tltest, &plugins);

    return error;
}
