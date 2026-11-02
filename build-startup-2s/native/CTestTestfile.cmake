# CMake generated Testfile for 
# Source directory: D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native
# Build directory: D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-startup-2s/native
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("gaussian_regression" "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-startup-2s/native/si_regression.exe" "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/../tests/fixtures")
set_tests_properties("gaussian_regression" PROPERTIES  _BACKTRACE_TRIPLES "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;36;add_test;D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;0;")
add_test("document_regression" "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-startup-2s/native/si_docx_smoke.exe")
set_tests_properties("document_regression" PROPERTIES  _BACKTRACE_TRIPLES "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;37;add_test;D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;0;")
add_test("ui_smoke" "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-startup-2s/native/si_ui_smoke.exe" "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/../tests/fixtures/native_geometry.log")
set_tests_properties("ui_smoke" PROPERTIES  _BACKTRACE_TRIPLES "D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;80;add_test;D:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;0;")
