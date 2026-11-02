# CMake generated Testfile for 
# Source directory: S:/native
# Build directory: S:/build-published-references/native
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("gaussian_regression" "S:/build-published-references/native/si_regression.exe" "S:/native/../tests/fixtures")
set_tests_properties("gaussian_regression" PROPERTIES  _BACKTRACE_TRIPLES "S:/native/CMakeLists.txt;36;add_test;S:/native/CMakeLists.txt;0;")
add_test("document_regression" "S:/build-published-references/native/si_docx_smoke.exe")
set_tests_properties("document_regression" PROPERTIES  _BACKTRACE_TRIPLES "S:/native/CMakeLists.txt;37;add_test;S:/native/CMakeLists.txt;0;")
add_test("ui_smoke" "S:/build-published-references/native/si_ui_smoke.exe" "S:/native/../tests/fixtures/native_geometry.log")
set_tests_properties("ui_smoke" PROPERTIES  _BACKTRACE_TRIPLES "S:/native/CMakeLists.txt;70;add_test;S:/native/CMakeLists.txt;0;")
