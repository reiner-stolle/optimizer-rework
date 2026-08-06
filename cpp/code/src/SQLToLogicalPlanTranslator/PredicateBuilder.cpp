#include "SQLToLogicalPlanTranslator/PredicateBuilder.hpp"
#include "SQLToLogicalPlanTranslator/TranslatorHelpers.hpp"

#include <stdexcept>

// --- WHERE clause splitting ---

void splitConjuncts(const Expr *e, std::vector<const Expr *> &out)
{
    if (!e)
        return;
    if (e->type == kExprOperator && e->opType == kOpAnd)
    {
        splitConjuncts(e->expr, out);
        splitConjuncts(e->expr2, out);
    }
    else
    {
        out.push_back(e);
    }
}

bool collectOrEqualsSameColumn(const Expr *e,
                               Column &colOut,
                               std::vector<std::string> &valuesOut,
                               const std::unordered_map<std::string, std::string> &aliasToBase)
{
    if (!e)
        return false;

    if (e->type == kExprOperator && e->opType == kOpOr)
    {
        return collectOrEqualsSameColumn(e->expr, colOut, valuesOut, aliasToBase) &&
               collectOrEqualsSameColumn(e->expr2, colOut, valuesOut, aliasToBase);
    }

    if (e->type == kExprOperator && e->opType == kOpEquals &&
        e->expr && e->expr2)
    {
        const Expr *colExpr = nullptr;
        const Expr *valExpr = nullptr;

        const Expr *left = unwrapSimpleFunction(e->expr);
        const Expr *right = unwrapSimpleFunction(e->expr2);

        if (left->type == kExprColumnRef)
        {
            colExpr = left;
            valExpr = right;
        }
        else if (right->type == kExprColumnRef)
        {
            colExpr = right;
            valExpr = left;
        }
        else
        {
            return false;
        }

        Column thisCol = toColumnFromExpr(colExpr, aliasToBase);

        if (colOut.column_name.empty() && colOut.table_name.empty())
        {
            colOut = thisCol;
        }
        else
        {
            if (colOut.column_name != thisCol.column_name ||
                colOut.table_name != thisCol.table_name)
            {
                return false;
            }
        }

        std::string v;
        if (valExpr->isType(kExprLiteralInt))
            v = std::to_string(valExpr->ival);
        else if (valExpr->isType(kExprLiteralFloat))
            v = std::to_string(valExpr->fval);
        else if (valExpr->isType(kExprLiteralString))
            v = valExpr->name ? valExpr->name : "";
        else
            return false;

        valuesOut.push_back(std::move(v));
        return true;
    }

    return false;
}

// --- Predicate node builders ---

std::shared_ptr<LogicalPlanNode> buildInPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    const Expr *left = unwrapSimpleFunction(pred->expr);
    if (!left || left->type != kExprColumnRef)
        throw std::runtime_error("IN: left side is not a column");

    if (pred->select)
        throw std::runtime_error("IN (subquery) not supported");

    if (!pred->exprList || pred->exprList->empty())
        throw std::runtime_error("IN: empty list");

    auto f = std::make_shared<LogicalPlanNode>();
    f->node_type = LogicalNodeType::FILTER;
    f->expression.comp_type = PlanCompType::IN;
    f->base_columns.push_back(toColumnFromExpr(left, aliasToBase));

    for (const Expr *v : *pred->exprList)
    {
        if (!v)
            continue;
        if (v->isType(kExprLiteralInt))
            f->expression.values.push_back(std::to_string(v->ival));
        else if (v->isType(kExprLiteralFloat))
            f->expression.values.push_back(std::to_string(v->fval));
        else if (v->isType(kExprLiteralString))
            f->expression.values.push_back(v->name ? v->name : "");
        else
            throw std::runtime_error("IN: only literal elements supported");
    }
    return f;
}

std::shared_ptr<LogicalPlanNode> buildOrPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    Column col;
    std::vector<std::string> values;
    if (collectOrEqualsSameColumn(pred, col, values, aliasToBase) && !values.empty())
    {
        auto f = std::make_shared<LogicalPlanNode>();
        f->node_type = LogicalNodeType::FILTER;
        f->expression.comp_type = PlanCompType::IN;
        f->base_columns.push_back(col);
        f->expression.values = std::move(values);
        return f;
    }

    auto left = buildPredicateNode(pred->expr, aliasToBase);
    auto right = buildPredicateNode(pred->expr2, aliasToBase);

    std::vector<std::shared_ptr<LogicalPlanNode>> leaves;
    collectUnionLeaves(left, leaves);
    collectUnionLeaves(right, leaves);

    std::string table;
    if (!leaves.empty() && allLeavesSameTable(leaves, table))
    {
        auto u = std::make_shared<LogicalPlanNode>();
        u->node_type = LogicalNodeType::SETOPERATION;
        u->expression.logical_rel_op = PlanLogicalRelOp::UNION;
        u->children = std::move(leaves);
        return u;
    }
    throw std::runtime_error("Unsupported OR predicate (different tables or complex OR)");
}

