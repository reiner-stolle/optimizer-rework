#include "LogicalPlanNode.hpp"
#include "QueryPlan.pb.h"
#include "WorkItem.pb.h"
#include "optimizer/GraphvizPhysicalVisualizer.hpp"
#include "PhysicalPlanNode.hpp"
#include "random"

inline ColumnType columnTypeToProto(PlanColumnType t) {
    switch (t) {
        case PlanColumnType::INTEGER: return TYPE_INTEGER;
        case PlanColumnType::FLOAT: return TYPE_FLOAT;
        case PlanColumnType::STRING: return TYPE_STRING;
        case PlanColumnType::BITMASK: return TYPE_BITMASK;
        case PlanColumnType::POSLIST: return TYPE_POSLIST;
        case PlanColumnType::PAIR_POSLIST: return TYPE_PAIR_POSLIST;
    }
    throw std::logic_error("Unknown ColumnType in columnTypeToProto");
}

inline CompType compTypeToProto(PlanCompType t) {
    switch (t) {
        case PlanCompType::LT: return COMP_LT;
        case PlanCompType::LE: return COMP_LE;
        case PlanCompType::EQ: return COMP_EQ;
        case PlanCompType::GE: return COMP_GE;
        case PlanCompType::GT: return COMP_GT;
        case PlanCompType::NE: return COMP_NE;
        case PlanCompType::BETWEEN: return COMP_BETWEEN;
        case PlanCompType::IN: return COMP_IN;
        case PlanCompType::LIKE: return COMP_LIKE;
    }
    throw std::logic_error("Unknown CompType in compTypeToProto");
}

inline StringOperation stringOpToProto(PlanStringOp t) {
    switch (t) {
        case PlanStringOp::LOWER: return STR_LOWER;
        case PlanStringOp::UPPER: return STR_UPPER;
    }
    throw std::logic_error("Unknown StringOperation in stringOpToProto");
}

inline RelOp relOpToProto(PlanLogicalRelOp t) {
    switch (t) {
        case PlanLogicalRelOp::UNION: return REL_UNION;
        case PlanLogicalRelOp::INTERSECTION: return REL_INTERSECTION;
        case PlanLogicalRelOp::NEGATION: return REL_NEGATION;
    }
    throw std::logic_error("Unknown RelOpCpp in toProto");
}

inline ArithOp arithOpToProto(PlanArithOp t) {
    switch (t) {
        case PlanArithOp::ADD: return ARITH_ADD;
        case PlanArithOp::SUB: return ARITH_SUB;
        case PlanArithOp::MUL: return ARITH_MUL;
        case PlanArithOp::DIV: return ARITH_DIV;
        case PlanArithOp::MOD: return ARITH_MOD;
    }
    throw std::logic_error("Unknown ArithOpCpp in toProto");
}

inline AggFunc aggFuncToProto(PlanAggFunc t) {
    switch (t) {
        case PlanAggFunc::COUNT: return AGG_COUNT;
        case PlanAggFunc::SUM: return AGG_SUM;
        case PlanAggFunc::MIN: return AGG_MIN;
        case PlanAggFunc::MAX: return AGG_MAX;
        case PlanAggFunc::AVG: return AGG_AVG;
    }
}

inline void columnToProto(const Column &column, ColumnMessage *output_column) {
    output_column->set_colname(column.column_name);
    output_column->set_tabname(column.getBaseTableName());
    output_column->set_coltype(columnTypeToProto(column.type));
    if (column.is_base.has_value()) {
        output_column->set_isbase(column.is_base.value());
    }
}

inline int generateRandomId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(1, 2147483640);
    return dis(gen);
}

