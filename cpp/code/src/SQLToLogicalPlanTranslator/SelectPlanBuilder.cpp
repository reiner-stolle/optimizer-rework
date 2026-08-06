#include "SQLToLogicalPlanTranslator/SelectPlanBuilder.hpp"
#include "SQLToLogicalPlanTranslator/TranslatorHelpers.hpp"
#include "SQLToLogicalPlanTranslator/PredicateBuilder.hpp"
#include "SQLToLogicalPlanTranslator/JoinTreeBuilder.hpp"

#include <stdexcept>

// --- Aggregation detection ---

std::optional<AggPattern> matchAgg(
    const Expr *e,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    if (!e || e->type != kExprFunctionRef || !e->name)
        return std::nullopt;

    AggPattern ap;

    if (iequals(e->name, "COUNT"))
        ap.func = PlanAggFunc::COUNT;
    else if (iequals(e->name, "SUM"))
        ap.func = PlanAggFunc::SUM;
    else if (iequals(e->name, "MIN"))
        ap.func = PlanAggFunc::MIN;
    else if (iequals(e->name, "MAX"))
        ap.func = PlanAggFunc::MAX;
    else if (iequals(e->name, "AVG"))
        ap.func = PlanAggFunc::AVG;
    else
        return std::nullopt;

    if (e->alias)
        ap.alias = e->alias;

    if (!e->exprList || e->exprList->empty())
    {
        ap.is_star = true;
        return ap;
    }

    const Expr *arg = (*e->exprList)[0];
    if (!arg)
        return std::nullopt;

    if (ap.func == PlanAggFunc::COUNT)
    {
        if (arg->type == kExprStar)
        {
            ap.is_star = true;
            return ap;
        }
        if (arg->type == kExprColumnRef && arg->name && std::string(arg->name) == "*")
        {
            ap.is_star = true;
            return ap;
        }
    }

    if (ap.func == PlanAggFunc::SUM &&
        arg->type == kExprOperator && arg->expr && arg->expr2 &&
        arg->expr->type == kExprColumnRef && arg->expr2->type == kExprColumnRef)
    {
        if (arg->opType == kOpAsterisk)
        {
            ap.is_sum_mul = true;
        }
        else if (arg->opType == kOpMinus)
        {
            ap.is_sum_sub = true;
        }
        else
        {
            return std::nullopt;
        }

        ap.leftCol = toColumnFromExpr(arg->expr, aliasToBase);
        ap.rightCol = toColumnFromExpr(arg->expr2, aliasToBase);
        return ap;
    }

    if (arg->type == kExprColumnRef)
    {
        ap.is_col = true;
        ap.col = toColumnFromExpr(arg, aliasToBase);
        return ap;
    }

    return std::nullopt;
}

// --- Aggregate alias generation ---

const std::unordered_map<PlanAggFunc, std::string> kAggFuncToName = {
    {PlanAggFunc::SUM, "sum"},
    {PlanAggFunc::COUNT, "count"},
    {PlanAggFunc::AVG, "avg"},
    {PlanAggFunc::MIN, "min"},
    {PlanAggFunc::MAX, "max"}};

const std::unordered_map<std::string, PlanAggFunc> kNameToAggFunc = {
    {"sum", PlanAggFunc::SUM},
    {"count", PlanAggFunc::COUNT},
    {"avg", PlanAggFunc::AVG},
    {"min", PlanAggFunc::MIN},
    {"max", PlanAggFunc::MAX}};

std::string generateAggAlias(TranslationContext &ctx, PlanAggFunc func)
{
    std::string baseName = "agg";
    auto it = kAggFuncToName.find(func);
    if (it != kAggFuncToName.end())
    {
        baseName = it->second;
    }

    int counter = ++ctx.aggAliasCounters[func];
    return baseName + std::to_string(counter);
}

std::string generateAggAlias(TranslationContext &ctx, const std::string &funcName)
{
    std::string lower = funcName;
    for (auto &c : lower)
        c = std::tolower(c);

    auto it = kNameToAggFunc.find(lower);
    if (it != kNameToAggFunc.end())
    {
        return generateAggAlias(ctx, it->second);
    }
    return "agg";
}

// --- Plan construction helpers ---

