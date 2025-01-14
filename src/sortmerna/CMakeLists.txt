#===============================================================================
# file: CMakeLists.txt
# created: Jun 08, 2017
#
# CMake file for sortmerna/src/sortmerna
#===============================================================================

cmake_minimum_required(VERSION 3.21)
project(sortmerna CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(Threads REQUIRED)

include(FindZLIB)
if (DEFINED CACHE{ZLIB_ROOT})
	message("searching ZLIB in ${ZLIB_ROOT}")
else()
	message("searching ZLIB in default locations")
endif()
find_package(ZLIB MODULE REQUIRED) # prevent CONFIG search mode

if(ZLIB_FOUND)
	get_target_property(lib ZLIB::ZLIB LOCATION)
	message("ZLIB::ZLIB LOCATION: ${lib}")
	get_target_property(lib ZLIB::ZLIB LOCATION_DEBUG)
	message("ZLIB::ZLIB LOCATION_DEBUG: ${lib}")
endif(ZLIB_FOUND)

# cmake has no FindRocksDB module, but RocksDB build provides cmake config,
# whence using CONFIG search.
# defines RocksDB::rocksdb (static) and RocksDB::rocksdb-shared targets. Note that
# these names can only be found by reading the package configuration files.
if (DEFINED CACHE{ROCKSDB_DIST})
	find_package(RocksDB CONFIG REQUIRED
	  PATHS ${ROCKSDB_DIST}
	  NO_DEFAULT_PATH
	)
else()
	message("searching RocksDB in default locations")
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules/")
	message("CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
  	find_package(RocksDB REQUIRED)
endif()

if(RocksDB_FOUND)
	message("RocksDB found")
	if(ROCKSDB_USE_STATIC_LIBS)
		message("Using Static RocksDB lib")
		get_target_property(lib RocksDB::rocksdb LOCATION)
		message("RocksDB::rocksdb LOCATION: ${lib}")
		get_target_property(lib RocksDB::rocksdb LOCATION_DEBUG)
		message("RocksDB::rocksdb LOCATION_DEBUG: ${lib}")
		# use ROCKSDB_LIB variable for potential switching between rocksdb and rocksb-shared
		set(ROCKSDB_LIB RocksDB::rocksdb)
	else()
		message("Using Shared RocksDB lib")
		get_target_property(lib RocksDB::rocksdb-shared LOCATION)
		message("RocksDB::rocksdb-shared LOCATION: ${lib}")
		get_target_property(lib RocksDB::rocksdb-shared LOCATION_DEBUG)
		message("RocksDB::rocksdb-shared LOCATION_DEBUG: ${lib}")
	    set(ROCKSDB_LIB RocksDB::rocksdb-shared)
	endif()
	get_property(trans_deps TARGET ${ROCKSDB_LIB} PROPERTY INTERFACE_LINK_LIBRARIES)
	message("${ROCKSDB_LIB} transitive link dependencies: ${trans_deps}")
else()
	message("RocksDB not found")
endif(RocksDB_FOUND)

# fix RocksDBTargets.cmake, which makes both static and shared libraries always dependent on libz.so
if(NOT WIN32)
	if(ZLIB_LIBRARY_RELEASE)
		# modify the dependent libraries of the RocksDB::rocksdb target
		set_target_properties(RocksDB::rocksdb PROPERTIES
			INTERFACE_LINK_LIBRARIES "${ZLIB_LIBRARY_RELEASE};Threads::Threads"
		)
	endif()
endif()

if(RocksDB_FOUND)
get_property(trans_deps TARGET RocksDB::rocksdb PROPERTY INTERFACE_LINK_LIBRARIES)
message("rocksdb transitive link dependencies AFTER modification: ${trans_deps}")
endif(RocksDB_FOUND)
#include(FindRapidJson)
 # prevent CONFIG search mode
#find_package(RapidJson MODULE REQUIRED)

# set additional compile flags
if(MSVC)
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi /nologo /EHsc /GS /Gd /GR /GF /fp:precise /Zc:wchar_t /Zc:forScope /errorReport:queue")
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FC /d2Zi+ /W4 /wd4127 /wd4800 /wd4996 /wd4351 /wd4100 /wd4204 /wd4324")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wextra -Wall -Xlinker -v") # -Xlinker -v / --verbose (linked libraries info)   '-Wl,-Map,mapfile.txt'  '-Wl,-v'
  #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -v -da -Q") # for segfault
  #                                                    |  |   |_show which function is causing the crash.
  #                                                    |  |_dumps the RTL to a file after each stage
  #                                                    |_show how the program was invoked
  #set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -v -da -Q -O0") # for segfault
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-compare -Wshadow -Wno-unused-parameter -Wno-unused-variable -Woverloaded-virtual -Wnon-virtual-dtor -Wno-missing-field-initializers -Wno-strict-aliasing")
endif()

#set(MY_OPTS -g -Wall) # using quotes "-g -Wall" throws error
#target_compile_options(smr_objs PUBLIC ${MY_OPTS})
#target_compile_options(smr_objs PUBLIC "$<$<CONFIG:DEBUG>:${MY_OPTS}>")
#target_compile_options(smr_objs PUBLIC "$<$<CONFIG:RELEASE>:${MY_OPTS}>")
#set_target_properties(smr_objs PROPERTIES COMPILE_OPTIONS ${MY_OPTS})

set(SMR_SRCS
	alignment.cpp
	bitvector.cpp
	#callbacks.cpp
	cmd.cpp
	izlib.cpp
	index.cpp
	indexdb.cpp
	kseq_load.cpp
	kvdb.cpp
	options.cpp
	output.cpp
	summary.cpp
	paralleltraversal.cpp
	processor.cpp
	read.cpp
	#read_control.cpp
	readfeed.cpp
	readstats.cpp
	references.cpp
	refstats.cpp
	ssw.c
	traverse_bursttrie.cpp
	util.cpp
	otumap.cpp
	report.cpp
	report_fx_base.cpp
	report_fastx.cpp
	report_fx_other.cpp
	report_blast.cpp
	report_denovo.cpp
	report_biom.cpp
	report_sam.cpp
	#writer.cpp
)

# SMR Objects - build a separate library to use with Tests
add_library(smr_objs OBJECT ${SMR_SRCS})
target_link_libraries(smr_objs
	PUBLIC
		alp
		${ROCKSDB_LIB}
		ZLIB::ZLIB
		#RapidJSON::RapidJSON
)
if(WIN32)
	target_include_directories(smr_objs 
		PUBLIC
			${CMAKE_SOURCE_DIR}/include
			$<TARGET_PROPERTY:winapi,INCLUDE_DIRECTORIES>
			$<TARGET_PROPERTY:cmph,INCLUDE_DIRECTORIES>
			${DIRENTWIN_HOME}/include
			${CONCURRENTQUEUE_HOME}
	)
else()
	target_include_directories(smr_objs 
		PUBLIC
			${CMAKE_SOURCE_DIR}/include
			$<TARGET_PROPERTY:cmph,INCLUDE_DIRECTORIES>
			${CONCURRENTQUEUE_HOME}
	)
endif()

get_property(trans_deps TARGET smr_objs PROPERTY INTERFACE_LINK_LIBRARIES)
message("SMR Objects transitive link dependencies: ${trans_deps}")

get_target_property(COMPILE_OPT smr_objs COMPILE_OPTIONS)
get_target_property(COMPILE_FLG smr_objs COMPILE_FLAGS)
message("SMR Objects COMPILE_OPT: ${COMPILE_OPT}  COMPILE_FLG: ${COMPILE_FLG}")

# SMR
add_executable(sortmerna main.cpp)
if(WIN32)
	target_include_directories(sortmerna 
		PRIVATE
			${CMAKE_SOURCE_DIR}/include
			${DIRENTWIN_HOME}/include
			${CONCURRENTQUEUE_HOME}
	)
	target_link_libraries(sortmerna
		build_version
		smr_objs
		winapi
		alp
		$<TARGET_OBJECTS:cmph>
		ZLIB::ZLIB
		${ROCKSDB_LIB}
		#RapidJSON::RapidJSON
		Rpcrt4.lib
		Cabinet.lib # rocksdb:XPRESS::CreateCompressor,Compress,CloseCompressor,CreateDecompressor,Decompress,CloseDecompressor
	)
else()
	target_include_directories(sortmerna 
		PRIVATE
			${CONCURRENTQUEUE_HOME}
			${CMAKE_SOURCE_DIR}/include
	)
	#get_property(rocksdb_trans_deps TARGET RocksDB::rocksdb PROPERTY INTERFACE_LINK_LIBRARIES)
	#message("rocksdb_trans_deps: ${rocksdb_trans_deps}")
	target_link_libraries(sortmerna
		build_version
		smr_objs
		alp # this is a transitive dependency of smr_objs but the linking fails without it. Why?
		$<TARGET_OBJECTS:cmph>
		${ROCKSDB_LIB}
		${CMAKE_DL_LIBS}
		# the following are all transitive dependencies of smr_objs i.e. no need to link: 
		# RapidJSON::RapidJSON ZLIB::ZLIB Threads::Threads (rockdb deps)
	)
	
	if(PORTABLE)
		target_link_options(sortmerna PRIVATE -static)
	endif(PORTABLE)
endif()
