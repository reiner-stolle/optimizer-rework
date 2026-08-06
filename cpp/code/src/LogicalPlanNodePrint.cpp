#include "LogicalPlanNode.hpp"
#include <iostream>
#include <optional>
#include <string>
#include <vector>

static void indent(int n)
{
    while (n--)
        std::cout << "  ";
}

static std::string nodeTypeName(LogicalNodeType t)
{
    switch (t)
    {
    case LogicalNodeType::SCAN:
        return "SCAN";
    case LogicalNodeType::FILTER:
        return "FILTER";
    case LogicalNodeType::PROJECTION:
        return "PROJECTION";
    case LogicalNodeType::JOIN:
        return "JOIN";
    case LogicalNodeType::AGGREGATE:
        return "AGGREGATE";
    case LogicalNodeType::MAP:
        return "MAP";
    case LogicalNodeType::SETOPERATION:
        return "SETOPERATION";
    case LogicalNodeType::SORT:
        return "SORT";
    }
    return "?";
}
static std::string compName(PlanCompType c)
{
    switch (c)
    {
    case PlanCompType::LT:
        return "LT";
    case PlanCompType::LE:
        return "LE";
    case PlanCompType::EQ:
        return "EQ";
    case PlanCompType::GE:
        return "GE";
    case PlanCompType::GT:
        return "GT";
    case PlanCompType::NE:
        return "NE";
    case PlanCompType::BETWEEN:
        return "BETWEEN";
    case PlanCompType::IN:
        return "IN";
    case PlanCompType::LIKE:
        return "LIKE";
    }
    return "?";
}
static std::string aggName(PlanAggFunc a)
{
    switch (a)
    {
    case PlanAggFunc::COUNT:
        return "COUNT";
    case PlanAggFunc::SUM:
        return "SUM";
    case PlanAggFunc::MIN:
        return "MIN";
    case PlanAggFunc::MAX:
        return "MAX";
    case PlanAggFunc::AVG:
        return "AVG";
    }
    return "?";
}
static std::string arithName(PlanArithOp a)
{
    switch (a)
    {
    case PlanArithOp::ADD:
        return "ADD";
    case PlanArithOp::SUB:
        return "SUB";
    case PlanArithOp::MUL:
        return "MUL";
    case PlanArithOp::DIV:
        return "DIV";
    case PlanArithOp::MOD:
        return "MOD";
    }
    return "?";
}
static std::string setOpName(PlanLogicalRelOp o)
{
    switch (o)
    {
    case PlanLogicalRelOp::UNION:
        return "UNION";
    case PlanLogicalRelOp::INTERSECTION:
        return "INTERSECTION";
    case PlanLogicalRelOp::NEGATION:
        return "NEGATION";
    }
    return "?";
}

static std::string formatColumn(const Column &c)
{
    return (c.table_name.empty() ? "" : c.table_name + ".") + c.column_name;
}

static void printColumns(const std::vector<Column> &cols, int depth, const char *label)
{
    if (cols.empty())
        return;
    indent(depth);
    std::cout << label << ":\n";
    for (const auto &c : cols)
    {
        indent(depth + 1);
        if (c.alias)
            std::cout << " AS " << *c.alias;
        std::cout << "\n";
    }
}

static void printSort(const LogicalPlanNode &n, int depth)
{
    indent(depth);
    std::cout << "order_by:\n";

    const auto &cols = n.base_columns;
    const auto &ord = n.expression.sort_order;

    for (size_t i = 0; i < cols.size(); ++i)
    {
        indent(depth + 1);
        const auto &c = cols[i];
        std::cout << (c.table_name.empty() ? "" : c.table_name + ".") << c.column_name;

        bool asc = true; // default
        if (ord && i < ord->size())
            asc = (*ord)[i];

        std::cout << " " << (asc ? "ASC" : "DESC") << "\n";
    }
}

static void printExpr(const Expression &e, int depth)
{
    if (e.comp_type)
    {
        indent(depth);
        std::cout << "comp=" << compName(*e.comp_type) << "\n";
    }
    if (!e.agg_specs.empty())
    {
        for (const auto &spec : e.agg_specs)
        {
            indent(depth);
            std::cout << "agg=" << aggName(spec.func);
            if (spec.is_star)
            {
                std::cout << "(*)";
            }
            else if (spec.input)
            {
                std::cout << "(" << formatColumn(*spec.input) << ")";
            }
            if (spec.result_alias)
            {
                std::cout << " AS " << *spec.result_alias;
            }
            std::cout << "\n";
        }
    }
    if (e.arith_op)
    {
        indent(depth);
        std::cout << "arith=" << arithName(*e.arith_op) << "\n";
    }
    if (e.logical_rel_op)
    {
        indent(depth);
        std::cout << "setop=" << setOpName(*e.logical_rel_op) << "\n";
    }
    if (!e.values.empty())
    {
        indent(depth);
        std::cout << "values: ";
        for (size_t i = 0; i < e.values.size(); ++i)
        {
            if (i)
                std::cout << ", ";
            std::cout << e.values[i];
        }
        std::cout << "\n";
    }
}

void printLogicalPlan(const LogicalPlanNode &n, int depth)
{
    indent(depth);
    std::cout << nodeTypeName(n.node_type);
    if (n.node_type == LogicalNodeType::SCAN && !n.base_table.empty())
    {
        std::cout << " table=" << n.base_table;
    }
    else if (n.node_type == LogicalNodeType::AGGREGATE)
    {
        if (n.expression.agg_specs.size() == 1)
        {
            std::cout << " agg=" << aggName(n.expression.agg_specs[0].func);
        }
        else if (n.expression.agg_specs.size() > 1)
        {
            std::cout << " agg=multi";
        }
    }
    else if (n.node_type == LogicalNodeType::MAP && n.expression.arith_op)
    {
        std::cout << " arith=" << arithName(*n.expression.arith_op);
    }
    else if ((n.node_type == LogicalNodeType::FILTER || n.node_type == LogicalNodeType::JOIN) && n.expression.comp_type)
    {
        std::cout << " comp=" << compName(*n.expression.comp_type);
    }
    else if (n.node_type == LogicalNodeType::SETOPERATION && n.expression.logical_rel_op)
    {
        std::cout << " setop=" << setOpName(*n.expression.logical_rel_op);
    }

    std::cout << "\n";

    // Columns
    printColumns(n.projected_columns, depth + 1, "projected_columns");

    if (!n.base_columns.empty() && n.node_type != LogicalNodeType::SORT)
        printColumns(n.base_columns, depth + 1, "base_columns");

    if (n.node_type == LogicalNodeType::SORT)
        printSort(n, depth + 1);

    // Expression (for relevant nodes)
    if (n.node_type == LogicalNodeType::FILTER ||
        n.node_type == LogicalNodeType::JOIN ||
        n.node_type == LogicalNodeType::AGGREGATE ||
        n.node_type == LogicalNodeType::MAP ||
        n.node_type == LogicalNodeType::SETOPERATION)
    {
        printExpr(n.expression, depth + 1);
    }

    // Children
    for (const auto &ch : n.children)
    {
        printLogicalPlan(*ch, depth + 1);
    }
}
