#include <cstdint>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <type_traits>

#include <signal.h>

#include "../Utils/autoinclude.h"
#include AUTOINCLUDE_VERILATED(VTestTop.h)

#include "../Sequencer/TLSequencer.hpp"
#include "../Plugins/PluginManager.hpp"
#include "../System/TLSystem.hpp"
#include "../PortGen/portgen_dynamic.hpp"
#include "../Events/TLSystemEvent.hpp"

#include <verilated_fst_c.h>

#if TLTEST_MEMORY == 1
#include "v3_memaxi.hpp"
#endif

static bool wave_enable     = true;
static uint64_t wave_begin  = 0;
static uint64_t wave_end    = 0;

static VerilatedFstC*   fst;
static VTestTop*        top;

static bool initialized = false;
static bool finalized = false;

TLSequencer*    tltest  = nullptr;
PluginManager*  plugins = nullptr;


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


template<typename, typename = void>
struct has_port_time : std::false_type {};

template<typename T>
struct has_port_time<T, std::void_t<decltype(std::declval<T>().time_sim)>> : std::true_type {};

template<typename T>
typename std::enable_if<has_port_time<T>::value>::type V3PushTime(T* top, uint64_t time)
{
    top->time_sim = time;
}

template<typename T>
typename std::enable_if<!has_port_time<T>::value>::type V3PushTime(T* top, uint64_t time)
{ }


template<typename T>
concept ConceptPortL2ToL1Hint = requires {
    { std::declval<T>().io_l1_0_l2Hint_valid           } -> std::convertible_to<uint8_t>;
    { std::declval<T>().io_l1_0_l2Hint_bits_sourceId   } -> std::convertible_to<uint32_t>;
    { std::declval<T>().io_l1_0_l2Hint_bits_isKeyword  } -> std::convertible_to<uint8_t>;
};

template<typename, typename = void>
struct has_port_l2_to_l1_hint : std::false_type {};

template<ConceptPortL2ToL1Hint T>
struct has_port_l2_to_l1_hint<T, void> : std::true_type {};

template<ConceptPortL2ToL1Hint T>
typename std::enable_if<has_port_l2_to_l1_hint<T>::value>::type V3PushL2ToL1Hint(
    T*          top,
    uint8_t&    valid,
    uint32_t&   sourceId,
    uint8_t&    isKeyword)
{
    valid       = top->io_l1_0_l2Hint_valid;
    sourceId    = top->io_l1_0_l2Hint_bits_sourceId;
    isKeyword   = top->io_l1_0_l2Hint_bits_isKeyword;
}

template<typename T>
typename std::enable_if<!has_port_l2_to_l1_hint<T>::value>::type V3PushL2ToL1Hint(
    T*          top,
    uint8_t&    valid,
    uint32_t&   sourceId,
    uint8_t&    isKeyword)
{ }

void V3PushL2ToL1Hint(VTestTop* verilated, TLSequencer* tltest)
{
    auto& io = tltest->L2ToL1Hint(0);

    V3PushL2ToL1Hint<VTestTop>(verilated,
        io.valid,
        io.sourceId,
        io.isKeyword);
}

template<typename T>
concept ConceptPortLogPerf = requires {
    { std::declval<T>().log_dump    } -> std::convertible_to<uint8_t>;
    { std::declval<T>().log_clean   } -> std::convertible_to<uint8_t>;
};

template<typename, typename = void>
struct has_port_log_perf : std::false_type {};

template<ConceptPortLogPerf T>
struct has_port_log_perf<T, void> : std::true_type {};

template<ConceptPortLogPerf T>
typename std::enable_if<has_port_log_perf<T>::value>::type V3PullLogPerf(
    T*      top,
    uint8_t dump,
    uint8_t clean)
{
    top->log_dump   = dump;
    top->log_clean  = clean;
}

template<typename T>
typename std::enable_if<!has_port_log_perf<T>::value>::type V3PullLogPerf(
    T*      top,
    uint8_t dump,
    uint8_t clean)
{ }


void V3Finalize()
{
    if (!initialized)
        return;

    if (!finalized)
    {
        TLFinalize(&tltest, &plugins);
        finalized = true;
    }
}

void V3SignalHandler(int signum)
{
    V3Finalize();
}


