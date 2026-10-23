file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS *.h *.cpp)

add_library(core STATIC ${CORE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CORE_SOURCES})

target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(core PRIVATE pch.h)
target_link_libraries(core PUBLIC shell32 advapi32)
