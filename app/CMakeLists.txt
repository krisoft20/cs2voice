file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS *.h *.cpp *.rc)

add_executable(cs2voice WIN32 ${APP_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${APP_SOURCES})

target_include_directories(cs2voice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(cs2voice SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/webview2/include)
target_precompile_headers(cs2voice PRIVATE pch.h)
target_link_libraries(cs2voice PRIVATE core ${PROJECT_SOURCE_DIR}/third_party/webview2/x64/WebView2LoaderStatic.lib
    ole32 shell32 shlwapi dwmapi version winhttp crypt32 windowscodecs bcrypt windowsapp)

set(CS2VOICE_UI ${PROJECT_SOURCE_DIR}/ui/dist)
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/ui/src/*)
add_custom_command(OUTPUT ${CS2VOICE_UI}/index.html
    COMMAND npm run build
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/ui
    DEPENDS ${UI_SOURCES} ${PROJECT_SOURCE_DIR}/ui/index.html ${PROJECT_SOURCE_DIR}/ui/package.json
            ${PROJECT_SOURCE_DIR}/ui/vite.config.ts
    COMMENT "building the ui"
    VERBATIM)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui.rc
    COMMAND ${CMAKE_COMMAND} -DSRC=${CS2VOICE_UI} -DDST=${CMAKE_CURRENT_BINARY_DIR} -P ${PROJECT_SOURCE_DIR}/cmake/pack_ui.cmake
    DEPENDS ${CS2VOICE_UI}/index.html ${PROJECT_SOURCE_DIR}/cmake/pack_ui.cmake
    COMMENT "packing the ui into the exe"
    VERBATIM)
target_sources(cs2voice PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ui.rc)
