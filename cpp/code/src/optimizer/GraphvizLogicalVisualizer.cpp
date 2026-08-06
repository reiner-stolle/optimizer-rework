#include "optimizer/GraphvizLogicalVisualizer.hpp"

std::string GraphvizLogicalVisualizer::nodeTypeToString(LogicalNodeType type)
{
    switch (type)
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
    default:
        return "UNKNOWN";
    }
}

std::string GraphvizLogicalVisualizer::compTypeToString(PlanCompType type)
{
    switch (type)
    {
    case PlanCompType::LT:
        return "&lt;";
    case PlanCompType::LE:
        return "&le;";
    case PlanCompType::EQ:
        return "=";
    case PlanCompType::GE:
        return "&ge;";
    case PlanCompType::GT:
        return "&gt;";
    case PlanCompType::NE:
        return "!=";
    case PlanCompType::BETWEEN:
        return "BETWEEN";
    case PlanCompType::IN:
        return "IN";
    case PlanCompType::LIKE:
        return "LIKE";
    default:
        return "?";
    }
}

std::string GraphvizLogicalVisualizer::aggFuncToString(PlanAggFunc type)
{
    switch (type)
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
    default:
        return "AGG";
    }
}

std::string GraphvizLogicalVisualizer::arithOpToString(PlanArithOp type)
{
    switch (type)
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
    default:
        return "?";
    }
}

std::string GraphvizLogicalVisualizer::logicalRelOpToString(PlanLogicalRelOp type)
{
    switch (type)
    {
    case PlanLogicalRelOp::UNION:
        return "UNION";
    case PlanLogicalRelOp::INTERSECTION:
        return "INTERSECT";
    case PlanLogicalRelOp::NEGATION:
        return "MINUS";
    default:
        return "SET-OP";
    }
}

