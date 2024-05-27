#include "TLSequencer.hpp"
//
//
//

#include <cstddef>

#include "../Events/TLSystemEvent.hpp"


#define ASSERTION_FAILURE_CAPTURE(exception) \
    auto& event = exception.GetCopiedEvent(); \
    LogFatal(cycles, Append("[assertion failure] captured by: ", __PRETTY_FUNCTION__).EndLine()); \
    LogFatal(cycles, Append("[assertion failure] at: ", event.GetSourceLocation()).EndLine()); \
    LogFatal(cycles, Append("[assertion failure] info: ", event.GetInfo()).EndLine());


TLSequencer::TLSequencer() noexcept
    : state         (State::NOT_INITIALIZED)
    , globalBoard   (nullptr)
    , config        ()
    , agents        (nullptr)
    , fuzzers       (nullptr)
    , io            (nullptr)
    , cycles        (0)
{ }

TLSequencer::~TLSequencer() noexcept
{
    delete[] fuzzers;
    delete[] agents;
    delete[] io;
}

const TLLocalConfig& TLSequencer::GetLocalConfig() const noexcept
{
    return config;
}

TLSequencer::State TLSequencer::GetState() const noexcept
{
    return state;
}

bool TLSequencer::IsAlive() const noexcept
{
    return state == State::ALIVE;
}

bool TLSequencer::IsFailed() const noexcept
{
    return state == State::FAILED;
}

bool TLSequencer::IsFinished() const noexcept
{
    return state == State::FINISHED;
}

size_t TLSequencer::GetCAgentCount() const noexcept
{
    return config.coreCount * config.masterCountPerCoreTLC;
}

size_t TLSequencer::GetULAgentCount() const noexcept
{
    return config.coreCount * config.masterCountPerCoreTLUL;
}

size_t TLSequencer::GetAgentCount() const noexcept
{
    return GetCAgentCount() + GetULAgentCount();
}

