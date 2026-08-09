#include "TCPServer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <Utility.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "UnitDefinition.pb.h"
#include "WorkResponse.pb.h"

namespace tuddbs {

/**
 * @brief Flags for the send() system call.
 *
 */
int sendFlags = MSG_NOSIGNAL | MSG_DONTWAIT;

/**
 * @brief Construct a new TCPServer::TCPServer object
 *
 * @param port
 */
TCPServer::TCPServer(int port) : _port(port) {
    auto clientInfoReceiveCallback = [this](ClientInfo* client) -> void {
        const size_t MAX_DATA_SIZE = 1024 * 1024 * 64;
        void* msg_buffer = malloc(MAX_DATA_SIZE);
        // If the memory allocation fails, we should not continue. --> can happen when there is not enough memory available.
        if (!msg_buffer) {
            std::cerr << "[ClientInfoCallback] Memory allocation failed!" << std::endl;
            return;
        }

        ssize_t received_bytes = 0;

        std::cout << "[ClientInfoCallback] Initialized client with Buffer MaxSize: " << MAX_DATA_SIZE << " Byte." << std::endl;

        while (!client->abort) {
            const ssize_t available_space = MAX_DATA_SIZE - client->unprocessed_bytes;
            if (available_space <= 0) {
                std::cerr << "[ClientInfoCallback] Buffer overflow risk detected!" << std::endl;
                client->abort = true;
                break;
            }

            received_bytes = recv(client->handle, reinterpret_cast<char*>(msg_buffer) + client->unprocessed_bytes, available_space, 0);
            if (received_bytes == 0) {
                std::cout << "[ClientInfoCallback] Client disconnected." << std::endl;
                client->abort = true;
                break;
            } else if (received_bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue; // Temporary failure
                }
                std::cerr << "[ClientInfoCallback] recv() failed: " << strerror(errno) << std::endl;
                break;
            }

            tuddbs::Utility::extractItemsModifyBuffer(msg_buffer, received_bytes + client->unprocessed_bytes, msg_buffer, client->unprocessed_bytes, &callbacks, true);
        }

        free(msg_buffer);
    };

    TCPServer::ConnectCallback server_connect_callback = [this, clientInfoReceiveCallback](ClientHandle handle) -> void {
        TCPMetaInfo info;
        const size_t bufferSize = 1024 * 64 * 4;
        void* buf = malloc(bufferSize);

        info.package_type = TcpPackageType::UPDATE_UNIT_TYPE;
        info.payload_size = 0;

        // Set TCP_NODELAY and TCP_QUICKACK to avoid stalling of the TCP stack with Nagle's algorithm, as we sent plenty of small messages.
        {
            int yes = 1;
            int result = setsockopt(serverHandle, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&yes), sizeof(int));  // 1 - on, 0 - off
            if (result != 0) {
                std::cout << "[Warning] Could not set TCP_NODELAY" << std::endl;
            }
            result = setsockopt(serverHandle, IPPROTO_TCP, TCP_QUICKACK, reinterpret_cast<char*>(&yes), sizeof(int));  // 1 - on, 0 - off
            if (result != 0) {
                std::cout << "[Warning] Could not set TCP_QUICKACK" << std::endl;
            }
        }

        // Request the UnitDefinition protobuf struct from the just-connected client.
        int res = 0;
        if ((res = send(handle, &info, info.bytesize(), sendFlags)) <= 0) {
            std::cout << "[TCPServer] ConnectCallback: Could not send UpdateUnitType to client with handle " << handle << std::endl;
        }

        // If the client takes longer than 2s to responed, drop it.
        setTimeoutToHandle(handle, 2, 0);
        ssize_t received_bytes = recv(handle, reinterpret_cast<char*>(buf), bufferSize - 1, 0);

