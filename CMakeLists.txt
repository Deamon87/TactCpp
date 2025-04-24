cmake_minimum_required(VERSION 4.0)
project(TactCpp)

set(CMAKE_CXX_STANDARD 20)

add_executable(TactCpp main.cpp utils/KeyService.cpp
        utils/Jenkins96.cpp
        utils/Jenkins96.h
        BLTE.cpp
        BLTE.h
        CDN.cpp
        CDN.h
        utils/stringUtils.h
        CASCIndexInstance.h
)
