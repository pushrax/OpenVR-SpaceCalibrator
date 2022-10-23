#pragma once

#include "../Protocol.h"

#include <thread>
#include <set>
#include <mutex>

class ServerTrackedDeviceProvider;

class IPCServer
{
public:
	IPCServer(ServerTrackedDeviceProvider *driver);
	~IPCServer();

	void Run();
	void Stop();

private:
	void HandleRequest(const protocol::Request &request, protocol::Response &response);

	static void RunThread(IPCServer *_this);
	std::thread mainThread;

	bool running = false;
	bool stop = false;

	ServerTrackedDeviceProvider *driver;
};
