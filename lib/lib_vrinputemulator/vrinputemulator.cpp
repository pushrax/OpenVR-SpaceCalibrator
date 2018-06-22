#include "vrinputemulator.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <cstdlib>
#include <functional>
#include <iostream>


#define WRITELOG(level, txt) std::cerr << txt;


namespace vrinputemulator {

// Receives and dispatches ipc messages
void VRInputEmulator::_ipcThreadFunc(VRInputEmulator * _this) {
	_this->_ipcThreadRunning = true;
	while (!_this->_ipcThreadStop) {
		try {
			ipc::Reply message;
			uint64_t recv_size;
			unsigned priority;
			boost::posix_time::ptime timeout = boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(50);
			if (_this->_ipcClientQueue->timed_receive(&message, sizeof(ipc::Reply), recv_size, priority, timeout)) {
				if (recv_size == sizeof(ipc::Reply)) {
					std::lock_guard<std::recursive_mutex> lock(_this->_mutex);
					auto i = _this->_ipcPromiseMap.find(message.messageId);
					if (i != _this->_ipcPromiseMap.end()) {
						if (i->second.isValid) {
							i->second.promise.set_value(message);
						} else {
							_this->_ipcPromiseMap.erase(i); // nobody wants it, so we delete it
						}
					}
				}
			} else {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		} catch (std::exception& ex) {
			WRITELOG(ERROR, "Exception in ipc receive loop: " << ex.what() << std::endl);
		}
	}
	_this->_ipcThreadRunning = false;
}


VRInputEmulator::VRInputEmulator(const std::string& serverQueue, const std::string& clientQueue) : _ipcServerQueueName(serverQueue), _ipcClientQueueName(clientQueue) {}

VRInputEmulator::~VRInputEmulator() {
	disconnect();
}

bool VRInputEmulator::isConnected() const {
	return _ipcServerQueue != nullptr;
}

void VRInputEmulator::connect() {
	if (!_ipcServerQueue) {
		// Open server-side message queue
		try {
			_ipcServerQueue = new boost::interprocess::message_queue(boost::interprocess::open_only, _ipcServerQueueName.c_str());
		} catch (std::exception& e) {
			_ipcServerQueue = nullptr;
			std::stringstream ss;
			ss << "Could not open server-side message queue: " << e.what();
			throw vrinputemulator_connectionerror(ss.str());
		}
		// Append random number to client queue name (and hopefully no other client uses the same random number)
		_ipcClientQueueName += std::to_string(_ipcRandomDist(_ipcRandomDevice));
		// Open client-side message queue
		try {
			boost::interprocess::message_queue::remove(_ipcClientQueueName.c_str());
			_ipcClientQueue = new boost::interprocess::message_queue(
				boost::interprocess::create_only,
				_ipcClientQueueName.c_str(),
				100,					//max message number
				sizeof(ipc::Reply)    //max message size
				);
		} catch (std::exception& e) {
			delete _ipcServerQueue;
			_ipcServerQueue = nullptr;
			_ipcClientQueue = nullptr;
			std::stringstream ss;
			ss << "Could not open client-side message queue: " << e.what();
			throw vrinputemulator_connectionerror(ss.str());
		}
		// Start ipc thread
		_ipcThreadStop = false;
		_ipcThread = std::thread(_ipcThreadFunc, this);
		// Send ClientConnect message to server
		ipc::Request message(ipc::RequestType::IPC_ClientConnect);
		auto messageId = _ipcRandomDist(_ipcRandomDevice);
		message.msg.ipc_ClientConnect.messageId = messageId;
		message.msg.ipc_ClientConnect.ipcProcotolVersion = IPC_PROTOCOL_VERSION;
		strncpy_s(message.msg.ipc_ClientConnect.queueName, _ipcClientQueueName.c_str(), 127);
		message.msg.ipc_ClientConnect.queueName[127] = '\0';
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		// Wait for response
		auto resp = respFuture.get();
		m_clientId = resp.msg.ipc_ClientConnect.clientId;
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		if (resp.status != ipc::ReplyStatus::Ok) {
			delete _ipcServerQueue;
			_ipcServerQueue = nullptr;
			delete _ipcClientQueue;
			_ipcClientQueue = nullptr;
			std::stringstream ss;
			ss << "Connection rejected by server: ";
			if (resp.status == ipc::ReplyStatus::InvalidVersion) {
				ss << "Incompatible ipc protocol versions (server: " << resp.msg.ipc_ClientConnect.ipcProcotolVersion << ", client: " << IPC_PROTOCOL_VERSION << ")";
				throw vrinputemulator_invalidversion(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_connectionerror(ss.str());
			}
		}
	}
}

void VRInputEmulator::disconnect() {
	if (_ipcServerQueue) {
		// Send disconnect message (so the server can free resources)
		ipc::Request message(ipc::RequestType::IPC_ClientDisconnect);
		auto messageId = _ipcRandomDist(_ipcRandomDevice);
		message.msg.ipc_ClientDisconnect.clientId = m_clientId;
		message.msg.ipc_ClientDisconnect.messageId = messageId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		m_clientId = resp.msg.ipc_ClientConnect.clientId;
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		// Stop ipc thread
		if (_ipcThreadRunning) {
			_ipcThreadStop = true;
			_ipcThread.join();
		}
		// delete message queues
		if (_ipcServerQueue) {
			delete _ipcServerQueue;
			_ipcServerQueue = nullptr;
		}
		if (_ipcClientQueue) {
			delete _ipcClientQueue;
			_ipcClientQueue = nullptr;
		}
	}
}

void VRInputEmulator::ping(bool modal, bool enableReply) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		uint64_t nonce = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::IPC_Ping);
		message.msg.ipc_Ping.clientId = m_clientId;
		message.msg.ipc_Ping.messageId = messageId;
		message.msg.ipc_Ping.nonce = nonce;
		if (modal) {
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			if (resp.status != ipc::ReplyStatus::Ok) {
				std::stringstream ss;
				ss << "Error while pinging server: Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			if (enableReply) {
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				message.msg.ipc_Ping.messageId = messageId;
				_ipcPromiseMap.insert({ messageId, _ipcPromiseMapEntry() });
			} else {
				message.msg.ipc_Ping.messageId = 0;
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::openvrUpdatePose(uint32_t deviceId, const vr::DriverPose_t & pose) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::OpenVR_PoseUpdate);
		message.msg.ipc_PoseUpdate.deviceId = deviceId;
		message.msg.ipc_PoseUpdate.pose = pose;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::openvrButtonEvent(ButtonEventType eventType, uint32_t deviceId, vr::EVRButtonId buttonId, double timeOffset) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::OpenVR_ButtonEvent);
		message.msg.ipc_ButtonEvent.eventCount = 1;
		message.msg.ipc_ButtonEvent.events[0].eventType = eventType;
		message.msg.ipc_ButtonEvent.events[0].deviceId = deviceId;
		message.msg.ipc_ButtonEvent.events[0].buttonId = buttonId;
		message.msg.ipc_ButtonEvent.events[0].timeOffset = timeOffset;
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::openvrAxisEvent(uint32_t deviceId, uint32_t axisId, const vr::VRControllerAxis_t & axisState) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::OpenVR_AxisEvent);
		message.msg.ipc_AxisEvent.eventCount = 1;
		message.msg.ipc_AxisEvent.events[0].deviceId = deviceId;
		message.msg.ipc_AxisEvent.events[0].axisId = axisId;
		message.msg.ipc_AxisEvent.events[0].axisState = axisState;
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::openvrProximitySensorEvent(uint32_t deviceId, bool sensorTriggered) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::OpenVR_ProximitySensorEvent);
		message.msg.ovr_ProximitySensorEvent.deviceId = deviceId;
		message.msg.ovr_ProximitySensorEvent.sensorTriggered = sensorTriggered;
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::openvrVendorSpecificEvent(uint32_t deviceId, vr::EVREventType eventType, const vr::VREvent_Data_t & eventData, double timeOffset) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::OpenVR_VendorSpecificEvent);
		message.msg.ovr_VendorSpecificEvent.deviceId = deviceId;
		message.msg.ovr_VendorSpecificEvent.eventType = eventType;
		message.msg.ovr_VendorSpecificEvent.eventData = eventData;
		message.msg.ovr_VendorSpecificEvent.timeOffset = timeOffset;
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


