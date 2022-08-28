OFLAGS = 
CC=g++
STD=-std=c++14
SHARED_FLAGS= -g -W -Wall -Wextra $(STD) -Wno-missing-field-initializers -Wshadow \
-I ../OpenVR-SpaceCalibratorDriver/ \
-isystem /usr/include/eigen3/ \
-isystem /home/zack/src/openvr/headers \
-isystem ../gl3w/build/include \
-isystem ../modules/gl3w/build/include \
-isystem ../lib/ \
$(OFLAGS)

CFLAGS=-c $(SHARED_FLAGS) 

LFLAGS= -g $(STD) $(OFLAGS) \
		-L /home/zack/src/openvr/lib/linux64 \
		-lGL -limgui -lopenvr_api -lglfw

.PHONY:clean 

Objects=Calibration.o Configuration.o EmbeddedFiles.o IPCClient.o OpenVR-SpaceCalibrator.o UserInterface.o stdafx.o
Copy=gl3w.o imgui_impl_glfw.o imgui.a Logging.o compat.o

all : $(Objects) spaceCal  

spaceCal : $(Objects) $(Copy)
	$(CC) $(Std) $(Objects) $(Copy) $(LFLAGS) -o spaceCal

$(Objects): %.o: %.cpp
	$(CC) $(CFLAGS) $<

gl3w.o: ../modules/gl3w/build/src/gl3w.c
	$(CC) $(CFLAGS)  ../modules/gl3w/build/src/gl3w.c -o gl3w.o

imgui_impl_glfw.o: ../modules/imgui/backends/imgui_impl_glfw.cpp
	$(CC) $(CFLAGS)  ../modules/imgui/backends/imgui_impl_glfw.cpp -o imgui_impl_glfw.o

imgui.a: ../lib/imgui/*cpp
	rm -f imgui.a
	$(CC) $(CFLAGS)  ../lib/imgui/imgui.cpp
	$(CC) $(CFLAGS)  ../lib/imgui/imgui_demo.cpp
	$(CC) $(CFLAGS)  ../lib/imgui/imgui_draw.cpp
	$(CC) $(CFLAGS)  ../lib/imgui/imgui_impl_glfw.cpp
	$(CC) $(CFLAGS)  ../lib/imgui/imgui_impl_opengl3.cpp
	ar rcs imgui.a imgui.o imgui_demo.o imgui_draw.o imgui_impl_glfw.o imgui_impl_opengl3.o
	
Logging.o: ../OpenVR-SpaceCalibratorDriver/Logging.cpp
	$(CC) $(CFLAGS)  ../OpenVR-SpaceCalibratorDriver/Logging.cpp -o Logging.o

compat.o: ../OpenVR-SpaceCalibratorDriver/compat.cpp
	$(CC) $(CFLAGS)  ../OpenVR-SpaceCalibratorDriver/compat.cpp -o compat.o

dbg: spaceCal
	gdb spaceCal

run: spaceCal
	./spaceCal

time: spaceCal
	time ./spaceCal

cache: spaceCal
	rm c*grind* -f
	valgrind --tool=cachegrind ./spaceCal

call: spaceCal
	rm c*grind* -f
	valgrind --tool=callgrind --collect-jumps=yes --dump-instr=yes ./spaceCal

inspect: 
	kcachegrind c*grind\.out\.*

clean:
	rm -f *o 
	rm -f imgui.a
	rm -f spaceCal
	rm -f c*grind\.out\.*

