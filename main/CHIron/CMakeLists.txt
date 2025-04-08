FILE(GLOB_RECURSE sources *.cpp *h *.hpp)

ADD_LIBRARY(CHIron OBJECT ${sources})