void TLSequencer::Initialize(const TLLocalConfig& cfg) noexcept
{
    if (state != State::NOT_INITIALIZED)
        return;

    Gravity::RegisterListener(
        Gravity::MakeListener<TLSystemFinishEvent>(
            Gravity::StringAppender("tltest.sequencer.finish:", uint64_t(this)).ToString(),
            0,
            [this] (TLSystemFinishEvent& event) -> void {

                if (!this->IsAlive())
                    return;

                this->state = State::FINISHED;

                //
                LogInfo(this->cycles, Append("[tltest.sequencer.finish] First Finish Event fired from: ").EndLine());

                #ifdef __LIBUNWIND__
                    // unwind stack trace before finish
                    unw_context_t   unw_ctx = {0};
                    unw_cursor_t    unw_cur = {0};
                    unw_word_t      unw_off = 0;
                    unw_word_t      unw_pc  = 0;

                    char            unw_func_name[128]  = {0};
                    int             unw_count           = 0;

                    if (0 != unw_getcontext(&unw_ctx))
                    {
                        LogInfo(this->cycles, Append("<libunwind: failed to getcontext>"));
                        return;
                    }

                    if (0 != unw_init_local(&unw_cur, &unw_ctx))
                    {
                        LogInfo(this->cycles, Append("<libunwind: failed to initialize local cursor>"));
                        return;
                    }
                    
                    while (0 < unw_step(&unw_cur))
                    {
                        if (unw_count < 8)
                        {
                            unw_count++;
                            continue;
                        }

                        if (0 != unw_get_proc_name(&unw_cur, unw_func_name, sizeof(unw_func_name), &unw_off))
                        {
                            LogInfo(this->cycles, Append("#", unw_count, " <libunwind: failed to get function name>").EndLine());
                            continue;
                        }

                        unw_get_reg(&unw_cur, UNW_REG_IP, &unw_pc);

                        LogInfo(this->cycles, ShowBase().Hex()
                            .Append("#", unw_count, " : (" , unw_pc, ") ", unw_func_name, " + ", unw_off));

                        unw_count++;
                    }

                #else
                    // stack trace before finish
                    size_t bktr_i, bktr_size;
                    void* array[1024];
                    bktr_size = backtrace(array, 1024);
                    char** bktr_strings = backtrace_symbols(array, bktr_size);
                    
                    for (bktr_i = 8; bktr_i < bktr_size; bktr_i++)
                    {
                        size_t pos;
                        std::string bktr_str(bktr_strings[bktr_i]);

                        if ((pos = bktr_str.find_last_of('/')) != std::string::npos)
                            bktr_str = bktr_str.substr(pos);

                        if ((pos = bktr_str.find_last_of('\\')) != std::string::npos)
                            bktr_str = bktr_str.substr(pos);
                        
                        LogInfo(this->cycles, Append("[tltest.sequencer.finish] #", bktr_i, " ", bktr_str
                            ).EndLine());
                    }
                    free(bktr_strings);
                #endif
            }
        )
    );

    LogInfo("INIT", Append("\"                                                                                             \"").EndLine());     
    LogInfo("INIT", Append("\" ████████╗██╗           ████████╗███████╗███████╗████████╗      ███╗   ██╗███████╗██╗    ██╗ \"").EndLine()); 
    LogInfo("INIT", Append("\" ╚══██╔══╝██║           ╚══██╔══╝██╔════╝██╔════╝╚══██╔══╝      ████╗  ██║██╔════╝██║    ██║ \"").EndLine()); 
    LogInfo("INIT", Append("\"    ██║   ██║     █████╗   ██║   █████╗  ███████╗   ██║   █████╗██╔██╗ ██║█████╗  ██║ █╗ ██║ \"").EndLine()); 
    LogInfo("INIT", Append("\"    ██║   ██║     ╚════╝   ██║   ██╔══╝  ╚════██║   ██║   ╚════╝██║╚██╗██║██╔══╝  ██║███╗██║ \"").EndLine()); 
    LogInfo("INIT", Append("\"    ██║   ███████╗         ██║   ███████╗███████║   ██║         ██║ ╚████║███████╗╚███╔███╔╝ \"").EndLine()); 
    LogInfo("INIT", Append("\"    ╚═╝   ╚══════╝         ╚═╝   ╚══════╝╚══════╝   ╚═╝         ╚═╝  ╚═══╝╚══════╝ ╚══╝╚══╝  \"").EndLine()); 
    LogInfo("INIT", Append("\"                                                                                             \"").EndLine()); 

    try 
    {
        this->config = cfg;

        TLSystemInitializationPreEvent(&this->config).Fire();

        globalBoard = new GlobalBoard<paddr_t>;

        LogInfo("INIT", Append("TLSequencer::Initialize: using seed: ", cfg.seed).EndLine());
        LogInfo("INIT", Append("TLSequencer::Initialize: core count: ", cfg.coreCount).EndLine());
        LogInfo("INIT", Append("TLSequencer::Initialize: TL-C Agent per-core count: ", cfg.masterCountPerCoreTLC).EndLine());
        LogInfo("INIT", Append("TLSequencer::Initialize: TL-UL Agent per-core count: ", cfg.masterCountPerCoreTLUL).EndLine());
        LogInfo("INIT", Append("TLSequencer::Initialize: Total agent count: ", GetAgentCount()).EndLine());

        size_t total_n_agents = GetAgentCount();

        //
        io      = new IOPort*       [total_n_agents];
        agents  = new BaseAgent*    [total_n_agents];
        fuzzers = new Fuzzer*       [total_n_agents];

        /*
        * Device ID of TileLink ports organization:
        *
        *   1. For 2-core example of 1 TL-C agent and 0 TL-UL agent:
        *       ==========================================
        *       [deviceId: 0] TL-C  Agent #0    of core #0
        *       ------------------------------------------
        *       [deviceId: 1] TL-C  Agent #0    of core #1
        *       ==========================================
        *   
        *   2. For 2-core example of 1 TL-C agent and 1 TL-UL agent:
        *       ==========================================
        *       [deviceId: 0] TL-C  Agent #0    of core #0
        *       [deviceId: 1] TL-UL Agent #0    of core #0
        *       ------------------------------------------
        *       [deviceId: 2] TL-C  Agent #0    of core #1
        *       [deviceId: 3] TL-UL Agent #0    of core #1
        *       ==========================================
        *
        *   3. For 2-core example of 1 TL-C agent and 2 TL-UL agents:
        *       ==========================================
        *       [deviceId: 0] TL-C  Agent #0    of core #0
        *       [deviceId: 1] TL-UL Agent #0    of core #0
        *       [deviceId: 2] TL-UL Agent #1    of core #0
        *       ------------------------------------------
        *       [deviceId: 3] TL-C  Agent #0    of core #1
        *       [deviceId: 4] TL-UL Agent #0    of core #1
        *       [deviceId: 5] TL-UL Agent #1    of core #1
        *       ==========================================
        *
        */

        //
        unsigned int i = 0;
        for (unsigned int j = 0; j < cfg.coreCount; j++)
        {
            for (unsigned int k = 0; k < cfg.masterCountPerCoreTLC; k++)
            {
                //
                io      [i] = new IOPort;
                agents  [i] = new CAgent(&this->config, globalBoard, i, cfg.seed, &cycles);
                agents  [i]->connect(io[i]);

                //
                fuzzers [i] = new CFuzzer(static_cast<CAgent*>(agents[i]));
                fuzzers [i]->set_cycles(&cycles);

                //
                LogInfo("INIT", Append("TLSequencer::Initialize: ")
                    .Append("Instantiated TL-C Agent #", k, " with deviceId=", i, " for Core #", j).EndLine());

                //
                i++;
            }

            for (unsigned int k = 0; k < cfg.masterCountPerCoreTLUL; k++)
            {
                //
                io      [i] = new IOPort;
                agents  [i] = new ULAgent(&this->config, globalBoard, i, cfg.seed, &cycles);
                agents  [i]->connect(io[i]);

                //
                fuzzers [i] = new ULFuzzer(static_cast<ULAgent*>(agents[i]));
                fuzzers [i]->set_cycles(&cycles);

                //
                LogInfo("INIT", Append("TLSequencer::Initialize: ")
                    .Append("Instantiated TL-UL Agent #", k, " with deviceId=", i, " for Core #", j).EndLine());

                //
                i++;
            }
        }

        // IO data field pre-allocation
        for (size_t i = 0; i < total_n_agents; i++)
        {
            io[i]->a.data = make_shared_tldata<BEATSIZE>();
            io[i]->c.data = make_shared_tldata<BEATSIZE>();
            io[i]->d.data = make_shared_tldata<BEATSIZE>();
        }

        // IO field reset value
        for (size_t i = 0; i < total_n_agents; i++)
        {
            io[i]->a.ready = 0;
            io[i]->a.valid = 0;

            io[i]->b.ready = 0;
            io[i]->b.valid = 0;

            io[i]->c.ready = 0;
            io[i]->c.valid = 0;

            io[i]->d.ready = 0;
            io[i]->d.valid = 0;

            io[i]->e.ready = 0;
            io[i]->e.valid = 0;
        }

        //
        this->state = State::ALIVE;

        //
        TLSystemInitializationPostEvent(this).Fire();
    }
    catch (const TLAssertFailureException& e)
    {
        this->state = State::FAILED;
        ASSERTION_FAILURE_CAPTURE(e)
    }
}