        if (received_bytes > 0) {
            // Check for correct message layout. TCP_START_DELIM should always appear first.
            if (*reinterpret_cast<uint32_t*>(buf) != TCP_START_DELIM) {
                std::cout << "[UpdateUnitType] Error. I did not find a message delimiter at index 0. Discarding client." << std::endl;
                freeHandle(handle);
            } else {
                // Message seems to be well-formed. Now parse it. Expected structure: [ TCP_START_DELIM | TCPMetaInfo | payload ]
                TCPMetaInfo* update_info = reinterpret_cast<TCPMetaInfo*>(buf);
                if (static_cast<size_t>(received_bytes) < update_info->bytesize()) {
                    std::cout << "[UpdateUnitType] Error. Please implement continuous receive for segmented unit info packages." << std::endl;
                    exit(-1);
                }
                // Only accept the UpdateUnitType response at this time. Drop any client which does not obey the protocol.
                if (update_info->package_type != TcpPackageType::UPDATE_UNIT_TYPE) {
                    std::cout << "[UpdateUnitType] Received something else than a UnitUpdate info. Discarding client." << std::endl;
                    freeHandle(handle);
                }
                // Let's parse the payload and see if we received a correct UnitDefinition protobuf message.
                try {
                    UnitDefinition unit;
                    unit.ParseFromArray(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), update_info->payload_size);
                    std::cout << "[UpdateUnitType] Unit Info:" << unit.DebugString() << std::endl;
                    ClientInfo* cl_info = new ClientInfo();
                    cl_info->handle = handle;
                    cl_info->prettyName = unit.prettyname();
                    // enum class values are encoded as their respective integer value on the wire.
                    const UnitType parsed_type = static_cast<UnitType>(unit.unit_type());
                    cl_info->type = parsed_type;
                    bool uuid_collision_resolved = false;
                    setTimeoutToHandle(handle, 1, 0);
                    // Probe if another client already uses the same UUID. It should actually never happen with random 64bit integers, but here we are for good measure.
                    do {
                        cl_mutex.lock();
                        const bool collision = clientUuidMap.contains(update_info->src_uuid);
                        cl_mutex.unlock();
                        if (collision) {
                            std::cout << "[TCPServer] Found a colliding UUID. Requesting new UUID from client. Was: " << update_info->src_uuid << std::endl;
                            // Collision detected, trigger UUID regeneration.
                            info.package_type = TcpPackageType::UUID_COLLISION;
                            info.payload_size = 0;
                            if ((res = send(handle, &info, info.bytesize(), sendFlags)) <= 0) {
                                std::cout << "[TCPServer] ConnectCallback: Could not send UpdateUnitType to client with handle " << handle << std::endl;
                                freeHandle(handle);
                                delete cl_info;
                                return;
                            }
                            received_bytes = recv(handle, reinterpret_cast<char*>(buf), bufferSize - 1, 0);
                            // If receiving failed or the message is ill-formed, abort the setup process.
                            if (received_bytes <= 0 || (*reinterpret_cast<uint32_t*>(buf) != TCP_START_DELIM)) {
                                std::cout << "[UpdateUnitType] Error during UUID collision handling. Terminating client connection." << std::endl;
                                freeHandle(handle);
                                delete cl_info;
                                return;
                            }
                            // Receiving another answer than an updated UUID cancels the setup process.
                            if (update_info->package_type != TcpPackageType::UUID_COLLISION) {
                                std::cout << "[UpdateUnitType] Error during UUID collision handling. Received wrong response: " << TCPServer::packageTypeToString(update_info->package_type) << " -- Dropping client." << std::endl;
                                freeHandle(handle);
                                delete cl_info;
                                return;
                            }
                        } else {
                            // Collision is resolved, update client info struct and add it to the server
                            uuid_collision_resolved = true;
                            cl_info->uuid = update_info->src_uuid;
                            std::lock_guard<std::recursive_mutex> _lk(cl_mutex);
                            clientMap[parsed_type].push_back(cl_info);
                            clientUuidMap[update_info->src_uuid] = cl_info;
                        }
                    } while (!uuid_collision_resolved);
                    // We are now ready to patiently wait for any data from this connection and spawn a receiver thread for this client.
                    setTimeoutToHandle(handle, 0, 0);
                    cl_info->receiver = new std::thread(clientInfoReceiveCallback, cl_info);
                    std::cout << "[UpdateUnitType] Added a new " << unitTypeToString(parsed_type) << " with uuid=<" << cl_info->uuid << ">" << std::endl;
                } catch (const std::exception& e) {
                    std::cout << e.what() << std::endl;
                }
            }
        } else {
            std::cout << "[ConnectCallback] Error. Did not receive a response. Discarding client." << std::endl;
            freeHandle(handle);
        }

        free(buf);
    };
    setConnectCallback(server_connect_callback);
}

