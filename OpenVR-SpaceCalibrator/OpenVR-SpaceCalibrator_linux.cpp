#include "stdafx.h"
#include "Calibration.h"
#include "Configuration.h"
#include "EmbeddedFiles.h"
#include "UserInterface.h"

#include <string>
#include <codecvt>
#include <locale>
#include "GL/gl3w.h"

#include <unistd.h>

#include "HandleCommandLine.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#include <openvr.h>

#include "StaticConfig.h"

void GLFWErrorCallback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

void openGLDebugCallback(GLenum /* source */, GLenum /* type */, GLuint id, GLenum /* severity */, GLsizei length, const GLchar *message, const void * /* userParam */ )
{
	fprintf(stderr, "OpenGL Debug %u: %.*s\n", id, length, message);
}

static GLFWwindow *glfwWindow = nullptr;
static vr::VROverlayHandle_t overlayMainHandle = 0, overlayThumbnailHandle = 0;
static GLuint fboHandle = 0, fboTextureHandle = 0;
static int fboTextureWidth = 0, fboTextureHeight = 0;

void CreateGLFWWindow()
{
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, false);

#ifdef DEBUG_LOGS
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

	fboTextureWidth = 1200;
	fboTextureHeight = 800;

	glfwWindow = glfwCreateWindow(fboTextureWidth, fboTextureHeight, "OpenVR-SpaceCalibrator", NULL, NULL);
	if (!glfwWindow)
		throw std::runtime_error("Failed to create window");

	glfwMakeContextCurrent(glfwWindow);
	glfwSwapInterval(1);
	gl3wInit();

	glfwIconifyWindow(glfwWindow);

#ifdef DEBUG_LOGS
	glDebugMessageCallback(openGLDebugCallback, nullptr);
	glEnable(GL_DEBUG_OUTPUT);
#endif

	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = nullptr;
	io.Fonts->AddFontFromMemoryCompressedTTF(DroidSans_compressed_data, DroidSans_compressed_size, 24.0f);

	ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	ImGui::StyleColorsDark();

	glGenTextures(1, &fboTextureHandle);
	glBindTexture(GL_TEXTURE_2D, fboTextureHandle);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fboTextureWidth, fboTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glGenFramebuffers(1, &fboHandle);
	glBindFramebuffer(GL_FRAMEBUFFER, fboHandle);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, fboTextureHandle, 0);

	GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, drawBuffers);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		throw std::runtime_error("OpenGL framebuffer incomplete");
	}
}

void TryCreateVROverlay()
{
	if (overlayMainHandle || !vr::VROverlay())
		return;

	vr::VROverlayError error = vr::VROverlay()->CreateDashboardOverlay(
		"pushrax.SpaceCalibrator", "Space Cal",
		&overlayMainHandle, &overlayThumbnailHandle
	);

	if (error == vr::VROverlayError_KeyInUse)
	{
		throw std::runtime_error("Another instance of OpenVR Space Calibrator is already running");
	}
	else if (error != vr::VROverlayError_None)
	{
		throw std::runtime_error("Error creating VR overlay: " + std::string(vr::VROverlay()->GetOverlayErrorNameFromEnum(error)));
	}

	vr::VROverlay()->SetOverlayWidthInMeters(overlayMainHandle, 3.0f);
	vr::VROverlay()->SetOverlayInputMethod(overlayMainHandle, vr::VROverlayInputMethod_Mouse);
	vr::VROverlay()->SetOverlayFlag(overlayMainHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

	std::string iconPath = cwd;
	iconPath += "\\icon.png";
	vr::VROverlay()->SetOverlayFromFile(overlayThumbnailHandle, iconPath.c_str());
}

void ActivateMultipleDrivers()
{
	vr::EVRSettingsError vrSettingsError;
	bool enabled = vr::VRSettings()->GetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool, &vrSettingsError);

	if (vrSettingsError != vr::VRSettingsError_None)
	{
		std::string err = "Could not read \"" + std::string(vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool) + "\" setting: "
			+ vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);

		throw std::runtime_error(err);
	}

	if (!enabled)
	{
		vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool, true, &vrSettingsError);
		if (vrSettingsError != vr::VRSettingsError_None)
		{
			std::string err = "Could not set \"" + std::string(vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool) + "\" setting: "
				+ vr::VRSettings()->GetSettingsErrorNameFromEnum(vrSettingsError);

			throw std::runtime_error(err);
		}

		std::cerr << "Enabled \"" << vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool << "\" setting" << std::endl;
	}
	else
	{
		std::cerr << "\"" << vr::k_pch_SteamVR_ActivateMultipleDrivers_Bool << "\" setting previously enabled" << std::endl;
	}
}

