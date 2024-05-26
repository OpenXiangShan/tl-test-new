#pragma once

#ifndef TLTEST_AUTOINCLUDE_H
#define TLTEST_AUTOINCLUDE_H


#define __STR0(x) #x
#define __STR1(x) __STR0(x)


#ifndef DUT_PATH
// This default path could be changed for IDE language server, normally not used in compilation
#   define DUT_PATH "../../dut/CoupledL2"
#endif

#ifndef DUT_PATH_TOKEN
// This default path could be changed for IDE language server, normally not used in compilation
#   define DUT_PATH_TOKEN ../../dut/CoupledL2
#endif


#ifndef DUT_BUILD_PATH
// This default path could be changed for IDE language server, normally not used in compilation
#   define DUT_BUILD_PATH "../../dut/CoupledL2/build"
#endif

#ifndef DUT_BUILD_PATH_TOKEN
// This default path could be changed for IDE language server, normally not used in compilation
#   define DUT_BUILD_PATH_TOKEN ../../dut/CoupledL2/build
#endif


#ifndef CHISELDB_PATH
// This default path could be changed for IDE language server, normally not used in compilation
#   define CHISELDB_PATH DUT_BUILD_PATH
#endif

#ifndef CHISELDB_PATH_TOKEN
// This default path could be changed for IDE language server, normally not used in compilation
#   define CHISELDB_PATH_TOKEN DUT_BUILD_PATH_TOKEN
#endif


#ifndef VERILATED_PATH_TOKEN
// This default path could be changed for IDE language server, normally not used in compilation
#   define VERILATED_PATH_TOKEN ../../verilated
#endif

#ifndef VERILATED_PATH
// This default path could be changed for IDE language server, normally not used in compilation
#   define VERILATED_PATH "../../verilated"  
#endif


#ifndef CURRENT_PATH
#   define CURRENT_PATH "."
#endif


#ifndef VERILATOR_INCLUDE
#   define VERILATOR_INCLUDE "."
#endif


/*
* *NOTICE: For using macros in #include lines.
*          :) This is my magic, you don't need to read these. Relax and just use it.
*/
#define __AUTOINCLUDE_DUT_BUILT(file)   DUT_BUILD_PATH_TOKEN/file
#define AUTOINCLUDE_DUT_BUILT(file)     __STR1(__AUTOINCLUDE_DUT_BUILT(file))

#define __AUTOINCLUDE_CHISELDB(file)    CHISELDB_PATH_TOKEN/file
#define AUTOINCLUDE_CHISELDB(file)      __STR1(__AUTOINCLUDE_CHISELDB(file))

#define __AUTOINCLUDE_VERILATED(file)   VERILATED_PATH_TOKEN/file
#define AUTOINCLUDE_VERILATED(file)     __STR1(__AUTOINCLUDE_VERILATED(file))


#endif // TLTEST_AUTOINCLUDE_H
