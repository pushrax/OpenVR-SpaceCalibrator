#include "Logging.h"
#include "../Version.h"

#if  !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>

__attribute__((constructor)) 
static void loaded(){
    int pid = getpid();
    LOG("OpenVR-SpaceCalibratorDriver " SPACECAL_VERSION_STRING " loaded into pid %d", pid);
}

__attribute__((destructor)) 
static void unloaded(){
    int pid = getpid();
    LOG("OpenVR-SpaceCalibratorDriver " SPACECAL_VERSION_STRING " unloaded from pid %d", pid);
}



#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		OpenLogFile();
		LOG("OpenVR-SpaceCalibratorDriver " SPACECAL_VERSION_STRING " loaded");
		break;
	case DLL_PROCESS_DETACH:
		LOG("OpenVR-SpaceCalibratorDriver unloaded");
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}

#endif
