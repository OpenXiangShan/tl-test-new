#
CMAKE_MINIMUM_REQUIRED(VERSION 3.16)
PROJECT(tltest_new)

#
if (NOT DEFINED CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Debug)
endif ()

if(NOT DEFINED BUILD_DPI)
    SET(BUILD_DPI ON)
endif ()

if (NOT DEFINED BUILD_V3)
    SET(BUILD_V3 ON)
endif ()

if (NOT DEFINED BUILD_PORTGEN)
    SET(BUILD_PORTGEN ON)
endif ()


#
if (NOT DEFINED DUT_PATH)
    SET(DUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../dut/CoupledL2")
endif ()

if (NOT DEFINED DUT_BUILD_PATH)
    SET(DUT_BUILD_PATH "${DUT_PATH}/build")
endif ()

if (NOT DEFINED CHISELDB_PATH)
    SET(CHISELDB_PATH "${DUT_PATH}/build")
endif ()

if (NOT DEFINED VERILATOR_INCLUDE)
    SET(VERILATOR_INCLUDE "/usr/local/share/verilator/include")
endif ()

if (NOT DEFINED VERIALTED_PATH)
    SET(VERILATED_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../verilated")
endif ()

if (NOT DEFINED TLTEST_PORTGEN_DYNAMIC)
    SET(TLTEST_PORTGEN_DYNAMIC, OFF)
endif ()

if (NOT DEFINED TLTEST_MEMORY)
    SET(TLTEST_MEMORY 1)
endif ()


#
ADD_DEFINITIONS(-DCXX_COMPILER="${CMAKE_CXX_COMPILER}")
ADD_DEFINITIONS(-DCURRENT_PATH="${CMAKE_CURRENT_SOURCE_DIR}")
ADD_DEFINITIONS(-DDUT_PATH="${DUT_PATH}")
ADD_DEFINITIONS(-DDUT_PATH_TOKEN=${DUT_PATH})
ADD_DEFINITIONS(-DDUT_BUILD_PATH="${DUT_BUILD_PATH}")
ADD_DEFINITIONS(-DDUT_BUILD_PATH_TOKEN=${DUT_BUILD_PATH})
ADD_DEFINITIONS(-DCHISELDB_PATH="${CHISELDB_PATH}")
ADD_DEFINITIONS(-DCHISELDB_PATH_TOKEN=${CHISELDB_PATH})
ADD_DEFINITIONS(-DVERILATED_PATH="${VERILATED_PATH}")
ADD_DEFINITIONS(-DVERILATED_PATH_TOKEN=${VERILATED_PATH})
ADD_DEFINITIONS(-DVERILATOR_INCLUDE="${VERILATOR_INCLUDE}")
ADD_DEFINITIONS(-DTLTEST_MEMORY=${TLTEST_MEMORY})

if (TLTEST_PORTGEN_DYNAMIC)
    ADD_DEFINITIONS(-DTLTEST_PORTGEN_DYNAMIC)
endif ()


#
SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -std=c++20 -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -std=c++20 -O3 -Wall")

ADD_COMPILE_OPTIONS(-fPIC)

#
ADD_SUBDIRECTORY(Base)
ADD_SUBDIRECTORY(Sequencer)
ADD_SUBDIRECTORY(Memory)
ADD_SUBDIRECTORY(TLAgent)
ADD_SUBDIRECTORY(Fuzzer)
ADD_SUBDIRECTORY(Events)
ADD_SUBDIRECTORY(System)
ADD_SUBDIRECTORY(CHIron)
ADD_SUBDIRECTORY(Plugins)

FIND_PACKAGE(SQLite3 REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)
FIND_PACKAGE(Threads REQUIRED)
INCLUDE_DIRECTORIES(${SQLite3_INCLUDE_DIRS})

#
if (BUILD_DPI)

    ADD_SUBDIRECTORY(DPI)

    ADD_LIBRARY(tltest_dpi SHARED Base Sequencer Memory TLAgent Fuzzer Events System CHIron Plugins DPI)
    TARGET_LINK_LIBRARIES(tltest_dpi PUBLIC
        Base DPI Sequencer Memory TLAgent Fuzzer Events System Plugins sqlite3)
endif ()

#
if (BUILD_V3)

    INCLUDE_DIRECTORIES(${VERILATOR_INCLUDE}) # change to your verilator root include on error
    INCLUDE_DIRECTORIES(${VERILATOR_INCLUDE}/vltstd)

    ADD_SUBDIRECTORY(V3)
    ADD_SUBDIRECTORY(PortGen)

    ADD_EXECUTABLE(tltest_v3lt Base Sequencer Memory TLAgent Fuzzer Events System CHIron Plugins PortGen V3)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        TARGET_LINK_LIBRARIES(tltest_v3lt 
            Base Sequencer Memory TLAgent Fuzzer Events System CHIron Plugins PortGen V3 sqlite3
            "-lz"
	    "-Wl,--whole-archive"
            ${VERILATED_PATH}/libvltdut.a
            "-Wl,--no-whole-archive"
	    Threads::Threads ZLIB::ZLIB atomic
	    "-rdynamic"
            ${CMAKE_DL_LIBS})
    else ()
        TARGET_LINK_LIBRARIES(tltest_v3lt 
            Base Sequencer Memory TLAgent Fuzzer Events System CHIron Plugins PortGen V3 sqlite3
	    "-Wl,--whole-archive"
            ${VERILATED_PATH}/libvltdut.a
            "-Wl,--no-whole-archive"
	    Threads::Threads ZLIB::ZLIB atomic
            "-rdynamic"
            ${CMAKE_DL_LIBS})
    endif ()


    if (NOT TLTEST_PORTGEN_DYNAMIC)

        ADD_EXECUTABLE(portgen PortGen/portgen_static.cpp PortGen/portgen_main.cpp)
        TARGET_LINK_LIBRARIES(portgen)

        ADD_CUSTOM_COMMAND(
            TARGET portgen
            POST_BUILD
            COMMAND portgen
            COMMAND ${CMAKE_CXX_COMPILER} portgen.cpp -fPIC -shared -o tltest_portgen.so -I${CMAKE_CURRENT_SOURCE_DIR} -I${VERILATED_PATH} -I${VERILATOR_INCLUDE} -I${VERILATOR_INCLUDE}/vltstd
            VERBATIM)
        
        SET_PROPERTY(
            TARGET portgen
            APPEND
            PROPERTY ADDITIONAL_CLEAN_FILES portgen.cpp tltest_portgen.so)
    endif()
endif ()