void InitVR()
{
	auto initError = vr::VRInitError_None;
	vr::VR_Init(&initError, vr::VRApplication_Other);
	if (initError != vr::VRInitError_None)
	{
		auto error = vr::VR_GetVRInitErrorAsEnglishDescription(initError);
		throw std::runtime_error("OpenVR error:" + std::string(error));
	}

	if (!vr::VR_IsInterfaceVersionValid(vr::IVRSystem_Version))
	{
		throw std::runtime_error("OpenVR error: Outdated IVRSystem_Version");
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVRSettings_Version))
	{
		throw std::runtime_error("OpenVR error: Outdated IVRSettings_Version");
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVROverlay_Version))
	{
		throw std::runtime_error("OpenVR error: Outdated IVROverlay_Version");
	}

	ActivateMultipleDrivers();
}

void RunLoop()
{
	while (!glfwWindowShouldClose(glfwWindow))
	{
		TryCreateVROverlay();

		double time = glfwGetTime();
		CalibrationTick(time);

		bool dashboardVisible = false;
		int width, height;
		glfwGetFramebufferSize(glfwWindow, &width, &height);

		if (overlayMainHandle && vr::VROverlay())
		{
			auto &io = ImGui::GetIO();
			dashboardVisible = vr::VROverlay()->IsActiveDashboardOverlay(overlayMainHandle);

			static bool keyboardOpen = false, keyboardJustClosed = false;

			// After closing the keyboard, this code waits one frame for ImGui to pick up the new text from SetActiveText
			// before clearing the active widget. Then it waits another frame before allowing the keyboard to open again,
			// otherwise it will do so instantly since WantTextInput is still true on the second frame.
			if (keyboardJustClosed && keyboardOpen)
			{
				ImGui::ClearActiveID();
				keyboardOpen = false;
			}
			else if (keyboardJustClosed)
			{
				keyboardJustClosed = false;
			}
			else if (!io.WantTextInput)
			{
				// User might close the keyboard without hitting Done, so we unset the flag to allow it to open again.
				keyboardOpen = false;
			}
			else if (io.WantTextInput && !keyboardOpen && !keyboardJustClosed)
			{
				char buf[0x400];
				ImGui::GetActiveText(buf, sizeof buf);
				buf[0x3ff] = 0;
				uint32_t unFlags = 0; // EKeyboardFlags 

				vr::VROverlay()->ShowKeyboardForOverlay(
					overlayMainHandle, vr::k_EGamepadTextInputModeNormal, vr::k_EGamepadTextInputLineModeSingleLine,
					unFlags, "Space Calibrator Overlay", sizeof buf, buf, 0
				);
				keyboardOpen = true;
			}

			vr::VREvent_t vrEvent;
			while (vr::VROverlay()->PollNextOverlayEvent(overlayMainHandle, &vrEvent, sizeof(vrEvent)))
			{
				switch (vrEvent.eventType) {
				case vr::VREvent_MouseMove:
					io.MousePos.x = vrEvent.data.mouse.x;
					//The mouse is flipped in linux
					io.MousePos.y = height - vrEvent.data.mouse.y;
					break;
				case vr::VREvent_MouseButtonDown:
					io.MouseDown[vrEvent.data.mouse.button == vr::VRMouseButton_Left ? 0 : 1] = true;
					break;
				case vr::VREvent_MouseButtonUp:
					io.MouseDown[vrEvent.data.mouse.button == vr::VRMouseButton_Left ? 0 : 1] = false;
					break;
				case vr::VREvent_ScrollDiscrete:
					io.MouseWheelH += vrEvent.data.scroll.xdelta * 360.0f * 8.0f;
					io.MouseWheel += vrEvent.data.scroll.ydelta * 360.0f * 8.0f;
					break;
				case vr::VREvent_KeyboardDone: {
					char buf[0x400];
					vr::VROverlay()->GetKeyboardText(buf, sizeof buf);
					ImGui::SetActiveText(buf, sizeof buf);
					keyboardJustClosed = true;
					break;
				}
				case vr::VREvent_Quit:
					return;
				}
			}
		}

		ImGui::GetIO().DisplaySize = ImVec2((float) fboTextureWidth, (float) fboTextureHeight);

		ImGui_ImplGlfw_SetReadMouseFromGlfw(!dashboardVisible);
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		BuildMainWindow(dashboardVisible);

		ImGui::Render();

		glBindFramebuffer(GL_FRAMEBUFFER, fboHandle);
		glViewport(0, 0, fboTextureWidth, fboTextureHeight);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		if (width && height)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, fboHandle);
			glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			glfwSwapBuffers(glfwWindow);
		}

		if (dashboardVisible)
		{
			vr::Texture_t vrTex;
			vrTex.eType = vr::TextureType_OpenGL;
			vrTex.eColorSpace = vr::ColorSpace_Auto;

			vrTex.handle = (void *)
#if defined _WIN64 || defined _LP64
			(uint64_t)
#endif
				fboTextureHandle;

			vr::HmdVector2_t mouseScale = { (float) fboTextureWidth, (float) fboTextureHeight };

			vr::VROverlay()->SetOverlayTexture(overlayMainHandle, &vrTex);
			vr::VROverlay()->SetOverlayMouseScale(overlayMainHandle, &mouseScale);
		}

		const double dashboardInterval = 1.0 / 90.0; // fps
		double waitEventsTimeout = CalCtx.wantedUpdateInterval;

		if (dashboardVisible && waitEventsTimeout > dashboardInterval)
			waitEventsTimeout = dashboardInterval;

		glfwWaitEventsTimeout(waitEventsTimeout);
	}
}

