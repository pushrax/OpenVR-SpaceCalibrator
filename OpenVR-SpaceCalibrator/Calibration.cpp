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
CalibrationState LastState = CalibrationState::None;
Eigen::Vector3d ReferenceTranslation;
Eigen::Vector3d ReferenceRotation;

void InitCalibrator()
{
	Driver.Connect();
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

Pose ReferencePose;

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

Eigen::Vector3d CalibrateRotation(const std::vector<Sample> &samples)
{
	std::vector<DSample> deltas;

	for (size_t i = 0; i < samples.size(); i++)
	{
		for (size_t j = 0; j < i; j++)
		{
			auto delta = DeltaRotationSamples(samples[i], samples[j]);
			if (delta.valid)
				deltas.push_back(delta);
		}
	}
	char buf[256];
	snprintf(buf, sizeof buf, "Got %zd samples with %zd delta samples\n", samples.size(), deltas.size());
	CalCtx.Log(buf);

	// Kabsch algorithm

	Eigen::MatrixXd refPoints(deltas.size(), 3), targetPoints(deltas.size(), 3);
	Eigen::Vector3d refCentroid(0,0,0), targetCentroid(0,0,0);

	for (size_t i = 0; i < deltas.size(); i++)
	{
		refPoints.row(i) = deltas[i].ref;
		refCentroid += deltas[i].ref;

		targetPoints.row(i) = deltas[i].target;
		targetCentroid += deltas[i].target;
	}

	refCentroid /= (double) deltas.size();
	targetCentroid /= (double) deltas.size();

	for (size_t i = 0; i < deltas.size(); i++)
	{
		refPoints.row(i) -= refCentroid;
		targetPoints.row(i) -= targetCentroid;
	}

	auto crossCV = refPoints.transpose() * targetPoints;

	Eigen::BDCSVD<Eigen::MatrixXd> bdcsvd;
	auto svd = bdcsvd.compute(crossCV, Eigen::ComputeThinU | Eigen::ComputeThinV);

	Eigen::Matrix3d i = Eigen::Matrix3d::Identity();
	if ((svd.matrixU() * svd.matrixV().transpose()).determinant() < 0)
	{
		i(2,2) = -1;
	}

	Eigen::Matrix3d rot = svd.matrixV() * i * svd.matrixU().transpose();
	rot.transposeInPlace();

	Eigen::Vector3d euler = rot.eulerAngles(2, 1, 0) * 180.0 / EIGEN_PI;

	snprintf(buf, sizeof buf, "Calibrated rotation: yaw=%.2f pitch=%.2f roll=%.2f\n", euler[1], euler[2], euler[0]);
	CalCtx.Log(buf);
	return euler;
}

Eigen::Vector3d CalibrateTranslation(const std::vector<Sample> &samples)
{
	std::vector<std::pair<Eigen::Vector3d, Eigen::Matrix3d>> deltas;

	for (size_t i = 0; i < samples.size(); i++)
	{
		for (size_t j = 0; j < i; j++)
		{
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

	for (size_t i = 0; i < deltas.size(); i++)
	{
		for (int axis = 0; axis < 3; axis++)
		{
			constants(i * 3 + axis) = deltas[i].first(axis);
			coefficients.row(i * 3 + axis) = deltas[i].second.row(axis);
		}
	}

	Eigen::Vector3d trans = coefficients.bdcSvd(Eigen::ComputeThinU | Eigen::ComputeThinV).solve(constants);
	auto transcm = trans * 100.0;

	char buf[256];
	snprintf(buf, sizeof buf, "Calibrated translation x=%.2f y=%.2f z=%.2f\n", transcm[0], transcm[1], transcm[2]);
	CalCtx.Log(buf);
	return transcm;
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
		CalCtx.Log("Reference device is not tracking\n"); ok = false;
	}
	if (!target.bPoseIsValid)
	{
		CalCtx.Log("Target device is not tracking\n"); ok = false;
	}
	if (!ok)
	{
		CalCtx.Log("Aborting calibration!\n");
		CalCtx.state = CalibrationState::None;
		return Sample();
	}

	return Sample(
		Pose(reference.mDeviceToAbsoluteTracking),
		Pose(target.mDeviceToAbsoluteTracking)
	);
}

vr::HmdQuaternion_t VRRotationQuat(Eigen::Vector3d eulerdeg)
{
	auto euler = eulerdeg * EIGEN_PI / 180.0;

	Eigen::Quaterniond rotQuat =
		Eigen::AngleAxisd(euler(0), Eigen::Vector3d::UnitZ()) *
		Eigen::AngleAxisd(euler(1), Eigen::Vector3d::UnitY()) *
		Eigen::AngleAxisd(euler(2), Eigen::Vector3d::UnitX());

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

static_assert(vr::k_unTrackedDeviceIndex_Hmd == 0, "HMD index expected to be 0");

void ScanAndApplyProfile(CalibrationContext &ctx)
{
	char buffer[vr::k_unMaxPropertyStringSize];
	ctx.enabled = ctx.validProfile;

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

		if (!ctx.enabled)
		{
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

		if (id == vr::k_unTrackedDeviceIndex_Hmd)
		{
			//auto p = ctx.devicePoses[id].mDeviceToAbsoluteTracking.m;
			//printf("HMD %d: %f %f %f\n", id, p[0][3], p[1][3], p[2][3]);

			if (trackingSystem != ctx.referenceTrackingSystem)
			{
				// Currently using an HMD with a different tracking system than the calibration.
				ctx.enabled = false;
			}

			ResetAndDisableOffsets(id);
			continue;
		}

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

	if (ctx.enabled && ctx.chaperone.valid && ctx.chaperone.autoApply)
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
	CalCtx.messages.clear();
}

void SetReferenceOffset() {
	auto &ctx = CalCtx;
	Pose pose(ctx.devicePoses[ctx.referenceID].mDeviceToAbsoluteTracking);
	ReferencePose = pose;
	ReferenceTranslation = ctx.calibratedTranslation;
	ReferenceRotation = ctx.calibratedRotation;
}

void CalibrationTick(double time)
{
	if (!vr::VRSystem())
		return;

	auto &ctx = CalCtx;
	if ((time - ctx.timeLastTick) < 0.05)
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

	if (ctx.state == CalibrationState::Referencing)
	{
		Pose pose(ctx.devicePoses[ctx.referenceID].mDeviceToAbsoluteTracking);
		Eigen::Vector3d deltaTrans = pose.trans - ReferencePose.trans;
		ctx.calibratedTranslation = (ReferenceTranslation + (deltaTrans * 100));

		// Attempt # 1, getting teh euler delta and adding it to the original reference rotation - does not work.
		//auto rotation = pose.rot.eulerAngles(2, 1, 0) * 180.0 / EIGEN_PI;
		/*ctx.calibratedRotation[0] = ReferenceRotation(0) + rotation(0);
		ctx.calibratedRotation[1] = ReferenceRotation(1) + rotation(1);
		ctx.calibratedRotation[2] = ReferenceRotation(2) + rotation(2);*/
		//ctx.calibratedRotation[0] = rotation(0);
		//ctx.calibratedRotation[1] = rotation(1);
		//ctx.calibratedRotation[2] = rotation(2);


		// Attempt #2, convert it all to quaternions ?? didnt get far with this one.
		/*Eigen::Quaterniond currentQuat =
			Eigen::AngleAxisd(ctx.calibratedRotation(0), Eigen::Vector3d::UnitZ()) *
			Eigen::AngleAxisd(ctx.calibratedRotation(1), Eigen::Vector3d::UnitY()) *
			Eigen::AngleAxisd(ctx.calibratedRotation(2), Eigen::Vector3d::UnitX());
		Eigen::Matrix3d deltaRot = pose.rot - ReferencePose.rot;
		Eigen::Quaternionf delta(deltaRot);
		vr::HmdQuaternion_t deltaQuat;
		deltaQuat.x = delta.coeffs()[0];
		deltaQuat.y = delta.coeffs()[1];
		deltaQuat.z = delta.coeffs()[2];
		deltaQuat.w = delta.coeffs()[3];*/
		//currentQuat.normalize();
		// Eigen::Matrix3d updatedRot = currentQuat.toRotationMatrix() + deltaRot;


		ctx.wantedUpdateInterval = 0.025;

		if ((time - ctx.timeLastScan) >= 0.025)
		{
			ScanAndApplyProfile(ctx);
			ctx.timeLastScan = time;
		}
		return;
	}
	LastState = ctx.state;

	if (ctx.state == CalibrationState::Begin)
	{
		bool ok = true;

		char referenceSerial[256], targetSerial[256];
		vr::VRSystem()->GetStringTrackedDeviceProperty(ctx.referenceID, vr::Prop_SerialNumber_String, referenceSerial, 256);
		vr::VRSystem()->GetStringTrackedDeviceProperty(ctx.targetID, vr::Prop_SerialNumber_String, targetSerial, 256);

		char buf[256];
		snprintf(buf, sizeof buf, "Reference device ID: %d, serial: %s\n", ctx.referenceID, referenceSerial);
		CalCtx.Log(buf);
		snprintf(buf, sizeof buf, "Target device ID: %d, serial %s\n", ctx.targetID, targetSerial);
		CalCtx.Log(buf);

		if (ctx.referenceID == -1)
		{
			CalCtx.Log("Missing reference device\n"); ok = false;
		}
		else if (!ctx.devicePoses[ctx.referenceID].bPoseIsValid)
		{
			CalCtx.Log("Reference device is not tracking\n"); ok = false;
		}

		if (ctx.targetID == -1)
		{
			CalCtx.Log("Missing target device\n"); ok = false;
		}
		else if (!ctx.devicePoses[ctx.targetID].bPoseIsValid)
		{
			CalCtx.Log("Target device is not tracking\n"); ok = false;
		}

		if (!ok)
		{
			ctx.state = CalibrationState::None;
			CalCtx.Log("Aborting calibration!\n");
			return;
		}

		ResetAndDisableOffsets(ctx.targetID);
		ctx.state = CalibrationState::Rotation;
		ctx.wantedUpdateInterval = 0.0;

		CalCtx.Log("Starting calibration...\n");
		return;
	}

	auto sample = CollectSample(ctx);
	if (!sample.valid)
	{
		return;
	}

	static std::vector<Sample> samples;
	samples.push_back(sample);

	CalCtx.Progress(samples.size(), CalCtx.SampleCount());

	if (samples.size() == CalCtx.SampleCount())
	{
		CalCtx.Log("\n");
		if (ctx.state == CalibrationState::Rotation)
		{
			ctx.calibratedRotation = CalibrateRotation(samples);

			auto vrRotQuat = VRRotationQuat(ctx.calibratedRotation);

			protocol::Request req(protocol::RequestSetDeviceTransform);
			req.setDeviceTransform = { ctx.targetID, true, vrRotQuat };
			Driver.SendBlocking(req);

			ctx.state = CalibrationState::Translation;
		}
		else if (ctx.state == CalibrationState::Translation)
		{
			ctx.calibratedTranslation = CalibrateTranslation(samples);

			auto vrTrans = VRTranslationVec(ctx.calibratedTranslation);

			protocol::Request req(protocol::RequestSetDeviceTransform);
			req.setDeviceTransform = { ctx.targetID, true, vrTrans };
			Driver.SendBlocking(req);

			ctx.validProfile = true;
			SaveProfile(ctx);
			CalCtx.Log("Finished calibration, profile saved\n");

			ctx.state = CalibrationState::None;
		}

		samples.clear();
	}
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
