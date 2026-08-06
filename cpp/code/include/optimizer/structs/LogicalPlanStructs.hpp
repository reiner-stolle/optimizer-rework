#ifndef LOGICAL_PLAN_STRUCTS_HPP
#define LOGICAL_PLAN_STRUCTS_HPP

#include "LogicalPlanNode.hpp"

inline void logicalPlanExample() {
    auto root = std::make_shared<LogicalPlanNode>();
    root->node_type = LogicalNodeType::PROJECTION;
    //root->base_column = {{"d_day_intermediate", "int"}};
    root->projected_columns = {{"dates", "d_day", PlanColumnType::INTEGER}};


    auto child1 = std::make_shared<LogicalPlanNode>();
    child1->node_type = LogicalNodeType::FILTER;
    child1->base_columns = {{"dates", "d_year", PlanColumnType::INTEGER}};
    child1->expression.comp_type = std::make_optional(PlanCompType::GT);
    child1->expression.values = {"1993"};

    auto child2 = std::make_shared<LogicalPlanNode>();
    child2->node_type = LogicalNodeType::SCAN;
    child2->base_table = "dates";

    root->children.push_back(child1);
    child1->children.push_back(child2);

    auto root2 = std::make_shared<LogicalPlanNode>();
    root2->node_type = LogicalNodeType::PROJECTION;
    root2->projected_columns = {{"", "sum", PlanColumnType::INTEGER}};

    auto child2_1 = std::make_shared<LogicalPlanNode>();
    child2_1->node_type = LogicalNodeType::AGGREGATE;
    child2_1->expression.agg_specs = {AggSpec{PlanAggFunc::SUM, std::nullopt, std::nullopt, false}};

    root2->children.push_back(child2_1);

    auto child2_2 = std::make_shared<LogicalPlanNode>();
    child2_2->node_type = LogicalNodeType::MAP;
    child2_2->base_columns = {{"lineorder", "lo_extendedprice", PlanColumnType::INTEGER}, {"lineorder", "lo_discount", PlanColumnType::INTEGER}};
    child2_2->expression.arith_op = std::make_optional(PlanArithOp::MUL);

    child2_2->children.push_back(child2_1);

    auto child2_3 = std::make_shared<LogicalPlanNode>();
    child2_3->node_type = LogicalNodeType::JOIN;
    child2_3->base_columns = {{"lineorder", "lo_orderdate", PlanColumnType::INTEGER}, {"dates", "d_datekey", PlanColumnType::INTEGER}};
    child2_3->expression.comp_type = std::make_optional(PlanCompType::EQ);
    //condition = "lo_orderdate = d_datekey";

    auto child2_4 = std::make_shared<LogicalPlanNode>();
    child2_4->node_type = LogicalNodeType::FILTER;
    child2_4->base_columns = {{"dates", "d_year", PlanColumnType::INTEGER}};
    child2_4->expression.comp_type = std::make_optional(PlanCompType::GT);
    child2_4->expression.values = {"1993"};

    auto child2_5 = std::make_shared<LogicalPlanNode>();
    child2_5->node_type = LogicalNodeType::FILTER;
    child2_5->base_columns = {{"lineorder", "lo_discount", PlanColumnType::INTEGER}};
    child2_5->expression.comp_type = std::make_optional(PlanCompType::BETWEEN);
    child2_5->expression.values = {"1", "3"};

    auto child2_6 = std::make_shared<LogicalPlanNode>();
    child2_6->node_type = LogicalNodeType::FILTER;
    child2_6->base_columns = {{"lineorder", "lo_quantity", PlanColumnType::INTEGER}};
    child2_6->expression.comp_type = std::make_optional(PlanCompType::LT);
    child2_6->expression.values = {"25"};

    child2_3->children.push_back(child2_4);
    child2_3->children.push_back(child2_5);

    auto child2_7 = std::make_shared<LogicalPlanNode>();
    child2_7->node_type = LogicalNodeType::SCAN;
    child2_7->base_table = "lineorder";

    // Order irrelevant
    child2_5->children.push_back(child2_6);
    child2_6->children.push_back(child2_7);

    auto child2_8 = std::make_shared<LogicalPlanNode>();
    child2_8->node_type = LogicalNodeType::SCAN;
    child2_8->base_table = "dates";

    child2_4->children.push_back(child2_8);

}

#endif // LOGICAL_PLAN_STRUCTS_HPP
