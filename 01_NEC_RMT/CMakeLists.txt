cmake_minimum_required(VERSION 3.16)

# Añadir la ruta de componentes antes de incluir project.cmake
set(EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(01_NEC_RMT)