int main(int argc, char **argv)
{
    atexit(V3Finalize);
    signal(SIGINT, V3SignalHandler);
    signal(SIGTERM, V3SignalHandler);
    signal(SIGABRT, V3SignalHandler);
    //

    uint64_t time       = 0;
    //

#if TLTEST_MEMORY == 1
    std::cout << "[V3Main] \033[1;32mAXI Memory Backend compiled in\033[0m." << std::endl;
#else
    std::cout << "[V3Main] \033[31mAXI Memory Backend not compiled in\033[0m." << std::endl;
#endif

    if constexpr (has_port_time<VTestTop>::value)
        std::cout << "[V3Main] \033[1;32mPassing ticking time to verilated TestTop\033[0m." << std::endl;
    else
        std::cout << "[V3Main] \033[31mNot passing ticking time to verilated TestTop\033[0m." << std::endl;

    if constexpr (has_port_l2_to_l1_hint<VTestTop>::value)
        std::cout << "[V3Main] \033[1;32mAccepting L2ToL1Hint from TestTop\033[0m." << std::endl;
    else
        std::cout << "[V3Main] \033[31mNot accepting L2ToL1Hint from TestTop\033[0m." << std::endl;

    if constexpr (has_port_log_perf<VTestTop>::value)
        std::cout << "[V3Main] \033[1;32mControlling Performance Logging of TestTop\033[0m." << std::endl;
    else
        std::cout << "[V3Main] \033[31mNot controlling Performance Logging of TestTop\033[0m." << std::endl;

    //
    Verilated::commandArgs(argc, argv);

    // initialize TL-Test subsystem
    TLInitialize(&tltest, &plugins, [](TLLocalConfig&) -> void {});
    initialized = true;

    Gravity::RegisterListener(
        Gravity::MakeListener<TLSystemFinishEvent>(
            Gravity::StringAppender("v3main.finish").ToString(),
            -1,
            [&time] (TLSystemFinishEvent& event) -> void {

                std::ios_base::sync_with_stdio(true);
                V3PullLogPerf(top, 1, 0);
            }
        )
    );

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

    V3PullLogPerf(top, 0, 0);

    V3Reset(time, top, 10);
    V3PushTime(top, time);

    while (tltest->IsAlive())
    {

        // Tick
        tltest->Tick(time);

        // Push
        V3::PortGen::PushChannelB(top, tltest);
        V3::PortGen::PushChannelD(top, tltest);

        V3PushL2ToL1Hint(top, tltest);

#if TLTEST_MEMORY == 1
        V3::Memory::PushChannelAW(top, tltest);
        V3::Memory::PushChannelW(top, tltest);
        V3::Memory::PushChannelAR(top, tltest);
#endif

        // Tock
        tltest->Tock();

        // Pull
        V3::PortGen::PullChannelA(top, tltest);
        V3::PortGen::PullChannelC(top, tltest);
        V3::PortGen::PullChannelE(top, tltest);

#if TLTEST_MEMORY == 1
        V3::Memory::PullChannelB(top, tltest);
        V3::Memory::PullChannelR(top, tltest);
#endif

        //
        V3PushTime(top, time);
        V3EvalNegedge(time, top);

        // Pull 'ready's
        V3::PortGen::PullChannelB(top, tltest);
        V3::PortGen::PullChannelD(top, tltest);

#if TLTEST_MEMORY == 1
        V3::Memory::PullChannelAW(top, tltest);
        V3::Memory::PullChannelW(top, tltest);
        V3::Memory::PullChannelAR(top, tltest);
#endif

        // Push 'ready's
        V3::PortGen::PushChannelA(top, tltest);
        V3::PortGen::PushChannelC(top, tltest);
        V3::PortGen::PushChannelE(top, tltest);

#if TLTEST_MEMORY == 1
        V3::Memory::PushChannelB(top, tltest);
        V3::Memory::PushChannelR(top, tltest);
#endif

        //
        V3PushTime(top, time);
        V3EvalPosedge(time, top);

        //
        if (!(time % 10000))
            std::cout << "[" << time << "] [tl-test-new] Simulation time elapsed: " << time << "ps" << std::endl;
    }

    //
    int error = 0;

    std::cerr << std::flush;
    std::cout << std::flush;

    //
    if (tltest->IsFinished())
    {
        TLSystemFinishedEvent().Fire();

        LogInfo("test_top", 
            Append("TL-Test TileLink subsystem FINISHED.")
            .EndLine());
    }
    else if (tltest->IsFailed())
    {
        TLSystemFailedEvent().Fire();

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
    V3Finalize();

    return error;
}
