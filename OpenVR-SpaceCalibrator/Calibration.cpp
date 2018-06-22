#include "stdafx.h"
#include "Calibration.h"

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <limits>
#include <iomanip>

#include <lib_vrinputemulator/vrinputemulator.h>
#include <openvr.h>
#include <Eigen/Dense>

enum CalibrationState
{
	None,
	Begin,
	Rotation,
	Translation,
};

struct CalibrationContext
{
	CalibrationState state = None;
	int referenceID, targetID;

	vr::HmdQuaternion_t calibratedRotation;
	vr::HmdVector3d_t calibratedTranslation;
	std::string calibratedTrackingSystem;

	bool profileLoaded = false, validProfile = false;
	int ticksSinceScan = 0;
};


static vrinputemulator::VRInputEmulator InputEmulator;
static CalibrationContext CalCtx;

void StartCalibration()
{
	CalCtx.state = Begin;
}

bool InitVR()
{
	auto initError = vr::VRInitError_None;
	vr::VR_Init(&initError, vr::VRApplication_Other);
	if (initError != vr::VRInitError_None)
	{
		auto error = vr::VR_GetVRInitErrorAsEnglishDescription(initError);
		std::cerr << "OpenVR error: " << error << std::endl;
		wchar_t message[1024];
		swprintf(message, 1024, L"%hs", error);
		MessageBox(nullptr, message, L"Failed to initialize OpenVR", 0);
		return false;
	}

	if (!vr::VR_IsInterfaceVersionValid(vr::IVRSystem_Version))
	{
		MessageBox(nullptr, L"Outdated IVRSystem_Version", L"Failed to initialize OpenVR", 0);
		return false;
	}
	else if (!vr::VR_IsInterfaceVersionValid(vr::IVRSettings_Version))
	{
		MessageBox(nullptr, L"Outdated IVRSettings_Version", L"Failed to initialize OpenVR", 0);
		return false;
	}

	InputEmulator.connect();
	return true;
}

struct Pose
{
	Eigen::Matrix3d rot;
	Eigen::Vector3d trans;

	Pose() { }
	Pose(vr::HmdMatrix34_t hmdMatrix)
	{
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				rot(i,j) = hmdMatrix.m[i][j];
			}
		}
		trans = Eigen::Vector3d(hmdMatrix.m[0][3], hmdMatrix.m[1][3], hmdMatrix.m[2][3]);
	}
	Pose(double x, double y, double z) : trans(Eigen::Vector3d(x,y,z)) { }
};

struct Sample
{
	Pose ref, target;
	bool valid;
	Sample() : valid(false) { }
	Sample(Pose ref, Pose target) : valid(true), ref(ref), target(target) { }
};

struct DSample
{
	bool valid;
	Eigen::Vector3d ref, target;
};

Eigen::Vector3d AxisFromRotationMatrix3(Eigen::Matrix3d rot)
{
	return Eigen::Vector3d(rot(2,1) - rot(1,2), rot(0,2) - rot(2,0), rot(1,0) - rot(0,1));
}

double AngleFromRotationMatrix3(Eigen::Matrix3d rot)
{
	return acos((rot(0,0) + rot(1,1) + rot(2,2) - 1.0) / 2.0);
}

DSample DeltaRotationSamples(Sample s1, Sample s2)
{
	// Difference in rotation between samples.
	auto dref = s1.ref.rot * s2.ref.rot.transpose();
	auto dtarget = s1.target.rot * s2.target.rot.transpose();

	// When stuck together, the two tracked objects rotate as a pair,
	// therefore their axes of rotation must be equal between any given pair of samples.
	DSample ds;
	ds.ref = AxisFromRotationMatrix3(dref);
	ds.target = AxisFromRotationMatrix3(dtarget);

	// Reject samples that were too close to each other.
	auto refA = AngleFromRotationMatrix3(dref);
	auto targetA = AngleFromRotationMatrix3(dtarget);
	ds.valid = refA > 0.4 && targetA > 0.4 && ds.ref.norm() > 0.01 && ds.target.norm() > 0.01;

	ds.ref.normalize();
	ds.target.normalize();
	return ds;
}

