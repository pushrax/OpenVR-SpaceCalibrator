#include "IPCClient.h"
#include "Comms.h"

#include <string>
#include <iostream>

struct IPCClientImpl{
	Client<protocol::Request, protocol::Response> pipe;
};

IPCClient::IPCClient()
{
	impl = std::make_unique<IPCClientImpl>();
}

IPCClient::~IPCClient()
{
	//NOP
}

void IPCClient::Connect()
{
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
	for(int i=0; i<10; i++){
		Send(request);
		try{
			return Receive();
		} catch (std::runtime_error &t) {
			LOG("%s: %s", "Recv timeout failed", t.what());
		}
	}
	throw std::runtime_error("Fell through read loop");
}

void IPCClient::Send(const protocol::Request &request)
{
	impl->pipe.Send(request);
}

protocol::Response IPCClient::Receive()
{
	protocol::Response response(protocol::ResponseInvalid);
	if( impl->pipe.Recv(&response) )
		return response;
	else
		throw std::runtime_error("No packet was received");
}
