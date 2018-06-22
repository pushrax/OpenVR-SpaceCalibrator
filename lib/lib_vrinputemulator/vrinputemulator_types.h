#pragma once

#include <stdint.h>


namespace vrinputemulator {

	static const char* const vrsettings_SectionName = "driver_00vrinputemulator";
	static const char* const vrsettings_overrideHmdManufacturer_string = "overrideHmdManufacturer";
	static const char* const vrsettings_overrideHmdModel_string = "overrideHmdModel";
	static const char* const vrsettings_overrideHmdTrackingSystem_string = "overrideHmdTrackingSystem";
	static const char* const vrsettings_genericTrackerFakeController_bool = "genericTrackerFakeController";

	enum class VirtualDeviceType : uint32_t {
		None = 0,
		TrackedController = 1
	};


	enum class ButtonEventType : uint32_t {
		None = 0,
		ButtonPressed = 1,
		ButtonUnpressed = 2,
		ButtonTouched = 3,
		ButtonUntouched = 4,
	};


	enum class DevicePropertyValueType : uint32_t {
		None = 0,
		FLOAT = 1,
		INT32 = 2,
		UINT64 = 3,
		BOOL = 4,
		STRING = 5,
		MATRIX34 = 20,
		MATRIX44 = 21,
		VECTOR3 = 22,
		VECTOR4 = 23
	};


	struct DeviceOffsets {
		uint32_t deviceId;
		bool offsetsEnabled;
		vr::HmdQuaternion_t worldFromDriverRotationOffset;
		vr::HmdVector3d_t worldFromDriverTranslationOffset;
		vr::HmdQuaternion_t driverFromHeadRotationOffset;
		vr::HmdVector3d_t driverFromHeadTranslationOffset;
		vr::HmdQuaternion_t deviceRotationOffset;
		vr::HmdVector3d_t deviceTranslationOffset;
	};


	struct DeviceInfo {
		uint32_t deviceId;
		vr::ETrackedDeviceClass deviceClass;
		int deviceMode;
		uint32_t refDeviceId;
		bool offsetsEnabled;
		bool redirectSuspended;
	};


	enum class MotionCompensationVelAccMode : uint32_t {
		Disabled = 0,
		SetZero = 1,
		SubstractMotionRef = 2,
		LinearApproximation = 3,
		KalmanFilter = 4
	};


	enum class DigitalBindingType : uint32_t {
		NoRemapping = 0,
		Disabled = 1,
		OpenVR = 2,
		Keyboard = 3,
		SuspendRedirectMode = 4,
		ToggleTouchpadEmulationFix = 5
	};


	struct DigitalBinding {
		DigitalBindingType type = DigitalBindingType::NoRemapping;
		
		union BindingUnion {
			struct {
				uint32_t controllerId;
				uint32_t buttonId;
			} openvr;

			struct {
				bool shiftPressed = false;
				bool ctrlPressed = false;
				bool altPressed = false;
				uint32_t keyCode = 0x00;
				bool sendScanCode = true;
			} keyboard;

			BindingUnion() {}
		} data;
		
		bool toggleEnabled = false;
		uint32_t toggleDelay = 0;
		
		bool autoTriggerEnabled = false;
		uint32_t autoTriggerFrequency = 1;

		DigitalBinding() {}
	};


	struct DigitalInputRemapping {
		bool valid;
		DigitalBinding binding;

		bool touchAsClick = false;

		bool longPressEnabled = false;
		uint32_t longPressThreshold = 1000u;
		DigitalBinding longPressBinding;
		bool longPressImmediateRelease = false;

		bool doublePressEnabled = false;
		uint32_t doublePressThreshold = 300;
		DigitalBinding doublePressBinding;
		bool doublePressImmediateRelease = false;

		DigitalInputRemapping(bool valid = false) : valid(valid) {}
	};


	enum class AnalogBindingType : uint32_t {
		NoRemapping,
		Disabled,
		OpenVR
	};

	struct AnalogBinding {
		AnalogBindingType type;
		union BindingUnion {
			struct {
				uint32_t controllerId = vr::k_unTrackedDeviceIndexInvalid;
				uint32_t axisId = 0;
			} openvr;
			BindingUnion() {}
		} data;
		bool invertXAxis = false;
		bool invertYAxis = false;
		bool swapAxes = false;
		float lowerDeadzone = 0.0;
		float upperDeadzone = 1.0;
		unsigned touchpadEmulationMode = 0; // 0 .. Disabled, 1 .. Position Based, 2 .. Position Based (Deferred Zero Updates)
		bool buttonPressDeadzoneFix = false;

		AnalogBinding(AnalogBindingType type = AnalogBindingType::NoRemapping) : type(type) {}
	};


	struct AnalogInputRemapping {
		bool valid;
		AnalogBinding binding;

		AnalogInputRemapping(bool valid = false) : valid(valid) {}
	};


} // end namespace vrinputemulator