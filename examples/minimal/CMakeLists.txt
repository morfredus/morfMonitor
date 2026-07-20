find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(morfmonitor_demo main.cpp)
target_link_libraries(morfmonitor_demo PRIVATE morfMonitor::morfMonitor Qt6::Core)

if(MSVC)
    target_compile_options(morfmonitor_demo PRIVATE /utf-8)
endif()