void initializeSelectContext(const SelectStatement *sel, TranslationContext &ctx)
{
    ctx.aliasToTable.clear();
    ctx.aggAliasCounters.clear();
    collectAliases(sel->fromTable, ctx.aliasToTable);
}

PredicateGroups extractPredicates(
    const Expr *whereClause,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    std::vector<const Expr *> preds;
    splitConjuncts(whereClause, preds);

    PredicateGroups groups;

    for (const Expr *p : preds)
    {
        auto n = buildPredicateNode(p, aliasToBase);
        if (!n)
            continue;

        if (n->node_type == LogicalNodeType::JOIN)
            groups.joinPreds.push_back(n);
        else
            groups.selectionPreds.push_back(n);
    }

    return groups;
}

std::shared_ptr<LogicalPlanNode> buildSelectInputTree(
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &joinPreds,
    const std::vector<std::string> &jsonJoinOrder)
{
    std::vector<std::shared_ptr<LogicalPlanNode>> extraJoinFilters;
    std::shared_ptr<LogicalPlanNode> current;
    bool usedJsonOrder = false;

    auto resolvedJoinOrder = resolveJoinOrderToScans(jsonJoinOrder, scanByTable);
    if (!jsonJoinOrder.empty() && resolvedJoinOrder.size() != scanByTable.size())
    {
        throw std::runtime_error("JSON join order is missing tables from the SQL FROM clause");
    }

    if (!resolvedJoinOrder.empty())
    {
        current = buildJoinTreeFromOrder(
            scanByTable,
            joinPreds,
            extraJoinFilters,
            resolvedJoinOrder,
            usedJsonOrder);
    }

    if (!current || !usedJsonOrder)
    {
        extraJoinFilters.clear();
        current = buildJoinTree(scanByTable, joinPreds, extraJoinFilters);
    }

    for (auto &f : extraJoinFilters)
    {
        if (f)
            pushDownSelectionPredicate(current, f);
    }

    return current;
}

void applySelectionPredicates(
    std::shared_ptr<LogicalPlanNode> &root,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &selectionPreds)
{
    for (const auto &p : selectionPreds)
    {
        if (p && p->node_type == LogicalNodeType::SETOPERATION)
            pushDownSelectionPredicate(root, p);
    }

    for (const auto &p : selectionPreds)
    {
        if (p && p->node_type != LogicalNodeType::SETOPERATION)
            pushDownSelectionPredicate(root, p);
    }
}

std::vector<Column> collectGroupByColumns(
    const SelectStatement *sel,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    std::vector<Column> groupByCols;

    if (sel->groupBy && sel->groupBy->columns)
    {
        for (const Expr *ge : *sel->groupBy->columns)
        {
            if (ge && ge->type == kExprColumnRef)
                groupByCols.push_back(toColumnFromExpr(ge, aliasToBase));
        }
    }

    return groupByCols;
}

void applyLimitOffset(std::shared_ptr<LogicalPlanNode> &root, const SelectStatement *sel)
{
    if (!sel->limit || (!sel->limit->limit && !sel->limit->offset))
        return;

    if (sel->limit->limit)
    {
        const Expr *le = sel->limit->limit;
        if (!le->isType(kExprLiteralInt))
            throw std::runtime_error("Unsupported LIMIT expression (expected integer literal)");
        root->expression.limit_count = static_cast<int>(le->ival);
    }

    if (sel->limit->offset)
    {
        const Expr *oe = sel->limit->offset;
        if (!oe->isType(kExprLiteralInt))
            throw std::runtime_error("Unsupported OFFSET expression (expected integer literal)");
        root->expression.limit_offset = static_cast<int>(oe->ival);
    }
}

// --- SELECT clause analysis ---

