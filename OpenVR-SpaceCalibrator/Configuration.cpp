#include "stdafx.h"
#include "Configuration.h"

#include <picojson.h>

#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <limits>

static void UpgradeProfileV1(CalibrationContext &ctx);
static void ParseProfileV2(CalibrationContext &ctx, std::istream &stream);

void LoadProfile(CalibrationContext &ctx)
{
	std::ifstream file("openvr_space_calibration.json");
	ctx.validProfile = false;

	if (!file.good())
	{
		UpgradeProfileV1(ctx);
		return;
	}

	try
	{
		ParseProfileV2(ctx, file);
		std::cout << "Loaded profile" << std::endl;
	}
	catch (const std::runtime_error &e)
	{
		std::cerr << "Error loading profile: " << e.what() << std::endl;
	}
}

static void ParseProfileV2(CalibrationContext &ctx, std::istream &stream)
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

	ctx.validProfile = true;
}

void SaveProfile(CalibrationContext &ctx)
{
	std::ofstream file("openvr_space_calibration.json");

	picojson::object profile;
	profile["reference_tracking_system"].set<std::string>(ctx.referenceTrackingSystem);
	profile["target_tracking_system"].set<std::string>(ctx.targetTrackingSystem);
	profile["roll"].set<double>(ctx.calibratedRotation(0));
	profile["yaw"].set<double>(ctx.calibratedRotation(1));
	profile["pitch"].set<double>(ctx.calibratedRotation(2));
	profile["x"].set<double>(ctx.calibratedTranslation(0));
	profile["y"].set<double>(ctx.calibratedTranslation(1));
	profile["z"].set<double>(ctx.calibratedTranslation(2));

	picojson::value profileV;
	profileV.set<picojson::object>(profile);

	picojson::array profiles;
	profiles.push_back(profileV);

	picojson::value profilesV;
	profilesV.set<picojson::array>(profiles);

	file << profilesV.serialize(true);

	ctx.validProfile = true;
}

static void UpgradeProfileV1(CalibrationContext &ctx)
{
	std::ifstream file("openvr_space_calibration.txt");
	ctx.validProfile = false;

	if (!file.good())
		return;

	file
		>> ctx.targetTrackingSystem
		>> ctx.calibratedRotation(1) // yaw
		>> ctx.calibratedRotation(2) // pitch
		>> ctx.calibratedRotation(0) // roll
		>> ctx.calibratedTranslation(0) // x
		>> ctx.calibratedTranslation(1) // y
		>> ctx.calibratedTranslation(2); // z

	ctx.validProfile = true;

	SaveProfile(ctx);

	file.close();
	std::remove("openvr_space_calibration.txt");
}
