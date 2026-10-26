add_executable(checks checks.cpp)
target_link_libraries(checks PRIVATE core psapi)