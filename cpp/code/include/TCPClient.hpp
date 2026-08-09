#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "TCPServer.hpp"
#include "WorkItem.pb.h"

namespace tuddbs {

class TCPClient {
   public:
    using ServerHandle = int;

   private:
    std::string _server_ip;
    uint64_t _session_uuid;
    int _port;
    bool _verbose;

    std::thread t;
    void* incomplete_message_buffer;
    size_t unprocessed_bytes;
    std::condition_variable channel_cv;
    std::mutex channel_mutex;

    std::map<TcpPackageType, ReceiveCallback> callbacks;
    void listenLoop();
    bool globalAbort = false;
    ServerHandle serverHandle;
    
    void generateSessionUuid();

   public:
    TCPClient(std::string ip, int port, bool verbose = false);
    ~TCPClient();

    void start();
    void closeConnection();
    bool isConnected() const;
    
    uint64_t getUuid() const;
    void addCallback(TcpPackageType type, ReceiveCallback cb);
    void notifyHost(void* data, size_t len);
    void textResponse(std::string text, uint64_t tgt_uuid = 0);

    void waitUntilChannelClosed();
};

}  // namespace tuddbs