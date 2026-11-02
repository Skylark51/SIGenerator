# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  [[native\CMakeFiles\SIGenerator_autogen.dir\AutogenUsed.txt]]
  [[native\CMakeFiles\SIGenerator_autogen.dir\ParseCache.txt]]
  [[native\CMakeFiles\si_ui_smoke_autogen.dir\AutogenUsed.txt]]
  [[native\CMakeFiles\si_ui_smoke_autogen.dir\ParseCache.txt]]
  [[native\SIGenerator_autogen]]
  [[native\si_ui_smoke_autogen]]
  )
endif()
