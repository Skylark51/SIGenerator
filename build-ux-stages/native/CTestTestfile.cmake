# CMake generated Testfile for 
# Source directory: E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native
# Build directory: E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-ux-stages/native
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("gaussian_regression" "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-ux-stages/native/si_regression.exe" "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/../tests/fixtures")
set_tests_properties("gaussian_regression" PROPERTIES  _BACKTRACE_TRIPLES "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;36;add_test;E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;0;")
add_test("document_regression" "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-ux-stages/native/si_docx_smoke.exe")
set_tests_properties("document_regression" PROPERTIES  _BACKTRACE_TRIPLES "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;37;add_test;E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;0;")
add_test("ui_smoke" "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/build-ux-stages/native/si_ui_smoke.exe" "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/../tests/fixtures/native_geometry.log")
set_tests_properties("ui_smoke" PROPERTIES  _BACKTRACE_TRIPLES "E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;70;add_test;E:/이영섭/프로젝트 개발/개발프로젝트/SI_generator_관련/SI_Generator_Project/native/CMakeLists.txt;0;")
