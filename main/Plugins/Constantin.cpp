#include "Constantin.hpp"

#include "../Utils/autoinclude.h"

#include <cstdlib>

#if __has_include(AUTOINCLUDE_DUT_BUILT(constantin.cpp))
#include AUTOINCLUDE_DUT_BUILT(constantin.cpp)

namespace ConstantinCompat {

void Reload() {
    if (!std::getenv("NOOP_HOME")) {
        setenv("NOOP_HOME", DUT_PATH, 0);
    }
    constantinLoad();
}

}

#else

namespace ConstantinCompat {

void Reload() {
}

}

#endif