/**
 * @brief Destroy the TCPServer::TCPServer object
 *
 */
TCPServer::~TCPServer() {
    closeConnection();
}

/**
 * @brief Set the callback function, which is invoked when a new TCP connection is established.
 *
 * @param cc The connect callback function.
 */
void TCPServer::setConnectCallback(ConnectCallback cc) {
    this->connectCallback = cc;
}

/**
 * @brief Check if any clients are connected to the server.
 *
 * @return true At least one client is connected, its unit type is irrelevant.
 * @return false No client is connected.
 */
bool TCPServer::hasClients() const {
    size_t clientCount = 0;
    for (auto it = clientMap.begin(); it != clientMap.end(); ++it) {
        clientCount += it->second.size();
    }
    return clientCount > 0;
}

/**
 * @brief Fetch a client info struct based on a given UUID.
 *
 * @param uuid The 64bit identifier which is provided during the connection setup process.
 * @return ClientInfo* Information container which represents a connected client.
 */
ClientInfo* TCPServer::getClientByUuid(uint64_t uuid) const {
    if (clientUuidMap.contains(uuid)) {
        return clientUuidMap.at(uuid);
    }
    return nullptr;
}

/**
 * @brief Fetch all pairs of clients pretty name and their respective UUID.
 *
 * @param type Determines the unit type for which the pairs are returned.
 * @return std::vector<std::pair<std::string, uint64_t>> Vector containing name and uuid pairs for all clients of the requested type.
 */
std::vector<std::pair<std::string, uint64_t>> TCPServer::getUuidForUnitType(UnitType type) {
    if (!hasClients()) {
        return {};
    }
    clear_aborted();
    if (type == UnitType::UNDEFINED_UNIT_TYPE) {
        std::vector<std::pair<std::string, uint64_t>> uuids;
        for (auto it = clientMap.begin(); it != clientMap.end(); ++it) {
            auto cl_info_vec = it->second;
            auto cl_it = cl_info_vec.cbegin();
            while (cl_it != cl_info_vec.cend()) {
                uuids.push_back({(*cl_it)->prettyName, (*cl_it)->uuid});
                ++cl_it;
            }
        }
        return uuids;
    } else if (clientMap.contains(type)) {
        auto cl_info_vec = clientMap.at(type);
        auto cl_it = cl_info_vec.cbegin();
        std::vector<std::pair<std::string, uint64_t>> uuids;
        uuids.reserve(cl_info_vec.size());
        while (cl_it != cl_info_vec.cend()) {
            uuids.push_back({(*cl_it)->prettyName, (*cl_it)->uuid});
            ++cl_it;
        }
        return uuids;
    }
    return {};
}

/**
 * @brief Send data to all connected clients.
 *
 * @param data Binary encoded message.
 * @param len data size in bytes.
 */
void TCPServer::sendToAll(const char* data, uint32_t len) {
    if (!hasClients()) {
        return;
    }
    std::cout << "Sending text to all clients." << std::endl;

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        // The map iterator holds a pair of UnitType and client info vectors.
        auto& cl_info_vec = it->second;
        auto cl_it = cl_info_vec.begin();
        while (cl_it != cl_info_vec.end()) {
            ClientHandle sck = (*cl_it)->handle;
            int res = send(sck, data, len, sendFlags);
            if (res < 0) {
                std::cout << "Res is < 0, erasing client." << std::endl;
                removeClient(it->first, *cl_it);
                continue;
            } else {
                std::cout << "Sent " << res << " chars to remote." << std::endl;
            }
            ++cl_it;
        }
        ++it;
    }
}

