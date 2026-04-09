# TL-Test-New Architecture

## Overview

TL-Test-New is a **Unified TileLink Memory Subsystem Tester** for XiangShan, designed to verify L2/LLC cache designs through constrained random stimulus generation, memory consistency checking, and automated port wiring. It supports two execution modes:

- **Verilator Host Mode** – runs as a standalone executable (`tltest_v3lt`) that drives a Verilated DUT directly.
- **DPI Guest Mode** – compiled as a shared library (`libtltest_dpi.so`) that is loaded by any Verilog simulator (VCS, Verdi, etc.) via DPI-C.

---

## Repository Layout

```
tl-test-new/
├── Makefile                    Top-level build orchestration
├── configs/                    Pre-set INI configuration files for each test case
├── scripts/                    Helper run scripts
├── dut/
│   ├── CoupledL2/              XiangShan CoupledL2 DUT submodule + build scripts
│   └── OpenLLC/                OpenLLC DUT submodule + build scripts
└── main/                       TL-Test-New C++ source tree
    ├── CMakeLists.txt
    ├── Base/                   Fundamental types, enumerations, and config structures
    ├── TLAgent/                TileLink channel agents (TL-C, TL-UL, MMIO)
    ├── Fuzzer/                 Fuzzing engines and ARI (Automated Range Iteration)
    ├── Sequencer/              Central simulation coordinator (TLSequencer)
    ├── System/                 Startup / teardown procedures
    ├── Memory/                 AXI memory backend
    ├── Events/                 System-level event declarations
    ├── PortGen/                Automated TileLink port wiring (static & dynamic)
    ├── Plugins/                Plugin framework (ChiselDB, CHIron)
    ├── CHIron/                 CHI logging support
    ├── DPI/                    DPI-C interface (headers, SystemVerilog bindings)
    ├── Utils/                  Shared utilities (scoreboard, common types, EventBus)
    └── V3/                     Verilator Host Mode main entry point
```

---

## Component Descriptions

### `Base/` – Fundamental Definitions

| File | Purpose |
|------|---------|
| `TLEnum.hpp` | TileLink opcode enumerations (`TLOpcodeA/B/C/D/E`), permission types (`TLPermission`), and their string helpers. |
| `TLLocal.hpp` | `TLLocalConfig` (per-run configuration: core count, address ranges, ARI parameters, etc.) and `TLLocalContext` (interface providing cycle count, system ID, and config access to all components). |
| `TLGlobal.hpp` | `TLGlobalConfiguration` (verbose logging flags) and the global singleton `glbl`. |

`TLLocalContext` is the base class for all agents and the memory backend, giving every component a uniform way to query the current simulation cycle, its own identity, and the shared configuration.

---

### `TLAgent/` – TileLink Agents

All agents inherit from `BaseAgent<BeatSize, DataSize>` which itself implements `TLLocalContext`.

```
BaseAgent<>
  ├── CAgent       TL-C coherence agent  (Channels A/B/C/D/E)
  ├── ULAgent      TL-UL uncached agent  (Channels A/D only)
  └── MMIOAgent    MMIO uncached agent   (Channels A/D, narrower beat width)
```

#### `BaseAgent`
- Owns an `IDPool` for source-ID allocation and reclamation.
- Holds pointers to the shared `GlobalBoard` (memory consistency checker) and `MemoryBackend`.
- Declares pure-virtual channel fire hooks: `fire_a/b/c/d/e`, `handle_channel`, `update_signal`.
- Provides a seeded `std::mt19937_64` RNG accessible as `rand64()`.

#### `CAgent` (TL-C Coherent Agent)
- Implements the full TileLink-C coherent protocol: AcquireBlock, AcquirePerm, ProbeAck, ProbeAckData, Release, ReleaseData, GrantAck, and CMO operations (CBO.clean / CBO.flush / CBO.inval).
- Maintains a **local scoreboard** (`LocalScoreBoard`, keyed by physical address) tracking per-alias status (an `S_*` state machine), permission level (`INVALID / BRANCH / TRUNK / TIP`), and dirty state.
- Tracks multi-beat in-progress transactions via `PendingTrans<>` for each channel.
- Fires a `GrantEvent` over the Gravity EventBus when a Grant/GrantData is received, allowing the `UnifiedFuzzer` to react.
- Records per-operation latency histograms (`latencyMapA`, `latencyMapC`) for bandwidth profiling.
- Manages CMO in-flight state via `CMOLocalStatus`.

