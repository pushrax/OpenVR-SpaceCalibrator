#pragma once

#include <Eigen/Core>
#include <openvr.h>

enum class CalibrationState
{
	None,
	Begin,
	Rotation,
	Translation,
	Editing,
};

struct CalibrationContext
{
	CalibrationState state = CalibrationState::None;
	uint32_t referenceID, targetID;

	Eigen::Vector3d calibratedRotation;
	Eigen::Vector3d calibratedTranslation;

	std::string referenceTrackingSystem;
	std::string targetTrackingSystem;

	bool validProfile = false;
	double timeLastTick = 0, timeLastScan = 0;
	double wantedUpdateInterval = 1.0;

	vr::TrackedDevicePose_t devicePoses[vr::k_unMaxTrackedDeviceCount];

	std::string messages;

	void Message(const std::string &msg)
	{
		messages += msg;
		std::cerr << msg;
	}
};

extern CalibrationContext CalCtx;

void InitCalibrator();
void CalibrationTick(double time);
void StartCalibration();