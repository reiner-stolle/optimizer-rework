#include <NetworkRequests.pb.h>
#include <UnitDefinition.pb.h>
#include <WorkItem.pb.h>
#include <WorkRequest.pb.h>
#include <WorkResponse.pb.h>

#include <ArgParser.hpp>
#include <TCPClient.hpp>
#include <TCPServer.hpp>
#include <Utility.hpp>
#include <bitset>
#include <chrono>
#include <functional>
#include <iostream>
#include <random>
#include <thread>
#include <unordered_map>


int main(int argc, char** argv) {
    ArgParser parser(argc, argv);

    const std::string& ip = parser.takeParseArg<std::string>("-ip", "[Error] No IP given. Pass a server IP with [-ip]", "127.0.0.1", false);
    const size_t port = parser.takeParseArg<size_t>("-port", "[Error] No Port given. Pass a port with [-port].", 23232, false);

    const std::string& prettyName = parser.takeParseArg<std::string>("-name", "[Info] No pretty name given. Set a name with [-name]. Default: [ComputeUnit].", "ComputeUnit", false);

    tuddbs::TCPClient client(ip, port);

    auto updateUnitInfo_cb = [&client, &prettyName](tuddbs::TCPMetaInfo* meta, void* data, size_t len) -> void {
        using namespace tuddbs;
        std::cout << "[UpdateUnitInfo Callback] Invoked." << std::endl;
        UnitDefinition unit;
        unit.set_unit_type(static_cast<uint32_t>(UnitType::ComputeUnit));
        unit.set_prettyname(prettyName);

        TCPMetaInfo info;
        info.package_type = TCPPackageType::UpdateUnitType;
        info.payload_size = unit.ByteSizeLong();
        info.src_uuid = client.getUuid();
        void* out_mem = malloc(sizeof(TCPMetaInfo) + info.payload_size);

        const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, unit, info);

        client.notifyHost(out_mem, message_size);
        free(out_mem);
    };

    auto text_cb = [&client](tuddbs::TCPMetaInfo* meta, void* data, size_t len) -> void {
        std::string str(reinterpret_cast<char*>(data), len);
        std::cout << "Text Received: " << str << std::endl;
    };

    auto work_cb = [&client](tuddbs::TCPMetaInfo* meta, void* data, size_t len) -> void {
        WorkRequest request;

        request.ParseFromArray(data, len);

        WorkResponse response;

        if (request.has_queryplan()) {
            std::cout << "[Work Callback] Received QueryPlan." << std::endl;
            response.set_planid(request.queryplan().planid());
            response.set_itemid(0);
            response.set_success(true);
            response.set_info("QueryPlan processed successfully.");
        } else if (request.has_workitem()) {
            std::cout << "[Work Callback] Received WorkItem." << std::endl;
            response.set_planid(request.workitem().planid());
            response.set_itemid(request.workitem().itemid());
            response.set_success(true);
            response.set_info("WorkItem processed successfully.");
        } else {
            std::cout << "[Work Callback] Received unknown WorkRequest type." << std::endl;
            return;
        }

        tuddbs::TCPMetaInfo info;
        info.package_type = tuddbs::TCPPackageType::TaskFinished;
        info.payload_size = response.ByteSizeLong();
        info.src_uuid = client.getUuid();
        info.tgt_uuid = meta->src_uuid;
        void* out_mem = malloc(sizeof(tuddbs::TCPMetaInfo) + info.payload_size);
        const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, response, info);
        client.notifyHost(out_mem, info.bytesize());
        free(out_mem);
    };

    client.addCallback(tuddbs::TCPPackageType::UpdateUnitType, updateUnitInfo_cb);
    client.addCallback(tuddbs::TCPPackageType::Work, work_cb);
    client.addCallback(tuddbs::TCPPackageType::Text, text_cb);

    client.start();
    client.waitUntilChannelClosed();

    return 0;
}