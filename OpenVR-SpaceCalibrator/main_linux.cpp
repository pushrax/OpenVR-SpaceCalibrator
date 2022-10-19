#include "Calibration.h"
#include "Configuration.h"
#include "EmbeddedFiles.h"
#include "UserInterface.h"

#include "OpenVR-SpaceCalibrator.h"
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

void printError(char const * err){
	std::cerr<<err<<std::endl;
}

static void HandleCommandLineFunction(char const * cmdLine, char *cwd);

int main(int argc, char ** argv)
{
	const int max_path = 2048;
	char cwd[max_path] = {0};
    char const * cmdLine;
    std::string str;
    for(int i=1; i<argc; i++){
        str += argv[i];
    }
    cmdLine = str.c_str();
	if( !getcwd(cwd, max_path) ){
        std::cerr << "Could not get the current working directory" << std::endl;
        return 1;
    }

	HandleCommandLineFunction(cmdLine, cwd);

	return MainApplication(cwd, printError);
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

static void HandleCommandLineFunction(char const * cmdLine, char * cwd)
{
	const int max_path = 2048;
	if (!strcmp(cmdLine, "-help") || !strcmp(cmdLine, "-h"))
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
	else if (!strcmp(cmdLine, "-openvrpath"))
	{
		char cruntimePath[max_path] = { 0 };
		HandleCommandLine::OpenVRPath(cruntimePath, max_path);
	}
	else if (!strcmp(cmdLine, "-installmanifest"))
	{
		HandleCommandLine::InstallManifest(cwd, max_path);
	}
	else if (!strcmp(cmdLine, "-removemanifest"))
	{
		HandleCommandLine::RemoveManifest(cwd);
	}
	else if (!strcmp(cmdLine, "-activatemultipledrivers"))
	{
		HandleCommandLine::ActivateMultipleDrivers();
	}
	else if (!strcmp(cmdLine, "-installdriver"))
	{
		InstallDriver(max_path);
	}
	else if (!strcmp(cmdLine, "-uninstalldriver"))
	{
		UninstallDriver(max_path);
	}
}

