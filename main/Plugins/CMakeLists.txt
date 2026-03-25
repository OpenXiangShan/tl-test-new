FILE(GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp *h *.hpp)

ADD_LIBRARY(Plugins OBJECT ${sources})