#### `ULAgent` (TL-UL Uncached Agent)
- Implements the uncached TileLink-UL protocol: Get, PutFullData, PutPartialData.
- Local scoreboard keyed by source ID (`UL_SBEntry`), tracking request type, status, and address.

#### `MMIOAgent`
- Implements uncached MMIO accesses over a TL-UL-like channel with a narrower data width (`DATASIZE_MMIO`).
- Coordinates with a shared `MMIOGlobalStatus` for cross-agent MMIO data consistency checks.

#### `Bundle` types (`Bundle.h`)
Defines C++ structs that mirror the SystemVerilog TileLink channel signals:

| Struct | Channel | Direction |
|--------|---------|-----------|
| `BundleChannelA<Usr,Echo,N>` | A | Master → Slave (requests) |
| `BundleChannelB` | B | Slave → Master (probes) |
| `BundleChannelC<Usr,Echo,N>` | C | Master → Slave (releases/probeacks) |
| `BundleChannelD<Usr,Echo,N>` | D | Slave → Master (grants/acks) |
| `BundleChannelE` | E | Master → Slave (grant acks) |
| `Bundle<Req,Resp,Echo,N>` | A+B+C+D+E | Full port |
| `BundleL2ToL1Hint` | side-band | L2 → L1 hint signal |

---

### `Fuzzer/` – Stimulus Generation

All fuzzer classes extend a common `Fuzzer` base that holds cycle pointer, address-range parameters, and ARI state.

```
Fuzzer (base)
  ├── CFuzzer        Drives a single CAgent with constrained-random TL-C traffic
  ├── ULFuzzer       Drives a single ULAgent with random Get/Put traffic
  ├── MMIOFuzzer     Drives a single MMIOAgent with random MMIO Get/Put traffic
  └── UnifiedFuzzer  Coordinates all agents; implements the "Anvil" pattern
```

#### `CFuzzer`
- Each tick either calls `randomTest()` (pick a random cached address and issue AcquireBlock, AcquirePerm, or ReleaseData) or `caseTest()` for specific corner-case sequences.
- Supports **ARI (Automated Range Iteration)**: iterates over a set of `CFuzzRange` entries (each specifying `maxTag`, `maxSet`, `maxAlias`), advancing to the next range after `fuzzARIRangeIterationInterval` cycles, cycling `fuzzARIRangeIterationTarget` times total.
- Optionally performs streaming-address fuzzing (`FUZZ_STREAM` mode).

#### `UnifiedFuzzer`
- Owns all `CAgent`, `ULAgent`, and `MMIOAgent` arrays.
- In **ANVIL** mode, implements a spatiotemporal access pattern: one core issues sequential AcquirePerm-then-ReleaseData pairs while other cores issue AcquireBlock reads on the same addresses, stress-testing coherence transitions.
- Listens to `GrantEvent` from the EventBus to track outstanding grants and schedule dependent operations.

---

### `Sequencer/` – Central Coordinator (`TLSequencer`)

`TLSequencer` is the central object that owns and wires together all simulation components.

**Owned resources:**
- Arrays of `CAgent*`, `ULAgent*`, `MMIOAgent*` (sized by `TLLocalConfig`)
- Arrays of `Fuzzer*` / `UnifiedFuzzer*` matching the agent arrays
- `GlobalBoard<paddr_t>` – shared memory consistency scoreboard
- `UncachedBoard<paddr_t>` – uncached (UL/MMIO) address tracking
- Raw simulation memory (`uint8_t* pmem`)
- `MemoryAgent*` array – AXI memory backend instances
- I/O port arrays: `IOPort**` (TL-C/UL), `MMIOPort**`, `L2ToL1HintPort**`, `MemoryAXIPort**`

**Simulation loop interface (called by V3 main or DPI stubs):**

| Method | Called at | Action |
|--------|-----------|--------|
| `Initialize(cfg)` | once at startup | Allocates agents, fuzzers, scoreboards; reads `tltest.ini` |
| `Tick(cycles)` | posedge | Fires channel-read handlers for incoming messages (B/D channels from DUT) |
| `Tock()` | negedge | Runs fuzzers, prepares channel-write outputs (A/C/E to DUT) |
| `Finalize()` | once at shutdown | Prints summary, deallocates |

Port accessor methods (`IO()`, `MMIO()`, `L2ToL1Hint()`, `MemoryAXI()`) return pointers/references to the shared channel signal structs so that `V3/PortGen` or DPI stubs can copy values in and out.

---

### `System/` – Startup and Teardown