uint32_t VRInputEmulator::getVirtualDeviceCount() {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::VirtualDevices_GetDeviceCount);
		message.msg.vd_GenericClientMessage.clientId = m_clientId;
		message.msg.vd_GenericClientMessage.messageId = messageId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		if (resp.status != ipc::ReplyStatus::Ok) {
			std::stringstream ss;
			ss << "Error while getting device count: Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
		return resp.msg.vd_GetDeviceCount.deviceCount;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


VirtualDeviceInfo VRInputEmulator::getVirtualDeviceInfo(uint32_t virtualDeviceId) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::VirtualDevices_GetDeviceInfo);
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
		message.msg.vd_GenericDeviceIdMessage.deviceId = virtualDeviceId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while getting device info: ";
		if (resp.status == ipc::ReplyStatus::InvalidId) {
			ss << "Invalid device id";
			throw vrinputemulator_invalidid(ss.str());
		} else if (resp.status == ipc::ReplyStatus::NotFound) {
			ss << "Device not found";
			throw vrinputemulator_notfound(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
		VirtualDeviceInfo retval;
		retval.openvrDeviceId = resp.msg.vd_GetDeviceInfo.openvrDeviceId;
		retval.virtualDeviceId = resp.msg.vd_GetDeviceInfo.virtualDeviceId;
		retval.deviceType = resp.msg.vd_GetDeviceInfo.deviceType;
		retval.deviceSerial = resp.msg.vd_GetDeviceInfo.deviceSerial;
		return retval;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


vr::DriverPose_t VRInputEmulator::getVirtualDevicePose(uint32_t virtualDeviceId) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::VirtualDevices_GetDevicePose);
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
		message.msg.vd_GenericDeviceIdMessage.deviceId = virtualDeviceId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while getting device info: ";
		if (resp.status == ipc::ReplyStatus::InvalidId) {
			ss << "Invalid device id";
			throw vrinputemulator_invalidid(ss.str());
		} else if (resp.status == ipc::ReplyStatus::NotFound) {
			ss << "Device not found";
			throw vrinputemulator_notfound(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
		return resp.msg.vd_GetDevicePose.pose;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


vr::VRControllerState_t VRInputEmulator::getVirtualControllerState(uint32_t virtualDeviceId) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::VirtualDevices_GetControllerState);
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
		message.msg.vd_GenericDeviceIdMessage.deviceId = virtualDeviceId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while getting device info: ";
		if (resp.status == ipc::ReplyStatus::InvalidId) {
			ss << "Invalid device id";
			throw vrinputemulator_invalidid(ss.str());
		} else if (resp.status == ipc::ReplyStatus::NotFound) {
			ss << "Device not found";
			throw vrinputemulator_notfound(ss.str());
		} else if (resp.status == ipc::ReplyStatus::InvalidType) {
			ss << "Device type does not support this";
			throw vrinputemulator_invalidtype(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
		return resp.msg.vd_GetControllerState.controllerState;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


uint32_t VRInputEmulator::addVirtualDevice(VirtualDeviceType deviceType, const std::string & deviceSerial, bool softfail) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::VirtualDevices_AddDevice);
		message.msg.vd_AddDevice.clientId = m_clientId;
		message.msg.vd_AddDevice.messageId = messageId;
		message.msg.vd_AddDevice.deviceType = deviceType;
		strncpy_s(message.msg.vd_AddDevice.deviceSerial, deviceSerial.c_str(), 127);
		message.msg.vd_AddDevice.deviceSerial[127] = '\0';
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while adding device: ";
		if (resp.status == ipc::ReplyStatus::TooManyDevices) {
			ss << "Too many devices";
			throw vrinputemulator_toomanydevices(ss.str());
		} else if (resp.status == ipc::ReplyStatus::AlreadyInUse) {
			if (!softfail) {
				ss << "Serial already in use";
				throw vrinputemulator_alreadyinuse(ss.str());
			}
		} else if (resp.status == ipc::ReplyStatus::InvalidType) {
			ss << "Device type not supported";
			throw vrinputemulator_invalidtype(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
		return resp.msg.vd_AddDevice.virtualDeviceId;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::publishVirtualDevice(uint32_t virtualDeviceId, bool modal) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::VirtualDevices_PublishDevice);
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
		message.msg.vd_GenericDeviceIdMessage.deviceId = virtualDeviceId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while publishing device: ";
		if (resp.status == ipc::ReplyStatus::InvalidId) {
			ss << "Invalid device id";
			throw vrinputemulator_invalidid(ss.str());
		} else if (resp.status == ipc::ReplyStatus::NotFound) {
			ss << "Device not found";
			throw vrinputemulator_notfound(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::_setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, std::function<void(ipc::Request&)> dataHandler, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::VirtualDevices_SetDeviceProperty);
		message.msg.vd_SetDeviceProperty.clientId = m_clientId;
		message.msg.vd_SetDeviceProperty.virtualDeviceId = virtualDeviceId;
		message.msg.vd_SetDeviceProperty.deviceProperty = deviceProperty;
		dataHandler(message);
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.vd_SetDeviceProperty.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting device property: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status == ipc::ReplyStatus::InvalidType) {
				ss << "Invalid value type";
				throw vrinputemulator_invalidtype(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			message.msg.vd_SetDeviceProperty.messageId = 0;
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, int32_t value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::INT32;
		msg.msg.vd_SetDeviceProperty.value.int32Value = value;
	}, modal);
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, uint64_t value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::UINT64;
		msg.msg.vd_SetDeviceProperty.value.uint64Value = value;
	}, modal);
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, float value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::FLOAT;
		msg.msg.vd_SetDeviceProperty.value.floatValue = value;
	}, modal);
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, bool value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::BOOL;
		msg.msg.vd_SetDeviceProperty.value.boolValue = value;
	}, modal);
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, const vr::HmdMatrix34_t& value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::MATRIX34;
		msg.msg.vd_SetDeviceProperty.value.matrix34Value = value;
	}, modal);
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, const char* value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::STRING;
		strncpy_s(msg.msg.vd_SetDeviceProperty.value.stringValue, value, 255);
		msg.msg.vd_SetDeviceProperty.value.stringValue[255] = '\0';
	}, modal);
}