void TLSequencer::Finalize() noexcept
{
    if (state != State::ALIVE && state != State::FAILED && state != State::FINISHED)
        return;

    LogFinal(cycles, Append(__PRETTY_FUNCTION__, ": called at ", cycles).EndLine());

    try 
    {
        TLSystemFinalizationPreEvent(this).Fire();

        Gravity::UnregisterListener<TLSystemFinishEvent>(
            Gravity::StringAppender("tltest.sequencer.finish:", uint64_t(this)).ToString());

        for (size_t i = 0; i < GetAgentCount(); i++)
        {
            if (fuzzers[i])
            {
                delete fuzzers[i];
                fuzzers[i] = nullptr;
            }

            if (agents[i])
            {
                delete agents[i];
                agents[i] = nullptr;
            }

            if (agents[i])
            {
                delete io[i];
                io[i] = nullptr;
            }
        }

        delete globalBoard;
        globalBoard = nullptr;

        if (state == State::FINISHED)
        {
            LogFinal(cycles, Append("\"\033[1;32m                                                           \033[0m\"").EndLine()); 
            LogFinal(cycles, Append("\"\033[1;32m ███████ ██ ███   ██ ██ ███████ ██   ██ ███████ ██████  ██ \033[0m\"").EndLine()); 
            LogFinal(cycles, Append("\"\033[1;32m ██      ██ ████  ██ ██ ██      ██   ██ ██      ██   ██ ██ \033[0m\"").EndLine()); 
            LogFinal(cycles, Append("\"\033[1;32m █████   ██ ██ ██ ██ ██ ███████ ███████ █████   ██   ██ ██ \033[0m\"").EndLine()); 
            LogFinal(cycles, Append("\"\033[1;32m ██      ██ ██  ████ ██      ██ ██   ██ ██      ██   ██    \033[0m\"").EndLine()); 
            LogFinal(cycles, Append("\"\033[1;32m ██      ██ ██   ███ ██ ███████ ██   ██ ███████ ██████  ██ \033[0m\"").EndLine()); 
            LogFinal(cycles, Append("\"\033[1;32m                                                           \033[0m\"").EndLine());  

            /*
            LogInfo(cycles, Append("\033[32m      ___                     ___                       ___           ___      \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m     /  /\\      ___          /__/\\        ___          /  /\\         /__/\\     \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m    /  /:/_    /  /\\         \\  \\:\\      /  /\\        /  /:/_        \\  \\:\\    \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m   /  /:/ /\\  /  /:/          \\  \\:\\    /  /:/       /  /:/ /\\        \\__\\:\\   \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m  /  /:/ /:/ /__/::\\      _____\\__\\:\\  /__/::\\      /  /:/ /::\\   ___ /  /::\\  \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m /__/:/ /:/  \\__\\/\\:\\__  /__/::::::::\\ \\__\\/\\:\\__  /__/:/ /:/\\:\\ /__/\\  /:/\\:\\ \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m \\  \\:\\/:/      \\  \\:\\/\\ \\  \\:\\~~\\~~\\/    \\  \\:\\/\\ \\  \\:\\/:/~/:/ \\  \\:\\/:/__\\/ \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m  \\  \\::/        \\__\\::/  \\  \\:\\  ~~~      \\__\\::/  \\  \\::/ /:/   \\  \\::/      \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m   \\  \\:\\        /__/:/    \\  \\:\\          /__/:/    \\__\\/ /:/     \\  \\:\\      \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m    \\  \\:\\       \\__\\/      \\  \\:\\         \\__\\/       /__/:/       \\  \\:\\     \033[0m").EndLine());
            LogInfo(cycles, Append("\033[32m     \\__\\/                   \\__\\/                     \\__\\/         \\__\\/     \033[0m").EndLine());
            */
        }

        this->state = State::NOT_INITIALIZED;

        TLSystemFinalizationPostEvent().Fire();
    }
    catch (const TLAssertFailureException& e)
    {
        this->state = State::FAILED;
        ASSERTION_FAILURE_CAPTURE(e)
    }

    LogFinal(cycles, Append(__PRETTY_FUNCTION__, ": tl-test subsystem finalized").EndLine());
}

void TLSequencer::Tick(uint64_t cycles) noexcept
{
    if (!IsAlive())
        return;

    this->cycles = cycles;
}

void TLSequencer::Tock() noexcept
{
    if (!IsAlive())
        return;

    try 
    {
        size_t total_n_agents = GetAgentCount();

        for (size_t i = 0; i < total_n_agents; i++)
            agents[i]->handle_channel();

        for (size_t i = 0; i < total_n_agents; i++)
            fuzzers[i]->tick();

        for (size_t i = 0; i < total_n_agents; i++)
            agents[i]->update_signal();
    }
    catch (const TLAssertFailureException& e) 
    {
        this->state = State::FAILED;
        ASSERTION_FAILURE_CAPTURE(e)
    }
}

TLSequencer::IOPort* TLSequencer::IO() noexcept
{
    return *io;
}

TLSequencer::IOPort& TLSequencer::IO(int deviceId) noexcept
{
    return *(io[deviceId]);
}
