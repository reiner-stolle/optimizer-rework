#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include "ArgParser.hpp"
#include "NetworkRequests.pb.h"
#include "TCPServer.hpp"
#include "Utility.hpp"
#include "WorkItem.pb.h"
#include "WorkResponse.pb.h"

// Added UnitType
#include <UnitDefinition.pb.h>


// This is where the conflict is coming in
using namespace tuddbs;

void globalExit(TCPServer& server) {
    server.closeConnection();
    exit(0);
}

int main(int argc, char* argv[]) {
    ArgParser parser(argc, argv);

    const std::string& port_string = parser.takeParseArg<std::string>("-port", "[Info] No Port given. I am listening on port 23232 by defualt.", "23232", false);

    TCPServer server(atol(port_string.c_str()));

    auto text_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[Text Callback] Invoked." << std::endl;
        if (meta->tgt_uuid == 0) {
            std::string str(reinterpret_cast<char*>(data), len);
            std::cout << " -- Received Text: " << str << std::endl;
        } else {
            ClientInfo* target = server.getClientByUuid(meta->tgt_uuid);
            if (target) {
                TCPMetaInfo message_info;
                message_info.package_type = TcpPackageType::TEXT;
                message_info.payload_size = len;
                message_info.src_uuid = meta->src_uuid;
                message_info.tgt_uuid = meta->tgt_uuid;

                const size_t message_size = sizeof(TCPMetaInfo) + len;
                void* buf = malloc(message_size);
                memcpy(buf, &message_info, sizeof(TCPMetaInfo));
                memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);
                std::cout << "== Forwarding Message ==" << std::endl;
                std::cout << "--       Target: " << target->prettyName << std::endl;
                std::cout << "-- Pacakge type: " << TCPServer::packageTypeToString(message_info.package_type) << std::endl;
                std::cout << "-- message_size: " << message_size << std::endl;
                server.sendTo(target, reinterpret_cast<const char*>(buf), message_size);
                server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
            } else {
                std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Could not send to destination." << std::endl;
            }
        }
    };

    auto reroute_work_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[RerouteWork] I received a message to reroute a previously issued WorkRequest." << std::endl;
        
        const size_t message_size = sizeof(TCPMetaInfo) + len;
        void* buf = malloc(message_size);
        TCPMetaInfo message_info;
        message_info.package_type = TcpPackageType::REROUTE_WORK;
        message_info.payload_size = len;
        message_info.src_uuid = meta->tgt_uuid;
        message_info.tgt_uuid = 0;
        memcpy(buf, &message_info, sizeof(TCPMetaInfo));
        memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);

        server.rerouteToAnyOfType(UnitType::COMPUTE_UNIT, meta->src_uuid, static_cast<const char*>(buf), message_size);
        server.sendToAllOfType(UnitType::MONITOR_UNIT, static_cast<const char*>(buf), message_size);
    };

    auto work_forward_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        const size_t message_size = sizeof(TCPMetaInfo) + len;
        void* buf = malloc(message_size);

        TCPMetaInfo message_info;
        message_info.package_type = TcpPackageType::WORK;
        message_info.payload_size = len;
        message_info.src_uuid = meta->src_uuid;
        message_info.tgt_uuid = meta->tgt_uuid;

        memcpy(buf, &message_info, sizeof(TCPMetaInfo));
        memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);
        if (message_info.tgt_uuid == 0) {
            std::cout << "[Work] Forwarding to _random_ target." << std::endl;
            server.sendToAnyOfType(UnitType::COMPUTE_UNIT, reinterpret_cast<const char*>(buf), message_size);
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
        } else {
            std::cout << "[Work] Forwarding to _specific_ target." << std::endl;
            ClientInfo* target = server.getClientByUuid(meta->tgt_uuid);
            if (target) {
                server.sendTo(target, reinterpret_cast<const char*>(buf), message_size);
                server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
            }
        }
        free(buf);
    };

    auto connect_forward_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[ConnectAction] Received a ConnectionAction. Forwarding to a specific ComputeUnit." << std::endl;

        ClientInfo* target = server.getClientByUuid(meta->tgt_uuid);
        if (target) {
            const size_t message_size = sizeof(TCPMetaInfo) + len;
            void* buf = malloc(message_size);

            TCPMetaInfo message_info;
            message_info.package_type = TcpPackageType::CONNECT_ACTION;
            message_info.payload_size = len;
            message_info.src_uuid = meta->src_uuid;
            message_info.tgt_uuid = meta->tgt_uuid;

            memcpy(buf, &message_info, sizeof(TCPMetaInfo));
            memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);

            std::cout << "--       Target: " << target->prettyName << std::endl;
            std::cout << "-- Pacakge type: " << TCPServer::packageTypeToString(message_info.package_type) << std::endl;
            std::cout << "-- message_size: " << message_size << std::endl;
            server.sendTo(target, reinterpret_cast<const char*>(buf), message_size);
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
            free(buf);
        } else {
            std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Could not send to destination." << std::endl;
        }
    };

    // TODO: We need to be more adaptive to not repeat the same code everytime we just want to forward something
    auto config_forward_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[ConfigurationAction] Received a ConfigurationAction. Forwarding to a specific ComputeUnit." << std::endl;

        ClientInfo* target = server.getClientByUuid(meta->tgt_uuid);
        if (target) {
            const size_t message_size = sizeof(TCPMetaInfo) + len;
            void* buf = malloc(message_size);

            TCPMetaInfo message_info;
            message_info.package_type = TcpPackageType::CONFIGURATION_ACTION;
            message_info.payload_size = len;
            message_info.src_uuid = meta->src_uuid;
            message_info.tgt_uuid = meta->tgt_uuid;

            memcpy(buf, &message_info, sizeof(TCPMetaInfo));
            memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);

            std::cout << "--       Target: " << target->prettyName << std::endl;
            std::cout << "-- Pacakge type: " << TCPServer::packageTypeToString(message_info.package_type) << std::endl;
            std::cout << "-- message_size: " << message_size << std::endl;
            server.sendTo(target, reinterpret_cast<const char*>(buf), message_size);
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
            free(buf);
        } else {
            std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Could not send to destination." << std::endl;
        }
    };
    
    // TODO: We need to be more adaptive to not repeat the same code everytime we just want to forward something
    auto connect_info_forward_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[ConfigurationAction] Received a ConnectActionInfo. Forwarding to a specific target." << std::endl;

        ClientInfo* target = server.getClientByUuid(meta->tgt_uuid);
        if (target) {
            const size_t message_size = sizeof(TCPMetaInfo) + len;
            void* buf = malloc(message_size);

            TCPMetaInfo message_info;
            message_info.package_type = TcpPackageType::CONNECT_ACTION_INFO;
            message_info.payload_size = len;
            message_info.src_uuid = meta->src_uuid;
            message_info.tgt_uuid = meta->tgt_uuid;

            memcpy(buf, &message_info, sizeof(TCPMetaInfo));
            memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);

            std::cout << "--       Target: " << target->prettyName << std::endl;
            std::cout << "-- Pacakge type: " << TCPServer::packageTypeToString(message_info.package_type) << std::endl;
            std::cout << "-- message_size: " << message_size << std::endl;
            server.sendTo(target, reinterpret_cast<const char*>(buf), message_size);
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
            free(buf);
        } else {
            std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Could not send to destination." << std::endl;
        }
    };

    auto finished_forward_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[Work] Received TaskFinished. Forwarding to Planner with uuid<" << meta->tgt_uuid << ">" << std::endl;

        ClientInfo* planner = server.getClientByUuid(meta->tgt_uuid);
        if (planner) {
            const size_t message_size = sizeof(TCPMetaInfo) + len;
            void* buf = malloc(message_size);

            TCPMetaInfo message_info;
            message_info.package_type = TcpPackageType::TASK_FINISHED;
            message_info.payload_size = len;
            message_info.src_uuid = meta->src_uuid;
            message_info.tgt_uuid = meta->tgt_uuid;

            memcpy(buf, &message_info, sizeof(TCPMetaInfo));
            memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), data, len);
            std::cout << "-- Pacakge type: " << static_cast<uint64_t>(message_info.package_type) << std::endl;
            std::cout << "-- message_size: " << message_size << std::endl;
            server.sendTo(planner, reinterpret_cast<const char*>(buf), message_size);
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_size);
            free(buf);
            std::cout << "[TCPServer] Message was forwarded to UUID<" << meta->tgt_uuid << std::endl;
        } else {
            std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Could not respond to destination." << std::endl;
        }
    };

    auto monitor_cb = [&server](TCPMetaInfo* meta, [[maybe_unused]] void* data, [[maybe_unused]] size_t len) -> void {
        ClientInfo* monitor = server.getClientByUuid(meta->src_uuid);
        if (monitor) {
            const std::string& monitorInfo = server.monitorInfoToString();

            TCPMetaInfo message_info;
            message_info.package_type = TcpPackageType::TEXT;
            message_info.payload_size = monitorInfo.size();
            message_info.src_uuid = meta->src_uuid;
            message_info.tgt_uuid = meta->tgt_uuid;

            void* buf = malloc(message_info.bytesize());
            memcpy(buf, &message_info, sizeof(TCPMetaInfo));
            memcpy(reinterpret_cast<char*>(buf) + sizeof(TCPMetaInfo), monitorInfo.c_str(), monitorInfo.size());
            server.sendTo(monitor, reinterpret_cast<const char*>(buf), message_info.bytesize());
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(buf), message_info.bytesize());
            free(buf);
        } else {
            std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Could not respond to destination." << std::endl;
        }
    };

    auto uuid_per_client_cb = [&server](TCPMetaInfo* meta, void* data, size_t len) -> void {
        ddd::network::UuidForUnitRequest request;
        request.ParseFromArray(data, len);

        ddd::network::UuidForUnitResponse response;
        response.set_requestedunittype(request.requestedunittype());
        auto uuid_response = response.mutable_uuids();
        auto name_response = response.mutable_names();

        UnitType type = static_cast<UnitType>(request.requestedunittype());
        auto uuids_vec = server.getUuidForUnitType(type);
        std::cout << "Uuids for type [" << TCPServer::unitTypeToString(type) << "]" << std::endl;
        for (auto it = uuids_vec.begin(); it != uuids_vec.end(); ++it) {
            std::cout << it->first << " " << it->second << std::endl;
            name_response->Add(it->first.c_str());
            uuid_response->Add(it->second);
        }
        std::cout << std::endl
                  << "Response Item:" << std::endl;
        response.PrintDebugString();

        tuddbs::TCPMetaInfo info;
        info.package_type = TcpPackageType::UUID_FOR_UNIT_RESPONSE;
        info.src_uuid = meta->src_uuid;
        info.tgt_uuid = meta->tgt_uuid;
        void* out_mem = malloc(info.bytesize());
        tuddbs::Utility::serializeItemToMemory(out_mem, response, info);
        ClientInfo* requester = server.getClientByUuid(meta->src_uuid);
        if (requester) {
            server.sendTo(requester, reinterpret_cast<const char*>(out_mem), info.bytesize());
            server.sendToAllOfType(UnitType::MONITOR_UNIT, reinterpret_cast<const char*>(out_mem), info.bytesize());
        } else {
            std::cout << "[TCPServer] ERROR - UUID<" << meta->tgt_uuid << "> not found. Please set your UUID before requesting." << std::endl;
        }
        free(out_mem);
    };

    // server.addCallback(TcpPackageTypeTaskFinished, task_finish_cb);
    server.addCallback(TcpPackageType::TEXT, text_cb);
    server.addCallback(TcpPackageType::WORK, work_forward_cb);
    server.addCallback(TcpPackageType::REROUTE_WORK, reroute_work_cb);
    server.addCallback(TcpPackageType::TASK_FINISHED, finished_forward_cb);
    server.addCallback(TcpPackageType::MONITOR_REQUEST, monitor_cb);
    server.addCallback(TcpPackageType::CONNECT_ACTION, connect_forward_cb);
    server.addCallback(TcpPackageType::CONNECT_ACTION_INFO, connect_info_forward_cb);
    server.addCallback(TcpPackageType::UUID_FOR_UNIT_REQUEST, uuid_per_client_cb);
    server.addCallback(TcpPackageType::CONFIGURATION_ACTION, config_forward_cb);

    std::string content;
    std::string op;

    server.start();

    bool abort = false;
    while (!abort) {
        op = "-1";
        std::cout << "Type \"exit\" to terminate." << std::endl;
        // std::cin >> op;
        std::getline(std::cin, op, '\n');
        if (op == "-1") {
            globalExit(server);
        }

        std::cout << "Chosen:" << op << std::endl;
        std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c) { return std::tolower(c); });

        if (op == "exit") {
            globalExit(server);
        } else {
            std::size_t id;
            bool converted = false;
            std::cout << " ?> " << std::flush;
            try {
                id = stol(op);
                converted = true;
            } catch (...) {
                std::cout << "No number given." << std::endl;
                continue;
            }
            if (converted) {
                std::cout << "Number: " << id << std::endl;
                switch (id) {
                    case 1: {
                        std::string text = "Hi from the server.";
                        TCPMetaInfo info;
                        info.package_type = TcpPackageType::TEXT;
                        info.payload_size = text.size();
                        const size_t message_size = sizeof(TCPMetaInfo) + text.size();
                        void* buf = malloc(message_size);
                        memcpy(buf, &info, sizeof(TCPMetaInfo));
                        memcpy(static_cast<char*>(buf) + sizeof(TCPMetaInfo), text.c_str(), text.size());
                        server.sendToAll(static_cast<const char*>(buf), message_size);
                        free(buf);
                    } break;
                    case 2: {
                        const size_t itemCount = tuddbs::Utility::generateRandomNumber(1, 10);

                        std::vector<WorkItem> items;
                        size_t itemsByteSize = 0;
                        for (size_t i = 0; i < itemCount; ++i) {
                            WorkItem item = tuddbs::Utility::generateRandomWorkItem(true);
                            item.set_itemid(i);
                            itemsByteSize += item.ByteSizeLong();
                            items.push_back(item);
                        }

                        std::cout << "Serializing " << itemCount << " items..." << std::endl;

                        const size_t message_size = itemsByteSize + (sizeof(TCPMetaInfo) * items.size());
                        void* buf = malloc(message_size);

                        auto serialize = [](void* mem, WorkItem& item, TCPMetaInfo& info) -> size_t {
                            info.payload_size = item.ByteSizeLong();
                            info.package_type = TcpPackageType::WORK;
                            memcpy(mem, &info, sizeof(TCPMetaInfo));
                            item.SerializeToArray(static_cast<char*>(mem) + sizeof(TCPMetaInfo), item.ByteSizeLong());
                            return item.ByteSizeLong() + sizeof(TCPMetaInfo);
                        };

                        void* tmp_buf = buf;
                        TCPMetaInfo message_info;
                        for (auto& item : items) {
                            size_t offest = serialize(tmp_buf, item, message_info);
                            tmp_buf = static_cast<char*>(tmp_buf) + offest;
                        }

                        // Send item(s) to clients
                        std::cout << "Total message size: " << message_size << std::endl;
                        server.sendToAllOfType(UnitType::COMPUTE_UNIT, static_cast<const char*>(buf), message_size);

                        free(buf);
                    } break;
                    case 3: {
                        const size_t itemCount = 10;

                        std::vector<WorkItem> items;
                        size_t itemsByteSize = 0;
                        for (size_t i = 0; i < itemCount; ++i) {
                            auto item = tuddbs::Utility::generateRandomWorkItem(true);
                            item.set_itemid(i);
                            itemsByteSize += item.ByteSizeLong();
                            items.push_back(item);
                        }

                        std::cout << "Serializing " << itemCount << " items..." << std::endl;

                        const size_t message_size = itemsByteSize + (sizeof(TCPMetaInfo) * items.size());
                        void* buf = malloc(message_size);

                        void* tmp_buf = buf;
                        TCPMetaInfo message_info;
                        message_info.package_type = TcpPackageType::WORK;
                        for (auto& item : items) {
                            size_t offest = tuddbs::Utility::serializeItemToMemory<WorkItem>(tmp_buf, item, message_info);
                            tmp_buf = static_cast<char*>(tmp_buf) + offest;
                        }

                        // Send overflow/incomplete chunks
                        std::cout << "Total message size: " << message_size << std::endl;

                        const size_t end_first_package = message_size / 3;
                        std::cout << "Sending first message part: " << end_first_package << std::endl;
                        server.sendToAllOfType(UnitType::COMPUTE_UNIT, static_cast<const char*>(buf), end_first_package);

                        std::cout << "Waiting 2s..." << std::endl;
                        {
                            using namespace std::chrono_literals;
                            std::this_thread::sleep_for(2s);
                        }

                        size_t size_left = message_size - end_first_package;

                        const size_t end_second_package = size_left / 2;
                        std::cout << "Sending second message part: " << end_second_package << std::endl;
                        server.sendToAllOfType(UnitType::COMPUTE_UNIT, static_cast<const char*>(buf) + end_first_package, end_second_package);

                        std::cout << "Waiting 2s..." << std::endl;
                        {
                            using namespace std::chrono_literals;
                            std::this_thread::sleep_for(2s);
                        }

                        size_left -= end_second_package;
                        std::cout << "Sending last message part: " << size_left << std::endl;
                        server.sendToAllOfType(UnitType::COMPUTE_UNIT, static_cast<const char*>(buf) + end_first_package + end_second_package, size_left);

                        free(buf);
                    } break;
                    case 4: {
                        server.clear_aborted();
                    } break;
                }
            }
        }
    }
}