std::shared_ptr<LogicalPlanNode> buildJoinPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    auto join = std::make_shared<LogicalPlanNode>();
    join->node_type = LogicalNodeType::JOIN;
    join->expression.comp_type = PlanCompType::EQ;
    join->base_columns.push_back(toColumnFromExpr(unwrapSimpleFunction(pred->expr), aliasToBase));
    join->base_columns.push_back(toColumnFromExpr(unwrapSimpleFunction(pred->expr2), aliasToBase));
    return join;
}

std::shared_ptr<LogicalPlanNode> buildBetweenPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    auto f = std::make_shared<LogicalPlanNode>();
    f->node_type = LogicalNodeType::FILTER;
    f->expression.comp_type = PlanCompType::BETWEEN;
    f->base_columns.push_back(toColumnFromExpr(unwrapSimpleFunction(pred->expr), aliasToBase));

    if (pred->exprList && pred->exprList->size() == 2)
    {
        f->expression.values.push_back(exprToLiteral((*pred->exprList)[0]));
        f->expression.values.push_back(exprToLiteral((*pred->exprList)[1]));
    }
    return f;
}

std::shared_ptr<LogicalPlanNode> buildComparisonPredicate(
    const Expr *pred, PlanCompType cmp,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    auto f = std::make_shared<LogicalPlanNode>();
    f->node_type = LogicalNodeType::FILTER;
    f->expression.comp_type = cmp;

    const Expr *L = nullptr;
    const Expr *R = nullptr;
    auto l_op = unwrapStringOp(pred->expr, &L);
    auto r_op = unwrapStringOp(pred->expr2, &R);

    if (cmp == PlanCompType::LIKE)
    {
        if (l_op && r_op)
        {
            if (*l_op == *r_op)
                f->expression.string_op = *l_op;
        }
        else if (l_op)
            f->expression.string_op = *l_op;
        else if (r_op)
            f->expression.string_op = *r_op;
    }

    const Expr *colSide = nullptr;
    const Expr *litSide = nullptr;
    if (L->type == kExprColumnRef)
    {
        colSide = L;
        litSide = R;
    }
    else if (R->type == kExprColumnRef)
    {
        colSide = R;
        litSide = L;
    }

    if (!colSide)
        return nullptr;

    f->base_columns.push_back(toColumnFromExpr(colSide, aliasToBase));

    if (litSide->type == kExprColumnRef)
        f->base_columns.push_back(toColumnFromExpr(litSide, aliasToBase));
    else
        appendLiteralValue(f->expression.values, litSide);

    return f;
}

std::shared_ptr<LogicalPlanNode> buildPredicateNode(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase)
{
    if (!pred)
        throw std::runtime_error("NULL predicate");

    if (pred->type == kExprOperator && pred->opType == kOpIn)
        return buildInPredicate(pred, aliasToBase);

    if (pred->type == kExprOperator && pred->opType == kOpOr)
        return buildOrPredicate(pred, aliasToBase);

    if (pred->type == kExprOperator && pred->opType == kOpEquals &&
        pred->expr && pred->expr2 &&
        unwrapSimpleFunction(pred->expr)->type == kExprColumnRef &&
        unwrapSimpleFunction(pred->expr2)->type == kExprColumnRef)
        return buildJoinPredicate(pred, aliasToBase);

    if (pred->type == kExprOperator && pred->opType == kOpBetween)
        return buildBetweenPredicate(pred, aliasToBase);

    if (pred->type == kExprOperator && pred->expr && pred->expr2)
    {
        if (auto cmp = mapCompOp(pred->opType))
        {
            auto result = buildComparisonPredicate(pred, *cmp, aliasToBase);
            if (result)
                return result;
        }
    }

    throw std::runtime_error(
        "Unsupported WHERE predicate: type=" + std::to_string((int)pred->type) +
        " opType=" + std::to_string((int)pred->opType));
}