inline std::vector<WorkItem> crateWorkItems(const std::shared_ptr<PhysicalPlanNode> &root, int planId) {
    struct Frame {
        PhysicalPlanNode *node;
        size_t nextChildIndex;
    };

    std::stack<Frame> st;
    std::unordered_set<PhysicalPlanNode *> seen;

    st.push(Frame{root.get(), 0});
    seen.insert(root.get());

    std::vector<PhysicalPlanNode *> order;
    order.reserve(128);

    while (!st.empty()) {
        Frame &f = st.top();
        if (f.nextChildIndex < f.node->children.size()) {
            PhysicalPlanNode *child = f.node->children[f.nextChildIndex++].get();
            if (child && !seen.count(child)) {
                st.push(Frame{child, 0});
                seen.insert(child);
            }
        } else {
            order.push_back(f.node);
            st.pop();
        }
    }

    std::unordered_map<PhysicalPlanNode *, int> idOf;
    idOf.reserve(order.size());

    for (PhysicalPlanNode *n : order) {
        idOf[n] = n->node_id;
    }

    std::vector<WorkItem> workItems;
    workItems.reserve(order.size());

    for (PhysicalPlanNode *n: order) {
        WorkItem workItem;
        workItem.set_planid(planId);
        workItem.set_itemid(idOf[n]);

        for (const std::shared_ptr<PhysicalPlanNode> &childPtr: n->children) {
            PhysicalPlanNode *child = childPtr.get();
            if (!child) continue;
            int depId = idOf[child];
            workItem.add_dependson(depId);
        }

        switch (n->node_type) {
            case PhysicalNodeType::FILTER: {
                workItem.set_operatorid(OP_FILTER);
                FilterItem *filter_item = workItem.mutable_filterdata();
                columnToProto(n->base_columns[0], filter_item->mutable_inputcolumn());
                columnToProto(n->result_columns[0], filter_item->mutable_outputcolumn());
                filter_item->set_filtertype(compTypeToProto(*n->expression.comp_type));
                // Cross-table filter: column-vs-column comparison (e.g. ci.movie_id = mc.movie_id)
                if (n->base_columns.size() >= 2) {
                    columnToProto(n->base_columns[1], filter_item->mutable_comparecolumn());
                } else {
                    for (auto val: n->expression.values) {
                        filter_item->add_filtervalue()->mutable_stringval()->set_value(val);
                    }
                    // Handle StringOperation (UPPER/LOWER)
                    if (n->expression.string_op.has_value()) {
                        filter_item->add_stringop(stringOpToProto(*n->expression.string_op));
                    }
                }
                break;
            }
            case PhysicalNodeType::IDXSCAN:
                workItem.set_operatorid(OP_IDXSCAN);
                break;
            case PhysicalNodeType::NLJ:
            case PhysicalNodeType::MERGEJOIN:
            case PhysicalNodeType::HASHJOIN: {
                if (n->node_type == PhysicalNodeType::NLJ)
                    workItem.set_operatorid(OP_NLJ);
                if (n->node_type == PhysicalNodeType::MERGEJOIN)
                    workItem.set_operatorid(OP_MERGEJOIN);
                if (n->node_type == PhysicalNodeType::HASHJOIN)
                    workItem.set_operatorid(OP_HASHJOIN);
                JoinItem *join_item = workItem.mutable_joindata();
                columnToProto(n->base_columns[0], join_item->mutable_innercolumn());
                columnToProto(n->base_columns[1], join_item->mutable_outercolumn());
                ColumnMessage *result_column = join_item->mutable_outputcolumn();
                std::string name = n->result_columns[0].column_name;
                result_column->set_colname(name.substr(0, name.size() - 2));
                result_column->set_tabname(n->result_columns[0].table_name);
                result_column->set_coltype(columnTypeToProto(n->result_columns[0].type));
                join_item->set_joinpredicate(compTypeToProto(*n->expression.comp_type));
                // Signal which output poslists of the join the executor should produce.
                if (n->result_columns.size() == 1) {
                    const std::string& col_name = n->result_columns[0].column_name;
                    bool is_inner = col_name.size() >= 2 && col_name.substr(col_name.size() - 2) == "_i";
                    join_item->set_produceinneroutput(is_inner);
                    join_item->set_produceouteroutput(!is_inner);
                }
                break;
            }
            case PhysicalNodeType::AGGREGATE: {
                workItem.set_operatorid(OP_AGGREGATE);
                AggItem *aggItem = workItem.mutable_aggdata();
                columnToProto(n->base_columns[0], aggItem->mutable_inputcolumn());
                columnToProto(n->result_columns[0], aggItem->mutable_outputcolumn());
                aggItem->set_aggfunc(aggFuncToProto(n->expression.agg_specs.front().func));
                //aggItem->mutable_groupcolumns()   ?????
                break;
            }
            case PhysicalNodeType::SETOPERATION: {
                workItem.set_operatorid(OP_SETOPERATION);
                SetOperationItem *set_operation_item = workItem.mutable_setdata();
                columnToProto(n->base_columns[0], set_operation_item->mutable_innercolumn());
                columnToProto(n->base_columns[1], set_operation_item->mutable_outercolumn());
                columnToProto(n->result_columns[0], set_operation_item->mutable_outputcolumn());
                set_operation_item->set_operation(relOpToProto(*n->expression.logical_rel_op));
                break;
            }
            case PhysicalNodeType::SORT: {
                workItem.set_operatorid(OP_SORT);
                SortItem *sort_item = workItem.mutable_sortdata();
                for (const Column &col: n->base_columns) {
                    columnToProto(col, sort_item->add_inputcolumns());
                }
                for (bool sort_order: *n->expression.sort_order) {
                    sort_item->add_sortorder(sort_order);
                }
                columnToProto(n->result_columns[0], sort_item->mutable_indexoutput());
                break;
            }
            case PhysicalNodeType::MAP: {
                workItem.set_operatorid(OP_MAP);
                MapItem *map_item = workItem.mutable_mapdata();
                columnToProto(n->base_columns[0], map_item->mutable_inputcolumn());
                columnToProto(n->result_columns[0], map_item->mutable_outputcolumn());
                map_item->set_operatortype(arithOpToProto(*n->expression.arith_op));
                if (n->base_columns.size() > 1) {
                    columnToProto(n->base_columns[1], map_item->mutable_partnercolumn());
                } else if (!n->expression.values.empty()) {
                    map_item->mutable_staticval()->mutable_stringval()->set_value(n->expression.values[0]);
                }
                break;
            }
            case PhysicalNodeType::GROUPBY: {
                workItem.set_operatorid(OP_GROUPBY);
                MultiGroupItem *multi_item = workItem.mutable_multigroupdata();
                for (const Column &col: n->base_columns) {
                    columnToProto(col, multi_item->add_groupcolumns());
                }
                columnToProto(*n->aggregationColumn, multi_item->mutable_aggregationcolumn());
                columnToProto(n->result_columns[0], multi_item->mutable_aggregationresultcolumn());
                //outputIndex
                ColumnMessage *outputIndex = multi_item->mutable_outputindex();
                outputIndex->set_colname(n->result_columns[0].column_name + "_idx");
                outputIndex->set_tabname(n->result_columns[0].table_name);
                outputIndex->set_coltype(TYPE_POSLIST);
                //outputCluster
                ColumnMessage *outputClusters = multi_item->mutable_outputclusters();
                outputClusters->set_colname(n->result_columns[0].column_name + "_cluster");
                outputClusters->set_tabname(n->result_columns[0].table_name);
                outputClusters->set_coltype(TYPE_POSLIST);
                if (n->expression.sort_order.has_value()) {
                    for (bool sort_order : *n->expression.sort_order) {
                        multi_item->add_sortorders(sort_order);
                    }
                }
                break;
            }
            case PhysicalNodeType::MATERIALIZE: {
                workItem.set_operatorid(OP_MATERIALIZE);
                MaterializeItem *materialize_item = workItem.mutable_materializedata();
                columnToProto(n->base_columns[0], materialize_item->mutable_filtercolumn());
                columnToProto(n->index.value(), materialize_item->mutable_indexcolumn());
                columnToProto(n->result_columns[0], materialize_item->mutable_outputcolumn());
                break;
            }
            case PhysicalNodeType::RESULT: {
                workItem.set_operatorid(OP_RESULT);
                ResultItem *result_item = workItem.mutable_resultdata();
                result_item->set_filename(*n->resultName);
                for (const Column &col: n->result_columns) {
                    columnToProto(col, result_item->add_resultcolumns());
                    result_item->add_resultheader(*col.alias);
                }
                // Handle LIMIT and OFFSET
                if (n->expression.limit_count.has_value()) {
                    result_item->set_limitcount(static_cast<uint32_t>(*n->expression.limit_count));
                }
                if (n->expression.limit_offset.has_value()) {
                    result_item->set_offsetcount(static_cast<uint32_t>(*n->expression.limit_offset));
                }
                break;
            }
            default:
                break;
        }
        workItems.push_back(workItem);
    }
    return workItems;
}

