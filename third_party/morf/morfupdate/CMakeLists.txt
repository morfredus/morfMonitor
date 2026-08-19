# morfUpdate — copie vendorée (détection de version via GitHub Releases).
#
# Source de vérité : dépôt morfUpdate. Ce dossier en est une copie synchronisée
# par scripts/sync-morf.(ps1|sh). NE PAS éditer le code ici : modifier dans le
# dépôt puis resynchroniser. Version vendorée : voir VERSION.
#
# Cœur SEULEMENT (Qt Core + Network) : pas de couche UI Widgets ni d'exemples.
# morfMonitor est un service headless ; il n'a pas besoin du dialogue de
# notification (src/UpdateDialog.cpp, présent dans la copie mais non compilé).

cmake_minimum_required(VERSION 3.21)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
set(CMAKE_AUTOMOC ON)

# Version vendorée, injectée comme define (même convention que le dépôt source).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MORFUPDATE_VENDORED_VERSION LIMIT_COUNT 1)
string(STRIP "${MORFUPDATE_VENDORED_VERSION}" MORFUPDATE_VENDORED_VERSION)

add_library(morfUpdate STATIC
    src/Version.cpp
    src/GitHubReleaseSource.cpp
    src/UpdateChecker.cpp
    include/morfupdate/Version.h
    include/morfupdate/ReleaseInfo.h
    include/morfupdate/morfUpdateConfig.h
    include/morfupdate/IUpdateSource.h
    include/morfupdate/GitHubReleaseSource.h
    include/morfupdate/UpdateChecker.h
)
add_library(morfUpdate::morfUpdate ALIAS morfUpdate)

target_include_directories(morfUpdate PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(morfUpdate PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(morfUpdate PUBLIC MORFUPDATE_VERSION="${MORFUPDATE_VENDORED_VERSION}")

if(MSVC)
    target_compile_options(morfUpdate PRIVATE /utf-8)
endif()
