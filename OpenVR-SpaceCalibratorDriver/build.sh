#!/bin/bash -

set -o nounset                                  # Treat unset variables as an error

g++  --std=c++11 -D__linux__ -g \
    IPCServer.cpp \
    InterfaceHookInjector.cpp \
    Logging.cpp \
    OpenVR-SpaceCalibratorDriver.cpp \
    ServerTrackedDeviceProvider.cpp \
    compat.cpp \
    dllmain.cpp \
    Hooking.cpp \
    subhook.a \
    -masm=intel \
    -fPIC -shared \
    -o 01spacecalibrator/bin/linux64/driver_01spacecalibrator.so \
    -isystem /home/zack/src/openvr/headers \
    -isystem $PWD/../modules/subhook/

#-Wall -Wextra \
#cp ../modules/subhook/build/libsubhook.so ./01spacecalibrator/bin/linux64/