void VRInputEmulator::setVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, const std::string& value, bool modal) {
	_setVirtualDeviceProperty(virtualDeviceId, deviceProperty, [value](ipc::Request& msg) {
		msg.msg.vd_SetDeviceProperty.valueType = DevicePropertyValueType::STRING;
		strncpy_s(msg.msg.vd_SetDeviceProperty.value.stringValue, value.c_str(), 255);
		msg.msg.vd_SetDeviceProperty.value.stringValue[255] = '\0';
	}, modal);
}

void VRInputEmulator::removeVirtualDeviceProperty(uint32_t virtualDeviceId, vr::ETrackedDeviceProperty deviceProperty, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::VirtualDevices_RemoveDeviceProperty);
		message.msg.vd_RemoveDeviceProperty.clientId = m_clientId;
		message.msg.vd_RemoveDeviceProperty.virtualDeviceId = virtualDeviceId;
		message.msg.vd_RemoveDeviceProperty.deviceProperty = deviceProperty;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.vd_RemoveDeviceProperty.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while removing device property: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			message.msg.vd_RemoveDeviceProperty.messageId = 0;
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setVirtualDevicePose(uint32_t virtualDeviceId, const vr::DriverPose_t & pose, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::VirtualDevices_SetDevicePose);
		message.msg.vd_SetDevicePose.clientId = m_clientId;
		message.msg.vd_SetDevicePose.virtualDeviceId = virtualDeviceId;
		message.msg.vd_SetDevicePose.pose = pose;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.vd_SetDevicePose.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting device pose: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			message.msg.vd_SetDevicePose.messageId = 0;
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setVirtualControllerState(uint32_t virtualDeviceId, const vr::VRControllerState_t & state, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::VirtualDevices_SetControllerState);
		message.msg.vd_SetControllerState.clientId = m_clientId;
		message.msg.vd_SetControllerState.virtualDeviceId = virtualDeviceId;
		message.msg.vd_SetControllerState.controllerState = state;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.vd_SetControllerState.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting controller state: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status == ipc::ReplyStatus::InvalidType) {
				ss << "Device type does not support this operation";
				throw vrinputemulator_invalidtype(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			message.msg.vd_SetControllerState.messageId = 0;
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::enableDeviceButtonMapping(uint32_t deviceId, bool enable, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_ButtonMapping);
		message.msg.dm_ButtonMapping.clientId = m_clientId;
		message.msg.dm_ButtonMapping.messageId = 0;
		message.msg.dm_ButtonMapping.deviceId = deviceId;
		message.msg.dm_ButtonMapping.enableMapping = enable ? 1 : 2;
		message.msg.dm_ButtonMapping.mappingOperation = 0;
		message.msg.dm_ButtonMapping.mappingCount = 0;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_ButtonMapping.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::addDeviceButtonMapping(uint32_t deviceId, vr::EVRButtonId button, vr::EVRButtonId mapped, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_ButtonMapping);
		message.msg.dm_ButtonMapping.clientId = m_clientId;
		message.msg.dm_ButtonMapping.messageId = 0;
		message.msg.dm_ButtonMapping.deviceId = deviceId;
		message.msg.dm_ButtonMapping.enableMapping = 0;
		message.msg.dm_ButtonMapping.mappingOperation = 1;
		message.msg.dm_ButtonMapping.mappingCount = 1;
		message.msg.dm_ButtonMapping.buttonMappings[0] = button;
		message.msg.dm_ButtonMapping.buttonMappings[1] = mapped;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_ButtonMapping.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::removeDeviceButtonMapping(uint32_t deviceId, vr::EVRButtonId button, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_ButtonMapping);
		message.msg.dm_ButtonMapping.clientId = m_clientId;
		message.msg.dm_ButtonMapping.messageId = 0;
		message.msg.dm_ButtonMapping.deviceId = deviceId;
		message.msg.dm_ButtonMapping.enableMapping = 0;
		message.msg.dm_ButtonMapping.mappingOperation = 2;
		message.msg.dm_ButtonMapping.mappingCount = 1;
		message.msg.dm_ButtonMapping.buttonMappings[0] = button;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_ButtonMapping.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::removeAllDeviceButtonMappings(uint32_t deviceId, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_ButtonMapping);
		message.msg.dm_ButtonMapping.clientId = m_clientId;
		message.msg.dm_ButtonMapping.messageId = 0;
		message.msg.dm_ButtonMapping.deviceId = deviceId;
		message.msg.dm_ButtonMapping.enableMapping = 0;
		message.msg.dm_ButtonMapping.mappingOperation = 3;
		message.msg.dm_ButtonMapping.mappingCount = 0;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_ButtonMapping.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::getDeviceOffsets(uint32_t deviceId, DeviceOffsets & data) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_GetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.deviceId = deviceId;
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while enabling device offsets: ";
		if (resp.status == ipc::ReplyStatus::Ok) {
			memcpy(&data, &resp.msg.dm_deviceOffsets, sizeof(DeviceOffsets));
		} else if (resp.status == ipc::ReplyStatus::InvalidId) {
			ss << "Invalid device id";
			throw vrinputemulator_invalidid(ss.str());
		} else if (resp.status == ipc::ReplyStatus::NotFound) {
			ss << "Device not found";
			throw vrinputemulator_notfound(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::enableDeviceOffsets(uint32_t deviceId, bool enable, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.enableOffsets = enable ? 1 : 2;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			message.msg.dm_DeviceOffsets.messageId = 0;
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setWorldFromDriverRotationOffset(uint32_t deviceId, const vr::HmdQuaternion_t & value, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.worldFromDriverRotationOffsetValid = true;
		message.msg.dm_DeviceOffsets.worldFromDriverRotationOffset = value;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setWorldFromDriverTranslationOffset(uint32_t deviceId, const vr::HmdVector3d_t & value, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.worldFromDriverTranslationOffsetValid = true;
		message.msg.dm_DeviceOffsets.worldFromDriverTranslationOffset = value;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDriverFromHeadRotationOffset(uint32_t deviceId, const vr::HmdQuaternion_t & value, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.driverFromHeadRotationOffsetValid = true;
		message.msg.dm_DeviceOffsets.driverFromHeadRotationOffset = value;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDriverFromHeadTranslationOffset(uint32_t deviceId, const vr::HmdVector3d_t & value, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.driverFromHeadTranslationOffsetValid = true;
		message.msg.dm_DeviceOffsets.driverFromHeadTranslationOffset = value;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDriverRotationOffset(uint32_t deviceId, const vr::HmdQuaternion_t & value, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.deviceRotationOffsetValid = true;
		message.msg.dm_DeviceOffsets.deviceRotationOffset = value;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDriverTranslationOffset(uint32_t deviceId, const vr::HmdVector3d_t & value, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetDeviceOffsets);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_DeviceOffsets.clientId = m_clientId;
		message.msg.dm_DeviceOffsets.messageId = 0;
		message.msg.dm_DeviceOffsets.deviceId = deviceId;
		message.msg.dm_DeviceOffsets.deviceTranslationOffsetValid = true;
		message.msg.dm_DeviceOffsets.deviceTranslationOffset = value;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::getDeviceInfo(uint32_t deviceId, DeviceInfo & info) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_GetDeviceInfo);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.deviceId = deviceId;
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		std::stringstream ss;
		ss << "Error while getting device info: ";
		if (resp.status == ipc::ReplyStatus::Ok) {
			info.deviceId = resp.msg.dm_deviceInfo.deviceId;
			info.deviceClass = resp.msg.dm_deviceInfo.deviceClass;
			info.deviceMode = resp.msg.dm_deviceInfo.deviceMode;
			info.refDeviceId = resp.msg.dm_deviceInfo.refDeviceId;
			info.offsetsEnabled = resp.msg.dm_deviceInfo.offsetsEnabled;
			info.redirectSuspended = resp.msg.dm_deviceInfo.redirectSuspended;
		} else if (resp.status == ipc::ReplyStatus::InvalidId) {
			ss << "Invalid device id";
			throw vrinputemulator_invalidid(ss.str());
		} else if (resp.status == ipc::ReplyStatus::NotFound) {
			ss << "Device not found";
			throw vrinputemulator_notfound(ss.str());
		} else if (resp.status != ipc::ReplyStatus::Ok) {
			ss << "Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str());
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDeviceNormalMode(uint32_t deviceId, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_DefaultMode);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.messageId = 0;
		message.msg.vd_GenericDeviceIdMessage.deviceId = deviceId;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_DeviceOffsets.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting normal mode: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDeviceFakeDisconnectedMode(uint32_t deviceId, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_FakeDisconnectedMode);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.vd_GenericDeviceIdMessage.clientId = m_clientId;
		message.msg.vd_GenericDeviceIdMessage.messageId = 0;
		message.msg.vd_GenericDeviceIdMessage.deviceId = deviceId;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.vd_GenericDeviceIdMessage.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting fake disconnection mode: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDeviceRedictMode(uint32_t deviceId, uint32_t target, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_RedirectMode);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_RedirectMode.clientId = m_clientId;
		message.msg.dm_RedirectMode.messageId = 0;
		message.msg.dm_RedirectMode.deviceId = deviceId;
		message.msg.dm_RedirectMode.targetId = target;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_RedirectMode.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting redirect mode: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str());
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str());
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str());
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDeviceSwapMode(uint32_t deviceId, uint32_t target, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SwapMode);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_SwapMode.clientId = m_clientId;
		message.msg.dm_SwapMode.messageId = 0;
		message.msg.dm_SwapMode.deviceId = deviceId;
		message.msg.dm_SwapMode.targetId = target;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_SwapMode.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting swap mode: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::setDeviceMotionCompensationMode(uint32_t deviceId, MotionCompensationVelAccMode velAccMode, bool modal) {
	bool retval = false;
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_MotionCompensationMode);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_MotionCompensationMode.clientId = m_clientId;
		message.msg.dm_MotionCompensationMode.messageId = 0;
		message.msg.dm_MotionCompensationMode.deviceId = deviceId;
		message.msg.dm_MotionCompensationMode.velAccCompensationMode = velAccMode;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_MotionCompensationMode.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting motion compensation mode: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::setMotionVelAccCompensationMode(MotionCompensationVelAccMode velAccMode, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetMotionCompensationProperties);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_SetMotionCompensationProperties.clientId = m_clientId;
		message.msg.dm_SetMotionCompensationProperties.messageId = 0;
		message.msg.dm_SetMotionCompensationProperties.velAccCompensationModeValid = true;
		message.msg.dm_SetMotionCompensationProperties.velAccCompensationMode = velAccMode;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterProcessNoiseValid = false;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterObservationNoiseValid = false;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_SetMotionCompensationProperties.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting motion compensation properties: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setMotionCompensationKalmanProcessNoise(double variance, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetMotionCompensationProperties);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_SetMotionCompensationProperties.clientId = m_clientId;
		message.msg.dm_SetMotionCompensationProperties.messageId = 0;
		message.msg.dm_SetMotionCompensationProperties.velAccCompensationModeValid = false;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterProcessNoiseValid = true;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterProcessNoise = variance;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterObservationNoiseValid = false;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_SetMotionCompensationProperties.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting motion compensation properties: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setMotionCompensationKalmanObservationNoise(double variance, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetMotionCompensationProperties);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_SetMotionCompensationProperties.clientId = m_clientId;
		message.msg.dm_SetMotionCompensationProperties.messageId = 0;
		message.msg.dm_SetMotionCompensationProperties.velAccCompensationModeValid = false;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterProcessNoiseValid = false;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterObservationNoiseValid = true;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterObservationNoise = variance;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_SetMotionCompensationProperties.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting motion compensation properties: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setMotionCompensationMovingAverageWindow(unsigned window, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_SetMotionCompensationProperties);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_SetMotionCompensationProperties.clientId = m_clientId;
		message.msg.dm_SetMotionCompensationProperties.messageId = 0;
		message.msg.dm_SetMotionCompensationProperties.velAccCompensationModeValid = false;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterProcessNoiseValid = false;
		message.msg.dm_SetMotionCompensationProperties.kalmanFilterObservationNoiseValid = false;
		message.msg.dm_SetMotionCompensationProperties.movingAverageWindowValid = true;
		message.msg.dm_SetMotionCompensationProperties.movingAverageWindow = window;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_SetMotionCompensationProperties.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting motion compensation properties: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}


void VRInputEmulator::triggerHapticPulse(uint32_t deviceId, uint32_t axisId, uint16_t durationMicroseconds, bool directMode, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::DeviceManipulation_TriggerHapticPulse);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.dm_triggerHapticPulse.clientId = m_clientId;
		message.msg.dm_triggerHapticPulse.messageId = 0;
		message.msg.dm_triggerHapticPulse.deviceId = deviceId;
		message.msg.dm_triggerHapticPulse.axisId = axisId;
		message.msg.dm_triggerHapticPulse.durationMicroseconds = durationMicroseconds;
		message.msg.dm_triggerHapticPulse.directMode = directMode;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.dm_triggerHapticPulse.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while enabling device offsets: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setDigitalInputRemapping(uint32_t deviceId, uint32_t buttonId, const DigitalInputRemapping & remapping, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::InputRemapping_SetDigitalRemapping);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.ir_SetDigitalRemapping.clientId = m_clientId;
		message.msg.ir_SetDigitalRemapping.messageId = 0;
		message.msg.ir_SetDigitalRemapping.controllerId = deviceId;
		message.msg.ir_SetDigitalRemapping.buttonId = buttonId;
		message.msg.ir_SetDigitalRemapping.remapData = remapping;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.ir_SetDigitalRemapping.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting digital input remapping: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

DigitalInputRemapping VRInputEmulator::getDigitalInputRemapping(uint32_t deviceId, uint32_t buttonId) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::InputRemapping_GetDigitalRemapping);
		message.msg.ir_GetDigitalRemapping.clientId = m_clientId;
		message.msg.ir_GetDigitalRemapping.messageId = messageId;
		message.msg.ir_GetDigitalRemapping.controllerId = deviceId;
		message.msg.ir_GetDigitalRemapping.buttonId = buttonId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		if (resp.status != ipc::ReplyStatus::Ok) {
			std::stringstream ss;
			ss << "Error while getting digital input remapping: Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str(), (int)resp.status);
		}
		return resp.msg.ir_getDigitalRemapping.remapData;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

void VRInputEmulator::setAnalogInputRemapping(uint32_t deviceId, uint32_t axisId, const AnalogInputRemapping & remapping, bool modal) {
	if (_ipcServerQueue) {
		ipc::Request message(ipc::RequestType::InputRemapping_SetAnalogRemapping);
		memset(&message.msg, 0, sizeof(message.msg));
		message.msg.ir_SetAnalogRemapping.clientId = m_clientId;
		message.msg.ir_SetAnalogRemapping.messageId = 0;
		message.msg.ir_SetAnalogRemapping.controllerId = deviceId;
		message.msg.ir_SetAnalogRemapping.axisId = axisId;
		message.msg.ir_SetAnalogRemapping.remapData = remapping;
		if (modal) {
			uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
			message.msg.ir_SetAnalogRemapping.messageId = messageId;
			std::promise<ipc::Reply> respPromise;
			auto respFuture = respPromise.get_future();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
			}
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
			auto resp = respFuture.get();
			{
				std::lock_guard<std::recursive_mutex> lock(_mutex);
				_ipcPromiseMap.erase(messageId);
			}
			std::stringstream ss;
			ss << "Error while setting analog input remapping: ";
			if (resp.status == ipc::ReplyStatus::InvalidId) {
				ss << "Invalid device id";
				throw vrinputemulator_invalidid(ss.str(), (int)resp.status);
			} else if (resp.status == ipc::ReplyStatus::NotFound) {
				ss << "Device not found";
				throw vrinputemulator_notfound(ss.str(), (int)resp.status);
			} else if (resp.status != ipc::ReplyStatus::Ok) {
				ss << "Error code " << (int)resp.status;
				throw vrinputemulator_exception(ss.str(), (int)resp.status);
			}
		} else {
			_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		}
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}

AnalogInputRemapping VRInputEmulator::getAnalogInputRemapping(uint32_t deviceId, uint32_t axisId) {
	if (_ipcServerQueue) {
		uint32_t messageId = _ipcRandomDist(_ipcRandomDevice);
		ipc::Request message(ipc::RequestType::InputRemapping_GetAnalogRemapping);
		message.msg.ir_GetAnalogRemapping.clientId = m_clientId;
		message.msg.ir_GetAnalogRemapping.messageId = messageId;
		message.msg.ir_GetAnalogRemapping.controllerId = deviceId;
		message.msg.ir_GetAnalogRemapping.axisId = axisId;
		std::promise<ipc::Reply> respPromise;
		auto respFuture = respPromise.get_future();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.insert({ messageId, std::move(respPromise) });
		}
		_ipcServerQueue->send(&message, sizeof(ipc::Request), 0);
		auto resp = respFuture.get();
		{
			std::lock_guard<std::recursive_mutex> lock(_mutex);
			_ipcPromiseMap.erase(messageId);
		}
		if (resp.status != ipc::ReplyStatus::Ok) {
			std::stringstream ss;
			ss << "Error while getting analog input remapping: Error code " << (int)resp.status;
			throw vrinputemulator_exception(ss.str(), (int)resp.status);
		}
		return resp.msg.ir_getAnalogRemapping.remapData;
	} else {
		throw vrinputemulator_connectionerror("No active connection.");
	}
}




} // end namespace vrinputemulator