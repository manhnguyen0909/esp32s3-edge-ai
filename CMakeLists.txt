cmake_minimum_required(VERSION 3.3)

set(EXTRA_COMPONENT_DIRS my_components)
set(CMAKE_CXX_STANDARD 17)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cadpro_motion_detection_esp32s3_cam_project)