// Walks the SELECT list and splits every item into either an aggregate or a plain projection.
//
// Example:  SELECT o.status, COUNT(*) AS cnt, SUM(price)
//   → projections:  [ Column{orders, status} ]
//   → aggregates:   [ {COUNT(*), "cnt"}, {SUM(price), "agg_0"} ]
//   → aggSpecs:     [ AggSpec{COUNT, star, result="cnt"},
//                     AggSpec{SUM,   col=price, result="agg_0"} ]
//   → hasAgg = true
SelectAnalysis analyzeSelectClause(
    const SelectStatement *sel,
    const std::unordered_map<std::string, std::string> &aliasToBase,
    TranslationContext &ctx)
{
    SelectAnalysis result;

    if (!sel->selectList)
        return result;

    for (const Expr *e : *sel->selectList)
    {
        // matchAgg returns a filled AggPattern if the expression is an aggregate function
        // (COUNT, SUM, MIN, MAX, AVG), otherwise nullptr.
        if (auto ap = matchAgg(e, aliasToBase))
        {
            result.hasAgg = true;

            // Use the SQL alias if given, otherwise auto-generate one.
            // e.g. COUNT(*) AS cnt → name = "cnt"
            //      SUM(price)      → name = "agg_0"  (auto-generated)
            std::string name;
            if (!ap->alias.empty())
                name = ap->alias;
            else
                name = generateAggAlias(ctx, ap->func);

            // aggregates: used by applySortNode to resolve ORDER BY aggregate references
            result.aggregates.push_back({*ap, name});

            // aggSpecs: passed to the AGGREGATE node to describe what to compute
            AggSpec spec;
            spec.func = ap->func;
            spec.is_star = ap->is_star; // true for COUNT(*)

            if (ap->is_col)
                spec.input = ap->col; // e.g. SUM(price) → input = Column{orders, price}

            if (!name.empty())
                spec.result_alias = name; // output column name in the AGGREGATE node

            result.aggSpecs.push_back(std::move(spec));
            continue;
        }

        // Plain column reference: SELECT o.status, u.name, ...
        if (e && e->type == kExprColumnRef)
        {
            Column c = toColumnFromExpr(e, aliasToBase);
            c.alias = e->alias ? std::optional<std::string>(e->alias) : std::nullopt;
            result.projections.push_back(std::move(c));
        }
    }

    return result;
}

// --- Output tree construction ---

std::shared_ptr<LogicalPlanNode> applyMapNodes(
    const std::shared_ptr<LogicalPlanNode> &inputRoot,
    SelectAnalysis &selectAnalysis)
{
    auto input = inputRoot;
    for (size_t aggIdx = 0; aggIdx < selectAnalysis.aggregates.size(); ++aggIdx)
    {
        const AggPattern &candidate = selectAnalysis.aggregates[aggIdx].pattern;
        if (candidate.is_sum_mul || candidate.is_sum_sub)
        {
            auto map = std::make_shared<LogicalPlanNode>();
            map->node_type = LogicalNodeType::MAP;
            map->expression.arith_op = candidate.is_sum_mul ? PlanArithOp::MUL : PlanArithOp::SUB;
            map->base_columns = {candidate.leftCol, candidate.rightCol};
            map->children.push_back(input);
            input = map;
            selectAnalysis.aggSpecs[aggIdx].input = std::nullopt;
        }
    }
    return input;
}

