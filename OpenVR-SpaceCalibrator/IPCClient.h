#pragma once

#ifdef __linux__
#include "Comms.h"
#else
#endif

#include "../Protocol.h"

class IPCClient
{
public:
	~IPCClient();

	void Connect();
	protocol::Response SendBlocking(const protocol::Request &request);

	void Send(const protocol::Request &request);
	protocol::Response Receive();

private:
#ifdef __linux__
    Client<protocol::Request, protocol::Response> pipe;
#else
    HANDLE pipe = INVALID_HANDLE_VALUE;
#endif
};
