file(GLOB_RECURSE SRC_FILES "src/*.cpp" "src/*.c")

add_library(sentinel_core STATIC ${SRC_FILES})

target_include_directories(sentinel_core 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sentinel_core PRIVATE ${PCAP_LIBRARIES} pthread)