`TLSystem.cpp` provides the two top-level C functions called by both V3 main and the DPI init stub:

```cpp
void TLInitialize(TLSequencer** tltest, PluginManager** plugins,
                  std::function<void(TLLocalConfig&)> tlcfgInit);
void TLFinalize(TLSequencer** tltest, PluginManager** plugins);
```

`TLInitialize` performs:
1. Reads `tltest.ini` (using `inicpp`) to build a `TLLocalConfig`.
2. Applies the optional `tlcfgInit` callback for programmatic overrides.
3. Fires `TLSystemInitializationPreEvent` on the EventBus.
4. Calls `TLSequencer::Initialize`.
5. Enables built-in plugins via `PluginManager`.
6. Fires `TLSystemInitializationPostEvent`.

---

### `Memory/` – AXI Memory Backend

`MemoryAgent` (namespace `axi_agent`) models a simple AXI4 subordinate that backs the DUT's memory port. It inherits both `TLLocalContext` and `MemoryBackend` (the abstract read/write interface used by agents for ground-truth data comparison).

- Handles AXI channels AW, W, B (write path) and AR, R (read path).
- Supports configurable out-of-order reads, read depth, write depth, and access latency (all from `TLLocalConfig`).
- Multiple `MemoryAgent` instances can be created for multi-port memory configurations.

`Bundle.hpp` in this directory defines the AXI channel structs (`BundleChannelAW`, `BundleChannelAR`, `BundleChannelW<N>`, `BundleChannelR<N>`, `BundleChannelB`).

---

### `Utils/` – Shared Utilities

| File | Purpose |
|------|---------|
| `ScoreBoard.h` | Generic `ScoreBoard<Tk,Tv>` template (hash-map with update callbacks). Specialised as `GlobalBoard<paddr_t>` (the shared ground-truth for all cached addresses) and per-agent local boards. |
| `ULScoreBoard.h` | Scoreboard variant for uncached (UL) address tracking (`UncachedBoard`). |
| `Common.h` | Shared type aliases (`paddr_t`, `shared_tldata_t<N>`), compile-time constants (`BEATSIZE`, `DATASIZE`, `NR_SOURCEID`, …), and assertion macros (`tlc_assert`). |
| `gravity_eventbus.hpp` | Gravity EventBus – a lightweight, type-safe publish/subscribe bus used for `GrantEvent`, system lifecycle events, and plugin events. |
| `gravity_utility.hpp` | `Gravity::StringAppender` – fluent string builder used throughout for log messages. |
| `inicpp.hpp` | INI file parser for runtime configuration (`tltest.ini`). |
| `concurrentqueue.hpp` | Lock-free concurrent queue (moodycamel) used internally. |
| `assert.hpp` | Custom assertion helpers. |
| `autoinclude.h` | Macro that builds the include path to the Verilated top header at compile time. |

#### `GlobalBoard<paddr_t>`
The central memory consistency checker. Maps each physical cache-line address to a `Global_SBEntry` that holds:
- `status`: `SB_INVALID`, `SB_VALID`, or `SB_PENDING`
- `data`: the last known committed data
- `pending_data`: data written but not yet committed (e.g., during a Release/Put)

When an agent receives data from the DUT, it calls `GlobalBoard::verify()` to confirm correctness against the reference model.

---

### `Events/` – System Lifecycle Events

Declares Gravity `Event<T>` subclasses fired at key lifecycle points:

| Event | Fired when |
|-------|-----------|
| `TLSystemInitializationPreEvent` | Just before `TLSequencer::Initialize` |
| `TLSystemInitializationPostEvent` | Just after sequencer is ready |
| `TLSystemFinalizationPreEvent` | Just before `TLSequencer::Finalize` |
| `TLSystemFinalizationPostEvent` | After finalization completes |
| `TLSystemFinishEvent` | A component signals normal test completion |
| `TLSystemFinishedEvent` | Main routine confirms all activity has ended |
| `TLSystemFailedEvent` | Main routine confirms a failure state |

---

### `PortGen/` – Automated Port Connection

PortGen eliminates manual wiring of TileLink signal ports between the C++ test harness and the Verilated DUT.

Two modes are supported:

#### Static PortGen (default)
- At **build time**, CMake runs the `portgen` executable which reads `tltest.ini` and generates `portgen.cpp` containing `PushChannel*` / `PullChannel*` functions hard-coded for the configured core count and UL port count.
- The generated source is compiled into `tltest_portgen.so`.
- `V3::PortGen::LoadStatic()` loads this library at startup.

