#include <iostream>
#include <string>
#include <thread>

#include "ArgParser.hpp"
#include "TCPClient.hpp"
#include "UnitDefinition.pb.h"
#include "Utility.hpp"
#include "WorkItem.pb.h"
#include "WorkRequest.pb.h"
#include "WorkResponse.pb.h"
#include "optimizer/QueryManager.hpp"
#include "optimizer/Logger.hpp"

namespace tuddbs {
    struct TCPMetaInfo;
}

using namespace tuddbs;

void work(std::function<void()> func) {
    std::thread workThread(func);
    workThread.detach();
}

std::vector<std::string> read_from_file(const std::string& path) {
    std::vector<std::string> queries;
    std::ifstream file(path);
    std::string query;
    while (std::getline(file, query, ';')) {
        if (query.find_first_not_of(" \t\n\r") != std::string::npos) {
            queries.push_back(query);
        }
    }
    return queries;
}


int main(int argc, char *argv[]) {
    ArgParser parser(argc, argv);

    const std::string &ip = parser.takeParseArg<std::string>("-ip", "[Error] No IP given. Pass a server IP with [-ip]", "127.0.0.1", false);
    const size_t port = parser.takeParseArg<size_t>("-port", "[Error] No Port given. Pass a port with [-port].", 23232, false);

    const std::string q = parser.takeParseArg<std::string>("-q", "", "", false);
    const std::string q_file_path = parser.takeParseArg<std::string>("-f", "", "", false);
    const size_t t = parser.takeParseArg<size_t>("-t", "[Warning] Number of threads not given. Only a single worker will be used. Specify workers with [-t]", 1, false);
    const std::string log_path = parser.takeParseArg<std::string>("-log", "", "optimizer.log", false);
    const bool optimize_chains = parser.takeParseArg<bool>("-optimize", "", true, false);
    const bool optimize_join_outputs = parser.takeParseArg<bool>("-optimize-join-outputs", "", true, false);

    const bool demo = parser.takeParseArg<bool>("-demo", "", false, false);
    
    Logger::instance().init(log_path);
    LOG_INFO("OptimizerUnit starting up on " + ip + ":" + std::to_string(port));

    TCPClient client(ip, port);
    WorkCompletionTracker completionTracker;

    QueryManager query_manager(&client, &completionTracker, t, optimize_chains, optimize_join_outputs, demo);

    auto chores = [&query_manager, &q, &q_file_path]() {
        if (!q.empty()) {
            LOG_INFO("Processing initial CLI query");
            query_manager.addQuery(q);
        }
        if (!q_file_path.empty()) {
            LOG_INFO("Reading queries from file: " + q_file_path);
            auto queries = read_from_file(q_file_path);
            for (std::string q : queries) {
                query_manager.addQuery(q);
            }
        }

        std::string line;
        std::cout << "tuddbs> " << std::flush;

        while (std::getline(std::cin, line)) {
            if (line == "exit" || line == "quit") {
                break;
            }

            if (!line.empty()) {
                LOG_INFO("Received interactive query: " + line);
                query_manager.addQuery(line);
            }

            std::cout << "tuddbs> " << std::flush;
        }
    };

    auto updateUnitInfo_cb = [&client, chores](TCPMetaInfo *meta, void *data, size_t len) -> void {
        LOG_INFO("UpdateUnitInfo Callback invoked");
        UnitDefinition unit;
        unit.set_unit_type(static_cast<uint32_t>(UnitType::OptimizerUnit));

        TCPMetaInfo info;
        info.package_type = TCPPackageType::UpdateUnitType;
        info.payload_size = unit.ByteSizeLong();
        info.src_uuid = client.getUuid();
        void *out_mem = malloc(sizeof(TCPMetaInfo) + info.payload_size);

        const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, unit, info);

        client.notifyHost(out_mem, message_size);
        free(out_mem);

        work(chores);
    };

    auto text_cb = [](TCPMetaInfo *meta, void *data, size_t len) -> void {
        std::string str(reinterpret_cast<char *>(data), len);
        LOG_INFO("Text Received from Server: " + str);
    };

    auto finished = [&completionTracker](TCPMetaInfo *meta, void *data, size_t len) -> void {
        WorkResponse workResponse;
        workResponse.ParseFromArray(data, len);
        LOG_INFO("WorkItem finished. ID: " + std::to_string(workResponse.itemid()));
        completionTracker.markCompleted(workResponse.planid(), workResponse.itemid());
    };

    client.addCallback(TCPPackageType::UpdateUnitType, updateUnitInfo_cb);
    client.addCallback(TCPPackageType::Text, text_cb);
    client.addCallback(TCPPackageType::TaskFinished, finished);

    client.start();
    client.waitUntilChannelClosed();

    LOG_WARN("Connection to server lost. Shutting down.");

    std::_Exit(0);
}