/**
 * @brief Send data to all connected clients of a specific type.
 *
 * @param type The unit type which qualifies a client to receive this message.
 * @param data Binary encoded message.
 * @param len data size in bytes.
 */
void TCPServer::sendToAllOfType(UnitType type, const char* data, uint32_t len) {
    if (!hasClients()) {
        return;
    }
    std::cout << "Sending data to all clients." << std::endl;

    if (clientMap.contains(type)) {
        auto cl_info_vec = clientMap[type];
        auto cl_it = cl_info_vec.begin();
        while (cl_it != cl_info_vec.end()) {
            ClientHandle sck = (*cl_it)->handle;
            int res = send(sck, data, len, sendFlags);
            if (res < 0) {
                std::cout << "Res is < 0, erasing client." << std::endl;
                removeClient(type, *cl_it);
                continue;
            } else {
                std::cout << "Sent " << res << " chars to remote." << std::endl;
            }
            ++cl_it;
        }
    }
}

/**
 * @brief Print debug information about currently connected clients.
 *
 */
void TCPServer::clientDebugInfo() const {
    std::cout << "Unique UnitTypes: " << clientMap.size() << std::endl
              << "Clients per type:" << std::endl;
    for (const auto& it : clientMap) {
        std::cout << unitTypeToString(it.first) << ": " << it.second.size() << std::endl;
    }
}

/**
 * @brief Send data to a random client of a specific type.
 *
 * @param type The unit type which qualifies a client to receive this message.
 * @param data Binary encoded message.
 * @param len data size in bytes.
 */
void TCPServer::sendToAnyOfType(UnitType type, const char* data, uint32_t len) {
    if (!hasClients()) {
        std::cout << "[TCPServer] No Clients are currently connected." << std::endl;
        return;
    }
    std::cout << "Sending WorkItem to a random Client." << std::endl;
    clientDebugInfo();

    if (clientMap.contains(type)) {
        bool success = false;
        while (!success) {
            auto& cl_info_vec = clientMap[type];
            if (cl_info_vec.empty()) {
                std::cout << "[TCPServer] No Clients for type " << static_cast<size_t>(type) << " are currently connected." << std::endl;
                break;
            }
            const size_t idx = Utility::generateRandomNumber(0, cl_info_vec.size() - 1);
            auto client = cl_info_vec[idx];
            std::cout << "[TCPServer] Client count: " << cl_info_vec.size() << " - I use: " << idx << " (Abort: " << std::boolalpha << client->abort << ")" << std::endl;
            if (sendTo(client, data, len)) {
                success = true;
            } else {
                std::cout << "[TCPServer] Original client did not answer. Retrying with new client." << std::endl;
            }
        }
    }
}

/**
 * @brief Reroute data to a random client of a specific type. Exclude the original recipient.
 *
 * @param type The unit type which qualifies a client to receive this message.
 * @param original_uuid The original recipient, which cannot serve the request.
 * @param data Binary encoded message.
 * @param len data size in bytes.
 */
void TCPServer::rerouteToAnyOfType(UnitType type, const uint64_t original_uuid, const char* original_data, uint32_t original_len) {
    if (!hasClients()) {
        std::cout << "[TCPServer] No Clients are currently connected." << std::endl;
        return;
    }
    std::cout << "Rerouting WorkItem to a random Client, ignoring " << original_uuid << std::endl;
    clientDebugInfo();

    auto find_and_send = [this, &original_uuid](const auto& clients, const size_t start, const size_t end, const char* data, const uint32_t len) -> bool {
        for (size_t i = start; i != end; ++i) {
            auto potential_target = clients[i];
            if (potential_target->uuid != original_uuid) {
                if (sendTo(potential_target, data, len)) {
                    return true;
                } else {
                    std::cout << "[TCPServer] Rerouting client did not answer. Retrying with new client." << std::endl;
                }
            }
        }
        return false;
    };

    if (clientMap.contains(type)) {
        bool success = false;
        auto& cl_info_vec = clientMap[type];
        if (cl_info_vec.empty()) {
            std::cout << "[TCPServer] No Clients for type " << static_cast<size_t>(type) << " are currently connected." << std::endl;
            return;
        }
        const size_t idx = Utility::generateRandomNumber(0, cl_info_vec.size() - 1);
        success = find_and_send(cl_info_vec, idx, cl_info_vec.size(), original_data, original_len);
        if (!success) {
            success = find_and_send(cl_info_vec, 0, idx, original_data, original_len);
        }
    }
}

