cmake_minimum_required(VERSION 3.24)
project(SIGeneratorNative VERSION 0.1.0 LANGUAGES CXX)
include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc /O2)
  add_link_options(/OPT:REF /OPT:ICF)
endif()

add_library(si_core STATIC
  src/GaussianParser.cpp
  src/SIModel.cpp
  src/DocumentModel.cpp
  src/StructureLabels.cpp
  src/ProjectStore.cpp
  src/ProjectHistory.cpp
  src/GaussianSnapshot.cpp
  src/DocxWriter.cpp
  src/XyzWriter.cpp
)
target_include_directories(si_core PUBLIC include)

add_executable(si_parse src/parse_main.cpp)
target_link_libraries(si_parse PRIVATE si_core)

add_executable(si_docx_smoke src/docx_smoke.cpp)
target_link_libraries(si_docx_smoke PRIVATE si_core)

add_executable(si_benchmark src/benchmark_main.cpp)
target_link_libraries(si_benchmark PRIVATE si_core)
add_executable(si_regression src/regression_main.cpp)
target_link_libraries(si_regression PRIVATE si_core)
if(BUILD_TESTING)
  add_test(NAME gaussian_regression COMMAND si_regression "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures")
  add_test(NAME document_regression COMMAND si_docx_smoke)
  add_executable(si_history_regression src/history_regression.cpp)
  target_link_libraries(si_history_regression PRIVATE si_core)
  add_test(NAME project_history_regression COMMAND si_history_regression)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent OpenGLWidgets Test)
target_link_libraries(si_core PUBLIC Qt6::Core)
qt_standard_project_setup()
qt_add_executable(SIGenerator
  src/main.cpp
  src/MolecularStyle.cpp src/MolecularStyle.hpp
  src/FigureSetupDialog.cpp src/FigureSetupDialog.hpp
  src/ReactionWorkspace.cpp
  src/ProjectHistoryDialog.cpp
  src/ProjectManagerDialog.cpp src/ProjectManagerDialog.hpp
  src/MainWindow.cpp
  src/MainWindow.hpp
  src/MoleculeViewWidget.cpp
  src/MoleculeViewWidget.hpp
  src/DocumentPreviewWidget.cpp
  src/DocumentPreviewWidget.hpp
)
if(WIN32)
  enable_language(RC)
  target_sources(SIGenerator PRIVATE resources/si-generator.rc)
  target_include_directories(SIGenerator PRIVATE resources)
endif()
target_link_libraries(SIGenerator PRIVATE si_core Qt6::Widgets Qt6::Concurrent Qt6::OpenGLWidgets)
set(SI_GENERATOR_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}" CACHE PATH "Application output directory")
set_target_properties(SIGenerator PROPERTIES
  WIN32_EXECUTABLE TRUE
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${SI_GENERATOR_OUTPUT_DIRECTORY}"
)

qt_add_executable(si_ui_smoke
  src/ui_smoke_main.cpp
  src/MolecularStyle.cpp src/MolecularStyle.hpp
  src/FigureSetupDialog.cpp src/FigureSetupDialog.hpp
  src/ReactionWorkspace.cpp
  src/ProjectHistoryDialog.cpp
  src/ProjectManagerDialog.cpp src/ProjectManagerDialog.hpp
  src/MainWindow.cpp src/MainWindow.hpp
  src/MoleculeViewWidget.cpp src/MoleculeViewWidget.hpp
  src/DocumentPreviewWidget.cpp src/DocumentPreviewWidget.hpp
)
target_link_libraries(si_ui_smoke PRIVATE si_core Qt6::Widgets Qt6::Concurrent Qt6::OpenGLWidgets Qt6::Test)
if(BUILD_TESTING)
  add_test(NAME ui_smoke COMMAND si_ui_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/native_geometry.log")
endif()

install(TARGETS SIGenerator RUNTIME DESTINATION .)

foreach(si_gui_target SIGenerator si_ui_smoke)
  qt_add_resources(${si_gui_target} si_document_references
    PREFIX "/si" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES resources/si-generator.png resources/recovered_computational_references.txt resources/published_computational_references.json)
endforeach()
