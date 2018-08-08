#include "stdafx.h"
#include "Calibration.h"
#include "Configuration.h"
#include "EmbeddedFiles.h"
#include "UserInterface.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

void GLFWErrorCallback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

GLFWwindow *glfwWindow = nullptr;

void CreateGLFWWindow()
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	glfwWindow = glfwCreateWindow(1280, 800, "OpenVR-SpaceCalibrator", NULL, NULL);
	if (!glfwWindow)
		throw std::runtime_error("Failed to create window");

	glfwMakeContextCurrent(glfwWindow);
	glfwSwapInterval(1);
	gl3wInit();

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;
	io.Fonts->AddFontFromMemoryCompressedTTF(DroidSans_compressed_data, DroidSans_compressed_size, 24.0f);

	ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
	ImGui_ImplOpenGL3_Init("#version 150");

	ImGui::StyleColorsDark();
}

void RunLoop()
{
	while (!glfwWindowShouldClose(glfwWindow))
	{
		double time = glfwGetTime();
		CalibrationTick(time);

		int width, height;
		glfwGetFramebufferSize(glfwWindow, &width, &height);

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		BuildMainWindow();

		ImGui::Render();

		glViewport(0, 0, width, height);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(glfwWindow);
		glfwWaitEventsTimeout(CalCtx.wantedUpdateInterval);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	//CreateConsole();

	if (lstrcmp(lpCmdLine, L"-openvrpath") == 0)
	{
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None)
		{
			printf("%s", vr::VR_RuntimePath());
			vr::VR_Shutdown();
			Sleep(2000);
			return 0;
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		return -2;
	}

	if (!glfwInit())
	{
		MessageBox(nullptr, L"Failed to initialize GLFW", L"", 0);
		return 0;
	}

	glfwSetErrorCallback(GLFWErrorCallback);

	try {
		InitVR();
		CreateGLFWWindow();
		LoadProfile(CalCtx);
		RunLoop();
	}
	catch (std::runtime_error &e)
	{
		std::cerr << "Runtime error: " << e.what() << std::endl;
		wchar_t message[1024];
		swprintf(message, 1024, L"%hs", e.what());
		MessageBox(nullptr, message, L"Runtime Error", 0);
	}

	if (glfwWindow)
		glfwDestroyWindow(glfwWindow);

	glfwTerminate();
	return 0;
}