cmake_minimum_required(VERSION 3.16)
# CI passes -DPROJECT_VER=v0.1.<run_number>. Local builds are explicitly dev,
# never an implicit git hash/dirty string masquerading as a release.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "dev")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components")
project(coffee_watch_demo)
