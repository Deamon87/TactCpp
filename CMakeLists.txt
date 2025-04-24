cmake_minimum_required(VERSION 4.0)
project(TactCpp)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)

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


add_executable(TactCpp main.cpp utils/KeyService.cpp
        utils/Jenkins96.cpp
        utils/Jenkins96.h
        BLTE.cpp
        BLTE.h
        CDN.cpp
        CDN.h
        utils/stringUtils.h
        CASCIndexInstance.h
        Settings.h
        RootInstance.cpp
        RootInstance.h
		MemoryMappedFile.cpp
		MemoryMappedFile.h
		utils/BinaryUtils.h
		EncodingInstance.cpp
		EncodingInstance.h
		IndexInstance.cpp
		IndexInstance.h
		InstallInstance.cpp
		InstallInstance.h
		BuildInstance.cpp
		BuildInstance.h
		Config.h
		GroupIndex.cpp
		GroupIndex.h
		wow/WoWRootFlags.h
		BuildInfo.h
)

target_link_libraries(TactCpp cpr::cpr)
target_link_libraries(TactCpp zlib)