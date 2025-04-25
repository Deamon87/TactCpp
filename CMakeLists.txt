cmake_minimum_required(VERSION 4.0)
project(TactCpp)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

#cpr
SET(CMAKE_USE_LIBSSH2 0)
SET(CPR_BUILD_TESTS 0)
SET(CURL_USE_LIBPSL 0)
SET(BUILD_LIBCURL_DOCS 0)
SET(ENABLE_CURL_MANUAL 0)
SET(CURL_ZLIB OFF CACHE STRING "" FORCE) # disable this lib to download the zlib as external project
FetchContent_Declare(cpr
		GIT_REPOSITORY https://github.com/libcpr/cpr.git
		GIT_TAG 2b2b16989708d81d4fe062240e085a2f5a61f74a # the commit hash for 1.9.9
		EXCLUDE_FROM_ALL
)

#FetchContent_MakeAvailable(cpr)
FetchContent_GetProperties(cpr)
message("cpr_POPULATED = ${cpr_POPULATED}")
if(NOT cpr_POPULATED)
#	 Fetch the content using previously declared details
	FetchContent_MakeAvailable(cpr)
	message("cpr_BINARY_DIR = ${cpr_BINARY_DIR}")
	message("cpr_SOURCE_DIR = ${cpr_SOURCE_DIR}")
#	 Bring the populated content into the build
	set(BUILD_SHARED_LIBS ON CACHE BOOL "")
#	add_subdirectory(${cpr_SOURCE_DIR} ${cpr_BINARY_DIR} EXCLUDE_FROM_ALL)
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
endif()

#zlib
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
FetchContent_Declare(zlib
		GIT_REPOSITORY https://github.com/madler/zlib.git
		GIT_TAG 5a82f71ed1dfc0bec044d9702463dbdf84ea3b71 # latest master commit
		EXCLUDE_FROM_ALL
)
FetchContent_GetProperties(zlib)
FetchContent_MakeAvailable(zlib)


add_library(TactCppLib STATIC
		TactCppLib/utils/KeyService.cpp
        TactCppLib/utils/Jenkins96.cpp
        TactCppLib/utils/Jenkins96.h
        TactCppLib/BLTE.cpp
        TactCppLib/BLTE.h
        TactCppLib/CDN.cpp
        TactCppLib/CDN.h
        TactCppLib/utils/stringUtils.h
        TactCppLib/CASCIndexInstance.h
        TactCppLib/Settings.h
        TactCppLib/RootInstance.cpp
        TactCppLib/RootInstance.h
		TactCppLib/MemoryMappedFile.cpp
		TactCppLib/MemoryMappedFile.h
		TactCppLib/utils/BinaryUtils.h
		TactCppLib/EncodingInstance.cpp
		TactCppLib/EncodingInstance.h
		TactCppLib/IndexInstance.cpp
		TactCppLib/IndexInstance.h
		TactCppLib/InstallInstance.cpp
		TactCppLib/InstallInstance.h
		TactCppLib/BuildInstance.cpp
		TactCppLib/BuildInstance.h
		TactCppLib/Config.h
		TactCppLib/GroupIndex.cpp
		TactCppLib/GroupIndex.h
		TactCppLib/wow/WoWRootFlags.h
		TactCppLib/BuildInfo.h
		TactCppLib/utils/TactConfigParser.cpp
		TactCppLib/utils/TactConfigParser.h
		TactCppLib/utils/DataReader.h
)

target_link_libraries(TactCppLib cpr::cpr)
target_link_libraries(TactCppLib zlib)


add_executable(TACTToolCpp src/main.cpp)

target_link_libraries(TACTToolCpp TactCppLib)