#include "TLSequencer.hpp"
//
//
//

#include <cstddef>
#include <cstdint>
#include <ostream>

#include "../Events/TLSystemEvent.hpp"


#define ASSERTION_FAILURE_CAPTURE(exception) \
    auto& event = exception.GetCopiedEvent(); \
    LogFatal(cycles, Append("[assertion failure] captured by: ", __PRETTY_FUNCTION__).EndLine()); \
    LogFatal(cycles, Append("[assertion failure] at: ", event.GetSourceLocation()).EndLine()); \
    LogFatal(cycles, Append("[assertion failure] info: ", event.GetInfo()).EndLine());


TLSequencer::TLSequencer() noexcept
    : state             (State::NOT_INITIALIZED)
    , globalBoard       (nullptr)
    , uncachedBoard     (nullptr)
    , config            ()
    , agents            (nullptr)
    , fuzzers           (nullptr)
    , mmioGlobalStatus  (nullptr)
    , mmioAgents        (nullptr)
    , mmioFuzzers       (nullptr)
    , memories          (nullptr)
    , io                (nullptr)
    , mmio              (nullptr)
    , memoryAXI         (nullptr)
    , cycles            (0)
{ }

TLSequencer::~TLSequencer() noexcept
{
    delete[] fuzzers;
    delete[] agents;
    delete[] mmioFuzzers;
    delete[] mmioAgents;
    delete[] memories;
    delete[] io;
    delete[] mmio;
    delete[] memoryAXI;
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
    return config.GetCAgentCount();
}

size_t TLSequencer::GetULAgentCount() const noexcept
{
    return config.GetULAgentCount();
}

size_t TLSequencer::GetMAgentCount() const noexcept
{
    return config.GetMAgentCount();
}

size_t TLSequencer::GetAgentCount() const noexcept
{
    return config.GetAgentCount();
}

