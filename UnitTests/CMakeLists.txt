cmake_minimum_required( VERSION 3.0 FATAL_ERROR )
set( CMAKE_VERBOSE_MAKEFILE ON )

project( UnitTests )

get_filename_component( CINDER_PATH "/code/lib/Cinder" ABSOLUTE )
get_filename_component( UNIT_DIR "." ABSOLUTE )

include( "${CINDER_PATH}/proj/cmake/modules/cinderMakeApp.cmake" )

set( SOURCES
	${UNIT_DIR}/src/TestMain.cpp
	${UNIT_DIR}/src/BasicTest.cpp
	${UNIT_DIR}/../src/FileWatcher.cpp
	${UNIT_DIR}/../src/filemonitor/fsevents/FileMonitorImpl.cpp
)

set( INCLUDES
	${UNIT_DIR}/src
	${UNIT_DIR}/../include
# use if( APPLE ) for OS specific targets
	${UNIT_DIR}/../include/filemonitor
	${UNIT_DIR}/../include/filemonitor/fsevents
)

ci_make_app(
	SOURCES     ${SOURCES}
	CINDER_PATH ${CINDER_PATH}
	INCLUDES    "${INCLUDES}"
)

if( APPLE )
	set( TESTBINDIR ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/UnitTests/UnitTests.app/Contents/MacOS )
else()
	set( TESTBINDIR ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/UnitTests )
endif()

add_test(
	NAME UnitTests
	COMMAND ${TESTBINDIR}/UnitTests
)