// Builds the upper part of the plan tree above the join/filter input.
//
// Without aggregation (e.g. SELECT o.status, u.name FROM ...):
//   PROJECTION(status, name)
//   └── <inputRoot>
//
// With aggregation (e.g. SELECT o.status, COUNT(*) FROM ... GROUP BY o.status):
//   PROJECTION(status, cnt)
//   └── AGGREGATE(GROUP BY status, COUNT(*))
//       └── MAP(...)
//           └── <inputRoot>
std::shared_ptr<LogicalPlanNode> buildSelectOutputTree(
    const std::shared_ptr<LogicalPlanNode> &inputRoot,
    const std::vector<Column> &groupByCols,
    SelectAnalysis &selectAnalysis,
    bool needsAggNode)
{
    // Simple case: no GROUP BY and no aggregate functions.
    // Just wrap the input in a PROJECTION that passes through the selected columns.
    // e.g. SELECT o.status, u.name  →  PROJECTION(status, name) → <inputRoot>
    if (!needsAggNode)
    {
        auto proj = std::make_shared<LogicalPlanNode>();
        proj->node_type = LogicalNodeType::PROJECTION;
        proj->projected_columns = selectAnalysis.projections;
        proj->children.push_back(inputRoot);
        return proj;
    }

    // Insert MAP nodes above the input to prepare aggregate input columns.
    // e.g. SUM(price) needs price to be accessible → MAP computes/renames it.
    auto input = applyMapNodes(inputRoot, selectAnalysis);

    // Build the AGGREGATE node.
    // base_columns = GROUP BY columns (empty if no GROUP BY but aggregate present).
    // agg_specs    = what to compute: COUNT(*), SUM(price), ...
    // e.g. GROUP BY o.status, COUNT(*) AS cnt
    //   → base_columns=[status],  agg_specs=[{COUNT, star, result="cnt"}]
    auto ag = std::make_shared<LogicalPlanNode>();
    ag->node_type = LogicalNodeType::AGGREGATE;
    ag->base_columns = groupByCols;
    ag->children.push_back(input);
    ag->expression.agg_specs = selectAnalysis.aggSpecs;

    // Build the PROJECTION on top of AGGREGATE.
    // It lists all output columns in SELECT order: plain columns first, then aggregates.
    auto proj = std::make_shared<LogicalPlanNode>();
    proj->node_type = LogicalNodeType::PROJECTION;

    // Add plain columns (e.g. o.status).
    for (const auto &c : selectAnalysis.projections)
        proj->projected_columns.push_back(c);

    // Add one output column per aggregate, using the generated/given alias as the name.
    // e.g. COUNT(*) AS cnt  → Column{"", "cnt", INTEGER, alias="cnt"}
    //      SUM(price)       → Column{"", "agg_0", INTEGER, alias=nullopt}
    for (const auto &aggEntry : selectAnalysis.aggregates)
    {
        std::optional<std::string> aggAlias;
        if (!aggEntry.pattern.alias.empty())
            aggAlias = aggEntry.pattern.alias;
        proj->projected_columns.emplace_back(
            "", aggEntry.name, PlanColumnType::INTEGER, aggAlias);
    }

    proj->children.push_back(ag);
    return proj;
}

std::string findAggAlias(
    const std::vector<AggWithName> &aggregates,
    PlanAggFunc func, bool isStar, const Column *col)
{
    for (const auto &aggEntry : aggregates)
    {
        const auto &p = aggEntry.pattern;
        if (p.func != func)
            continue;
        if (isStar && p.is_star)
            return aggEntry.name;
        if (col && p.is_col &&
            p.col.column_name == col->column_name &&
            (p.col.table_name.empty() || col->table_name.empty() ||
             iequals(p.col.table_name.c_str(), col->table_name.c_str())))
            return aggEntry.name;
        if (!col && !isStar && p.func == func)
            return aggEntry.name;
    }
    for (const auto &aggEntry : aggregates)
    {
        if (aggEntry.pattern.func == func)
            return aggEntry.name;
    }
    return "";
}

void ensureDefaultAggregate(SelectAnalysis &selectAnalysis)
{
    if (!selectAnalysis.aggSpecs.empty())
        return;

    AggPattern defaultAgg;
    defaultAgg.func = PlanAggFunc::COUNT;
    defaultAgg.is_star = true;
    std::string name = "agg";
    selectAnalysis.aggregates.push_back({defaultAgg, name});

    AggSpec spec;
    spec.func = PlanAggFunc::COUNT;
    spec.is_star = true;
    spec.result_alias = name;
    selectAnalysis.aggSpecs.push_back(std::move(spec));
}

