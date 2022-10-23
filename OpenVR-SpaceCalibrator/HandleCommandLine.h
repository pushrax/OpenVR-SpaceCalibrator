#include <openvr.h>
#define OPENVR_APPLICATION_KEY "pushrax.SpaceCalibrator"
#include <memory>
#include <string.h>
#include <iostream>


namespace HandleCommandLine {

std::string ManifestPath(std::string cwd);

static inline void OpenVRPath(char * cruntimePath, int cruntimePathLength){
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None)
		{
			unsigned int pathLen;
			vr::VR_GetRuntimePath(cruntimePath,  cruntimePathLength, &pathLen);

			printf("%s", cruntimePath);
			vr::VR_Shutdown();
			exit(0);
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		exit(-2);
}

static inline void InstallManifest(char const * const cwd, int max_path) {
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None)
		{
			if (vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY))
			{
				auto oldWd = std::make_unique<char[]>(max_path);
				memset((void*)oldWd.get(), 0, max_path);
				
				auto vrAppErr = vr::VRApplicationError_None;
				vr::VRApplications()->GetApplicationPropertyString(OPENVR_APPLICATION_KEY, vr::VRApplicationProperty_WorkingDirectory_String, oldWd.get(), max_path, &vrAppErr);
				if (vrAppErr != vr::VRApplicationError_None)
				{
					fprintf(stderr, "Failed to get old working dir, skipping removal: %s\n", vr::VRApplications()->GetApplicationsErrorNameFromEnum(vrAppErr));
				}
				else
				{
					std::string manifestPath = oldWd.get();
					manifestPath = HandleCommandLine::ManifestPath(manifestPath);
					std::cout << "Removing old manifest path: " << manifestPath << std::endl;
					vr::VRApplications()->RemoveApplicationManifest(manifestPath.c_str());
				}
			}
			std::string manifestPath = cwd;
			manifestPath = HandleCommandLine::ManifestPath(manifestPath);

			std::cout << "Adding manifest path: " << manifestPath << std::endl;
			auto vrAppErr = vr::VRApplications()->AddApplicationManifest(manifestPath.c_str());
			if (vrAppErr != vr::VRApplicationError_None)
			{
				fprintf(stderr, "Failed to add manifest: %s\n", vr::VRApplications()->GetApplicationsErrorNameFromEnum(vrAppErr));
			}
			else
			{
				vr::VRApplications()->SetApplicationAutoLaunch(OPENVR_APPLICATION_KEY, true);
			}
			vr::VR_Shutdown();
			exit(-2);
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		exit(-2);
}

static inline void RemoveManifest(char const * const cwd) {
		auto vrErr = vr::VRInitError_None;
		vr::VR_Init(&vrErr, vr::VRApplication_Utility);
		if (vrErr == vr::VRInitError_None)
		{
			if (vr::VRApplications()->IsApplicationInstalled(OPENVR_APPLICATION_KEY))
			{
				std::string manifestPath = cwd;
				manifestPath = ManifestPath(manifestPath);

				std::cout << "Removing manifest path: " << manifestPath << std::endl;
				vr::VRApplications()->RemoveApplicationManifest(manifestPath.c_str());
			}
			vr::VR_Shutdown();
			exit(0);
		}
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
		vr::VR_Shutdown();
		exit(-2);
}

static inline void ActivateMultipleDrivers() {
	int ret = -2;
	auto vrErr = vr::VRInitError_None;
	vr::VR_Init(&vrErr, vr::VRApplication_Utility);
	if (vrErr == vr::VRInitError_None)
	{
		try
		{
			ActivateMultipleDrivers();
			ret = 0;
		}
		catch (std::runtime_error &e)
		{
			std::cerr << e.what() << std::endl;
		}
	}
	else
	{
		fprintf(stderr, "Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(vrErr));
	}
	vr::VR_Shutdown();
	exit(ret);
}

} // namespace HandleCommandLine
