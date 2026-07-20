# Demon de service morfMonitor : execute la bibliotheque en tache de fond.
# Le binaire s'appelle « morfmonitor » (comme l'unite systemd et /opt/morfmonitor).
add_executable(morfmonitor main.cpp)

target_link_libraries(morfmonitor PRIVATE morfMonitor::morfMonitor Qt6::Core)

if(MSVC)
    target_compile_options(morfmonitor PRIVATE /utf-8)
endif()

install(TARGETS morfmonitor RUNTIME DESTINATION bin)