/**
 * @brief Send data to a specific client.
 *
 * @param cl_info The client info structure of the target client.
 * @param data Binary encoded message.
 * @param len data size in bytes.
 * @return true Sending the message succeeded.
 * @return false Sending the message failed, either the client has already disconnected or the sending process failed.
 */
bool TCPServer::sendTo(ClientInfo* cl_info, const char* data, uint32_t len) {
    if (cl_info->abort) {
        removeClient(cl_info);
        return false;
    }
    ssize_t res = send(cl_info->handle, data, len, sendFlags);
    if (res < 0) {
        std::cout << "[TCPServer-sendTo] Response is < 0, erasing client." << std::endl;
        removeClient(cl_info);
        return false;
    } else {
        std::cout << "Sent " << res << " chars to a " << unitTypeToString(cl_info->type) << " (\"" << cl_info->prettyName << "\")" << std::endl;
    }
    return true;
}

/**
 * @brief Start the loop for accepting connections.
 *
 */
void TCPServer::start() {
    t = std::thread(&TCPServer::acceptLoop, this);
}

/**
 * @brief Remove all disconnected clients.
 *
 */
void TCPServer::clear_aborted() {
    std::cout << "[TCPServer] Removing all aborted clients." << std::endl;
    size_t aborted_cnt = 0;
    for (auto it = clientMap.begin(); it != clientMap.end(); ++it) {
        auto& cl_vec = it->second;
        for (auto info_it = cl_vec.begin(); info_it != cl_vec.end();) {
            if ((*info_it)->abort) {
                removeClient(it->first, *info_it);
                ++aborted_cnt;
                continue;
            }
            ++info_it;
        }
    }
    std::cout << "[TCPServer] I cleaned " << aborted_cnt << " clients." << std::endl;
    clientDebugInfo();
}

/**
 * @brief Close and terminate the socket of a client.
 *
 * @param handle The file descriptor handle of the to-be-closed socket.
 */
void TCPServer::freeHandle(ClientHandle handle) {
    ::shutdown(handle, SHUT_RDWR);
    close(handle);
}

/**
 * @brief Create an informative string holding all currently connected clients with their pretty names and IPs.
 *
 * @return std::string A string representation of the server state.
 */
std::string TCPServer::monitorInfoToString() {
    clear_aborted();
    std::stringstream ss;
    for (auto it = clientMap.begin(); it != clientMap.end(); ++it) {
        ss << unitTypeToString(it->first) << " [" << std::endl;
        const auto& cl_vec = it->second;
        for (auto info_it = cl_vec.begin(); info_it != cl_vec.end(); ++info_it) {
            struct sockaddr_in peername;
            socklen_t addr_len = sizeof(sockaddr_in);
            getpeername((*info_it)->handle, reinterpret_cast<sockaddr*>(&peername), &addr_len); // TODO: Check whether this does something after I removed the unused return
            ss << "  " << (*info_it)->prettyName << " (" << inet_ntoa(peername.sin_addr) << ")" << std::endl;
        }
        ss << "]" << std::endl;
    }
    return ss.str();
}

/**
 * @brief Terminate the server.
 *
 */
