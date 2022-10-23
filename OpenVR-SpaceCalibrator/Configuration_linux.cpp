#include "Configuration.h"

#include <picojson.h>

#include <string>
#include "stdio.h"
#include <fstream>
#include <iomanip>
#include <limits>

#include "Logging.h"

#include <unistd.h> 
#include <string.h>
#include <sys/stat.h>
#include "StaticConfig.h"
#define LINUX_CONFIG_FILE "spacecal-config.json"

static picojson::array FloatArray(const float *buf, int numFloats)
{
	picojson::array arr;

	for (int i = 0; i < numFloats; i++)
		arr.push_back(picojson::value(double(buf[i])));

	return arr;
}

static void LoadFloatArray(const picojson::value &obj, float *buf, int numFloats)
{
	if (!obj.is<picojson::array>())
		throw std::runtime_error("expected array, got " + obj.to_str());

	auto &arr = obj.get<picojson::array>();
	if (arr.size() != (size_t) numFloats)
		throw std::runtime_error("wrong buffer size");

	for (int i = 0; i < numFloats; i++)
		buf[i] = (float) arr[i].get<double>();
}

static void ParseProfile(CalibrationContext &ctx, std::istream &stream)
{
	picojson::value v;
	std::string err = picojson::parse(v, stream);
	if (!err.empty())
		throw std::runtime_error(err);

	auto arr = v.get<picojson::array>();
	if (arr.size() < 1)
		throw std::runtime_error("no profiles in file");

	auto obj = arr[0].get<picojson::object>();

	ctx.referenceTrackingSystem = obj["reference_tracking_system"].get<std::string>();
	ctx.targetTrackingSystem = obj["target_tracking_system"].get<std::string>();
	ctx.calibratedRotation(0) = obj["roll"].get<double>();
	ctx.calibratedRotation(1) = obj["yaw"].get<double>();
	ctx.calibratedRotation(2) = obj["pitch"].get<double>();
	ctx.calibratedTranslation(0) = obj["x"].get<double>();
	ctx.calibratedTranslation(1) = obj["y"].get<double>();
	ctx.calibratedTranslation(2) = obj["z"].get<double>();

	if (obj["scale"].is<double>())
		ctx.calibratedScale = obj["scale"].get<double>();
	else
		ctx.calibratedScale = 1.0;

	if (obj["calibration_speed"].is<double>())
		ctx.calibrationSpeed = (CalibrationContext::Speed)(int) obj["calibration_speed"].get<double>();

	if (obj["chaperone"].is<picojson::object>())
	{
		auto chaperone = obj["chaperone"].get<picojson::object>();
		ctx.chaperone.autoApply = chaperone["auto_apply"].get<bool>();

		LoadFloatArray(chaperone["play_space_size"], ctx.chaperone.playSpaceSize.v, 2);

		LoadFloatArray(
			chaperone["standing_center"],
			(float *) ctx.chaperone.standingCenter.m,
			sizeof(ctx.chaperone.standingCenter.m) / sizeof(float)
		);

		if (!chaperone["geometry"].is<picojson::array>())
			throw std::runtime_error("chaperone geometry is not an array");

		auto &geometry = chaperone["geometry"].get<picojson::array>();

		if (geometry.size() > 0)
		{
			ctx.chaperone.geometry.resize(geometry.size() * sizeof(float) / sizeof(ctx.chaperone.geometry[0]));
			LoadFloatArray(chaperone["geometry"], (float *) ctx.chaperone.geometry.data(), geometry.size());

			ctx.chaperone.valid = true;
		}
	}

	ctx.validProfile = true;
}

static void WriteProfile(CalibrationContext &ctx, std::ostream &out)
{
	if (!ctx.validProfile)
		return;

	picojson::object profile;
	profile["reference_tracking_system"].set<std::string>(ctx.referenceTrackingSystem);
	profile["target_tracking_system"].set<std::string>(ctx.targetTrackingSystem);
	profile["roll"].set<double>(ctx.calibratedRotation(0));
	profile["yaw"].set<double>(ctx.calibratedRotation(1));
	profile["pitch"].set<double>(ctx.calibratedRotation(2));
	profile["x"].set<double>(ctx.calibratedTranslation(0));
	profile["y"].set<double>(ctx.calibratedTranslation(1));
	profile["z"].set<double>(ctx.calibratedTranslation(2));
	profile["scale"].set<double>(ctx.calibratedScale);

	double speed = (int) ctx.calibrationSpeed;
	profile["calibration_speed"].set<double>(speed);

	if (ctx.chaperone.valid)
	{
		picojson::object chaperone;
		chaperone["auto_apply"].set<bool>(ctx.chaperone.autoApply);
		chaperone["play_space_size"].set<picojson::array>(FloatArray(ctx.chaperone.playSpaceSize.v, 2));

		chaperone["standing_center"].set<picojson::array>(FloatArray(
			(float *) ctx.chaperone.standingCenter.m,
			sizeof(ctx.chaperone.standingCenter.m) / sizeof(float)
		));

		chaperone["geometry"].set<picojson::array>(FloatArray(
			(float *) ctx.chaperone.geometry.data(),
			sizeof(ctx.chaperone.geometry[0]) / sizeof(float) * ctx.chaperone.geometry.size()
		));

		profile["chaperone"].set<picojson::object>(chaperone);
	}

	picojson::value profileV;
	profileV.set<picojson::object>(profile);

	picojson::array profiles;
	profiles.push_back(profileV);

	picojson::value profilesV;
	profilesV.set<picojson::array>(profiles);

	out << profilesV.serialize(true);
}

