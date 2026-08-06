#include "SQLToLogicalPlanTranslator/JoinTreeBuilder.hpp"
#include "SQLToLogicalPlanTranslator/TranslatorHelpers.hpp"
#include "SQLToLogicalPlanTranslator/SchemaResolver.hpp"

#include <algorithm>
#include <stdexcept>

// --- Scan collection ---

void collectScans(const TableRef *from, std::vector<std::shared_ptr<LogicalPlanNode>> &scans)
{
    if (!from)
        return;
    switch (from->type)
    {
    case kTableName:
    {
        auto s = std::make_shared<LogicalPlanNode>();
        s->node_type = LogicalNodeType::SCAN;
        if (from->alias && from->alias->name)
        {
            s->base_table = from->alias->name;
        }
        else
        {
            s->base_table = from->name ? from->name : "";
        }
        scans.push_back(s);
        break;
    }
    case kTableCrossProduct:
    {
        if (from->list)
            for (auto t : *from->list)
                collectScans(t, scans);
        break;
    }
    case kTableJoin:
    {
        if (from->join)
        {
            collectScans(from->join->left, scans);
            collectScans(from->join->right, scans);
        }
        break;
    }
    default:
        break;
    }
}

std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>>
buildScanMap(const TableRef *from)
{
    std::vector<std::shared_ptr<LogicalPlanNode>> scans;
    collectScans(from, scans);

    if (scans.empty())
        throw std::runtime_error("No tables in FROM clause");

    std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> scanByTable;
    scanByTable.reserve(scans.size());

    for (const auto &s : scans)
    {
        if (!s || s->node_type != LogicalNodeType::SCAN || s->base_table.empty())
            continue;
        scanByTable[s->base_table] = s;
    }

    return scanByTable;
}

// --- Join order from JSON ---

std::string findMatchingScanTable(const std::string &jsonTable,
                                  const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable)
{
    std::vector<std::string> candidates;
    auto base = normalizeTableToken(jsonTable);
    candidates.push_back(base);
    if (base.rfind("dim_", 0) == 0)
        candidates.push_back(base.substr(4));

    for (const auto &cand : candidates)
    {
        for (const auto &[key, _] : scanByTable)
        {
            auto scanName = normalizeTableToken(key);
            if (namesMatch(cand, scanName))
                return key;
        }
    }
    return "";
}

std::vector<std::string> resolveJoinOrderToScans(
    const std::vector<std::string> &jsonOrder,
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable)
{
    std::vector<std::string> resolved;
    std::unordered_set<std::string> seen;

    for (const auto &name : jsonOrder)
    {
        auto mapped = findMatchingScanTable(name, scanByTable);
        if (mapped.empty())
            continue;

        auto key = toLower(mapped);
        if (seen.insert(key).second)
            resolved.push_back(mapped);
    }
    return resolved;
}

std::vector<std::string> extractJoinOrderFromJson(const nlohmann::json &plan)
{
    struct ScanRec
    {
        std::string table;
        int depth = 0;
        int seq = 0;
    };

    std::vector<ScanRec> scans;
    int seqCounter = 0;

    auto getBaseTableName = [](const nlohmann::json &node) -> std::string
    {
        if (!node.is_object())
            return "";
        if (!node.contains("plan_params"))
            return "";
        const auto &params = node.at("plan_params");
        if (!params.is_object() || !params.contains("base_table"))
            return "";
        const auto &bt = params.at("base_table");
        if (!bt.is_object() || !bt.contains("full_name") || !bt.at("full_name").is_string())
            return "";
        return bt.at("full_name").get<std::string>();
    };

    std::function<void(const nlohmann::json &, int)> dfs = [&](const nlohmann::json &node, int depth)
    {
        if (!node.is_object())
            return;

        std::string tableName = getBaseTableName(node);
        if (!tableName.empty())
        {
            scans.push_back(ScanRec{tableName, depth, seqCounter++});
            return;
        }

        if (node.contains("children") && node.at("children").is_array())
        {
            for (const auto &child : node.at("children"))
                dfs(child, depth + 1);
        }
    };

    dfs(plan, 0);

    std::stable_sort(scans.begin(), scans.end(),
                     [](const ScanRec &a, const ScanRec &b)
                     {
                         if (a.depth != b.depth)
                             return a.depth > b.depth;
                         return a.seq < b.seq;
                     });

    std::vector<std::string> order;
    std::unordered_set<std::string> seen;
    order.reserve(scans.size());

    for (const auto &rec : scans)
    {
        auto key = toLower(rec.table);
        if (seen.insert(key).second)
            order.push_back(rec.table);
    }

    return order;
}

// --- Join tree construction ---

