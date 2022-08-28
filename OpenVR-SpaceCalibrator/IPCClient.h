#pragma once

#if  !defined(_WIN32) && !defined(_WIN64)
#include "compat.h"
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
#if  !defined(_WIN32) && !defined(_WIN64)
    Client<protocol::Request, protocol::Response> pipe;
#else
    HANDLE pipe = INVALID_HANDLE_VALUE;
#endif
};