int MFuzzer::index = 0;

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

        size_t total_c_agents = GetCAgentCount();
        size_t total_n_agents = GetAgentCount();

        //
        io          = new IOPort*       [total_n_agents];
        agents      = new BaseAgent*    [total_n_agents];
        fuzzers     = new Fuzzer*       [total_n_agents];

        //
        mmio        = new MMIOPort*     [total_c_agents];
        mmioAgents  = new MMIOAgent*    [total_c_agents];
        mmioFuzzers = new MMIOFuzzer*   [total_c_agents];

        //
        memoryAXI   = new MemoryAXIPort*[1];
        memories    = new MemoryAgent*  [1];

        // UL Scoreboard
        uncachedBoard   = new UncachedBoard<paddr_t>(total_n_agents);

        // MMIO Global Status
        mmioGlobalStatus = new MMIOGlobalStatus(&this->config);

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
                agents  [i] = new CAgent(&this->config, globalBoard, uncachedBoard, j, i, cfg.seed, &cycles);
                agents  [i]->connect(io[i]);

                fuzzers [i] = new CFuzzer(static_cast<CAgent*>(agents[i]));
                fuzzers [i]->set_cycles(&cycles);

                LogInfo("INIT", Append("TLSequencer::Initialize: ")
                    .Append("Instantiated TL-C Agent #", k, " with deviceId=", i, " for Core #", j).EndLine());

                //
                i++;
            }

            for (unsigned int k = 0; k < cfg.masterCountPerCoreTLUL; k++)
            {
                //
                io      [i] = new IOPort;
                agents  [i] = new ULAgent(&this->config, globalBoard, uncachedBoard, j, i, cfg.seed, &cycles);
                agents  [i]->connect(io[i]);

                fuzzers [i] = new ULFuzzer(static_cast<ULAgent*>(agents[i]));
                fuzzers [i]->set_cycles(&cycles);

                LogInfo("INIT", Append("TLSequencer::Initialize: ")
                    .Append("Instantiated TL-UL Agent #", k, " with deviceId=", i, " for Core #", j).EndLine());

                //
                i++;
            }

            for (unsigned int k = 0; k < cfg.masterCountPerCoreTLM; k++)
            {
                //
                io      [i] = new IOPort;
                agents  [i] = new MAgent(&this->config, globalBoard, uncachedBoard, j, i, cfg.seed, &cycles);
                agents  [i]->connect(io[i]);

                fuzzers [i] = new MFuzzer(static_cast<MAgent*>(agents[i]));
                fuzzers [i]->set_cycles(&cycles);

                LogInfo("INIT", Append("TLSequencer::Initialize: ")
                    .Append("Instantiated TL-M Agent #", k, " with deviceId=", i, " for Core #", j).EndLine());

                //
                i++;
            }
            //
            mmio        [j] = new MMIOPort;
            mmioAgents  [j] = new MMIOAgent(&this->config, mmioGlobalStatus, j, cfg.seed, &cycles);
            mmioAgents  [j]->connect(mmio[j]);

            mmioFuzzers [j] = new MMIOFuzzer(mmioAgents[j]);
            mmioFuzzers [j]->set_cycles(&cycles);

            LogInfo("INIT", Append("TLSequencer::Initialize: ")
                .Append("Instantiated MMIO Agent #", j, " with deviceId=", j, " for Core #", j).EndLine());
        }

        //
        for (unsigned int i = 0; i < 1; i++)
        {
            memoryAXI   [i] = new MemoryAXIPort;
            memories    [i] = new MemoryAgent(&this->config, cfg.seed, &cycles);
            memories    [i]->connect(memoryAXI[i]);

            LogInfo("INIT", Append("TLSequencer::Initialize: ")
                .Append("Instantiated Memory Agent #", i).EndLine());
        }

        // IO data field pre-allocation
        for (size_t i = 0; i < total_n_agents; i++)
        {
            io[i]->a.data = make_shared_tldata<BEATSIZE>();
            io[i]->c.data = make_shared_tldata<BEATSIZE>();
            io[i]->d.data = make_shared_tldata<BEATSIZE>();
            io[i]->m.data = make_shared_tldata<DATASIZE>();
        }

        for (size_t i = 0; i < total_c_agents; i++)
        {
            mmio[i]->a.data = make_shared_tldata<DATASIZE_MMIO>();
            mmio[i]->c.data = make_shared_tldata<DATASIZE_MMIO>();
            mmio[i]->d.data = make_shared_tldata<DATASIZE_MMIO>();
        }

        for (size_t i = 0; i < 1; i++)
        {
            memoryAXI[i]->w.data = make_shared_tldata<BEATSIZE_MEMORY>();
            memoryAXI[i]->r.data = make_shared_tldata<BEATSIZE_MEMORY>();
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

            io[i]->m.ready = 0;
            io[i]->m.valid = 0;
        }

        for (size_t i = 0; i < total_c_agents; i++)
        {
            mmio[i]->a.ready = 0;
            mmio[i]->a.valid = 0;

            mmio[i]->b.ready = 0;
            mmio[i]->b.valid = 0;

            mmio[i]->c.ready = 0;
            mmio[i]->c.valid = 0;

            mmio[i]->d.ready = 0;
            mmio[i]->d.valid = 0;

            mmio[i]->e.ready = 0;
            mmio[i]->e.valid = 0;
        }

        for (size_t i = 0; i < 1; i++)
        {
            memoryAXI[i]->aw.ready  = 0;
            memoryAXI[i]->aw.valid  = 0;

            memoryAXI[i]->w.ready   = 0;
            memoryAXI[i]->w.valid   = 0;

            memoryAXI[i]->b.ready   = 0;
            memoryAXI[i]->b.valid   = 0;

            memoryAXI[i]->ar.ready  = 0;
            memoryAXI[i]->ar.valid  = 0;

            memoryAXI[i]->r.ready   = 0;
            memoryAXI[i]->r.valid   = 0;
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

        size_t total_n_agents = GetAgentCount();
        size_t total_c_agents = GetCAgentCount();
        size_t total_m_agents = GetMAgentCount();

        // TL-C
        for (size_t i = 0; i < total_c_agents; i++){}
        // TL-UL fuzzers
        for (size_t i = total_c_agents; i < total_n_agents-total_m_agents; i++){
        }
        // TL-M fuzzers
        int total_data_size=0;
        int total_cycles=1;
        uint32_t StartCycle=-1;
        uint32_t EndCycle=0;
        for (size_t i = total_n_agents-total_m_agents; i < total_n_agents; i++){
            StartCycle = StartCycle < fuzzers[i]->perfCycleStart? StartCycle:fuzzers[i]->perfCycleStart;
            EndCycle = StartCycle > fuzzers[i]->perfCycleEnd? StartCycle:fuzzers[i]->perfCycleEnd;
            total_data_size+=fuzzers[i]->blkCountLimit*64;
        }
        total_cycles=(EndCycle-StartCycle)/2;
        if(total_cycles!=0) std::cout<<"perf debug : "<<total_data_size<<" / "<<total_cycles<<" = "<<total_data_size/total_cycles<<"B/Cycle"<<std::endl;

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

            if (io[i])
            {
                delete io[i];
                io[i] = nullptr;
            }
        }

        for (size_t i = 0; i < GetCAgentCount(); i++)
        {
            if (mmioFuzzers[i])
            {
                delete mmioFuzzers[i];
                mmioFuzzers[i] = nullptr;
            }

            if (mmioAgents[i])
            {
                delete mmioAgents[i];
                mmioAgents[i] = nullptr;
            }

            if (mmio[i])
            {
                delete mmio[i];
                mmio[i] = nullptr;
            }
        }

        for (size_t i = 0; i < 1; i++)
        {
            if (memories[i])
            {
                delete memories[i];
                memories[i] = nullptr;
            }

            if (memoryAXI[i])
            {
                delete memoryAXI[i];
                memoryAXI[i] = nullptr;
            }
        }

        delete globalBoard;
        globalBoard = nullptr;

        delete uncachedBoard;
        uncachedBoard = nullptr;

        if (state == State::FINISHED)
        {
            LogFinal(cycles, Append("\"\033[1;32m                                                           \033[0m\"").EndLine());
            LogFinal(cycles, Append("\"\033[1;32m ███████ ██ ███   ██ ██ ███████ ██   ██ ███████ ██████  ██ \033[0m\"").EndLine());
            LogFinal(cycles, Append("\"\033[1;32m ██      ██ ████  ██ ██ ██      ██   ██ ██      ██   ██ ██ \033[0m\"").EndLine());
            LogFinal(cycles, Append("\"\033[1;32m █████   ██ ██ ██ ██ ██ ███████ ███████ █████   ██   ██ ██ \033[0m\"").EndLine());
            LogFinal(cycles, Append("\"\033[1;32m ██      ██ ██  ████ ██      ██ ██   ██ ██      ██   ██    \033[0m\"").EndLine());
            LogFinal(cycles, Append("\"\033[1;32m ██      ██ ██   ███ ██ ███████ ██   ██ ███████ ██████  ██ \033[0m\"").EndLine());
            LogFinal(cycles, Append("\"\033[1;32m                                                           \033[0m\"").EndLine());
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
        size_t total_c_agents = GetCAgentCount();
        size_t total_m_agents = GetMAgentCount();

        //
        for (size_t i = 0; i < total_n_agents; i++)
            agents[i]->handle_channel();

        bool endTLM=true;

        // TL-C fuzzers
        for (size_t i = 0; i < total_c_agents; i++){
            fuzzers[i]->tick();
            #ifdef TLC_TEST
            endTLM = endTLM && (fuzzers[i]->perfCycleEnd);
            #endif
        }
        // TL-UL fuzzers
        for (size_t i = total_c_agents; i < total_n_agents-total_m_agents; i++){
            fuzzers[i]->tick();
        }
        // TL-M fuzzers
        for (size_t i = total_n_agents-total_m_agents; i < total_n_agents; i++){
            MFuzzer::setIndex(i);
            fuzzers[i]->tick();
            endTLM = endTLM && fuzzers[i]->perfCycleEnd;
        }
        if(endTLM){
            TLSystemFinishEvent().Fire();
        }
        for (size_t i = 0; i < total_n_agents; i++)
            agents[i]->update_signal();

        //
        for (size_t i = 0; i < total_c_agents; i++)
            mmioAgents[i]->handle_channel();

        for (size_t i = 0; i < total_c_agents; i++)
            mmioFuzzers[i]->tick();

        for (size_t i = 0; i < total_c_agents; i++)
            mmioAgents[i]->update_signal();

        //
        for (size_t i = 0; i < 1; i++)
            memories[i]->handle_channel();

        for (size_t i = 0; i < 1; i++)
            memories[i]->update_signal();
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

TLSequencer::MMIOPort* TLSequencer::MMIO() noexcept
{
    return *mmio;
}

TLSequencer::MMIOPort& TLSequencer::MMIO(int deviceId) noexcept
{
    return *(mmio[deviceId]);
}

TLSequencer::MemoryAXIPort* TLSequencer::MemoryAXI() noexcept
{
    return *memoryAXI;
}

TLSequencer::MemoryAXIPort& TLSequencer::MemoryAXI(int deviceId) noexcept
{
    return *(memoryAXI[deviceId]);
}