using convert_t = std::codecvt_utf8<wchar_t>;
std::wstring_convert<convert_t, wchar_t> strconverter;

std::string to_string(std::wstring wstr)
{
    return strconverter.to_bytes(wstr);
}

std::wstring to_wstring(std::string str)
{
    return strconverter.from_bytes(str);
}

static void HandleCommandLineFunction(wchar_t const * lpCmdLine);
int main(int argc, char ** argv)
{
	const int max_path = 2048;
	char cwd[max_path] = {0};
    wchar_t const * lpCmdLine;
    std::wstring wide;
    for(int i=1; i<argc; i++){
        wide += to_wstring(argv[i]);
    }
    lpCmdLine = wide.c_str();
	if( !getcwd(cwd, max_path) ){
        std::cerr << "Could not get the current working directory" << std::endl;
        return 1;
    }

	HandleCommandLineFunction(lpCmdLine, cwd);

	if (!glfwInit())
	{
		std::wcerr << L"Failed to initialize GLFW";
		return 0;
	}

	glfwSetErrorCallback(GLFWErrorCallback);

	try {
		InitVR();
		CreateGLFWWindow();
		InitCalibrator();
		LoadProfile(CalCtx);
		RunLoop();

		vr::VR_Shutdown();

		if (fboHandle)
			glDeleteFramebuffers(1, &fboHandle);

		if (fboTextureHandle)
			glDeleteTextures(1, &fboTextureHandle);

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}
	catch (std::runtime_error &e)
	{
		std::cerr << "Runtime error: " << e.what() << std::endl;
		wchar_t message[1024];
		swprintf(message, 1024, L"%hs", e.what());
		std::wcerr << L"Runtime Error" << message;
	}

	if (glfwWindow)
		glfwDestroyWindow(glfwWindow);

	glfwTerminate();
	return 0;
}
bool StringMatch(wchar_t const * first, wchar_t const * second) {
    bool ret;
	ret =  wcscmp(first, second) == 0;
    return ret;
}

