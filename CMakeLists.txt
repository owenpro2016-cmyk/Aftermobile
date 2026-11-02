cmake_minimum_required(VERSION 3.21)
project(InputAssist VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED src/main.cpp)

target_link_libraries(${PROJECT_NAME} Geode::Geode)

geode_mod(${PROJECT_NAME})
