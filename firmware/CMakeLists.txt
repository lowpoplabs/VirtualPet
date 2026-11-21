cmake_minimum_required(VERSION 3.16)

# Shared app code lives outside the firmware folder so the simulator can use it too.
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../app)

# The version is version.txt; changing it re-runs this step, so the next
# build carries the new number.
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/version.txt PROJECT_VER LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/version.txt)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(virtualpet)
