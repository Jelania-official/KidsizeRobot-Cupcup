cmake_minimum_required(VERSION 3.16)
project(cupcup_match_trace)
set(CMAKE_CXX_STANDARD 14)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(common REQUIRED)
set(WEBOTS_HOME /usr/local/webots)
get_filename_component(CUPCUP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(match_trace_supervisor
  supervisor.cpp
  "${CUPCUP_ROOT}/src/simulation/controller/src/WebotsUtils.cpp")
target_include_directories(match_trace_supervisor PRIVATE
  "${WEBOTS_HOME}/include/controller/cpp"
  "${CUPCUP_ROOT}/src/simulation/controller/src")
target_link_directories(match_trace_supervisor PRIVATE "${WEBOTS_HOME}/lib/controller")
target_link_libraries(match_trace_supervisor CppController)
ament_target_dependencies(match_trace_supervisor rclcpp common)