inline std::vector<std::vector<WorkItem> > crateWorkItemsParallel(const std::shared_ptr<PhysicalPlanNode> &root) {
    int planId = generateRandomId();
    std::vector<WorkItem> linear = crateWorkItems(root, planId);
    if (linear.empty()) {
        return {};
    }

    std::unordered_map<int, int> level;
    int maxLevel = 0;

    for (const auto &wi: linear) {
        int curLevel = 0;
        for (int depId: wi.dependson()) {
            if (level.find(depId) != level.end()) {
                curLevel = std::max(curLevel, level[depId] + 1);
            }
        }
        level[wi.itemid()] = curLevel;
        maxLevel = std::max(maxLevel, curLevel);
    }

    std::vector<std::vector<WorkItem> > grouped(static_cast<size_t>(maxLevel + 1));
    for (auto &wi: linear) {
        int l = level[wi.itemid()];
        grouped[static_cast<size_t>(l)].push_back(std::move(wi));
    }

    return grouped;
}

inline QueryPlan createQueryPlan(const std::shared_ptr<PhysicalPlanNode> &root) {
    int planId = generateRandomId();
    QueryPlan plan;

    plan.set_planid(static_cast<::google::protobuf::uint32>(planId));

    std::vector<WorkItem> workItems = crateWorkItems(root, planId);

    for (const auto &item: workItems) {
        *plan.add_planitems() = item;
    }

    return plan;
}