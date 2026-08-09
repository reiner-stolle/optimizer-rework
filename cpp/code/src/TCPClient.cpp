#include "TCPClient.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <functional>
#include <iostream>

#include "Utility.hpp"

int sendFlags = MSG_NOSIGNAL | MSG_DONTWAIT;

namespace tuddbs {

TCPClient::TCPClient(std::string ip, int port, bool verbose) : _server_ip(ip), _session_uuid(0), _port(port), _verbose(verbose) {
    auto update_uuid_on_collision = [this](tuddbs::TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[TCPClient] UUID collision signaled from TCPServer. Creating a new UUID." << std::endl;
        generateSessionUuid();
        TCPMetaInfo info;
        info.package_type = TcpPackageType::UUID_COLLISION;
        info.src_uuid = getUuid();
        notifyHost(&info, sizeof(TCPMetaInfo));
    };
    addCallback(TcpPackageType::UUID_COLLISION, update_uuid_on_collision);
}

TCPClient::~TCPClient() {
    closeConnection();
}

void TCPClient::generateSessionUuid() {
    std::mt19937_64 gen(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());
    _session_uuid = dist(gen);
}

void TCPClient::start() {
    serverHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverHandle < 0) {
        throw std::runtime_error("Could not open socket.");
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = htons(_port);

    if (inet_pton(AF_INET, _server_ip.c_str(), &address.sin_addr) <= 0) {
        throw std::runtime_error("Could not set target IP.");
    }

    if (connect(serverHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("Could not connect to server.");
    }

    int yes = 1;
    int result = setsockopt(serverHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof(int));  // 1 - on, 0 - off
    if (result != 0) {
        std::cout << "[Warning] Could not set TCP_NODELAY" << std::endl;
    }
    result = setsockopt(serverHandle, IPPROTO_TCP, TCP_QUICKACK, reinterpret_cast<char*>(&yes), sizeof(int));  // 1 - on, 0 - off
    if (result != 0) {
        std::cout << "[Warning] Could not set TCP_QUICKACK" << std::endl;
    }
    generateSessionUuid();
    t = std::thread(&TCPClient::listenLoop, this);
}

void TCPClient::closeConnection() {
    ::shutdown(serverHandle, SHUT_RDWR);
    close(serverHandle);
    globalAbort = true;
    if (t.joinable()) {
        t.join();
    }
}

bool TCPClient::isConnected() const {
    return !globalAbort;
}

void TCPClient::notifyHost(void* data, size_t len) {
    ssize_t res = send(serverHandle, data, len, sendFlags);
}

void TCPClient::textResponse(const std::string text, uint64_t tgt_uuid) {
    TCPMetaInfo info;
    info.package_type = TcpPackageType::TEXT;
    info.payload_size = text.size();
    info.src_uuid = getUuid();
    info.tgt_uuid = tgt_uuid;
    size_t msgSz = sizeof(TCPMetaInfo) + text.size();
    void* buf = malloc(msgSz);
    memcpy(buf, &info, sizeof(TCPMetaInfo));
    memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), text.c_str(), text.size());
    notifyHost(buf, msgSz);
    free(buf);
}

uint64_t TCPClient::getUuid() const {
    return _session_uuid;
}

void TCPClient::addCallback(TcpPackageType type, ReceiveCallback cb) {
    callbacks[type] = cb;
}

void TCPClient::listenLoop() {
    const size_t MAX_DATA_SIZE = 1024 * 1024 * 2;
    void* msg_buffer = malloc(MAX_DATA_SIZE);
    memset(msg_buffer, 0, MAX_DATA_SIZE);

    unprocessed_bytes = 0;
    ssize_t received_bytes = 0;

    std::cout << "[TCPClient] Initialized client with Buffer MaxSize: " << MAX_DATA_SIZE << " Byte." << std::endl;

    std::vector<WorkItem> items;

    while (!globalAbort) {
        void* receive_buffer = reinterpret_cast<char*>(msg_buffer) + unprocessed_bytes;
        const size_t available_buffer_size = MAX_DATA_SIZE - 1 - unprocessed_bytes;

        if ((received_bytes = recv(serverHandle, receive_buffer, available_buffer_size, 0)) <= 0) {
            if (!globalAbort) {
                std::cout << "[Error] Received " << received_bytes << " Bytes. Channel closed? Terminating Receiver." << std::endl;
                const std::lock_guard<std::mutex> lk(channel_mutex);
                globalAbort = true;
                channel_cv.notify_all();
                break;
            }
        } else {
            if (_verbose) {
                std::cout << "Received " << received_bytes << " Bytes." << std::endl;
            }
            tuddbs::Utility::extractItemsModifyBuffer(msg_buffer, unprocessed_bytes + received_bytes, msg_buffer, unprocessed_bytes, &callbacks);
        }
    }

    free(msg_buffer);
}

void TCPClient::waitUntilChannelClosed() {
    std::unique_lock<std::mutex> lk(channel_mutex);
    bool& globalAbortWrapper = globalAbort;
    channel_cv.wait(lk, [&globalAbortWrapper] { return globalAbortWrapper; });
}

}  // namespace tuddbs