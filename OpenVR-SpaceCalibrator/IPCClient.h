#pragma once

#include <memory>
#include "../Protocol.h"

struct IPCClientImpl; 

class IPCClient
{
public:
	~IPCClient();
	IPCClient();

	void Connect();
	protocol::Response SendBlocking(const protocol::Request &request);

	void Send(const protocol::Request &request);
	protocol::Response Receive();

private:
	std::unique_ptr<IPCClientImpl> impl;
};