void TCPServer::closeConnection() {
    if (!cleanupDone) {
        globalAbort = true;
        for (auto it = clientMap.begin(); it != clientMap.end(); ++it) {
            auto& cl_vec = it->second;
            for (auto info_it = cl_vec.begin(); info_it != cl_vec.end(); ++info_it) {
                (*info_it)->release();
                delete (*info_it);
            }
            cl_vec.clear();
        }
        freeHandle(serverHandle);
        std::cout << "[TCPServer-closeConnection] Waiting for join..." << std::endl;
        if (t.joinable()) {
            t.join();
        }
        std::cout << "Done!" << std::endl;
        cleanupDone = true;
    }
}

/**
 * @brief Register a callback function, which is called whenever a message with a specific type arrives.
 *
 * @param type The TCP message type, found in a TCPMetaInfo struct, which triggers a callback.
 * @param cb The callback function for a given TCPPackageType.
 */
void TCPServer::addCallback(TcpPackageType type, ReceiveCallback cb) {
    callbacks[type] = cb;
}

/**
 * @brief Convenience function to set a timeout on a socket handle. sec and used are added for the total value.
 *
 * @param handle The socket handle whose timeout value is to be updated.
 * @param sec Timeout component in seconds.
 * @param usec Timeout component in microseconds.
 */
