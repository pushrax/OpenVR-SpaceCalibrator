#include "IPCServer.h"
#include "Logging.h"
#include "ServerTrackedDeviceProvider.h"
#include "Comms.h"

#include ""

struct IPCServerImpl {
};

void IPCServer::HandleRequest(const protocol::Request &request, protocol::Response &response)
{
	switch (request.type)
	{
	case protocol::RequestHandshake:
		response.type = protocol::ResponseHandshake;
		response.protocol.version = protocol::Version;
		break;

	case protocol::RequestSetDeviceTransform:
		driver->SetDeviceTransform(request.setDeviceTransform);
		response.type = protocol::ResponseSuccess;
		break;

	default:
		LOG("Invalid IPC request: %d", request.type);
		break;
	}
}

IPCServer::~IPCServer()
{
	Stop();
}

void IPCServer::Run()
{
	mainThread = std::thread(RunThread, this);
}

void IPCServer::Stop()
{
	TRACE("%s", "IPCServer::Stop()");
	if (!running)
		return;
	stop = true;
}
void IPCServer::RunThread(IPCServer *_this)
{
    Comms<protocol::Response, protocol::Request> comms;

    protocol::Response response;
    protocol::Request request;

    while(!_this->stop){
        comms.Recv(&request);
        _this->HandleRequest(request, response);
        comms.Send(response);
    }
    LOG("%s", "Stop requested");
}

