cmake_minimum_required(VERSION 3.21)

# =============================================================================
#  morfMonitor — squelette reutilisable de service morfSystem.
#
#  POUR CREER UN NOUVEAU SERVICE : le plus simple est le script de clonage
#      scripts/new-service.sh   morfmonservice MorfMonService   (Linux/macOS)
#      scripts/new-service.ps1  morfmonservice MorfMonService   (Windows)
#  qui copie ce projet en remplacant les noms. Sinon, remplacer a la main :
#      morfMonitor  (nom du projet / bibliotheque)
#      morfmonitor         (namespace, dossier include, binaire, unite systemd)
#      MORFMONITOR         (prefixe de macro)
#
#  Ensuite : coder le metier dans src/ExampleModule.* (ou de nouveaux modules),
#  adapter la fabrique (src/ModuleFactory.cpp) et la liste de sources ci-dessous.
# =============================================================================

# Version pilotee par le fichier VERSION (convention morfSystem).
set(MORFMONITOR_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MORFMONITOR_VERSION_FILE}")
file(STRINGS "${MORFMONITOR_VERSION_FILE}" MORFMONITOR_VERSION_FROM_FILE LIMIT_COUNT 1)
string(STRIP "${MORFMONITOR_VERSION_FROM_FILE}" MORFMONITOR_VERSION_FROM_FILE)

project(morfMonitor VERSION "${MORFMONITOR_VERSION_FROM_FILE}" LANGUAGES CXX)

# --- Standard C++ ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Qt : generation automatique du moc (classes QObject) -----------------
set(CMAKE_AUTOMOC ON)
# AUTORCC : compile resources/web.qrc (assets de l'interface Web) dans le
# binaire. Fourni par Qt Core — n'introduit aucune dependance Qt Widgets.
set(CMAKE_AUTORCC ON)

# Core + Network : serveur HTTP entrant, client HTTP sortant, JSON. Ajouter
# ici les modules Qt dont le METIER a besoin (SerialPort, Sql, ...).
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# --- Dependance morfBeacon (annonce de presence sur le LAN) ---------------
# morfBeacon est EMBARQUE (copie vendoree) dans third_party/morf/beacon, lie
# statiquement : le projet se compile sans dependre d'aucun depot externe, a
# l'identique sous Windows, Linux x64 et Raspberry Pi (ARM64).
# Ne pas editer third_party/ ; resynchroniser avec scripts/sync-morf.(sh|ps1).
if(NOT TARGET morfBeacon::morfBeacon)
    add_subdirectory(third_party/morf/beacon)
endif()

# --- Bibliotheque ---------------------------------------------------------
# >>> A ADAPTER : ajouter/retirer ici les sources de VOS modules metier. <<<
add_library(morfMonitor STATIC
    resources/web.qrc                     # interface Web (assets embarques)
    src/ModuleRegistry.cpp
    src/HttpServer.cpp
    src/Service.cpp
    src/ModuleFactory.cpp
    src/MonitorModule.cpp
    src/SharedConfig.cpp
    src/HostCollectors.cpp
    src/Supervisor.cpp                 # <-- remplacer par votre metier
    include/morfmonitor/IModule.h
    include/morfmonitor/ModuleRegistry.h
    include/morfmonitor/ServiceConfig.h
    include/morfmonitor/ModuleFactory.h
    include/morfmonitor/HttpServer.h
    include/morfmonitor/Service.h
    include/morfmonitor/MonitorModule.h
    include/morfmonitor/SharedConfig.h
    include/morfmonitor/Collectors.h  # <-- remplacer par votre metier
    include/morfmonitor/Version.h
)
add_library(morfMonitor::morfMonitor ALIAS morfMonitor)

target_include_directories(morfMonitor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(morfMonitor
    PUBLIC  Qt6::Core Qt6::Network
    PRIVATE morfBeacon::morfBeacon)

# Numero de version accessible depuis le code.
target_compile_definitions(morfMonitor PUBLIC MORFMONITOR_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(morfMonitor PRIVATE /utf-8)
endif()

# --- Cible service (le demon autonome) ------------------------------------
add_subdirectory(service)

# --- Exemple (compile seulement en build autonome, pas via add_subdirectory) -
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_morfmonitor_standalone ON)
else()
    set(_morfmonitor_standalone OFF)
endif()
option(MORFMONITOR_BUILD_EXAMPLES "Compiler l'exemple de demonstration" ${_morfmonitor_standalone})

if(MORFMONITOR_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
endif()