#### Dynamic PortGen (experimental)
- At **run time**, `V3::PortGen::LoadDynamic(...)` generates and compiles `portgen.cpp` on-the-fly using the host C++ compiler and loads the resulting shared library from `/tmp`.
- Allows port configuration changes without a full rebuild.

Both modes expose the same interface used by the V3 main loop:

```cpp
namespace V3::PortGen {
    void PushChannelB(VTestTop*, TLSequencer*);  // DUT->TLTest (B channel data)
    void PushChannelD(VTestTop*, TLSequencer*);  // DUT->TLTest (D channel data)
    void PullChannelA(VTestTop*, TLSequencer*);  // TLTest->DUT (A channel data)
    void PullChannelC(VTestTop*, TLSequencer*);  // TLTest->DUT (C channel data)
    void PullChannelE(VTestTop*, TLSequencer*);  // TLTest->DUT (E channel data)
    // + ready-signal variants for each channel
}
```

---

### `V3/` – Verilator Host Mode Entry Point

`v3_main.cpp` contains the `main()` function for the `tltest_v3lt` executable.

**Startup sequence:**
1. Register signal/exit handlers that call `TLFinalize`.
2. Call `TLInitialize` to build the sequencer.
3. Load PortGen (static or dynamic).
4. Create `VTestTop` (Verilated DUT) and open FST waveform file.
5. Assert reset for 10 cycles.

**Per-cycle simulation loop (`while (tltest->IsAlive())`):**

```
tltest->Tick(time)
  PortGen::Push B, D (DUT outputs → TLTest inputs)
  V3PushL2ToL1Hint
  [optional] Memory::Push AW, W, AR
tltest->Tock()
  PortGen::Pull A, C, E (TLTest outputs → DUT inputs)
  [optional] Memory::Pull B, R
V3EvalNegedge          ← half clock
  PortGen::Pull B, D readys
  [optional] Memory::Pull AW, W, AR readys
  PortGen::Push A, C, E readys
  [optional] Memory::Push B, R readys
V3EvalPosedge          ← rising edge
```

The `v3_memaxi.hpp` helper (compiled in when `TLTEST_MEMORY=1`) provides `V3::Memory::Push/Pull*` functions that wire the AXI memory port of `VTestTop` to the `MemoryAgent` instances in the sequencer.

Compile-time C++20 concepts (`ConceptPortL2ToL1Hint`, `ConceptPortLogPerf`) gracefully handle DUTs that do or do not expose the optional hint and performance-log ports.

---

### `DPI/` – DPI Guest Mode Interface

Exposes the TileLink test system as a set of `extern "C"` DPI-C functions callable from any SystemVerilog simulator.

**System lifecycle:**
```c
void TileLinkSystemInitialize();
void TileLinkSystemFinalize();
int  TileLinkSystemIsAlive();
int  TileLinkSystemIsFailed();
int  TileLinkSystemIsFinished();
void TileLinkSystemTick(uint64_t cycles);
void TileLinkSystemTock();
```

**Per-channel signal exchange (one function pair per channel per direction):**
```c
void TileLinkPushChannelA(int deviceId, uint8_t ready);
void TileLinkPullChannelA(int deviceId, uint8_t* valid, uint8_t* opcode, ...);
// B, C, D, E variants follow the same pattern
// MMIO variants: TileLinkMMIOPushChannelA, TileLinkMMIOPullChannelD, ...
```

The corresponding `*.svh` files (`tilelink_dpi.svh`, `coupledL2_dpi.svh`, `memory_dpi.svh`) provide the SystemVerilog `import "DPI-C"` declarations and wrapper tasks that the DUT testbench can call each clock edge.

---

### `Plugins/` – Plugin Framework

`PluginManager` maintains a named registry of `Plugin` objects. Each plugin implements:

```cpp
virtual void OnEnable();
virtual void OnDisable();
```

Plugin lifecycle events (`PluginEvent::PreEnable`, `PostEnable`, `PreDisable`, `PostDisable`) are broadcast on the Gravity EventBus so other components can react.

Built-in plugins:

| Plugin | File | Function |
|--------|------|----------|
| **CHIronCLogB** | `CHIronCLogB.cpp` | Connects the CHIron CHI logging bus to the CLog-B structured log format |
| **ChiselDB** | `ChiselDB.cpp` | Integrates ChiselDB (Chisel/FIRRTL debug database) for structured RTL event capture |

---

### `CHIron/` – CHI Logging

