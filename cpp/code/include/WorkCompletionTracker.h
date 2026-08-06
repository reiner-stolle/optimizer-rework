#pragma once

#include <mutex>
#include <condition_variable>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <tuple>

struct TupleHash {
    std::size_t operator()(const std::tuple<int, int> &k) const {
        // Simple XOR combine hash
        return std::hash<int>{}(std::get<0>(k)) ^ (std::hash<int>{}(std::get<1>(k)) << 1);
    }
};

class WorkCompletionTracker {
public:
    WorkCompletionTracker() : stopped_(false) {
    }

    void markCompleted(int planId, int nodeId) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_ids_.insert(std::make_tuple(planId, nodeId));
        }
        cv_.notify_all();
    }

    bool waitNextCompletions(const std::vector<std::tuple<int, int> > &ids) {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [&] {
            if (stopped_) return true;

            return std::all_of(ids.begin(), ids.end(), [&](const auto &id) {
                return completed_ids_.count(id) > 0;
            });
        });

        if (stopped_) return false;

        for (const auto &id: ids) {
            completed_ids_.erase(id);
        }

        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_set<std::tuple<int, int>, TupleHash> completed_ids_;
    bool stopped_;
};
