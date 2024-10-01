cmake_minimum_required(VERSION 3.10)

project(Divee VERSION 1)

find_package(FLEX)
find_package(BISON)

set(CMAKE_CXX_FLAGS "-Wall")

flex_target(HdbScanner hdb.ll ${CMAKE_CURRENT_BINARY_DIR}/hdb_scanner.cc)
bison_target(HdbParser hdb.yy ${CMAKE_CURRENT_BINARY_DIR}/hdb_parser.cc DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/hdb_parser.h)
add_flex_bison_dependency(HdbScanner HdbParser)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable(divee 
    divee.cc
    parse_hdb.cc
    hdb_driver.cc
    harmonydb.cc
    execution_engine.cc
    ${FLEX_HdbScanner_OUTPUTS}
    ${BISON_HdbParser_OUTPUTS}
)
target_include_directories(divee PUBLIC "${CMAKE_CURRENT_LIST_DIR}" )
target_link_libraries(divee readline)
