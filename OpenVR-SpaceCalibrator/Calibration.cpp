#include "stdafx.h"
#include "Calibration.h"
#include "Configuration.h"
#include "IPCClient.h"

#include <string>
#include <vector>
#include <iostream>

#include <Eigen/Dense>


static IPCClient Driver;
CalibrationContext CalCtx;

void InitCalibrator()
{
	Driver.Connect();
}

struct Pose
{
	Eigen::Matrix4d m = Eigen::Matrix4d::Identity();

	Pose() { }
	Pose(vr::HmdMatrix34_t hmdMatrix)
	{
		for (int row = 0; row < 3; row++) {
			for (int col = 0; col < 4; col++) {
				m(row, col) = hmdMatrix.m[row][col];
			}
		}
	}
};

struct Sample
{
	Pose ref, target;
	bool valid;
	Sample() : valid(false) { }
	Sample(Pose ref, Pose target) : valid(true), ref(ref), target(target) { }
};

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

Sample CollectSample(const CalibrationContext &ctx)
{
	vr::TrackedDevicePose_t reference, target;
	reference.bPoseIsValid = false;
	target.bPoseIsValid = false;

	reference = ctx.devicePoses[ctx.referenceID];
	target = ctx.devicePoses[ctx.targetID];

	bool ok = true;
	if (!reference.bPoseIsValid)
	{
		CalCtx.Message("Reference device is not tracking\n"); ok = false;
	}
	if (!target.bPoseIsValid)
	{
		CalCtx.Message("Target device is not tracking\n"); ok = false;
	}
	if (!ok)
	{
		CalCtx.Message("Aborting calibration!\n");
		CalCtx.state = CalibrationState::None;
		return Sample();
	}

	return Sample(
		Pose(reference.mDeviceToAbsoluteTracking),
		Pose(target.mDeviceToAbsoluteTracking)
	);
}

Eigen::Quaterniond RotationQuat(Eigen::Vector3d eulerdeg)
{
	auto euler = eulerdeg * EIGEN_PI / 180.0;

	return
		Eigen::AngleAxisd(euler(0), Eigen::Vector3d::UnitZ()) *
		Eigen::AngleAxisd(euler(1), Eigen::Vector3d::UnitY()) *
		Eigen::AngleAxisd(euler(2), Eigen::Vector3d::UnitX());
}

vr::HmdQuaternion_t VRRotationQuat(Eigen::Vector3d eulerdeg)
{
	auto rotQuat = RotationQuat(eulerdeg);
	vr::HmdQuaternion_t vrRotQuat;
	vrRotQuat.x = rotQuat.coeffs()[0];
	vrRotQuat.y = rotQuat.coeffs()[1];
	vrRotQuat.z = rotQuat.coeffs()[2];
	vrRotQuat.w = rotQuat.coeffs()[3];
	return vrRotQuat;
}

vr::HmdVector3d_t VRTranslationVec(Eigen::Vector3d transcm)
{
	auto trans = transcm * 0.01;
	vr::HmdVector3d_t vrTrans;
	vrTrans.v[0] = trans[0];
	vrTrans.v[1] = trans[1];
	vrTrans.v[2] = trans[2];
	return vrTrans;
}

void ResetAndDisableOffsets(uint32_t id)
{
	vr::HmdVector3d_t zeroV;
	zeroV.v[0] = zeroV.v[1] = zeroV.v[2] = 0;

	vr::HmdQuaternion_t zeroQ;
	zeroQ.x = 0; zeroQ.y = 0; zeroQ.z = 0; zeroQ.w = 1;

	protocol::Request req(protocol::RequestSetDeviceTransform);
	req.setDeviceTransform = { id, false, zeroV, zeroQ };
	Driver.SendBlocking(req);
}

