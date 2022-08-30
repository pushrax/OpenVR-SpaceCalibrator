cmake_minimum_required(VERSION 3.10)

project (OpenVR-SpaceCalibrator VERSION 1.0)
add_definitions(-D__linux__)

add_library(imgui 
	lib/imgui/imgui.cpp
	lib/imgui/imgui_demo.cpp
	lib/imgui/imgui_draw.cpp
	lib/imgui/imgui_impl_glfw.cpp
	lib/imgui/imgui_impl_opengl3.cpp
    modules/imgui/backends/imgui_impl_glfw.cpp
)

find_library(glfw libglfw.so)
find_library(GL libgl.so)
find_library(openvr_api  openvr_api.so)

include_directories(OpenVR-SpaceCalibratorDriver)
include_directories(/usr/include/eigen3)
include_directories(/usr/include/openvr)
include_directories(lib/gl3w/include)
include_directories(lib)
include_directories(modules/openvr/headers)

add_library(gl3w lib/gl3w/src/gl3w.c)

#configure_file(LinixConfig.h.in LinuxConfig.h)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(OpenVR-SpaceCalibrator 
    OpenVR-SpaceCalibrator/OpenVR-SpaceCalibrator.cpp
    OpenVR-SpaceCalibrator/Calibration.cpp
    OpenVR-SpaceCalibrator/Configuration.cpp
    OpenVR-SpaceCalibrator/EmbeddedFiles.cpp
    OpenVR-SpaceCalibrator/IPCClient.cpp
    OpenVR-SpaceCalibrator/OpenVR-SpaceCalibrator.cpp
    OpenVR-SpaceCalibrator/UserInterface.cpp
    OpenVR-SpaceCalibrator/stdafx.cpp
    ./OpenVR-SpaceCalibratorDriver/compat.cpp
    ./OpenVR-SpaceCalibratorDriver/Logging.cpp
)
TARGET_LINK_LIBRARIES(OpenVR-SpaceCalibrator PRIVATE openvr_api imgui gl3w GL glfw)

add_library(
    driver_01spacecalibrator
    SHARED
    OpenVR-SpaceCalibratorDriver/OpenVR-SpaceCalibratorDriver.cpp
    OpenVR-SpaceCalibratorDriver/Hooking.cpp
    OpenVR-SpaceCalibratorDriver/IPCServer.cpp
    OpenVR-SpaceCalibratorDriver/InterfaceHookInjector.cpp
    OpenVR-SpaceCalibratorDriver/Logging.cpp
    OpenVR-SpaceCalibratorDriver/ServerTrackedDeviceProvider.cpp
    OpenVR-SpaceCalibratorDriver/compat.cpp
    OpenVR-SpaceCalibratorDriver/dllmain.cpp
)

set_target_properties(driver_01spacecalibrator PROPERTIES
    OUTPUT_NAME driver_01spacecalibrator
    PREFIX ""
)

configure_file( ./OpenVR-SpaceCalibrator/manifest.vrmanifest manifest.vrmanifest )
configure_file( ./OpenVR-SpaceCalibratorDriver/01spacecalibrator/driver.vrdrivermanifest driver.vrdrivermanifest )
