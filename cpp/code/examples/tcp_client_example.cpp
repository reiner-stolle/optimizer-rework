#include <iostream>
#include <string>

#include "ArgParser.hpp"
#include "TCPClient.hpp"
#include "UnitDefinition.pb.h"
#include "Utility.hpp"
#include "WorkItem.pb.h"
#include "WorkResponse.pb.h"

using namespace tuddbs;

int main(int argc, char* argv[]) {
    ArgParser parser(argc, argv);

    const std::string& ip = parser.takeParseArg<std::string>("-ip", "[Error] No IP given. Pass a server IP with [-ip]", "127.0.0.1", false);
    const size_t port = parser.takeParseArg<size_t>("-port", "[Error] No Port given. Pass a port with [-port].", 23232, false);

    TCPClient client(ip, port);
    client.start();

    auto work_cb = [&client](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[Work Callback] Invoked." << std::endl;
        WorkItem item;
        item.ParseFromArray(data, len);

        switch (item.opData_case()) {
            case WorkItem::OpDataCase::kJoinData: {
                std::cout << "Item contains a Join Operator." << std::endl;
            } break;
            case WorkItem::OpDataCase::kFilterData: {
                std::cout << "Item contains a Filter Operator." << std::endl;
            } break;
            default: {
                std::cout << "An unkown entity is packed in this WorkItem." << std::endl;
            }
        }

        WorkResponse response;
        response.set_planid(item.planid());
        response.set_itemid(item.itemid());
        response.set_info("Your intermediates are ready!");

        TCPMetaInfo info;
        info.package_type = TcpPackageType::TASK_FINISHED;
        info.payload_size = response.ByteSizeLong();
        void* out_mem = malloc(sizeof(TCPMetaInfo) + info.payload_size);

        const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, response, info);

        client.notifyHost(out_mem, message_size);
        free(out_mem);
    };

    auto updateUnitInfo_cb = [&client](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::cout << "[UpdateUnitInfo Callback] Invoked." << std::endl;
        UnitDefinition unit;
        unit.set_unit_type(static_cast<uint32_t>(UnitType::COMPUTE_UNIT));

        TCPMetaInfo info;
        info.package_type = TcpPackageType::UPDATE_UNIT_TYPE;
        info.payload_size = unit.ByteSizeLong();
        void* out_mem = malloc(sizeof(TCPMetaInfo) + info.payload_size);

        const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, unit, info);

        client.notifyHost(out_mem, message_size);
        free(out_mem);
    };

    auto text_cb = [&client](TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::string str(reinterpret_cast<char*>(data), len);
        std::cout << "Text Received: " << str << std::endl;
    };

    client.addCallback(TcpPackageType::WORK, work_cb);
    client.addCallback(TcpPackageType::UPDATE_UNIT_TYPE, updateUnitInfo_cb);
    client.addCallback(TcpPackageType::TEXT, text_cb);

    std::string content;
    std::string op;

    bool abort = false;

    while (!abort) {
        op = "-1";
        std::cout << "Type \"exit\" to terminate." << std::endl;
        std::getline(std::cin, op, '\n');
        if (op == "-1") {
            break;
        }

        std::cout << "Chosen:" << op << std::endl;
        std::transform(op.begin(), op.end(), op.begin(), [](unsigned char c) { return std::tolower(c); });

        if (op == "exit") {
            client.closeConnection();
            abort = true;
        }
    }
}