void ScanAndApplyProfile(CalibrationContext &ctx)
{
	char buffer[vr::k_unMaxPropertyStringSize];

	for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; ++id)
	{
		auto deviceClass = vr::VRSystem()->GetTrackedDeviceClass(id);
		if (deviceClass == vr::TrackedDeviceClass_Invalid)
			continue;

		/*if (deviceClass == vr::TrackedDeviceClass_HMD) // for debugging unexpected universe switches
		{
			vr::ETrackedPropertyError err = vr::TrackedProp_Success;
			auto universeId = vr::VRSystem()->GetUint64TrackedDeviceProperty(id, vr::Prop_CurrentUniverseId_Uint64, &err);
			printf("uid %d err %d\n", universeId, err);
			ResetAndDisableOffsets(id);
			continue;
		}*/

		if (!ctx.validProfile)
		{
			ResetAndDisableOffsets(id);
			continue;
		}

		if (deviceClass == vr::TrackedDeviceClass_TrackingReference || deviceClass == vr::TrackedDeviceClass_HMD)
		{
			//auto p = ctx.devicePoses[id].mDeviceToAbsoluteTracking.m;
			//printf("REF %d: %f %f %f\n", id, p[0][3], p[1][3], p[2][3]);
			ResetAndDisableOffsets(id);
			continue;
		}

		vr::ETrackedPropertyError err = vr::TrackedProp_Success;
		vr::VRSystem()->GetStringTrackedDeviceProperty(id, vr::Prop_TrackingSystemName_String, buffer, vr::k_unMaxPropertyStringSize, &err);

		if (err != vr::TrackedProp_Success)
		{
			ResetAndDisableOffsets(id);
			continue;
		}

		std::string trackingSystem(buffer);

		if (trackingSystem != ctx.targetTrackingSystem)
		{
			ResetAndDisableOffsets(id);
			continue;
		}

		protocol::Request req(protocol::RequestSetDeviceTransform);
		req.setDeviceTransform = {
			id,
			true,
			VRTranslationVec(ctx.calibratedTranslation),
			VRRotationQuat(ctx.calibratedRotation)
		};
		Driver.SendBlocking(req);
	}

	if (ctx.chaperone.valid && ctx.chaperone.autoApply)
	{
		uint32_t quadCount = 0;
		vr::VRChaperoneSetup()->GetLiveCollisionBoundsInfo(nullptr, &quadCount);

		// Heuristic: when SteamVR resets to a blank-ish chaperone, it uses empty geometry,
		// but manual adjustments (e.g. via a play space mover) will not touch geometry.
		if (quadCount != ctx.chaperone.geometry.size())
		{
			ApplyChaperoneBounds();
		}
	}
}

void StartCalibration()
{
	CalCtx.state = CalibrationState::Begin;
	CalCtx.wantedUpdateInterval = 0.0;
	CalCtx.messages = "";
}

