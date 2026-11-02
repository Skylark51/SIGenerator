cmake_minimum_required(VERSION 3.24)
project(SIGeneratorWorkspace LANGUAGES CXX)
include(CTest)

# Native application; the retained Python package is not part of the release build.
add_subdirectory(native)