void TCPServer::setTimeoutToHandle(ClientHandle handle, const size_t sec, const size_t usec) {
    struct timeval timeout;
    timeout.tv_sec = sec;
    timeout.tv_usec = usec;
    setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

/**
 * @brief Convenience fucntion to convert the UnitType enum class to a string.
 *
 * @param type The UnitType to covnert.
 * @return std::string String representation of the UnitType type.
 */
std::string TCPServer::unitTypeToString(UnitType type) {
    switch (type) {
        case UnitType::UNDEFINED_UNIT_TYPE:
            return "Undefined";
        case UnitType::QUERY_PLANER:
            return "QueryPlaner";
        case UnitType::COMPUTE_UNIT:
            return "ComputeUnit";
        case UnitType::MEMORY_UNIT:
            return "MemoryUnit";
        case UnitType::META_UNIT:
            return "MetaUnit";
        case UnitType::MONITOR_UNIT:
            return "MonitorUnit";
        case UnitType::DATABASE_UNIT:
            return "DatabaseUnit";
        case UnitType::OPTIMIZER_UNIT:
            return "OptimizerUnit";
    }
    return "Unknown unit type";
}

/**
 * @brief Convenience fucntion to convert the TCPPackageType enum class to a string.
 *
 * @param type The TCPPackageType to covnert.
 * @return std::string String representation of the TCPPackageType type.
 */
std::string TCPServer::packageTypeToString(TcpPackageType type) {
    switch (type) {
        case TcpPackageType::UNDEFINED_PACKAGE_TYPE:
            return "Undefined";
        case TcpPackageType::WORK:
            return "Work";
        case TcpPackageType::REROUTE_WORK:
            return "RerouteWork";
        case TcpPackageType::TASK_FINISHED:
            return "TaskFinished";
        case TcpPackageType::TEXT:
            return "Text";
        case TcpPackageType::UPDATE_UNIT_TYPE:
            return "UpdateUnitType";
        case TcpPackageType::QUERY_PLAN:
            return "QueryPlan";
        case TcpPackageType::MONITOR_REQUEST:
            return "MonitorRequest";
        case TcpPackageType::CONNECT_ACTION:
            return "ConnectAction";
        case TcpPackageType::CONNECT_ACTION_INFO:
            return "ConnectActionInfo";
        case TcpPackageType::CONFIGURATION_ACTION:
            return "ConfigurationAction";
        case TcpPackageType::UUID_FOR_UNIT_REQUEST:
            return "UuidForTypeRequest";
        case TcpPackageType::UUID_FOR_UNIT_RESPONSE:
            return "UuidForTypeResponse";
        case TcpPackageType::UUID_COLLISION:
            return "UuidCollision";
    }
    return "Unknown package type";
}

/**
 * @brief The event loop of the server. Accepts new connetions as long as no global abort is requested.
 *
 */
void TCPServer::acceptLoop() {
    serverHandle = socket(AF_INET, SOCK_STREAM, 0);
    if (serverHandle == 0) {
        throw std::runtime_error("[TCPServer] Error. Failed to create server socket.");
    }

    int opt = 1;
    setsockopt(serverHandle, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(_port);

    if (bind(serverHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error("[TCPServer] Error. Failed to bind to socket.");
    }
    if (listen(serverHandle, 3) < 0) {
        throw std::runtime_error("[TCPServer] Error. Failed to listen from socket.");
    }

    while (!globalAbort) {
        int addrlen = sizeof(address);
        ClientHandle sck = accept(serverHandle, reinterpret_cast<sockaddr*>(&address), reinterpret_cast<socklen_t*>(&addrlen));
        if (globalAbort) {
            break;
        }
        if (sck < 0) {
            throw std::runtime_error("failed to accept");
        }
        std::cout << "[TCPServer-Info] Received a connection from " << inet_ntoa(address.sin_addr) << " on port " << address.sin_port << std::endl;

        // inform the conenct callback
        if (connectCallback) {
            connectCallback(sck);
        }
    }
}

/**
 * @brief Remove a specific client from the server, e.g. if it disconnected or does not answer anymore.
 *
 * @param info The to-be-removed client info.
 */
void TCPServer::removeClient(ClientInfo* info) {
    std::cout << "[TCPServer] Removing Client by Info-Struct" << std::endl;
    std::lock_guard<std::recursive_mutex> _lk(cl_mutex);
    for (auto it = clientMap.begin(); it != clientMap.end(); ++it) {
        auto& cl_vec = it->second;
        for (auto info_it = cl_vec.begin(); info_it != cl_vec.end(); ++info_it) {
            if ((*info_it)->handle == info->handle) {
                removeClient(it->first, *info_it);
                return;
            }
        }
    }
}

/**
 * @brief Remove a specific client from the server, e.g. if it disconnected or does not answer anymore.
 *
 * @param type The unit type of the client.
 * @param info The to-be-removed client info.
 */
void TCPServer::removeClient(UnitType type, ClientInfo* info) {
    std::cout << "[TCPServer] Removing Client by Type and Info-Struct" << std::endl;
    std::lock_guard<std::recursive_mutex> _lk(cl_mutex);
    auto& cl_vec = clientMap[type];
    auto cl_it = std::find(cl_vec.begin(), cl_vec.end(), info);
    if (cl_it != cl_vec.end()) {
        std::cout << "[TCPServer] Removing uuid<" << info->uuid << "> from aliases." << std::endl;
        clientUuidMap.erase(info->uuid);
        info->release();
        delete info;
        cl_vec.erase(cl_it);
    }
}

/**
 * @brief Tear down an initialized client info structure.
 *
 */
void ClientInfo::release() {
    std::cout << "[ClientInfo] Releasing ClientInfo for handle " << handle << "..." << std::flush;
    abort = true;
    TCPServer::freeHandle(handle);
    if (receiver) {
        std::cout << "joining..." << std::flush;
        receiver->join();
        std::cout << "done." << std::endl;
        delete receiver;
        receiver = nullptr;
    }
    cleanupDone = true;
}

/**
 * @brief Assignment operator for client info structures
 *
 * @param other The client info structure of whom to inherit the information from.
 * @return ClientInfo& The assignee.
 */
ClientInfo& ClientInfo::operator=(const ClientInfo& other) {
    if (&other == this) {
        return *this;
    }

    this->handle = other.handle;
    this->receiver = other.receiver;
    this->unprocessed_bytes = other.unprocessed_bytes;
    this->type = other.type;
    this->uuid = other.uuid;
    this->prettyName = other.prettyName;
    this->abort = other.abort;
    return *this;
}

/**
 * @brief Destroy the Client Info:: Client Info object
 *
 */
ClientInfo::~ClientInfo() {
    if (!cleanupDone) {
        release();
    }
}

}  // namespace tuddbs