void CalibrationTick(double time)
{
	if (!vr::VRSystem())
		return;

	auto &ctx = CalCtx;
	if ((time - ctx.timeLastTick) < 0.02)
		return;

	ctx.timeLastTick = time;
	vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseRawAndUncalibrated, 0.0f, ctx.devicePoses, vr::k_unMaxTrackedDeviceCount);

	if (ctx.state == CalibrationState::None)
	{
		ctx.wantedUpdateInterval = 1.0;

		if ((time - ctx.timeLastScan) >= 1.0)
		{
			ScanAndApplyProfile(ctx);
			ctx.timeLastScan = time;
		}
		return;
	}

	if (ctx.state == CalibrationState::Editing)
	{
		ctx.wantedUpdateInterval = 0.1;

		if ((time - ctx.timeLastScan) >= 0.1)
		{
			ScanAndApplyProfile(ctx);
			ctx.timeLastScan = time;
		}
		return;
	}

	if (ctx.state == CalibrationState::Begin)
	{
		bool ok = true;

		if (ctx.referenceID == -1)
		{
			ctx.Message("Missing reference device\n"); ok = false;
		}
		else if (!ctx.devicePoses[ctx.referenceID].bPoseIsValid)
		{
			ctx.Message("Reference device is not tracking\n"); ok = false;
		}

		if (ctx.targetID == -1)
		{
			ctx.Message("Missing target device\n"); ok = false;
		}
		else if (!ctx.devicePoses[ctx.targetID].bPoseIsValid)
		{
			ctx.Message("Target device is not tracking\n"); ok = false;
		}

		if (!ok)
		{
			ctx.state = CalibrationState::None;
			ctx.Message("Aborting calibration!\n");
			return;
		}

		ResetAndDisableOffsets(ctx.targetID);
		ctx.state = CalibrationState::Rotation;
		ctx.wantedUpdateInterval = 0.0;
		ctx.samples = 0;
		ctx.AtA.setZero();
		ctx.Atb.setZero();

		char buf[256];
		snprintf(buf, sizeof buf, "Starting calibration, referenceID=%d targetID=%d\n", ctx.referenceID, ctx.targetID);
		ctx.Message(buf);
		return;
	}

	auto sample = CollectSample(ctx);
	if (!sample.valid)
	{
		return;
	}
	ctx.Message(".");

	// For all samples,
	//   sample.target.m * L = G * sample.ref.m
	//
	// L and G have 12 unknowns each, a rotation mat3x3 and translation vec3,
	// when multipled we have 12 equations in 24 unknowns.

	Eigen::Matrix<double, 4, 6> eq;

	// Multiply out all the equations
	// A^A * A * x = A^T * b, solve for x
	// In x, the first 4x3 has coefficients for G, last 4x3 has coefficients for L
	for (int b = 0; b < 3; b++)
	{
		for (int e = 0; e < 4; e++)
		{
			eq.setZero();

			eq.block<4, 1>(0, b) = sample.ref.m.block<4, 1>(0, e);
			eq.block<1, 3>(e, 3) = -sample.target.m.block<1, 3>(b, 0);

			double rhs = e == 3 ? sample.target.m(b, 3) : 0.0;

			// Enter equation into the least squares system
			for (int i = 0; i < 24; ++i)
			{
				for (int j = 0; j < 24; ++j)
					ctx.AtA(i, j) += eq(i) * eq(j);

				ctx.Atb(i) += eq(i) * rhs;
			}
		}
	}

	if (++ctx.samples < 205)
		return;

	ctx.Message("\n");

	// AtA * solved = Atb
	auto solved = ctx.AtA.fullPivLu().solve(ctx.Atb);

	// Pull first 4x3 and rotate, this is G
	Eigen::Matrix4d refToTargetSpace = Eigen::Matrix4d::Identity();
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			refToTargetSpace(i, j) = solved(i * 4 + j);

	auto rot = refToTargetSpace.block<3, 3>(0, 0);
	// TODO: polar decomposition of rot may improve precision
	Eigen::Vector3d euler = -rot.eulerAngles(2, 1, 0) * 180.0 / EIGEN_PI;
	euler(0) = -euler(0); // SteamVR forward is -Z, so roll is inverted

	char buf[256];
	snprintf(buf, sizeof buf, "Delta rotation yaw=%.2f pitch=%.2f roll=%.2f\n", euler[1], euler[2], euler[0]);
	ctx.Message(buf);

	Eigen::Vector3d trans = refToTargetSpace.block<3, 1>(0, 3);
	trans = RotationQuat(euler) * trans;
	auto transcm = -trans * 100.0;

	snprintf(buf, sizeof buf, "Delta translation x=%.2f y=%.2f z=%.2f\n", transcm[0], transcm[1], transcm[2]);
	ctx.Message(buf);

	ctx.calibratedRotation = euler;
	ctx.calibratedTranslation = transcm;

	auto vrRotQuat = VRRotationQuat(ctx.calibratedRotation);
	auto vrTrans = VRTranslationVec(ctx.calibratedTranslation);

	protocol::Request req(protocol::RequestSetDeviceTransform);
	req.setDeviceTransform = { ctx.targetID, true, vrTrans, vrRotQuat };
	Driver.SendBlocking(req);

	SaveProfile(ctx);
	ctx.Message("Finished calibration, profile saved\n");

	ctx.state = CalibrationState::None;
}

void LoadChaperoneBounds()
{
	vr::VRChaperoneSetup()->RevertWorkingCopy();

	uint32_t quadCount = 0;
	vr::VRChaperoneSetup()->GetLiveCollisionBoundsInfo(nullptr, &quadCount);

	CalCtx.chaperone.geometry.resize(quadCount);
	vr::VRChaperoneSetup()->GetLiveCollisionBoundsInfo(&CalCtx.chaperone.geometry[0], &quadCount);
	vr::VRChaperoneSetup()->GetWorkingStandingZeroPoseToRawTrackingPose(&CalCtx.chaperone.standingCenter);
	vr::VRChaperoneSetup()->GetWorkingPlayAreaSize(&CalCtx.chaperone.playSpaceSize.v[0], &CalCtx.chaperone.playSpaceSize.v[1]);
	CalCtx.chaperone.valid = true;
}

void ApplyChaperoneBounds()
{
	vr::VRChaperoneSetup()->RevertWorkingCopy();
	vr::VRChaperoneSetup()->SetWorkingCollisionBoundsInfo(&CalCtx.chaperone.geometry[0], CalCtx.chaperone.geometry.size());
	vr::VRChaperoneSetup()->SetWorkingStandingZeroPoseToRawTrackingPose(&CalCtx.chaperone.standingCenter);
	vr::VRChaperoneSetup()->SetWorkingPlayAreaSize(CalCtx.chaperone.playSpaceSize.v[0], CalCtx.chaperone.playSpaceSize.v[1]);
	vr::VRChaperoneSetup()->CommitWorkingCopy(vr::EChaperoneConfigFile_Live);
}