void InstallDriver(int max_path){
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr != vr::VRInitError_None)
		{
            fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
            vr::VR_Shutdown();
            exit(-2);
        }

		auto cruntimePath = std::make_unique<char[]>(max_path);
        unsigned int pathLen;
        vr::VR_GetRuntimePath(cruntimePath.get(), max_path, &pathLen);

        const int cmdLength = 8196;
        char cmd[cmdLength];

        snprintf(cmd, cmdLength, "python " DRIVER_INSTALLER_PATH "/driverInstall.py --toInstall " DRIVER_MANIFEST_PATH " --vrpathreg %s/bin/vrpathreg.sh", cruntimePath.get());
        printf("cmd: %s\n", cmd);
        system(cmd);

        vr::VR_Shutdown();
        exit(0);
}

void UninstallDriver(int max_path){
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr != vr::VRInitError_None)
		{
            fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
            vr::VR_Shutdown();
            exit(-2);
        }

		auto cruntimePath = std::make_unique<char[]>(max_path);
        unsigned int pathLen;
        vr::VR_GetRuntimePath(cruntimePath.get(), max_path, &pathLen);

        const int cmdLength = 8196;
        char cmd[cmdLength];

        snprintf(cmd, cmdLength,  "\"%s/bin/vrpathreg.sh\" removedriverwithname 01spacecalibrator", cruntimePath.get());
        printf("cmd: %s\n", cmd);

        vr::VR_Shutdown();
        exit(0);
}

static void HandleCommandLineFunction(wchar_t const * lpCmdLine, char * cwd)
{
	const int max_path = 2048;
	if (StringMatch(lpCmdLine, L"-help") || StringMatch(lpCmdLine, L"-h"))
    {
        std::cout << "usage - OpenVR SpaceCalibrator, only pick one option" << std::endl;
        std::cout << "-openvrpath                  print runtime path of openvr" << std::endl;
        std::cout << "-installmanifest             install the application vrmanifest" << std::endl;
        std::cout << "-removemanifest              remove the application vrmanifest" << std::endl;
        std::cout << "-activatemultipledrivers     enable multiple drivers in steamvr" << std::endl;
		//Note that the next 2 are not on the windows build
        std::cout << "-installdriver               install the steam vr driver." << std::endl;
        std::cout << "-uninstalldriver             uninstall the steam vr driver." << std::endl;
        std::cout << "-help -h                     print this message" << std::endl;
        exit(0);
    }
	else if (StringMatch(lpCmdLine, L"-openvrpath"))
	{
		char cruntimePath[max_path] = { 0 };
		HandleCommandLine::OpenVRPath(cruntimePath, max_path);
	}
	else if (StringMatch(lpCmdLine, L"-installmanifest"))
	{
		HandleCommandLine::InstallManifest(cwd, max_path);
	}
	else if (StringMatch(lpCmdLine, L"-removemanifest"))
	{
		HandleCommandLine::RemoveManifest(cwd);
	}
	else if (StringMatch(lpCmdLine, L"-activatemultipledrivers"))
	{
		HandleCommandLine::ActivateMultipleDrivers();
	}
	else if (StringMatch(lpCmdLine, L"-installdriver"))
	{
		InstallDriver(max_path);
	}
	else if (StringMatch(lpCmdLine, L"-uninstalldriver"))
	{
		UninstallDriver(max_path);
	}
}
