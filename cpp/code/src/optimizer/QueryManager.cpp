#include <optimizer/QueryManager.hpp>
#include <chrono>

#include "Utility.hpp"
#include "WorkRequest.pb.h"
#include "optimizer/Logger.hpp"

size_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

QueryManager::QueryManager(tuddbs::TCPClient* c, WorkCompletionTracker* t, const size_t num_threads, bool optimize_chains, bool optimize_join_outputs, bool demo)
    : client(c), work_completion_tracker(t), optimize_chains(optimize_chains), optimize_join_outputs(optimize_join_outputs), demo(demo) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back(&QueryManager::workerRoutine, this);
    }
}

QueryManager::~QueryManager() {
    stop_flag = true;
    if (work_completion_tracker) {
        work_completion_tracker->stop();
    }
    cv.notify_all();
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void QueryManager::addQuery(const std::string& sql) {
    auto ctx = std::make_shared<QueryContext>();
    ctx->id = query_counter++;
    ctx->sql = sql;
    ctx->start_ts = now();

    LOG_INFO("Query added to queue. ID: " + std::to_string(ctx->id));

    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.push_back(ctx);
    }
    cv.notify_one();
}

void QueryManager::workerRoutine() {
    LOG_DEBUG("Worker thread started");
    while (true) {
        std::shared_ptr<QueryContext> ctx;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [this] { return stop_flag || !queue.empty(); });

            if (stop_flag && queue.empty()) {
                LOG_DEBUG("Worker thread stopping");
                return;
            }

            ctx = queue.front();
            queue.pop_front();
        }

        {
            std::lock_guard<std::mutex> lock(running_mutex);
            running.push_back(ctx);
        }

        LOG_INFO("Worker processing Query ID: " + std::to_string(ctx->id));
        processQuery(ctx);

        {
            std::lock_guard<std::mutex> r_lock(running_mutex);
            running.erase(std::remove(running.begin(), running.end(), ctx), running.end());
        }
        
        ctx->end_ts = now();
        
        {
            std::lock_guard<std::mutex> lock(finished_mutex);
            finished.push_back(ctx);
        }
    }
}

void QueryManager::processQuery(std::shared_ptr<QueryContext> ctx) {
    try {
        size_t idx = ctx->sql.find_first_not_of(" \t\n\r");
        if (idx == std::string::npos) return;

        bool parallel = false;
        std::string query_name;

        while (idx != std::string::npos) {
            if (ctx->sql.compare(idx, 8, "PARALLEL") == 0) {
                parallel = true;
                idx += 8;
            } else if (ctx->sql.compare(idx, 4, "NAME") == 0) {
                idx += 4;
                idx = ctx->sql.find_first_not_of(" \t\n\r", idx);
                if (idx == std::string::npos) break;

                size_t end = ctx->sql.find_first_of(" \t\n\r", idx);
                if (end == std::string::npos) {
                    query_name = ctx->sql.substr(idx);
                    idx = std::string::npos;
                } else {
                    query_name = ctx->sql.substr(idx, end - idx);
                    idx = end;
                }
            } else {
                break;
            }
            if (idx != std::string::npos) idx = ctx->sql.find_first_not_of(" \t\n\r", idx);
        }

        std::string final_sql = (idx == std::string::npos) ? "" : ctx->sql.substr(idx);

        int current_vis_nr = vis_counter++;

        auto logical_plans = translateSQLToLogicalPlan(final_sql, std::nullopt, demo, current_vis_nr);
        if (logical_plans.empty()) {
            LOG_WARN("Translator returned empty logical plan for Query ID: " + std::to_string(ctx->id));
            return;
        }

        ctx->logical_plan = logical_plans[0];
        size_t phys_start = now();
        if (!query_name.empty()) {
            PhysicalOptimizer optimizer(ctx->logical_plan, optimize_chains, optimize_join_outputs, query_name, demo, current_vis_nr);
            ctx->physical_plan = optimizer.optimize();
            if (!ctx->physical_plan) {
                throw std::runtime_error("Physical optimization failed");
            }
        } else {
            PhysicalOptimizer optimizer(ctx->logical_plan, optimize_chains, optimize_join_outputs, "", demo, current_vis_nr);
            ctx->physical_plan = optimizer.optimize();
            if (!ctx->physical_plan) {
                throw std::runtime_error("Physical optimization failed");
            }
        }
        size_t phys_end = now();
        LOG_INFO("Physical optimizer completed in " + std::to_string(phys_end - phys_start) + " ms");

        if (parallel) {
            LOG_INFO("Executing Parallel Plan for Query ID: " + std::to_string(ctx->id));
            sendQueryPlanParallel(ctx->physical_plan);
        } else {
            LOG_INFO("Executing Sequential Plan for Query ID: " + std::to_string(ctx->id));
            sendQueryPlan(ctx->physical_plan);
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception in processQuery (ID " + std::to_string(ctx->id) + "): " + e.what());
        LOG_ERROR("Faulty Query: " + ctx->sql);
    }
}

void QueryManager::sendQueryPlan(const std::shared_ptr<PhysicalPlanNode>& physical_plan) {

    WorkRequest workRequest;
    workRequest.mutable_queryplan()->CopyFrom(createQueryPlan(physical_plan));
    tuddbs::TCPMetaInfo info;
    info.package_type = tuddbs::TCPPackageType::Work;
    info.payload_size = workRequest.ByteSizeLong();
    info.src_uuid = client->getUuid();
    void *out_mem = malloc(sizeof(tuddbs::TCPMetaInfo) + info.payload_size);
    const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, workRequest, info);
    client->notifyHost(out_mem, message_size);
    free(out_mem);
}

void QueryManager::sendQueryPlanParallel(const std::shared_ptr<PhysicalPlanNode>& physical_plan) {
    LOG_DEBUG("Generating parallel work items...");
    auto workItems = crateWorkItemsParallel(physical_plan);
    int planId = workItems.at(0).at(0).planid();
    for (const auto &items: workItems) {
        std::vector<std::tuple<int, int>> ids;

        //{
          //  std::lock_guard<std::mutex> lock(client_mutex);
            for (const auto &item: items) {

                // std::cout << item.DebugString() << std::endl;

                ids.emplace_back(planId, item.itemid());
                WorkRequest workRequest;
                workRequest.mutable_workitem()->CopyFrom(item);

                tuddbs::TCPMetaInfo info;
                info.package_type = tuddbs::TCPPackageType::Work;
                info.payload_size = workRequest.ByteSizeLong();
                info.src_uuid = client->getUuid();
                
                void *out_mem = malloc(sizeof(tuddbs::TCPMetaInfo) + info.payload_size);
                const size_t message_size = tuddbs::Utility::serializeItemToMemory(out_mem, workRequest, info);
                
                client->notifyHost(out_mem, message_size);
                free(out_mem);
            }
        //}
        LOG_DEBUG("Waiting for batch completion...");
        if (!work_completion_tracker->waitNextCompletions(ids)) {
            LOG_WARN("Completion tracker shutdown signal received");
            return;
        }
    }
}