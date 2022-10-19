#include "IPCClient.h"
#ifdef __linux__
#include "Comms.h"
#endif

#include <string>
#include <iostream>

#ifdef __linux__
// NOP
#else
static std::string LastErrorString(DWORD lastError)
{
	LPSTR buffer = nullptr;
	size_t size = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buffer, 0, NULL
	);

	std::string message(buffer, size);
	LocalFree(buffer);
	return message;
}
#endif

IPCClient::~IPCClient()
{
#ifdef __linux__
    //NOP
#else
	if (pipe && pipe != INVALID_HANDLE_VALUE)
		CloseHandle(pipe);
#endif
}

void IPCClient::Connect()
{
#ifdef __linux__
#else
	LPTSTR pipeName = TEXT(OPENVR_SPACECALIBRATOR_PIPE_NAME);

	WaitNamedPipe(pipeName, 1000);
	pipe = CreateFile(pipeName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);

	if (pipe == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("Space Calibrator driver unavailable. Make sure SteamVR is running, and the Space Calibrator addon is enabled in SteamVR settings.");
	}

	DWORD mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(pipe, &mode, 0, 0))
	{
		throw std::runtime_error("Couldn't set pipe mode. Error: " + LastErrorString(GetLastError()));
	}
#endif
	auto response = SendBlocking(protocol::Request(protocol::RequestHandshake));
	if (response.type != protocol::ResponseHandshake || response.protocol.version != protocol::Version)
	{
		throw std::runtime_error(
			"Incorrect driver version installed, try reinstalling OpenVR-SpaceCalibrator. (Client: " +
			std::to_string(protocol::Version) +
			", Driver: " +
			std::to_string(response.protocol.version) +
			")"
		);
	}
}

protocol::Response IPCClient::SendBlocking(const protocol::Request &request)
{
#ifdef __linux__
    for(int i=0; i<10; i++){
        Send(request);
        try{
            return Receive();
        } catch (std::runtime_error &t) {
            LOG("%s: %s", "Recv timeout failed", t.what());
        }
    }
    throw std::runtime_error("Fell through read loop");
#else
    Send(request);
    return Receive();
#endif
}

void IPCClient::Send(const protocol::Request &request)
{
#ifdef __linux__
    pipe.Send(request);
#else
	DWORD bytesWritten;
	BOOL success = WriteFile(pipe, &request, sizeof request, &bytesWritten, 0);
	if (!success)
	{
		throw std::runtime_error("Error writing IPC request. Error: " + LastErrorString(GetLastError()));
	}
#endif
}

protocol::Response IPCClient::Receive()
{
	protocol::Response response(protocol::ResponseInvalid);
#ifdef __linux__
    if( pipe.Recv(&response) )
        return response;
    else
		throw std::runtime_error("No packet was received");
#else
	DWORD bytesRead;

	BOOL success = ReadFile(pipe, &response, sizeof response, &bytesRead, 0);
	if (!success)
	{
		DWORD lastError = GetLastError();
		if (lastError != ERROR_MORE_DATA)
		{
			throw std::runtime_error("Error reading IPC response. Error: " + LastErrorString(lastError));
		}
	}

	if (bytesRead != sizeof response)
	{
		throw std::runtime_error("Invalid IPC response with size " + std::to_string(bytesRead));
	}

	return response;
#endif
}
