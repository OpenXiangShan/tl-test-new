#pragma once


#ifndef TLTEST_DEFAULT_H
#define TLTEST_DEFAULT_H


#ifndef TLTEST_DEFAULT_SEED
#   define TLTEST_DEFAULT_SEED                          0
#endif


#ifndef TLTEST_DEFAULT_CORE_COUNT
#   define TLTEST_DEFAULT_CORE_COUNT                    1
#endif

#ifndef TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLC
#   define TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLC     1
#endif

#ifndef TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLUL
#   define TLTEST_DEFAULT_MASTER_COUNT_PER_CORE_TLUL    0
#endif


#ifndef TLTEST_DEFAULT_MEMORY_START
#   define TLTEST_DEFAULT_MEMORY_START                  0x00000000
#endif

#ifndef TLTEST_DEFAULT_MEMORY_END
#   define TLTEST_DEFAULT_MEMORY_END                    0x80000000
#endif

#ifndef TLTEST_DEFAULT_MMIO_START
#   define TLTEST_DEFAULT_MMIO_START                    0x80000000
#endif

#ifndef TLTEST_DEFAULT_MMIO_END
#   define TLTEST_DEFAULT_MMIO_END                      0x90000000
#endif


#endif // TLTEST_DEFAULT_H