bool StartsWith(const std::string &str, const std::string &prefix)
{
	if (str.length() < prefix.length())
		return false;

	return str.compare(0, prefix.length(), prefix) == 0;
}

bool EndsWith(const std::string &str, const std::string &suffix)
{
	if (str.length() < suffix.length())
		return false;

	return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

Eigen::Matrix3d CalibrateRotation(const std::vector<Sample> &samples)
{
	std::vector<DSample> deltas;

	for (size_t i = 0; i < samples.size(); i++) {
		for (size_t j = 0; j < i; j++) {
			auto delta = DeltaRotationSamples(samples[i], samples[j]);
			if (delta.valid)
				deltas.push_back(delta);
		}
	}
	printf("\ngot %zd samples with %zd delta samples\n", samples.size(), deltas.size());

	// Kabsch algorithm

	Eigen::MatrixXd refPoints(deltas.size(), 3), targetPoints(deltas.size(), 3);
	Eigen::Vector3d refCentroid(0,0,0), targetCentroid(0,0,0);

	for (size_t i = 0; i < deltas.size(); i++) {
		refPoints.row(i) = deltas[i].ref;
		refCentroid += deltas[i].ref;

		targetPoints.row(i) = deltas[i].target;
		targetCentroid += deltas[i].target;
	}

	refCentroid /= (double) deltas.size();
	targetCentroid /= (double) deltas.size();

	for (size_t i = 0; i < deltas.size(); i++) {
		refPoints.row(i) -= refCentroid;
		targetPoints.row(i) -= targetCentroid;
	}

	auto crossCV = refPoints.transpose() * targetPoints;

	Eigen::BDCSVD<Eigen::MatrixXd> bdcsvd;
	auto svd = bdcsvd.compute(crossCV, Eigen::ComputeThinU | Eigen::ComputeThinV);

	Eigen::Matrix3d i = Eigen::Matrix3d::Identity();
	if ((svd.matrixU() * svd.matrixV().transpose()).determinant() < 0) {
		i(2,2) = -1;
	}

	Eigen::Matrix3d rot = svd.matrixV() * i * svd.matrixU().transpose();
	rot.transposeInPlace();

	Eigen::Vector3d euler = rot.eulerAngles(2, 1, 0) * 180.0 / EIGEN_PI;
	printf("rotation yaw=%.2f pitch=%.2f roll=%.2f\n", euler[1], euler[0], euler[2]);
	return rot;
}

Eigen::Vector3d CalibrateTranslation(const std::vector<Sample> &samples)
{
	std::vector<std::pair<Eigen::Vector3d, Eigen::Matrix3d>> deltas;

	for (size_t i = 0; i < samples.size(); i++) {
		for (size_t j = 0; j < i; j++) {
			auto QAi = samples[i].ref.rot.transpose();
			auto QAj = samples[j].ref.rot.transpose();
			auto dQA = QAj - QAi;
			auto CA = QAj * (samples[j].ref.trans - samples[j].target.trans) - QAi * (samples[i].ref.trans - samples[i].target.trans);
			deltas.push_back(std::make_pair(CA, dQA));

			auto QBi = samples[i].target.rot.transpose();
			auto QBj = samples[j].target.rot.transpose();
			auto dQB = QBj - QBi;
			auto CB = QBj * (samples[j].ref.trans - samples[j].target.trans) - QBi * (samples[i].ref.trans - samples[i].target.trans);
			deltas.push_back(std::make_pair(CB, dQB));
		}
	}

	Eigen::VectorXd constants(deltas.size() * 3);
	Eigen::MatrixXd coefficients(deltas.size() * 3, 3);

	for (size_t i = 0; i < deltas.size(); i++) {
		for (int axis = 0; axis < 3; axis++) {
			constants(i * 3 + axis) = deltas[i].first(axis);
			coefficients.row(i * 3 + axis) = deltas[i].second.row(axis);
		}
	}

	Eigen::Vector3d trans = coefficients.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(constants);
	trans(0) *= -1;
	auto transcm = trans * 100.0;

	printf("\ntranslation x=%.2f y=%.2f z=%.2f\n", transcm[0], transcm[1], transcm[2]);
	return trans;
}

Sample CollectSample(const CalibrationContext &ctx)
{
	vr::TrackedDevicePose_t devicePoses[vr::k_unMaxTrackedDeviceCount], reference, target;
	reference.bPoseIsValid = false;
	target.bPoseIsValid = false;

	vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, devicePoses, vr::k_unMaxTrackedDeviceCount);

	reference = devicePoses[ctx.referenceID];
	target = devicePoses[ctx.targetID];

	if (!reference.bPoseIsValid || !target.bPoseIsValid)
	{
		printf("invalid pose for:%s%s\n", reference.bPoseIsValid ? "" : " <left controller>", target.bPoseIsValid ? "" : " <vive tracker>");
		return Sample();
	}

	return Sample(
		Pose(reference.mDeviceToAbsoluteTracking),
		Pose(target.mDeviceToAbsoluteTracking)
	);
}

