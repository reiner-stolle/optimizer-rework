#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "LogicalPlanNode.hpp"

#include "SQLParser.h"
#include "util/sqlhelper.h"

using namespace hsql;

// --- DSU (Disjoint Set Union) ---
// Used to track which tables are already connected by JOINs.
// Each table starts as its own group; unite() merges two groups when a JOIN is built between them.
// find() returns the group representative, used as the key into the `tree` map in buildJoinTree().
// If two tables already share a group (find(t1) == find(t2)), their predicate becomes a FILTER instead.
// Reference: https://cp-algorithms.com/data_structures/disjoint_set_union.html

struct DSU
{
    std::unordered_map<std::string, std::string> p;

    void add(const std::string &x) { p.emplace(x, x); }

    std::string find(const std::string &x)
    {
        auto it = p.find(x);
        if (it == p.end())
            return "";
        if (it->second == x)
            return x;
        return it->second = find(it->second);
    }

    bool unite(const std::string &a, const std::string &b)
    {
        auto ra = find(a), rb = find(b);
        if (ra.empty() || rb.empty() || ra == rb)
            return false;
        p[rb] = ra;
        return true;
    }
};

// --- Scan collection ---

void collectScans(const TableRef *from, std::vector<std::shared_ptr<LogicalPlanNode>> &scans);

std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>>
buildScanMap(const TableRef *from);

// --- Join order from JSON ---

std::string findMatchingScanTable(const std::string &jsonTable,
                                  const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable);

std::vector<std::string> resolveJoinOrderToScans(
    const std::vector<std::string> &jsonOrder,
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable);

std::vector<std::string> extractJoinOrderFromJson(const nlohmann::json &plan);

// --- Join tree construction ---

std::shared_ptr<LogicalPlanNode> buildJoinTree(
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &joinPreds,
    std::vector<std::shared_ptr<LogicalPlanNode>> &extraJoinFilters);

std::shared_ptr<LogicalPlanNode> buildJoinTreeFromOrder(
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &joinPreds,
    std::vector<std::shared_ptr<LogicalPlanNode>> &extraJoinFilters,
    const std::vector<std::string> &joinOrder,
    bool &usedJsonOrder);
