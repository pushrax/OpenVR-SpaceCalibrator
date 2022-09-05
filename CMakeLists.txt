cmake_minimum_required(VERSION 3.10)

project (OpenVR-SpaceCalibrator VERSION 1.0)
add_definitions(-D__linux__)

set(CMAKE_BUILD_TYPE Debug)
set(DRIVER_LOG_FILE "/tmp/spaceCalDriverLog.txt" CACHE PATH "Driver Log Path" )
set(LINUX_CONFIG_DIR, ".config/OpenVR-SpaceCalibrator" )

add_library(imgui 
	lib/imgui/imgui.cpp
	lib/imgui/imgui_demo.cpp
	lib/imgui/imgui_draw.cpp
	lib/imgui/imgui_impl_glfw.cpp
	lib/imgui/imgui_impl_opengl3.cpp
    modules/imgui/backends/imgui_impl_glfw.cpp
)

find_library(glfw libglfw.so)
find_library(GL libGL.so)
find_library( openvr_api  libopenvr_api.so PATHS ./modules/openvr/bin/linux64/libopenvr_api.so )

include_directories(OpenVR-SpaceCalibratorDriver)
include_directories(/usr/include/eigen3)
include_directories(lib/gl3w/include)
include_directories(lib)
include_directories(modules/openvr/headers)

add_library( gl3w lib/gl3w/src/gl3w.c )

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

#######################################################################################
set( APP_MANIFEST_PATH ${CMAKE_INSTALL_PREFIX}/share/openvr-spacecalibrator/manifest.vrmanifest )
set( DRIVER_INSTALLER_PATH ${CMAKE_INSTALL_PREFIX}/share/openvr-spacecalibrator )
set( DRIVER_MANIFEST_PATH ${CMAKE_INSTALL_PREFIX}/lib/steamvr/OpenVR-SpaceCalibrator/01spacecalibrator )
configure_file( StaticConfig.h.in StaticConfig.h )

include_directories("${CMAKE_CURRENT_BINARY_DIR}")

configure_file( ./OpenVR-SpaceCalibrator/manifest.vrmanifest ${CMAKE_CURRENT_BINARY_DIR}/manifest.vrmanifest )
configure_file( ./OpenVR-SpaceCalibratorDriver/01spacecalibrator/driver.vrdrivermanifest ${CMAKE_CURRENT_BINARY_DIR}/driver.vrdrivermanifest )
configure_file( ./driverInstall.py ${CMAKE_CURRENT_BINARY_DIR}/driverInstall.py )


#######################################################################################
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/manifest.vrmanifest
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/openvr-spacecalibrator
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/driver.vrdrivermanifest
    DESTINATION lib/steamvr/OpenVR-SpaceCalibrator/01spacecalibrator
)

install(
    TARGETS OpenVR-SpaceCalibrator 
)
install(
    TARGETS driver_01spacecalibrator
    DESTINATION lib/steamvr/OpenVR-SpaceCalibrator/01spacecalibrator/bin/linux64/
)

install(
    FILES driverInstall.py
    DESTINATION "${DRIVER_INSTALLER_PATH}"
)

find_package(Git QUIET)

if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    # Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