bool PickDevices(CalibrationContext &ctx)
{
	char buffer[vr::k_unMaxPropertyStringSize];

	vr::TrackedDevicePose_t devicePoses[vr::k_unMaxTrackedDeviceCount];
	vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, devicePoses, vr::k_unMaxTrackedDeviceCount);

	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id)
	{
		auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
		if (deviceClass == vr::TrackedDeviceClass_Invalid)
			continue;

		if (devicePoses[id].bPoseIsValid)
		{
			vr::ETrackedPropertyError err = vr::TrackedProp_Success;
			vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_SerialNumber_String, buffer, vr::k_unMaxPropertyStringSize, &err);

			if (err == vr::TrackedProp_Success)
			{
				std::string serial(buffer);
				//printf("got valid pose for id=%d serial=%s\n", id, serial.c_str());

				if (EndsWith(serial, "_Controller_Left"))
				{
					ctx.referenceID = id;
				}
				else if (StartsWith(serial, "LHR-"))
				{
					ctx.targetID = id;

					vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, vr::k_unMaxPropertyStringSize, &err);

					if (err == vr::TrackedProp_Success)
					{
						ctx.calibratedTrackingSystem = std::string(buffer);
					}
					else
					{
						printf("failed to get tracking system name for %s\n", serial.c_str());
						return false;
					}
				}
			}
			else
			{
				//printf("got valid pose for id=%d serial=<unknown>\n", id);
			}
		}
	}

	if (ctx.referenceID == -1 || ctx.targetID == -1)
	{
		printf("missing devices:%s%s\n", ctx.referenceID == -1 ? "" : " <left controller>", ctx.targetID == -1 ? "" : " <vive tracker>");
		return false;
	}
	return true;
}

void LoadProfile(CalibrationContext &ctx)
{
	std::ifstream file("openvr_space_calibration.txt");
	ctx.profileLoaded = true;
	ctx.validProfile = false;

	if (!file.good())
		return;

	file
		>> ctx.calibratedTrackingSystem
		>> ctx.calibratedRotation.w
		>> ctx.calibratedRotation.x
		>> ctx.calibratedRotation.y
		>> ctx.calibratedRotation.z
		>> ctx.calibratedTranslation.v[0]
		>> ctx.calibratedTranslation.v[1]
		>> ctx.calibratedTranslation.v[2];

	ctx.validProfile = true;
}

void SaveProfile(CalibrationContext &ctx)
{
	std::ofstream file("openvr_space_calibration.txt");

	file
		<< ctx.calibratedTrackingSystem << std::endl
		<< std::setprecision(std::numeric_limits<double>::digits10 + 1)
		<< ctx.calibratedRotation.w << " "
		<< ctx.calibratedRotation.x << " "
		<< ctx.calibratedRotation.y << " "
		<< ctx.calibratedRotation.z << std::endl
		<< ctx.calibratedTranslation.v[0] << " "
		<< ctx.calibratedTranslation.v[1] << " "
		<< ctx.calibratedTranslation.v[2] << std::endl;

	ctx.profileLoaded = true;
	ctx.validProfile = true;
}

