cmake_minimum_required(VERSION 3.16)
project(cupcup_kick_calibration)
set(CMAKE_CXX_STANDARD 14)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(common REQUIRED)
find_package(sensor_msgs REQUIRED)
set(WEBOTS_HOME /usr/local/webots)
get_filename_component(CUPCUP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(calibration_supervisor
  supervisor.cpp
  "${CUPCUP_ROOT}/src/simulation/controller/src/WebotsUtils.cpp")
target_include_directories(calibration_supervisor PRIVATE
  "${WEBOTS_HOME}/include/controller/cpp"
  "${CUPCUP_ROOT}/src/simulation/controller/src")
target_link_directories(calibration_supervisor PRIVATE "${WEBOTS_HOME}/lib/controller")
target_link_libraries(calibration_supervisor CppController)
ament_target_dependencies(calibration_supervisor rclcpp common sensor_msgs)
