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

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent OpenGLWidgets Test)
qt_standard_project_setup()

add_library(si_calculation STATIC
  src/GaussianParser.cpp
  src/SIModel.cpp
  src/StructureLabels.cpp
)
target_include_directories(si_calculation PUBLIC include)
target_link_libraries(si_calculation PUBLIC Qt6::Core)

add_library(si_project STATIC
  src/ProjectStore.cpp
  src/ProjectHistory.cpp
  src/GaussianSnapshot.cpp
)
target_link_libraries(si_project PUBLIC si_calculation)

add_library(si_document STATIC
  src/DocumentModel.cpp
  src/DocxWriter.cpp
  src/XyzWriter.cpp
)
target_link_libraries(si_document PUBLIC si_calculation)

add_executable(si_parse src/parse_main.cpp)
target_link_libraries(si_parse PRIVATE si_calculation)
add_executable(si_benchmark src/benchmark_main.cpp)
target_link_libraries(si_benchmark PRIVATE si_calculation)
add_executable(si_docx_smoke src/docx_smoke.cpp)
target_link_libraries(si_docx_smoke PRIVATE si_document)
add_executable(si_regression src/regression_main.cpp)
target_link_libraries(si_regression PRIVATE si_project si_document)
if(BUILD_TESTING)
  add_test(NAME gaussian_regression COMMAND si_regression "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures")
  add_test(NAME document_regression COMMAND si_docx_smoke)
  add_executable(si_history_regression src/history_regression.cpp)
  target_link_libraries(si_history_regression PRIVATE si_project)
  add_test(NAME project_history_regression COMMAND si_history_regression)
endif()

qt_add_library(si_editor STATIC
  src/MainWindow.cpp src/MainWindow.hpp
  src/GaussianImport.cpp
  src/ReactionWorkspace.cpp
  src/ProjectWorkspace.cpp
  src/ProjectHistoryDialog.cpp
  src/ProjectManagerDialog.cpp src/ProjectManagerDialog.hpp
  src/DocumentAssembly.cpp
  src/DocumentExport.cpp
  src/DocumentPreviewWidget.cpp src/DocumentPreviewWidget.hpp
  src/MolecularStyle.cpp src/MolecularStyle.hpp
  src/FigureSetupDialog.cpp src/FigureSetupDialog.hpp
  src/MoleculeViewWidget.cpp src/MoleculeViewWidget.hpp
)
target_link_libraries(si_editor PUBLIC si_project si_document Qt6::Widgets Qt6::Concurrent Qt6::OpenGLWidgets)

qt_add_executable(SIGenerator src/main.cpp)
if(WIN32)
  enable_language(RC)
  target_sources(SIGenerator PRIVATE resources/si-generator.rc)
  target_include_directories(SIGenerator PRIVATE resources)
endif()
target_link_libraries(SIGenerator PRIVATE si_editor)
set(SI_GENERATOR_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}" CACHE PATH "Application output directory")
set_target_properties(SIGenerator PROPERTIES
  WIN32_EXECUTABLE TRUE
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${SI_GENERATOR_OUTPUT_DIRECTORY}"
)

qt_add_executable(si_ui_smoke src/ui_smoke_main.cpp)
target_link_libraries(si_ui_smoke PRIVATE si_editor Qt6::Test)
if(BUILD_TESTING)
  add_test(NAME ui_smoke COMMAND si_ui_smoke "${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/native_geometry.log")
endif()

install(TARGETS SIGenerator RUNTIME DESTINATION .)

foreach(si_gui_target SIGenerator si_ui_smoke)
  qt_add_resources(${si_gui_target} si_document_references
    PREFIX "/si" BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES resources/si-generator.png resources/recovered_computational_references.txt resources/published_computational_references.json)
endforeach()