void ScanAndApplyProfile(const CalibrationContext &ctx)
{
	char buffer[vr::k_unMaxPropertyStringSize];

	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id)
	{
		auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
		if (deviceClass == vr::TrackedDeviceClass_Invalid)
			continue;

		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, vr::k_unMaxPropertyStringSize, &err);

		if (err == vr::TrackedProp_Success)
		{
			std::string trackingSystem(buffer);

			if (trackingSystem == ctx.calibratedTrackingSystem)
			{
				//printf("setting calibration for %d (%s)\n", id, buffer);
				InputEmulator.setWorldFromDriverRotationOffset(id, ctx.calibratedRotation);
				InputEmulator.setWorldFromDriverTranslationOffset(id, ctx.calibratedTranslation);
				InputEmulator.enableDeviceOffsets(id, true);
			}
		}
	}
}

void ResetAndDisableOffsets(uint32_t id)
{
	vr::HmdVector3d_t zeroV;
	zeroV.v[0] = zeroV.v[1] = zeroV.v[2] = 0;

	vr::HmdQuaternion_t zeroQ;
	zeroQ.x = 0; zeroQ.y = 0; zeroQ.z = 0; zeroQ.w = 1;

	InputEmulator.setWorldFromDriverRotationOffset(id, zeroQ);
	InputEmulator.setWorldFromDriverTranslationOffset(id, zeroV);
	InputEmulator.enableDeviceOffsets(id, false);
}

void CalibrationTick()
{
	if (!vr::VRSystem())
		return;

	auto &ctx = CalCtx;

	if (ctx.state == None)
	{
		if (!ctx.profileLoaded)
		{
			LoadProfile(ctx);
		}
		else if (!ctx.validProfile)
		{
			return;
		}

		if (ctx.ticksSinceScan == 0)
		{
			ScanAndApplyProfile(ctx);
		}
		ctx.ticksSinceScan = (ctx.ticksSinceScan + 1) % 40;
		return;
	}

	if (ctx.state == Begin)
	{
		ctx.referenceID = -1;
		ctx.targetID = -1;
		if (PickDevices(ctx))
		{
			ResetAndDisableOffsets(ctx.targetID);
			ctx.state = Rotation;
			printf("starting calibration, referenceID=%d targetID=%d\n", ctx.referenceID, ctx.targetID);
		}
		return;
	}

	auto sample = CollectSample(ctx);
	if (!sample.valid)
	{
		return;
	}
	printf(".");

	const int totalSamples = 100;
	static std::vector<Sample> samples;
	samples.push_back(sample);

	if (samples.size() == totalSamples)
	{
		if (ctx.state == Rotation)
		{
			auto rot = CalibrateRotation(samples);
			Eigen::Quaterniond rotQuat(rot);

			vr::HmdQuaternion_t vrRotQuat;
			vrRotQuat.x = rotQuat.coeffs()[0];
			vrRotQuat.y = rotQuat.coeffs()[1];
			vrRotQuat.z = rotQuat.coeffs()[2];
			vrRotQuat.w = rotQuat.coeffs()[3];

			InputEmulator.setWorldFromDriverRotationOffset(ctx.targetID, vrRotQuat);
			InputEmulator.enableDeviceOffsets(ctx.targetID, true);

			ctx.calibratedRotation = vrRotQuat;
			ctx.state = Translation;
		}
		else if (ctx.state == Translation)
		{
			auto trans = CalibrateTranslation(samples);

			vr::HmdVector3d_t vrTrans;
			vrTrans.v[0] = trans[0];
			vrTrans.v[1] = trans[1];
			vrTrans.v[2] = trans[2];

			InputEmulator.setWorldFromDriverTranslationOffset(ctx.targetID, vrTrans);

			ctx.calibratedTranslation = vrTrans;
			ctx.state = None;

			SaveProfile(ctx);
			printf("finished calibration, profile saved\n");
		}

		samples.clear();
	}
}