// Wraps the current root in a SORT node if the query has an ORDER BY clause.
// Handles two kinds of sort expressions:
//   1. Plain column:   ORDER BY o.status ASC
//   2. Aggregate:      ORDER BY COUNT(*) DESC
//
// Result for ORDER BY o.status ASC, COUNT(*) DESC:
//   SORT(base_columns=[status, agg_0], sort_order=[true, false])
//   └── <root>
void applySortNode(
    std::shared_ptr<LogicalPlanNode> &root,
    const SelectStatement *sel,
    const std::vector<AggWithName> &aggregates,
    const std::unordered_map<std::string, std::string> &aliasToBase,
    TranslationContext &ctx)
{
    if (!sel->order || sel->order->empty())
        return;

    auto sort = std::make_shared<LogicalPlanNode>();
    sort->node_type = LogicalNodeType::SORT;

    // Parallel to base_columns
    std::vector<bool> directions;

    for (const OrderDescription *od : *sel->order)
    {
        if (!od || !od->expr)
            continue;

        if (od->expr->type == kExprColumnRef)
        {
            // Plain column reference: ORDER BY o.status
            sort->base_columns.push_back(toColumnFromExpr(od->expr, aliasToBase));
        }
        else if (od->expr->type == kExprFunctionRef && od->expr->name)
        {
            // Aggregate function: ORDER BY COUNT(*) or ORDER BY SUM(price)
            // → find the alias that was assigned to this aggregate in the SELECT clause,
            //   so the SORT node can reference it by name.
            auto orderAgg = matchAgg(od->expr, aliasToBase);
            if (orderAgg)
            {
                bool isStar = orderAgg->is_star;
                const Column *aggCol = orderAgg->is_col ? &orderAgg->col : nullptr;
                // Look up the alias assigned during analyzeSelectClause.
                // e.g. COUNT(*) AS cnt  →  matchedName = "cnt"
                std::string matchedName = findAggAlias(aggregates, orderAgg->func, isStar, aggCol);
                if (matchedName.empty())
                {
                    matchedName = generateAggAlias(ctx, orderAgg->func);
                }
                sort->base_columns.emplace_back("", matchedName, PlanColumnType::INTEGER);
            }
            else
            {
                throw std::runtime_error("Unsupported ORDER BY function");
            }
        }
        else
        {
            throw std::runtime_error("Unsupported ORDER BY expression");
        }

        // true = ASC (default), false = DESC.
        bool asc = (od->type != kOrderDesc);
        directions.push_back(asc);
    }

    if (!sort->base_columns.empty())
    {
        sort->expression.sort_order = std::move(directions);
        sort->children.push_back(root);
        root = sort;
    }
}

// --- Main SELECT builder ---

std::shared_ptr<LogicalPlanNode> buildPlanForSelect(
    const SelectStatement *sel,
    const std::vector<std::string> &jsonJoinOrder,
    TranslationContext &ctx)
{
    initializeSelectContext(sel, ctx);
    auto &aliasToBase = ctx.aliasToTable;

    // Creates one SCAN node per table in the FROM clause.
    // e.g. FROM orders, users  →  { "orders": SCAN(orders), "users": SCAN(users) }
    auto scanByTable = buildScanMap(sel->fromTable);

    // Splits WHERE conjuncts into join predicates and selection predicates.
    // e.g. WHERE o.user_id = u.id AND o.status = 'shipped'
    //   →  joinPreds: [o.user_id = u.id],  selectionPreds: [o.status = 'shipped']
    auto predicates = extractPredicates(sel->whereClause, aliasToBase);

    // Connects SCAN nodes via JOIN nodes using the join predicates.
    // e.g. joinPreds: [o.user_id = u.id]  →  JOIN(o.user_id=u.id) → SCAN(orders), SCAN(users)
    auto current = buildSelectInputTree(scanByTable, predicates.joinPreds, jsonJoinOrder);

    // Pushes selection predicates as deep as possible into the tree.
    applySelectionPredicates(current, predicates.selectionPreds);

    // Extracts GROUP BY columns.
    // e.g. GROUP BY o.status  →  [Column{orders, status}]
    auto groupByCols = collectGroupByColumns(sel, aliasToBase);

    // Separates aggregates from plain projections in the SELECT list.
    // e.g. SELECT o.status, COUNT(*)  →  projections: [status],  aggregates: [COUNT(*) as agg_0]
    auto selectAnalysis = analyzeSelectClause(sel, aliasToBase, ctx);

    // An AGGREGATE node is needed when there's a GROUP BY or any aggregate function.
    // e.g. SELECT COUNT(*) FROM orders  →  needsAggNode = true
    bool needsAggNode = selectAnalysis.hasAgg || !groupByCols.empty();

    // If GROUP BY is present but no aggregate was written, insert a default COUNT(*).
    if (needsAggNode)
        ensureDefaultAggregate(selectAnalysis);

    // Builds the upper plan: PROJECTION (→ AGGREGATE → MAP) above the input tree.
    // e.g.  PROJECTION(status, agg_0) → AGGREGATE(GROUP BY status) → MAP → JOIN → ...
    auto root = buildSelectOutputTree(current, groupByCols, selectAnalysis, needsAggNode);

    applySortNode(root, sel, selectAnalysis.aggregates, aliasToBase, ctx);

    applyLimitOffset(root, sel);

    return root;
}
