cmake_minimum_required(VERSION 3.21)

# =============================================================================
#  morfAnalytics — moteur d'analyse de l'ecosysteme morfSystem.
#
#  Recopie l'historique d'un equipement dans un cache de travail local, puis
#  execute a la demande des analyses sur cette copie. L'equipement reste la
#  source de verite : le service n'emet que des requetes de lecture.
#
#  Organisation des sources :
#      src/collect/    recopie incrementale depuis l'equipement
#      src/data/       cache de travail (SQLite) et serie temporelle generique
#      src/analysis/   moteur d'analyse et jeu d'analyses meteo
# =============================================================================

# Version pilotee par le fichier VERSION (convention morfSystem).
set(MORFANALYTICS_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MORFANALYTICS_VERSION_FILE}")
file(STRINGS "${MORFANALYTICS_VERSION_FILE}" MORFANALYTICS_VERSION_FROM_FILE LIMIT_COUNT 1)
string(STRIP "${MORFANALYTICS_VERSION_FROM_FILE}" MORFANALYTICS_VERSION_FROM_FILE)

project(morfAnalytics VERSION "${MORFANALYTICS_VERSION_FROM_FILE}" LANGUAGES CXX)

# --- Standard C++ ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Qt : generation automatique du moc (classes QObject) -----------------
set(CMAKE_AUTOMOC ON)

# Core + Network : serveur HTTP entrant, client HTTP sortant, JSON.
# Sql : cache de travail local en SQLite (pilote QSQLITE, fourni avec Qt — aucune
# dependance supplementaire a installer sur le Raspberry Pi).
find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)

# --- Dependance morfBeacon (annonce de presence sur le LAN) ---------------
# morfBeacon est EMBARQUE (copie vendoree) dans third_party/morf/beacon, lie
# statiquement : le projet se compile sans dependre d'aucun depot externe, a
# l'identique sous Windows, Linux x64 et Raspberry Pi (ARM64).
# Ne pas editer third_party/ ; resynchroniser avec scripts/sync-morf.(sh|ps1).
if(NOT TARGET morfBeacon::morfBeacon)
    add_subdirectory(third_party/morf/beacon)
endif()

# --- Bibliotheque ---------------------------------------------------------

add_library(morfAnalytics STATIC
    src/ModuleRegistry.cpp
    src/HttpServer.cpp
    src/Service.cpp
    src/ModuleFactory.cpp
    src/AnalyticsModule.cpp               # logique metier : moteur d'analyse
    src/data/SampleStore.cpp              # cache de travail local (SQLite)
    src/collect/MeteoHubCollector.cpp     # collecte incrementale depuis MeteoHub
    src/analysis/AnalysisRegistry.cpp     # moteur d'analyse (generique)
    src/analysis/MeteoMath.cpp            # formules meteorologiques (pures)
    src/analysis/MeteoAnalyses.cpp        # jeu d'analyses meteo
    include/morfanalytics/analysis/IAnalysis.h
    include/morfanalytics/analysis/AnalysisRegistry.h
    include/morfanalytics/analysis/MeteoMath.h
    include/morfanalytics/data/Series.h
    include/morfanalytics/data/SampleStore.h
    include/morfanalytics/collect/MeteoHubCollector.h
    include/morfanalytics/IModule.h
    include/morfanalytics/ModuleRegistry.h
    include/morfanalytics/ServiceConfig.h
    include/morfanalytics/ModuleFactory.h
    include/morfanalytics/HttpServer.h
    include/morfanalytics/Service.h
    include/morfanalytics/AnalyticsModule.h # logique metier : moteur d'analyse
    include/morfanalytics/Version.h
)
add_library(morfAnalytics::morfAnalytics ALIAS morfAnalytics)

target_include_directories(morfAnalytics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(morfAnalytics
    PUBLIC  Qt6::Core Qt6::Network Qt6::Sql
    PRIVATE morfBeacon::morfBeacon)

# Numero de version accessible depuis le code.
target_compile_definitions(morfAnalytics PUBLIC MORFANALYTICS_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(morfAnalytics PRIVATE /utf-8)
endif()

# --- Cible service (le demon autonome) ------------------------------------
add_subdirectory(service)

# --- Exemple (compile seulement en build autonome, pas via add_subdirectory) -
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_morfanalytics_standalone ON)
else()
    set(_morfanalytics_standalone OFF)
endif()
option(MORFANALYTICS_BUILD_EXAMPLES "Compiler l'exemple de demonstration" ${_morfanalytics_standalone})

if(MORFANALYTICS_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
endif()