Provides a logging interface (`clog.hpp`, `clog_b.hpp`) for CHI (Coherent Hub Interface) protocol events. `clogdpi_b.cpp` exports DPI-C functions (`clogdpi_b.svh`) that the DUT can call to inject CHI log records into the host-side structured log.

---

## Data Flow and Execution Model

```
                        ┌──────────────────────────────────────────┐
                        │            TLSequencer                   │
                        │                                          │
        ┌───────────────┤  GlobalBoard  UncachedBoard  MemoryAgent │
        │  Tick/Tock    │                                          │
        │               │  CAgent[0..N]   ULAgent[0..M]           │
        │               │  CFuzzer[0..N]  ULFuzzer[0..M]          │
        │               │  UnifiedFuzzer                           │
        │               └────────────┬─────────────────────────────┘
        │                            │  IOPort / MMIOPort / MemoryAXIPort
        │                            │  (shared signal structs)
        │          ┌─────────────────▼──────────────┐
        │          │           PortGen               │
        │          │  (Push/Pull channel functions)  │
        │          └─────────────────┬───────────────┘
        │                            │  Direct struct field access
        │          ┌─────────────────▼───────────────┐
        │          │         VTestTop (Verilated DUT) │   ← Verilator Host Mode
        │          │   – or –                         │
        │          │       Verilog Simulator via DPI  │   ← DPI Guest Mode
        └──────────└──────────────────────────────────┘
```

### Tick / Tock Protocol

Each simulation clock cycle proceeds in two halves:

1. **Tick** (rising edge of DUT clock): The sequencer reads incoming channel data from the port structs (populated by PortGen/DPI from DUT outputs) and dispatches them to agent `handle_channel()` / `fire_*()` methods.  Agents update their internal state machines and scoreboards.

2. **Tock** (falling edge): Fuzzers run and decide the next transaction. Agents fill the outgoing channel structs. PortGen/DPI copies them into DUT input ports.

### Memory Consistency Checking

- `GlobalBoard<paddr_t>` is the single source of truth for cached memory.
- When an agent receives data (e.g., `GrantData`, `AccessAckData`), it calls `GlobalBoard::verify()` comparing DUT-returned data against the reference.
- Writes are staged as `SB_PENDING` and promoted to `SB_VALID` when the corresponding ack is received.
- On mismatch, `tlc_assert` terminates the simulation with a detailed error message.

---

## Configuration System

TL-Test-New uses a two-layer configuration approach:

### Compile-time (`TLLocalConfig` defaults in `TLSystem.cpp`)
Hard-coded default values for all `TLLocalConfig` fields, plus preprocessor constants in `Utils/Common.h` (`BEATSIZE`, `DATASIZE`, `NR_SOURCEID`, etc.).

### Runtime (`tltest.ini`)
An INI file (parsed by `inicpp`) read from the working directory at startup. Three sections:

| Section | Key examples | Controls |
|---------|-------------|----------|
| `[tltest.config]` | `core`, `core.tl_c`, `core.tl_ul` | Agent/port counts |
| `[tltest.fuzzer]` | `seed`, `ari.interval`, `ari.target` | Fuzzing parameters |
| `[tltest.logger]` | `verbose`, `verbose.xact_fired`, … | Log verbosity flags |

Pre-built configuration presets for each DUT/topology combination live in `configs/`.

---

## DUT Integration

### CoupledL2
- Submodule at `dut/CoupledL2/`
- Chisel-generated Verilog emitted to `dut/CoupledL2/build/`
- Test topologies: `l2l3` (L2 + L3) and `l2l3l2` (L2 + L3 + another L2)
- CMake flag: `-DDUT_PATH=.../dut/CoupledL2 -DTLTEST_MEMORY=0`

### OpenLLC
- Submodule at `dut/OpenLLC/`
- Same topology options as CoupledL2
- CMake flag: `-DDUT_PATH=.../dut/OpenLLC`

Both DUTs are Verilated into `./verilated/` and linked as `libvltdut.a` during build.

---

## Build Artifacts

| Artifact | Mode | Description |
|----------|------|-------------|
| `tltest_v3lt` | Verilator Host | Main simulation executable |
| `tltest_portgen.so` | Verilator Host | PortGen channel-wiring library (static mode) |
| `portgen` | Verilator Host | PortGen code-generation utility |
| `portgen.cpp` | Verilator Host | Generated port-wiring source |
| `libtltest_dpi.so` | DPI Guest | Shared library loaded by Verilog simulators |