static std::string ReadRegistryKey()
{
	char configPath[1024];
	const char * home = getenv("HOME");
	snprintf( configPath, 1024, "%s/" LINUX_CONFIG_DIR, home);

	struct stat statResult;
	if(stat(LINUX_CONFIG_DIR, &statResult)){
		if(errno != 2){ // no idea why 2 is returned instead of the documented ENOTDIR
			int rr = errno;
			LOG("Error determining if %s is a directory: %s, %s", configPath, strerror(rr), strerror(2));
			return "";
		} else {
			int rr = errno;
			LOG("The directory %s is confirmed to not exist %d-%s", configPath, rr, strerror(rr));
			int retCode;

			char cmd[1500];
			snprintf(cmd, 1500, "mkdir -p %s", configPath);
			LOG("Running: %s", cmd);
			if( (retCode = system(cmd)) ){
				LOG("Error %d making directory " LINUX_CONFIG_DIR, retCode);
				return "";
			}
		}
	}

	char configFilePath[2000];
	snprintf(configFilePath, 2000, "%s/" LINUX_CONFIG_FILE, configPath);

	LOG("Opening file at %s", configFilePath);

	FILE* file = fopen(configFilePath, "r");

	if(!file) return "";

	std::string ret;
	const int buffSize = 4097;
	int count = 0;
	char buff[buffSize];
	buff[buffSize-1] = 0;

	do{
		count = fread((void*) buff, 1, buffSize, file);
		if(count > 0){
			ret += buff;
		}
	}while(buffSize == count);
	fclose(file);
	return ret;
}

static void WriteRegistryKey(std::string str)
{
	struct stat statResult;

	char configPath[1024];
	const char * home = getenv("HOME");
	snprintf( configPath, 1024, "%s/" LINUX_CONFIG_DIR, home);

	if(stat(LINUX_CONFIG_DIR, &statResult)){
		if(errno != 2){ // no idea why 2 is returned instead of the documented ENOTDIR
			int rr = errno;
			LOG("Error determining if %s is a directory: %s, %s", configPath, strerror(rr), strerror(2));
			return;
		} else {
			int rr = errno;
			LOG("The directory %s is confirmed to not exist %d-%s", configPath, rr, strerror(rr));
			int retCode;

			char cmd[1500];
			snprintf(cmd, 1500, "mkdir -p %s", configPath);
			LOG("Running: %s", cmd);
			if( (retCode = system(cmd)) ){
				LOG("Error %d making directory " LINUX_CONFIG_DIR, retCode);
				return;
			}
		}
	}

	char configFilePath[2000];
	snprintf(configFilePath, 2000, "%s/" LINUX_CONFIG_FILE, configPath);

	FILE* file = fopen(configFilePath, "w");
	if(!file) {
		LOG("%s - %d-%s", "Error opening config file for writing", errno, strerror(errno));
		return;
	} else {
		LOG("Opened file at %s to save settings", configFilePath);
	}

	fprintf(file, "%s", str.c_str());
	fclose(file);
}

void LoadProfile(CalibrationContext &ctx)
{
	ctx.validProfile = false;

	auto str = ReadRegistryKey();
	if (str == "")
	{
		std::cout << "Profile is empty" << std::endl;
		ctx.Clear();
		return;
	}

	try
	{
		std::stringstream io(str);
		ParseProfile(ctx, io);
		std::cout << "Loaded profile" << std::endl;
	}
	catch (const std::runtime_error &e)
	{
		std::cerr << "Error loading profile: " << e.what() << std::endl;
	}
}

void SaveProfile(CalibrationContext &ctx)
{
	std::cout << "Saving profile to registry" << std::endl;

	std::stringstream io;
	WriteProfile(ctx, io);
	WriteRegistryKey(io.str());
}