static std::string escapeHtml(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string GraphvizLogicalVisualizer::formatColumn(const Column &col)
{    
    // Build column part, with column alias if present
    std::string name = (col.table_name.empty() ? "" : col.table_name + ".") + col.column_name;
    if (col.alias.has_value() && !col.alias->empty() && col.alias.value() != col.column_name) {
        name += " AS " + col.alias.value();
    }
    
    return escapeHtml(name);
}

std::string GraphvizLogicalVisualizer::formatColumns(const std::vector<Column> &cols)
{
    if (cols.empty())
        return "";
    std::stringstream ss;
    for (size_t i = 0; i < cols.size(); ++i)
    {
        ss << formatColumn(cols[i]);
        if (i < cols.size() - 1)
            ss << ", ";
    }
    return ss.str();
}

int GraphvizLogicalVisualizer::traverse(const LogicalPlanNode &node, std::stringstream &ss)
{
    if (visited_nodes_.count(&node))
    {
        return visited_nodes_[&node];
    }

    int current_id = node_id_counter_++;
    visited_nodes_[&node] = current_id;

    std::string label = "<b>" + nodeTypeToString(node.node_type) + "</b>";

    switch (node.node_type)
    {
    case LogicalNodeType::SCAN:
        label += "<br/>Table: " + escapeHtml(node.base_table);
        break;
    case LogicalNodeType::FILTER:
        if (node.expression.comp_type.has_value())
        {
            const PlanCompType comp = node.expression.comp_type.value();
            // Column-vs-Column predicate
            if (node.base_columns.size() >= 2)
            {
                label += "<br/>Cond: " + formatColumn(node.base_columns[0]) + " " +
                         compTypeToString(comp) + " " +
                         formatColumn(node.base_columns[1]);
            }
            // Column-vs-Literal predicate
            else if (node.base_columns.size() == 1 && !node.expression.values.empty())
            {
                const std::string col = formatColumn(node.base_columns[0]);
                const auto &vals = node.expression.values;

                if (comp == PlanCompType::IN)
                {
                    // Render as: col IN (v1, v2, ...)
                    std::stringstream vss;
                    for (size_t i = 0; i < vals.size(); ++i)
                    {
                        vss << escapeHtml(vals[i]);
                        if (i + 1 < vals.size())
                            vss << ", ";
                    }
                    label += "<br/>Cond: " + col + " IN (" + vss.str() + ")";
                }
                else if (comp == PlanCompType::BETWEEN && vals.size() >= 2)
                {
                    // Render as: col BETWEEN v0 AND v1
                    label += "<br/>Cond: " + col + " BETWEEN " + escapeHtml(vals[0]) + " AND " + escapeHtml(vals[1]);
                }
                else if (vals.size() == 1)
                {
                    label += "<br/>Cond: " + col + " " + compTypeToString(comp) + " " + escapeHtml(vals[0]);
                }
                else
                {
                    // Multiple values for other operators: render as e.g. col = (v1 OR v2)
                    std::stringstream vss;
                    for (size_t i = 0; i < vals.size(); ++i)
                    {
                        vss << escapeHtml(vals[i]);
                        if (i + 1 < vals.size())
                            vss << " OR ";
                    }
                    label += "<br/>Cond: " + col + " " + compTypeToString(comp) + " (" + vss.str() + ")";
                }
            }
            else
            {
                label += "<br/>Comp: " + compTypeToString(comp);
            }
        }
        break;
    case LogicalNodeType::PROJECTION:
        label += "<br/>Cols: ";
        label += formatColumns(node.projected_columns);
        if (node.expression.limit_count.has_value() || node.expression.limit_offset.has_value())
        {
            label += "<br/>Limit: ";
            if (node.expression.limit_count.has_value())
            {
                label += std::to_string(node.expression.limit_count.value());
            }
            else
            {
                label += "ALL";
            }
            if (node.expression.limit_offset.has_value())
            {
                label += " Offset: " + std::to_string(node.expression.limit_offset.value());
            }
        }
        break;
    case LogicalNodeType::JOIN:
        if (node.base_columns.size() >= 2 && node.expression.comp_type.has_value())
        {
            label += "<br/>Cond: " + formatColumn(node.base_columns[0]) + " " +
                     compTypeToString(node.expression.comp_type.value()) + " " +
                     formatColumn(node.base_columns[1]);
        }
        break;
    case LogicalNodeType::AGGREGATE:
    {
        if (!node.expression.agg_specs.empty())
        {
            for (const auto &spec : node.expression.agg_specs)
            {
                std::string agg_label = aggFuncToString(spec.func);
                if (spec.is_star)
                {
                    agg_label += "(*)";
                }
                else if (spec.input.has_value())
                {
                    agg_label += "(" + formatColumn(*spec.input) + ")";
                }
                else if (!node.projected_columns.empty())
                {
                    agg_label += "(" + formatColumns(node.projected_columns) + ")";
                }
                if (spec.result_alias.has_value())
                {
                    agg_label += " AS " + escapeHtml(*spec.result_alias);
                }
                label += "<br/>Agg: " + agg_label;
            }
        }

        if (!node.base_columns.empty())
        {
            label += "<br/>Group By: " + formatColumns(node.base_columns);
        }
    }
    break;

    case LogicalNodeType::MAP:
        if (node.base_columns.size() >= 2 && node.expression.arith_op.has_value())
        {
            label += "<br/>Expr: " + formatColumn(node.base_columns[0]) + " " +
                     arithOpToString(node.expression.arith_op.value()) + " " +
                     formatColumn(node.base_columns[1]);
        }
        break;
    case LogicalNodeType::SETOPERATION:
        if (node.expression.logical_rel_op.has_value())
        {
            label += "<br/>Op: " + logicalRelOpToString(node.expression.logical_rel_op.value());
        }
        break;
    case LogicalNodeType::SORT:
        if (!node.base_columns.empty())
        {
            label += "<br/>Sort: " + formatColumns(node.base_columns);
        }
        if (node.expression.sort_order.has_value())
        {
            const auto &dirs = node.expression.sort_order.value();
            label += "<br/>Order: ";
            for (size_t i = 0; i < dirs.size(); ++i)
            {
                label += dirs[i] ? "ASC" : "DESC";
                if (i + 1 < dirs.size())
                {
                    label += ", ";
                }
            }
        }
        if (node.expression.limit_count.has_value() || node.expression.limit_offset.has_value())
        {
            label += "<br/>Limit: ";
            if (node.expression.limit_count.has_value())
            {
                label += std::to_string(node.expression.limit_count.value());
            }
            else
            {
                label += "ALL";
            }
            if (node.expression.limit_offset.has_value())
            {
                label += " Offset: " + std::to_string(node.expression.limit_offset.value());
            }
        }
        break;
    }

    ss << "  " << current_id << " [label=<" << label << ">, shape=box, style=filled, fillcolor=lightblue];\n";

    for (const auto &child : node.children)
    {
        int child_id = traverse(*child, ss);

        std::string edge_label = formatColumns(child->projected_columns);

        ss << "  " << current_id << " -> " << child_id
           << " [label=\"" << edge_label << "\", fontsize=10, fontcolor=\"darkgreen\"];\n";
    }

    return current_id;
}

void GraphvizLogicalVisualizer::visualize(const LogicalPlanNode &root, const std::string &output_filename)
{
    std::stringstream dot_stream;
    dot_stream << "digraph LogicalPlan {\n";
    dot_stream << "  rankdir=TB;\n";
    dot_stream << "  node [fontname=\"Helvetica\"];\n";
    dot_stream << "  edge [fontname=\"Helvetica\" , dir=back];\n";

    node_id_counter_ = 0;
    visited_nodes_.clear();

    traverse(root, dot_stream);

    dot_stream << "}\n";

    std::string dot_filename = output_filename + ".dot";
    std::ofstream out_file(dot_filename);
    out_file << dot_stream.str();
    out_file.close();

    std::cout << "Graphviz DOT file generated: " << dot_filename << std::endl;
    //std::cout << "Render with: dot -Tpng " << dot_filename << " -o " << output_filename << ".png" << std::endl;
}
