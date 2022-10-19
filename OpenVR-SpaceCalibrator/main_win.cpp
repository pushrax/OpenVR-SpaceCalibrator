#include "stdafx.h"

#include "OpenVR-SpaceCalibrator.h"

#include "Calibration.h"
#include "Configuration.h"
#include "EmbeddedFiles.h"
#include "UserInterface.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#include <openvr.h>
#include <direct.h>
#include "HandleCommandLine.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;

static char cwd[MAX_PATH];

static void HandleCommandLineFunction(LPWSTR lpCmdLine)
{
	if (lstrcmp(lpCmdLine, L"-openvrpath") == 0)
	{
		char cruntimePath[MAX_PATH] = { 0 };
		HandleCommandLine::OpenVRPath(cruntimePath, MAX_PATH);
	}
	else if (lstrcmp(lpCmdLine, L"-installmanifest") == 0)
	{
		HandleCommandLine::InstallManifest(cwd, MAX_PATH);
	}
	else if (lstrcmp(lpCmdLine, L"-removemanifest") == 0)
	{
		HandleCommandLine::RemoveManifest(cwd);
	}
	else if (lstrcmp(lpCmdLine, L"-activatemultipledrivers") == 0)
	{
		HandleCommandLine::ActivateMultipleDrivers();
	}
}

void CreateConsole()
{
	static bool created = false;
	if (!created)
	{
		AllocConsole();
		FILE *file = nullptr;
		freopen_s(&file, "CONIN$", "r", stdin);
		freopen_s(&file, "CONOUT$", "w", stdout);
		freopen_s(&file, "CONOUT$", "w", stderr);
		created = true;
	}
}

void MessageBoxCallback(char * err){
		std::cerr << "Runtime error: " << err << std::endl;
		wchar_t message[1024];
		swprintf(message, 1024, L"%hs", err);
		MessageBox(nullptr, message, L"Runtime Error", 0);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	_getcwd(cwd, MAX_PATH);
	HandleCommandLineFunction(lpCmdLine);

#ifdef DEBUG_LOGS
	CreateConsole();
#endif

	return MainApplication(cwd, MessageBoxCallback);
}


