#pragma once

#include <cstdint>

#ifndef _OPENVR_API
#include <openvr_driver.h>
#endif

#define OPENVR_SPACECALIBRATOR_PIPE_NAME "\\\\.\\pipe\\OpenVRSpaceCalibratorDriver"


#define COMM_PORT_SERVER 5473
#define COMM_PORT_CLIENT 5474

namespace protocol
{
	const uint32_t Version = 2;

	enum RequestType
	{
		RequestInvalid,
		RequestHandshake,
		RequestSetDeviceTransform,
	};

	enum ResponseType
	{
		ResponseInvalid,
		ResponseHandshake,
		ResponseSuccess,
	};

	struct Protocol
	{
		uint32_t version = Version;
	};

	struct SetDeviceTransform
	{
		uint32_t openVRID;
		bool enabled;
		bool updateTranslation;
		bool updateRotation;
		bool updateScale;
		vr::HmdVector3d_t translation;
		vr::HmdQuaternion_t rotation;
		double scale;

		SetDeviceTransform(uint32_t id, bool _enabled) :
			openVRID(id), enabled(_enabled), updateTranslation(false), updateRotation(false), updateScale(false) { }

		SetDeviceTransform(uint32_t id, bool _enabled, vr::HmdVector3d_t _translation) :
			openVRID(id), enabled(_enabled), updateTranslation(true), updateRotation(false), updateScale(false), translation(_translation) { }

		SetDeviceTransform(uint32_t id, bool _enabled, vr::HmdQuaternion_t _rotation) :
			openVRID(id), enabled(_enabled), updateTranslation(false), updateRotation(true), updateScale(false), rotation(_rotation) { }

		SetDeviceTransform(uint32_t id, bool _enabled, double _scale) :
			openVRID(id), enabled(_enabled), updateTranslation(false), updateRotation(false), updateScale(true), scale(_scale) { }

		SetDeviceTransform(uint32_t id, bool _enabled, vr::HmdVector3d_t _translation, vr::HmdQuaternion_t _rotation) :
			openVRID(id), enabled(_enabled), updateTranslation(true), updateRotation(true), updateScale(false), translation(_translation), rotation(_rotation) { }

		SetDeviceTransform(uint32_t id, bool _enabled, vr::HmdVector3d_t _translation, vr::HmdQuaternion_t _rotation, double _scale) :
			openVRID(id), enabled(_enabled), updateTranslation(true), updateRotation(true), updateScale(true), translation(_translation), rotation(_rotation), scale(_scale) { }
	};

	struct Request
	{
		RequestType type;

		union {
			SetDeviceTransform setDeviceTransform;
		};

		Request() : type(RequestInvalid) { }
		Request(RequestType _type) : type(_type) { }
	};

	struct Response
	{
		ResponseType type;

		union {
			Protocol protocol;
		};

		Response() : type(ResponseInvalid) { }
		Response(ResponseType _type) : type(_type) { }
	};
}
