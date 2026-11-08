cmake_minimum_required(VERSION 3.21)
project(FlibusteAlign VERSION 0.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# MSVC : <windows.h> définit des macros min et max qui cassent std::numeric_limits<T>::max() (Rubber Band et le code de JUCE s'en protègent ainsi).
if(MSVC)
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
endif()

# Le cœur (core/) n'a aucune dépendance : alignement seul. Les étireurs (Rubber Band, Signalsmith Stretch) sont dans tools/ pour l'essai.
add_library(align_core STATIC core/src/Align.cpp)
target_include_directories(align_core PUBLIC core/include)
set_target_properties(align_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Rendu : les trois étireurs. Rubber Band (GPL 2+) en un seul fichier, Signalsmith Stretch (MIT) en-têtes seuls.
set(TP ${CMAKE_SOURCE_DIR}/third_party)
add_library(rubberband_single STATIC ${TP}/rubberband/single/RubberBandSingle.cpp)
target_include_directories(rubberband_single PUBLIC ${TP}/rubberband)
set_target_properties(rubberband_single PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(align_render STATIC core/src/Render.cpp core/src/Pipeline.cpp)
target_include_directories(align_render PUBLIC core/include PRIVATE ${TP})
target_link_libraries(align_render PUBLIC align_core PRIVATE rubberband_single)
set_target_properties(align_render PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(ALIGN_BUILD_PLUGIN "Plugin CLAP (JUCE + clap-juce-extensions)" ON)
option(ALIGN_BUILD_TESTS "Tests du cœur" ON)
option(ALIGN_BUILD_TOOLS "Outils d'essai (comparaison des étireurs)" ON)
if(ALIGN_BUILD_TESTS OR ALIGN_BUILD_TOOLS OR ALIGN_BUILD_PLUGIN)
  include(FetchContent)
endif()
# JUCE 8.0.11 et clap-juce-extensions au commit 7adee3a : les mêmes versions que Neptune (combinaison dont la CI amont est verte).
if(ALIGN_BUILD_PLUGIN)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.11
    GIT_SHALLOW    TRUE)
  FetchContent_Declare(clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
    GIT_SUBMODULES "clap-libs/clap" "clap-libs/clap-helpers")
  FetchContent_MakeAvailable(JUCE clap_juce_extensions)
endif()
if(ALIGN_BUILD_TESTS)
  FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.16.1 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  enable_testing()
  add_subdirectory(tests)
endif()
if(ALIGN_BUILD_TOOLS)
  find_package(SndFile CONFIG QUIET)
  add_subdirectory(tools)
endif()
if(ALIGN_BUILD_PLUGIN)
  add_subdirectory(adapter-juce)
endif()
