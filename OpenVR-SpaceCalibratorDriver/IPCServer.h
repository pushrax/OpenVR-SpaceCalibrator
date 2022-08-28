#pragma once

#include "../Protocol.h"
#include "compat.h"

#include <thread>
#include <set>
#include <mutex>

#if  !defined(_WIN32) && !defined(_WIN64)
//NOP: linux isn't windows
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

class ServerTrackedDeviceProvider;

class IPCServer
{
public:
	IPCServer(ServerTrackedDeviceProvider *driver) : driver(driver) { }
	~IPCServer();

	void Run();
	void Stop();

private:
	void HandleRequest(const protocol::Request &request, protocol::Response &response);

#if  !defined(_WIN32) && !defined(_WIN64)
    //NOP: Not used in Linux build
#else
	struct PipeInstance
	{
		OVERLAPPED overlap; // Used by the API
		HANDLE pipe;
		IPCServer *server;

		protocol::Request request;
		protocol::Response response;
	};

	PipeInstance *CreatePipeInstance(HANDLE pipe);
	void ClosePipeInstance(PipeInstance *pipeInst);
	static void WINAPI CompletedReadCallback(DWORD err, DWORD bytesRead, LPOVERLAPPED overlap);
	static void WINAPI CompletedWriteCallback(DWORD err, DWORD bytesWritten, LPOVERLAPPED overlap);
	static BOOL CreateAndConnectInstance(LPOVERLAPPED overlap, HANDLE &pipe);

	std::set<PipeInstance *> pipes;
	HANDLE connectEvent;
#endif
	static void RunThread(IPCServer *_this);
	std::thread mainThread;

	bool running = false;
	bool stop = false;

	ServerTrackedDeviceProvider *driver;
};
