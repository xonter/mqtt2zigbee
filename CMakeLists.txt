cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "1.1.0")
project(mqtt2zigbee)
