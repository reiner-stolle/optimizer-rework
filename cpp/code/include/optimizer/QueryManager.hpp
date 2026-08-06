//
// Created by daniel on 1/10/26.
//

#ifndef OPTIMIZER_QUERYMANAGER_H
#define OPTIMIZER_QUERYMANAGER_H
#include <memory>
#include <stack>
#include <atomic>

#include "LogicalPlanNode.hpp"
#include "TCPClient.hpp"
#include "WorkCompletionTracker.h"
#include "PhysicalPlanNode.hpp"
#include <SQLToLogicalPlanTranslator.hpp>
#include "optimizer/PhysicalOptimizer.hpp"
#include "Translator.hpp"

struct QueryContext {
    size_t id;
    std::string sql;
    std::shared_ptr<LogicalPlanNode> logical_plan;
    std::shared_ptr<PhysicalPlanNode> physical_plan;
    size_t start_ts;
    size_t end_ts;
};

class QueryManager {
private:
    std::atomic<int> vis_counter{0};

    std::deque<std::shared_ptr<QueryContext>> queue;
    std::vector<std::shared_ptr<QueryContext>> running;
    std::vector<std::shared_ptr<QueryContext>> finished;

    std::mutex queue_mutex;
    std::mutex running_mutex;
    std::mutex finished_mutex;

    std::mutex client_mutex;
    tuddbs::TCPClient* client;
    WorkCompletionTracker* work_completion_tracker;

    std::vector<std::thread> workers;
    std::condition_variable cv;
    std::atomic<bool> stop_flag{false};
    std::atomic<size_t> query_counter{0};

    bool optimize_chains = true;
    bool optimize_join_outputs = true;
    bool demo = false;


    void workerRoutine();
    void processQuery(std::shared_ptr<QueryContext> ctx);
    void sendQueryPlanParallel(const std::shared_ptr<PhysicalPlanNode>& physical_plan);
    void sendQueryPlan(const std::shared_ptr<PhysicalPlanNode>& physical_plan);

public:
    QueryManager(tuddbs::TCPClient* c, WorkCompletionTracker* t, size_t num_threads, bool optimize_chains = true, bool optimize_join_outputs = true, bool demo = false);
    ~QueryManager();
    void addQuery(const std::string& sql);
};

#endif //OPTIMIZER_QUERYMANAGER_H