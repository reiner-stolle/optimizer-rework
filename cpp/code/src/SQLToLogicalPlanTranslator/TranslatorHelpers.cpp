#include "SQLToLogicalPlanTranslator/TranslatorHelpers.hpp"
#include "SQLToLogicalPlanTranslator/SchemaResolver.hpp"

#include <cctype>
#include <stdexcept>

// --- String helpers ---

bool iequals(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    while (*a && *b)
    {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

std::string toLower(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// --- SQL expression helpers ---

const Expr *unwrapSimpleFunction(const Expr *e)
{
    if (!e || e->type != kExprFunctionRef || !e->name)
        return e;
    if (!e->exprList || e->exprList->size() != 1)
        return e;
    if (iequals(e->name, "LOWER") || iequals(e->name, "UPPER") ||
        iequals(e->name, "LCASE") || iequals(e->name, "UCASE"))
    {
        return (*e->exprList)[0];
    }
    return e;
}

std::optional<PlanStringOp> stringOpFromFunctionName(const char *name)
{
    if (!name)
        return std::nullopt;
    if (iequals(name, "LOWER") || iequals(name, "LCASE"))
        return PlanStringOp::LOWER;
    if (iequals(name, "UPPER") || iequals(name, "UCASE"))
        return PlanStringOp::UPPER;
    return std::nullopt;
}

std::optional<PlanStringOp> unwrapStringOp(const Expr *e, const Expr **unwrapped)
{
    if (!e)
    {
        if (unwrapped)
            *unwrapped = e;
        return std::nullopt;
    }
    if (e->type == kExprFunctionRef && e->exprList && e->exprList->size() == 1)
    {
        if (auto op = stringOpFromFunctionName(e->name))
        {
            if (unwrapped)
                *unwrapped = (*e->exprList)[0];
            return op;
        }
    }
    if (unwrapped)
        *unwrapped = e;
    return std::nullopt;
}

std::optional<PlanCompType> mapCompOp(OperatorType t)
{
    switch (t)
    {
    case kOpEquals:
        return PlanCompType::EQ;
    case kOpNotEquals:
        return PlanCompType::NE;
    case kOpGreater:
        return PlanCompType::GT;
    case kOpLess:
        return PlanCompType::LT;
    case kOpGreaterEq:
        return PlanCompType::GE;
    case kOpLessEq:
        return PlanCompType::LE;
    case kOpLike:
    case kOpILike:
        return PlanCompType::LIKE;
    default:
        return std::nullopt;
    }
}

// --- Tree utilities ---

std::unordered_set<std::string> toTableSet(const std::vector<std::string> &v)
{
    std::unordered_set<std::string> s;
    s.reserve(v.size());
    for (const auto &x : v)
        s.insert(x);
    return s;
}

bool isSubset(const std::unordered_set<std::string> &need,
              const std::unordered_set<std::string> &have)
{
    for (const auto &t : need)
        if (have.find(t) == have.end())
            return false;
    return true;
}

std::unordered_set<std::string> getPredicateTables(const std::shared_ptr<LogicalPlanNode> &p)
{
    std::unordered_set<std::string> ts;
    if (!p)
        return ts;

    if (p->node_type == LogicalNodeType::SETOPERATION)
    {
        for (const auto &ch : p->children)
        {
            auto cts = getPredicateTables(ch);
            ts.insert(cts.begin(), cts.end());
        }
        return ts;
    }

    for (const auto &c : p->base_columns)
    {
        std::string t = resolveTableName(c);
        if (!t.empty())
            ts.insert(std::move(t));
    }
    return ts;
}

std::vector<std::string> getProvidedTables(const std::shared_ptr<LogicalPlanNode> &node)
{
    std::vector<std::string> tables;
    if (!node)
        return tables;

    if (node->node_type == LogicalNodeType::SCAN)
    {
        if (!node->base_table.empty())
            tables.push_back(node->base_table);
    }
    else
    {
        for (const auto &child : node->children)
        {
            auto childTables = getProvidedTables(child);
            tables.insert(tables.end(), childTables.begin(), childTables.end());
        }
    }
    return tables;
}

std::shared_ptr<LogicalPlanNode> clonePlanSubtree(const std::shared_ptr<LogicalPlanNode> &node)
{
    if (!node)
        return nullptr;
    auto copy = std::make_shared<LogicalPlanNode>(*node);
    copy->children.clear();
    for (const auto &child : node->children)
        copy->children.push_back(clonePlanSubtree(child));
    return copy;
}

std::shared_ptr<LogicalPlanNode> clonePredicateTemplate(const std::shared_ptr<LogicalPlanNode> &pred)
{
    if (!pred)
        return nullptr;

    auto c = std::make_shared<LogicalPlanNode>(*pred);

    if (pred->node_type != LogicalNodeType::SETOPERATION)
    {
        c->children.clear();
        return c;
    }

    std::vector<std::shared_ptr<LogicalPlanNode>> kids;
    kids.reserve(pred->children.size());
    for (const auto &ch : pred->children)
    {
        if (!ch)
            continue;
        auto leaf = std::make_shared<LogicalPlanNode>(*ch);
        leaf->children.clear();
        kids.push_back(leaf);
    }
    c->children = std::move(kids);
    return c;
}

void collectUnionLeaves(const std::shared_ptr<LogicalPlanNode> &n,
                        std::vector<std::shared_ptr<LogicalPlanNode>> &out)
{
    if (!n)
        return;

    if (n->node_type == LogicalNodeType::SETOPERATION &&
        n->expression.logical_rel_op.has_value() &&
        *n->expression.logical_rel_op == PlanLogicalRelOp::UNION)
    {
        for (auto &c : n->children)
            collectUnionLeaves(c, out);
    }
    else
    {
        out.push_back(n);
    }
}

// --- Table name matching helpers ---

bool namesMatch(const std::string &a, const std::string &b)
{
    if (a == b)
        return true;
    if (a + "s" == b)
        return true;
    if (a == b + "s")
        return true;
    return false;
}

std::string normalizeTableToken(const std::string &name)
{
    std::string lower = toLower(name);
    auto dotPos = lower.rfind('.');
    if (dotPos != std::string::npos && dotPos + 1 < lower.size())
        lower = lower.substr(dotPos + 1);
    return lower;
}

// --- Predicate target helpers ---

std::string getFilterTableName(const std::shared_ptr<LogicalPlanNode> &filterNode)
{
    if (filterNode->base_columns.empty())
        return "";
    return resolveTableName(filterNode->base_columns[0]);
}

bool allLeavesSameTable(const std::vector<std::shared_ptr<LogicalPlanNode>> &leaves,
                        std::string &tableOut)
{
    tableOut.clear();
    for (auto &l : leaves)
    {
        if (!l || l->node_type != LogicalNodeType::FILTER)
            return false;
        auto t = getFilterTableName(l);
        if (t.empty())
            return false;

        if (tableOut.empty())
            tableOut = t;
        else if (!iequals(tableOut.c_str(), t.c_str()))
            return false;
    }
    return !tableOut.empty();
}

std::string getPredicateTargetTable(const std::shared_ptr<LogicalPlanNode> &node)
{
    if (!node)
        return "";
    if (node->node_type == LogicalNodeType::FILTER)
        return getFilterTableName(node);
    if (node->node_type == LogicalNodeType::SETOPERATION)
    {
        std::string table;
        for (const auto &child : node->children)
        {
            std::string childTable = getPredicateTargetTable(child);
            if (childTable.empty())
                return "";
            if (table.empty())
                table = childTable;
            else if (!iequals(table.c_str(), childTable.c_str()))
                return "";
        }
        return table;
    }
    return "";
}

// --- Alias collection ---

void collectAliases(const TableRef *from,
                    std::unordered_map<std::string, std::string> &aliasToBase)
{
    if (!from)
        return;

    switch (from->type)
    {
    case kTableName:
    {
        std::string base = from->name ? from->name : "";
        if (!base.empty())
        {
            aliasToBase[base] = base;
            if (from->alias && from->alias->name)
            {
                aliasToBase[from->alias->name] = base;
            }
        }
        break;
    }
    case kTableCrossProduct:
        if (from->list)
        {
            for (auto t : *from->list)
                collectAliases(t, aliasToBase);
        }
        break;
    case kTableJoin:
        if (from->join)
        {
            collectAliases(from->join->left, aliasToBase);
            collectAliases(from->join->right, aliasToBase);
        }
        break;
    default:
        break;
    }
}

// --- Selection pushdown ---

// Inserts predicate directly above target.
static void attachPredicate(std::shared_ptr<LogicalPlanNode> &target,
                            std::shared_ptr<LogicalPlanNode> predicate)
{
    auto predCopy = clonePredicateTemplate(predicate);

    // Simple: prepend filter above target
    // FILTER(status='shipped') → SCAN(orders)
    if (predCopy->node_type != LogicalNodeType::SETOPERATION)
    {
        predCopy->children.push_back(target);
        target = predCopy;
        return;
    }

    // Complex: e.g. FILTER(a=1) AND FILTER(b=2) — each gets its own copy of target
    // FILTER(a=1) → SCAN(orders)
    // FILTER(b=2) → SCAN(orders)  ← separate copies
    std::vector<std::shared_ptr<LogicalPlanNode>> newChildren;
    newChildren.reserve(predCopy->children.size());
    for (const auto &child : predCopy->children)
    {
        if (!child)
            continue;
        auto childPred = std::make_shared<LogicalPlanNode>(*child);
        childPred->children.clear();
        childPred->children.push_back(clonePlanSubtree(target));
        newChildren.push_back(childPred);
    }
    predCopy->children = std::move(newChildren);
    target = predCopy;
}

void pushDownSelectionPredicate(std::shared_ptr<LogicalPlanNode> &node,
                                std::shared_ptr<LogicalPlanNode> predicate)
{
    if (!node || !predicate)
        return;

    // UNION: copy predicate into every branch
    // e.g. FILTER(status='shipped') over UNION(orders_2023, orders_2024)
    //   → FILTER → orders_2023  and  FILTER → orders_2024
    if (node->node_type == LogicalNodeType::SETOPERATION &&
        node->expression.logical_rel_op.has_value() &&
        *node->expression.logical_rel_op == PlanLogicalRelOp::UNION &&
        predicate->node_type != LogicalNodeType::SETOPERATION)
    {
        for (auto &ch : node->children)
        {
            auto predCopy = clonePredicateTemplate(predicate);
            pushDownSelectionPredicate(ch, predCopy);
        }
        return;
    }

    auto neededTables = getPredicateTables(predicate);

    // No table binding — attach here, can't push further
    if (neededTables.empty())
    {
        attachPredicate(node, predicate);
        return;
    }

    // SCAN: table matches — this is the target, attach filter directly above
    // e.g. neededTables={"orders"}, node=SCAN(orders) → match → attach
    if (node->node_type == LogicalNodeType::SCAN)
    {
        if (!node->base_table.empty() && neededTables.size() == 1 &&
            iequals(node->base_table.c_str(), neededTables.begin()->c_str()))
        {
            attachPredicate(node, predicate);
            return;
        }
    }

    // JOIN: push to whichever side owns all needed tables
    // e.g. FILTER(orders.status='shipped') over JOIN(orders, users)
    //   → neededTables={"orders"} ⊆ left → recurse left
    //   → neededTables spans both sides → attach above JOIN
    if (node->node_type == LogicalNodeType::JOIN && node->children.size() == 2)
    {
        auto leftSet = toTableSet(getProvidedTables(node->children[0]));
        auto rightSet = toTableSet(getProvidedTables(node->children[1]));

        if (isSubset(neededTables, leftSet))
        {
            pushDownSelectionPredicate(node->children[0], predicate);
            return;
        }
        if (isSubset(neededTables, rightSet))
        {
            pushDownSelectionPredicate(node->children[1], predicate);
            return;
        }

        attachPredicate(node, predicate);
        return;
    }

    // FILTER: not at target yet — pass through to child
    if (node->node_type == LogicalNodeType::FILTER && !node->children.empty())
    {
        pushDownSelectionPredicate(node->children[0], predicate);
        return;
    }

    // Fallback: can't push further — attach here
    attachPredicate(node, predicate);
}

// --- Column conversion ---

Column toColumnFromExpr(const Expr *e,
                        const std::unordered_map<std::string, std::string> &aliasToBase)
{
    e = unwrapSimpleFunction(e);
    if (e && e->type == kExprColumnRef)
    {
        std::string table = e->table ? e->table : "";
        std::string name = e->name ? e->name : "";
        std::optional<std::string> columnAlias = e->alias ? std::optional<std::string>(e->alias) : std::nullopt;
        std::optional<std::string> baseTable = std::nullopt;

        if (!table.empty())
        {
            auto it = aliasToBase.find(table);
            if (it != aliasToBase.end() && it->second != table)
            {
                baseTable = it->second;
            }
        }

        if (table.empty())
        {
            Column tmp("", name, PlanColumnType::STRING);
            table = resolveTableName(tmp);
        }

        std::string tableForType = baseTable.value_or(table);
        PlanColumnType columnType = resolveColumnType(tableForType, name);
        return Column(table, name, columnType, columnAlias, baseTable);
    }
    return Column("", "", PlanColumnType::STRING);
}

// --- Literal helpers ---

std::string exprToLiteral(const Expr *e)
{
    if (!e)
        return "";
    if (e->isType(kExprLiteralInt))
        return std::to_string(e->ival);
    if (e->isType(kExprLiteralFloat))
        return std::to_string(e->fval);
    if (e->isType(kExprLiteralString))
        return e->name ? e->name : "";
    return e->name ? e->name : "";
}

bool appendLiteralValue(std::vector<std::string> &values, const Expr *e)
{
    if (!e)
        return false;
    if (e->isType(kExprLiteralInt))
        values.push_back(std::to_string(e->ival));
    else if (e->isType(kExprLiteralFloat))
        values.push_back(std::to_string(e->fval));
    else if (e->isType(kExprLiteralString))
        values.push_back(e->name ? e->name : "");
    else if (e->name)
        values.push_back(e->name);
    else
        return false;
    return true;
}