// Builds a left-deep join tree automatically from the set of join predicates using DSU.
// The DSU tracks which tables are already connected; each predicate either creates a new
// JOIN node (if the two tables are in different groups) or becomes an extraJoinFilter
// (if they are already connected — e.g. a second condition between the same two tables).
//
// Example: FROM orders o, users u, products p
//          WHERE o.user_id = u.id AND o.product_id = p.id AND o.x = u.y
//
// Init:    dsu={o,u,p},  tree={o:SCAN(o), u:SCAN(u), p:SCAN(p)}
//
// Pred 1: o.user_id = u.id
//   r1="o", r2="u" → different groups → build JOIN(o,u)
//   unite("o","u"), rep="o"
//   tree = { "o": JOIN(o,u), "p": SCAN(p) }
//
// Pred 2: o.product_id = p.id
//   r1="o", r2="p" → different groups → build JOIN(JOIN(o,u), p)
//   unite("o","p"), rep="o"
//   tree = { "o": JOIN(JOIN(o,u), p) }
//
// Pred 3: o.x = u.y
//   r1="o", r2="u" → same group already! → becomes extraJoinFilter
//   extraJoinFilters = [ FILTER(o.x = u.y) ]
//   tree unchanged = { "o": JOIN(JOIN(o,u), p) }
//
// Result:
//   JOIN(o.product_id=p.id)
//   ├── JOIN(o.user_id=u.id)
//   │   ├── SCAN(o)
//   │   └── SCAN(u)
//   └── SCAN(p)
//
//   extraJoinFilters: [ FILTER(o.x = u.y) ]  ← pushed down later
std::shared_ptr<LogicalPlanNode> buildJoinTree(
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &joinPreds,
    std::vector<std::shared_ptr<LogicalPlanNode>> &extraJoinFilters)
{
    if (scanByTable.empty())
        throw std::runtime_error("No tables in FROM");

    DSU dsu;
    std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> tree;

    // Register every table as its own group and its own subtree.
    for (const auto &[t, scan] : scanByTable)
    {
        dsu.add(t);
        tree[t] = scan;
    }

    for (const auto &jp : joinPreds)
    {
        if (!jp || jp->base_columns.size() != 2 || !jp->expression.comp_type)
            continue;

        std::string t1 = resolveTableName(jp->base_columns[0]);
        std::string t2 = resolveTableName(jp->base_columns[1]);
        if (t1.empty() || t2.empty())
            throw std::runtime_error("Join predicate with unknown table");

        dsu.add(t1);
        dsu.add(t2);
        // r1/r2 are the group representatives — i.e. the keys in `tree`
        auto r1 = dsu.find(t1), r2 = dsu.find(t2);

        // Same group → tables already connected, can't build another JOIN.
        // Treat as a FILTER instead that gets pushed down later.
        if (r1 == r2)
        {
            auto f = std::make_shared<LogicalPlanNode>(*jp);
            f->node_type = LogicalNodeType::FILTER;
            extraJoinFilters.push_back(f);
            continue;
        }

        // Different groups → build a new JOIN connecting the two subtrees.
        auto j = std::make_shared<LogicalPlanNode>();
        j->node_type = LogicalNodeType::JOIN;
        j->expression.comp_type = jp->expression.comp_type;
        j->base_columns = jp->base_columns;
        j->children = {tree[r1], tree[r2]};

        // Merge the two groups; the new representative becomes the key for the JOIN node.
        dsu.unite(r1, r2);
        auto rep = dsu.find(r1);
        tree.erase(r1);
        tree.erase(r2);
        tree[rep] = j;
    }

    // Find the representative of the final merged group (the root of the join tree).
    std::string rep;
    for (const auto &[t, _] : scanByTable)
    {
        auto r = dsu.find(t);
        if (rep.empty())
            rep = r;
    }

    auto it = tree.find(rep);
    if (it == tree.end() || !it->second)
        throw std::runtime_error("Internal error: join tree missing");
    return it->second;
}

std::shared_ptr<LogicalPlanNode> buildJoinTreeFromOrder(
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &joinPreds,
    std::vector<std::shared_ptr<LogicalPlanNode>> &extraJoinFilters,
    const std::vector<std::string> &joinOrder,
    bool &usedJsonOrder)
{
    usedJsonOrder = false;
    if (joinOrder.size() < 2)
        return nullptr;

    auto normalizeKey = [](const std::string &name)
    { return toLower(name); };

    auto it = scanByTable.find(joinOrder[0]);
    if (it == scanByTable.end())
        return nullptr;

    auto current = it->second;
    std::unordered_set<std::string> joined{normalizeKey(joinOrder[0])};
    std::vector<bool> used(joinPreds.size(), false);

    for (size_t idx = 1; idx < joinOrder.size(); ++idx)
    {
        auto itNext = scanByTable.find(joinOrder[idx]);
        if (itNext == scanByTable.end())
            return nullptr;

        std::string nextNorm = normalizeKey(joinOrder[idx]);
        size_t predicateIdx = joinPreds.size();

        for (size_t i = 0; i < joinPreds.size(); ++i)
        {
            if (used[i])
                continue;

            const auto &jp = joinPreds[i];
            if (!jp || jp->base_columns.size() != 2)
                continue;

            std::string t1 = resolveTableName(jp->base_columns[0]);
            std::string t2 = resolveTableName(jp->base_columns[1]);
            if (t1.empty() || t2.empty())
                continue;

            std::string t1Norm = normalizeKey(t1);
            std::string t2Norm = normalizeKey(t2);
            bool t1In = joined.count(t1Norm) > 0;
            bool t2In = joined.count(t2Norm) > 0;

            if ((t1In && namesMatch(t2Norm, nextNorm)) ||
                (t2In && namesMatch(t1Norm, nextNorm)))
            {
                predicateIdx = i;
                break;
            }
        }

        if (predicateIdx == joinPreds.size())
            return nullptr;

        auto jp = joinPreds[predicateIdx];
        auto joinNode = std::make_shared<LogicalPlanNode>();
        joinNode->node_type = LogicalNodeType::JOIN;
        joinNode->expression.comp_type = jp->expression.comp_type;
        joinNode->base_columns = jp->base_columns;
        joinNode->children = {current, itNext->second};

        used[predicateIdx] = true;
        joined.insert(nextNorm);
        current = joinNode;
    }

    std::vector<std::shared_ptr<LogicalPlanNode>> localExtras;
    for (size_t i = 0; i < joinPreds.size(); ++i)
    {
        if (!used[i])
        {
            auto f = std::make_shared<LogicalPlanNode>(*joinPreds[i]);
            f->node_type = LogicalNodeType::FILTER;
            localExtras.push_back(f);
        }
    }

    extraJoinFilters = std::move(localExtras);
    usedJsonOrder = true;
    return current;
}
