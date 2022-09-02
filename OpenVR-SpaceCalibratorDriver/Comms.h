#pragma once

#include <cstdint>
#include <memory>
#include <sys/socket.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "Logging.h"
#include "../Protocol.h"

class UDPServerSocket{
    int sock;
    public:
        UDPServerSocket(const UDPServerSocket& other) = delete;
        UDPServerSocket& operator=(const UDPServerSocket& other) = delete;

        UDPServerSocket(){}


        bool Open(unsigned short port){
            sockaddr_in my_addr = {};

            my_addr.sin_family = AF_INET;
            my_addr.sin_port = htons(port);
            my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

            //TODO verify that 0 is valid for UDP.
            sock = socket(AF_INET, SOCK_DGRAM, 0);
            if(sock == -1){
                LOG("Failed to create socket: %s", strerror(errno));
                return false;
            }

            int err = bind(sock, (sockaddr*)&my_addr, sizeof(my_addr));
            if(err){
                LOG("Failed to bind socket: %s", strerror(errno));
                return false;
            }
            return true;
        }

        template <typename RecvObj>
        bool Recv(RecvObj* obj, sockaddr* client, socklen_t* clientSize) {
            size_t targetSize = sizeof(*obj);
            size_t bytes = recvfrom(sock, (void*)obj, targetSize, 0,
                                 client, clientSize);
            if(bytes != targetSize){
                LOG("Error, recieved %d bytes when %d were expected", bytes, (int)targetSize);
                return false;
            }
            else {
                return true;
            }
        }

        template<typename SendObj>
        bool Send(const SendObj& obj, sockaddr* sendTo, size_t sendToSize) {
            ssize_t targetSize = sizeof(obj);
            ssize_t bytes = sendto(sock, (void*)&obj, targetSize, 0, sendTo, sendToSize);
            return targetSize == bytes;
        }
};

template<typename SendObj, typename RecvObj>
class Comms{
    sockaddr_in lastClient;
    socklen_t peer_addr_len;

    UDPServerSocket sock;
    public:
        Comms(const Comms& other) = delete;
        Comms& operator=(const Comms& other) = delete;

        Comms(){
            if( !sock.Open(COMM_PORT_SERVER) ){
                LOG("%s", "Error opening server socket");
            }
        }

        void Recv(RecvObj* obj){
            while(true){
                if(sock.Recv(obj, (sockaddr*) &lastClient, &peer_addr_len)){
                    return;
                } else {
                    LOG("%s", "failed to recve value");
                }
            }
        }

        void Send(const SendObj& obj ) {
            sock.Send(obj, (sockaddr*) &lastClient, sizeof(lastClient));
        }
};


template<typename SendObj, typename RecvObj>
class Client{
    sockaddr_in serverAddr = {};
    socklen_t peer_addr_len = {};

    UDPServerSocket sock;
    public:
        Client(const Client& other) = delete;
        Client& operator=(const Client& other) = delete;

        Client(){
            if( !sock.Open(COMM_PORT_CLIENT) ){
                LOG("%s", "Error opening server socket");
            }

            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(COMM_PORT_SERVER);
            serverAddr.sin_addr.s_addr = INADDR_ANY;
        }

        void Recv(RecvObj* obj){
            while(true){
                sockaddr_in lastClient;
                lastClient.sin_family = AF_INET;
                lastClient.sin_port = htons(COMM_PORT_SERVER);
                lastClient.sin_addr.s_addr = INADDR_ANY;
 
                if(sock.Recv(obj, (sockaddr*) &lastClient, &peer_addr_len)){
                    return;
                } else {
                    LOG("%s", "failed to recve value");
                }
            }
        }

        void Send(const SendObj& obj ) {
            sock.Send(obj, (sockaddr*) &serverAddr, sizeof(serverAddr));
        }
};

