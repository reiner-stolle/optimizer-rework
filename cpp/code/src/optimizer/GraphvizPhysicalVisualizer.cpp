#include "optimizer/GraphvizPhysicalVisualizer.hpp"

std::string GraphvizPhysicalVisualizer::phyTypeToString(PhysicalNodeType type) {
        switch (type) {
            case PhysicalNodeType::FILTER: return "FILTER";
            case PhysicalNodeType::IDXSCAN: return "IDXSCAN";
            case PhysicalNodeType::NLJ: return "NLJ";
            case PhysicalNodeType::HASHJOIN: return "HASHJOIN";
            case PhysicalNodeType::MERGEJOIN: return "MERGEJOIN";
            case PhysicalNodeType::AGGREGATE: return "AGGREGATE";
            case PhysicalNodeType::SETOPERATION: return "SETOPERATION";
            case PhysicalNodeType::SORT: return "SORT";
            case PhysicalNodeType::MAP: return "MAP";
            case PhysicalNodeType::MATERIALIZE: return "MATERIALIZE";
            case PhysicalNodeType::GROUPBY: return "GROUPBY";
            case PhysicalNodeType::INTERMEDIATE: return "INTERMEDIATE";
            case PhysicalNodeType::RESULT:
                return "RESULT";
            default:
                return "UNKNOWN";
            }
    }

    std::string GraphvizPhysicalVisualizer::compTypeToString(PlanCompType type) {
        switch (type) {
            case PlanCompType::LT: return "&lt;";
            case PlanCompType::LE: return "&le;";
            case PlanCompType::EQ: return "=";
            case PlanCompType::GE: return "&ge;";
            case PlanCompType::GT: return "&gt;";
            case PlanCompType::NE: return "!=";
            case PlanCompType::BETWEEN: return "BETWEEN";
            case PlanCompType::IN: return "IN";
            case PlanCompType::LIKE: return "LIKE";
            default: return "?";
        }
    }

    std::string GraphvizPhysicalVisualizer::aggFuncToString(PlanAggFunc type) {
        switch (type) {
            case PlanAggFunc::COUNT: return "COUNT";
            case PlanAggFunc::SUM: return "SUM";
            case PlanAggFunc::MIN: return "MIN";
            case PlanAggFunc::MAX: return "MAX";
            case PlanAggFunc::AVG: return "AVG";
            default: return "AGG";
        }
    }

    std::string GraphvizPhysicalVisualizer::arithOpToString(PlanArithOp type) {
        switch (type) {
            case PlanArithOp::ADD: return "ADD";
            case PlanArithOp::SUB: return "SUB";
            case PlanArithOp::MUL: return "MUL";
            case PlanArithOp::DIV: return "DIV";
            case PlanArithOp::MOD: return "MOD";
            default: return "?";
        }
    }

    std::string GraphvizPhysicalVisualizer::logicalRelOpToString(PlanLogicalRelOp type) {
        switch (type) {
            case PlanLogicalRelOp::UNION: return "UNION";
            case PlanLogicalRelOp::INTERSECTION: return "INTERSECT";
            case PlanLogicalRelOp::NEGATION: return "MINUS";
            default: return "SET-OP";
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

    std::string GraphvizPhysicalVisualizer::formatColumns(const std::vector<Column> &cols) {
        if (cols.empty()) return "";
        std::stringstream ss;
        for (size_t i = 0; i < cols.size(); ++i) {
            std::string tablePart = cols[i].table_name;
            // Also show column alias if present
            std::string colName = cols[i].column_name;
            if (cols[i].alias.has_value() && !cols[i].alias->empty()) {
                colName += " AS " + cols[i].alias.value();
            }
            const std::string full = (tablePart.empty() ? "" : tablePart + ".") + colName;
            ss << escapeHtml(full);
            if (i < cols.size() - 1) ss << ", ";
        }
        return ss.str();
    }

    int GraphvizPhysicalVisualizer::traverse(const std::shared_ptr<PhysicalPlanNode> &node, std::stringstream &ss) {
        if (!node) {
            std::cerr << "ERROR: Null node encountered in traverse!" << std::endl;
            return -1;
        }
        
        if (visited_nodes_.count(node.get())) {
            return visited_nodes_[node.get()];
        }

        int current_id = node_id_counter_++;
        visited_nodes_[node.get()] = current_id;

        std::string label = "<b>" + phyTypeToString(node->node_type) + " :: " + std::to_string(node->node_id) + "</b>";

        if (!node->base_columns.empty()) {
            label += "<br/><font color='darkblue' point-size='10'>In: " + formatColumns(node->base_columns) + "</font>";
        }

        if (node->aggregationColumn.has_value()) {
            label += "<br/><font color='purple' point-size='10'>AggCol: " + formatColumns(std::vector<Column>{node->aggregationColumn.value()}) + "</font>";
        }

        if (node->clusterColumn.has_value()) {
            label += "<br/><font color='darkorange' point-size='10'>Cluster: " + formatColumns(std::vector<Column>{node->clusterColumn.value()}) + "</font>";
        }

        if (node->index.has_value()) {
            label += "<br/><font color='darkblue' point-size='10'>Index: " + formatColumns(std::vector<Column>{node->index.value()}) + "</font>";
        }

        std::string exprStr = "";
        switch (node->node_type) {
            case PhysicalNodeType::FILTER:
                if (node->expression.comp_type.has_value() && !node->expression.values.empty()) {
                    const PlanCompType comp = *node->expression.comp_type;
                    const auto &vals = node->expression.values;

                    if (comp == PlanCompType::IN) {
                        // Render: Cond: IN (v1, v2, ...)
                        std::stringstream vss;
                        for (size_t i = 0; i < vals.size(); ++i) {
                            vss << escapeHtml(vals[i]);
                            if (i + 1 < vals.size()) vss << ", ";
                        }
                        exprStr = "Cond: IN (" + vss.str() + ")";
                    } else if (comp == PlanCompType::BETWEEN && vals.size() >= 2) {
                        exprStr = "Cond: BETWEEN " + escapeHtml(vals[0]) + " AND " + escapeHtml(vals[1]);
                    } else {
                        if (vals.size() == 1) {
                            exprStr = "Cond: " + compTypeToString(comp) + " " + escapeHtml(vals[0]);
                        } else {
                            std::stringstream vss;
                            for (size_t i = 0; i < vals.size(); ++i) {
                                vss << escapeHtml(vals[i]);
                                if (i + 1 < vals.size()) vss << " OR ";
                            }
                            exprStr = "Cond: " + compTypeToString(comp) + " (" + vss.str() + ")";
                        }
                    }
                }
                break;
            case PhysicalNodeType::HASHJOIN:
            case PhysicalNodeType::MERGEJOIN:
            case PhysicalNodeType::NLJ: 
                exprStr = "Join";
                if (node->node_type == PhysicalNodeType::HASHJOIN && node->result_columns.size() == 1)
                    exprStr = "Hash Join (single output)";
                break;
            case PhysicalNodeType::AGGREGATE: 
                if (!node->expression.agg_specs.empty()) {
                    exprStr = aggFuncToString(node->expression.agg_specs.front().func);
                }
                break;
            case PhysicalNodeType::MAP: 
                if (node->expression.arith_op.has_value()) {
                    exprStr = "Op: " + arithOpToString(*node->expression.arith_op);
                }
                break;
            case PhysicalNodeType::SETOPERATION: 
                if (node->expression.logical_rel_op.has_value()) {
                    exprStr = logicalRelOpToString(*node->expression.logical_rel_op);
                }
                break;
            case PhysicalNodeType::SORT:
                exprStr = "SORT";
                if (node->expression.sort_order.has_value()) {
                    for (const auto asc : *node->expression.sort_order) {
                        exprStr += (asc ? " ASC" : " DESC");
                    }
                }
                if (node->expression.limit_count.has_value() || node->expression.limit_offset.has_value())
                {
                    exprStr += " | Limit: ";
                    if (node->expression.limit_count.has_value())
                    {
                        exprStr += std::to_string(node->expression.limit_count.value());
                    }
                    else
                    {
                        exprStr += "ALL";
                    }
                    if (node->expression.limit_offset.has_value())
                    {
                        exprStr += " Offset: " + std::to_string(node->expression.limit_offset.value());
                    }
                }
                break;
            case PhysicalNodeType::GROUPBY:
                exprStr = "GROUP BY";
                break;
            case PhysicalNodeType::RESULT:
            case PhysicalNodeType::MATERIALIZE:
            case PhysicalNodeType::INTERMEDIATE:
                if (node->expression.limit_count.has_value() || node->expression.limit_offset.has_value())
                {
                    exprStr = "Limit: ";
                    if (node->expression.limit_count.has_value())
                    {
                        exprStr += std::to_string(node->expression.limit_count.value());
                    }
                    else
                    {
                        exprStr += "ALL";
                    }
                    if (node->expression.limit_offset.has_value())
                    {
                        exprStr += " Offset: " + std::to_string(node->expression.limit_offset.value());
                    }
                }
                break;
            default: break;
        }
        if (!exprStr.empty()) label += "<br/><i>" + exprStr + "</i>";

        ss << "  " << current_id << " [label=<" << label << ">, shape=box, style=filled, fillcolor=lightyellow];\n";

        for (const auto &col : node->base_columns) {
            if (!col.is_base) continue;

            const std::string key = col.table_name + "." + col.column_name;

            int col_node_id;
            auto it = base_col_node_ids_.find(key);
            if (it == base_col_node_ids_.end()) {
                col_node_id = node_id_counter_++;
                base_col_node_ids_[key] = col_node_id;

                ss << "  " << col_node_id << " [label=\"" << escapeHtml(key) << "\", shape=ellipse, style=filled, fillcolor=lightblue, fontsize=10];\n";
            } else {
                col_node_id = it->second;
            }

            ss << "  " << current_id << " -> " << col_node_id << " [style=dashed, color=\"gray50\", fontsize=9];\n";
        }

        for (const auto &child: node->children) {
            if (!child) {
                std::cerr << "WARNING: Null child found in node " << current_id << std::endl;
                continue;
            }
            
            int child_id = traverse(child, ss);
            if (child_id < 0) {
                continue; // Skip invalid children
            }
            
            std::string edge_label = formatColumns(child->result_columns);

            ss << "  " << current_id << " -> " << child_id
                    << " [label=\"" << edge_label << "\", fontsize=10, fontcolor=\"darkgreen\"];\n";
        }

        return current_id;
    }

void GraphvizPhysicalVisualizer::visualize(const std::shared_ptr<PhysicalPlanNode> &root, const std::string &output_filename) {
        std::stringstream dot_stream;
        dot_stream << "digraph PhysicalPlan {\n";
        dot_stream << "  rankdir=TB;\n";
        dot_stream << "  node [fontname=\"Helvetica\"];\n";
        dot_stream << "  edge [fontname=\"Helvetica\", dir=back];\n";

        node_id_counter_ = 0;
        visited_nodes_.clear();
        base_col_node_ids_.clear();

        traverse(root, dot_stream);

        dot_stream << "}\n";

        std::string dot_filename = output_filename + ".dot";
        std::ofstream out_file(dot_filename);
        out_file << dot_stream.str();
        out_file.close();

        std::cout << "Physical Plan DOT generated: " << dot_filename << std::endl;
        //std::cout << "Render: dot -Tpng " << dot_filename << " -o " << output_filename << ".png" << std::endl;
    }
