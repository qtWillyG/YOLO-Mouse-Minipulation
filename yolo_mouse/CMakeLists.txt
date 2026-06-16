cmake_minimum_required(VERSION 3.15)
project(yolo_mouse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point this at the extracted ONNX Runtime package, e.g.
#   cmake -DONNXRUNTIME_DIR="C:/onnxruntime-win-x64-1.20.1" ..
if(NOT DEFINED ONNXRUNTIME_DIR)
    message(FATAL_ERROR "Set -DONNXRUNTIME_DIR=path/to/onnxruntime-win-x64-x.y.z")
endif()

add_executable(yolo_mouse src/main.cpp)

target_include_directories(yolo_mouse PRIVATE "${ONNXRUNTIME_DIR}/include")
target_link_libraries(yolo_mouse PRIVATE "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib")

# Copy the ONNX Runtime DLL and config next to the executable after build.
add_custom_command(TARGET yolo_mouse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:yolo_mouse>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config.ini"
            "$<TARGET_FILE_DIR:yolo_mouse>")
