#ifndef PHYSICAL_PLAN_STRUCTS_HPP
#define PHYSICAL_PLAN_STRUCTS_HPP

#include <string>
#include <utility>
#include <vector>
#include <memory>

#include "LogicalPlanNode.hpp"
#include "PhysicalPlanNode.hpp"
#include "WorkItem.pb.h"

inline std::shared_ptr<PhysicalPlanNode> physicalPlanExample() {
    auto result = std::make_shared<PhysicalPlanNode>();
    result->node_type = PhysicalNodeType::RESULT;
    result->result_columns = {{"result", "sum", PlanColumnType::INTEGER, "revenue"}};
    result->resultName = "result_q_1_1";

    // Build the physical plan from the example
    auto sum = std::make_shared<PhysicalPlanNode>();
    sum->node_type = PhysicalNodeType::AGGREGATE;
    sum->base_columns = {{"intermediate", "i11", PlanColumnType::INTEGER}};
    sum->result_columns = {{"result", "sum", PlanColumnType::INTEGER}};
    sum->expression.agg_specs = {AggSpec{PlanAggFunc::SUM, std::nullopt, std::nullopt, false}};

    result->children.push_back(sum);

    auto mul = std::make_shared<PhysicalPlanNode>();
    mul->node_type = PhysicalNodeType::MAP;
    mul->base_columns = {{"intermediate", "i10", PlanColumnType::INTEGER}, {"intermediate", "i9", PlanColumnType::INTEGER}};
    mul->result_columns = {{"intermediate", "i11", PlanColumnType::INTEGER}};
    mul->expression.arith_op = PlanArithOp::MUL;

    sum->children.push_back(mul);

    auto mat6 = std::make_shared<PhysicalPlanNode>();
    mat6->node_type = PhysicalNodeType::MATERIALIZE;
    mat6->base_columns = {{"intermediate", "i8", PlanColumnType::INTEGER}, {"intermediate", "i6_o", PlanColumnType::POSLIST}};
    mat6->result_columns = {{"intermediate", "i10", PlanColumnType::INTEGER}};

    auto mat5 = std::make_shared<PhysicalPlanNode>();
    mat5->node_type = PhysicalNodeType::MATERIALIZE;
    mat5->base_columns = {{"intermediate", "i7", PlanColumnType::INTEGER}, {"intermediate", "i6_o", PlanColumnType::POSLIST}};
    mat5->result_columns = {{"intermediate", "i9", PlanColumnType::INTEGER}};

    mul->children.push_back(mat6);
    mul->children.push_back(mat5);

    auto mat4 = std::make_shared<PhysicalPlanNode>();
    mat4->node_type = PhysicalNodeType::MATERIALIZE;
    mat4->base_columns = {{"lineorder", "lo_discount", PlanColumnType::INTEGER}, {"intermediate", "i4", PlanColumnType::POSLIST}};
    mat4->result_columns = {{"intermediate", "i8", PlanColumnType::INTEGER}};

    auto mat3 = std::make_shared<PhysicalPlanNode>();
    mat3->node_type = PhysicalNodeType::MATERIALIZE;
    mat3->base_columns = {{"lineorder", "lo_extendetprice", PlanColumnType::INTEGER}, {"intermediate", "i4", PlanColumnType::POSLIST}};
    mat3->result_columns = {{"intermediate", "i7", PlanColumnType::INTEGER}};

    auto join = std::make_shared<PhysicalPlanNode>();
    join->node_type = PhysicalNodeType::HASHJOIN;
    join->base_columns = {{"intermediate", "i5", PlanColumnType::INTEGER}, {"intermediate", "i2", PlanColumnType::INTEGER}};
    join->result_columns = {{"intermediate", "i6_i", PlanColumnType::POSLIST}, {"intermediate", "i6_o", PlanColumnType::POSLIST}};

    mat6->children.push_back(mat4);
    mat5->children.push_back(join);
    mat5->children.push_back(mat3);

    auto mat2 = std::make_shared<PhysicalPlanNode>();
    mat2->node_type = PhysicalNodeType::MATERIALIZE;
    mat2->base_columns = {{"lineorder", "lo_orderdate", PlanColumnType::INTEGER}, {"intermediate", "i4", PlanColumnType::POSLIST}};
    mat2->result_columns = {{"intermediate", "i5", PlanColumnType::INTEGER}};

    auto set = std::make_shared<PhysicalPlanNode>();
    set->node_type = PhysicalNodeType::SETOPERATION;
    set->base_columns = {{"intermediate", "i2", PlanColumnType::POSLIST}, {"intermediate", "i3", PlanColumnType::POSLIST}};
    set->result_columns = {{"intermediate", "i4", PlanColumnType::POSLIST}};
    set->expression.logical_rel_op = PlanLogicalRelOp::INTERSECTION;

    mat2->children.push_back(set);
    mat4->children.push_back(set);
    mat3->children.push_back(set);

    auto filter3 = std::make_shared<PhysicalPlanNode>();
    filter3->node_type = PhysicalNodeType::FILTER;
    filter3->base_columns = {{"dates", "d_year", PlanColumnType::INTEGER}};
    filter3->result_columns = {{"intermediate", "i3", PlanColumnType::POSLIST}};
    filter3->expression.comp_type = PlanCompType::BETWEEN;
    filter3->expression.values = {"1", "3"};

    auto filter2 = std::make_shared<PhysicalPlanNode>();
    filter2->node_type = PhysicalNodeType::FILTER;
    filter2->base_columns = {{"lineorder", "lo_quantity", PlanColumnType::INTEGER}};
    filter2->result_columns = {{"intermediate", "i2", PlanColumnType::POSLIST}};
    filter2->expression.comp_type = PlanCompType::LT;
    filter2->expression.values = {"25"};

    set->children.push_back(filter2);
    set->children.push_back(filter3);

    auto mat1 = std::make_shared<PhysicalPlanNode>();
    mat1->node_type = PhysicalNodeType::MATERIALIZE;
    mat1->base_columns = {{"dates", "d_datekey", PlanColumnType::INTEGER}, {"intermediate", "i1", PlanColumnType::POSLIST}};
    mat1->result_columns = {{"intermediate", "i2", PlanColumnType::INTEGER}};

    join->children.push_back(mat1);
    join->children.push_back(mat2);

    auto filter1 = std::make_shared<PhysicalPlanNode>();
    filter1->node_type = PhysicalNodeType::FILTER;
    filter1->base_columns = {{"dates", "d_year", PlanColumnType::INTEGER}};
    filter1->result_columns = {{"intermediate", "i1", PlanColumnType::POSLIST}};
    filter1->expression.comp_type = PlanCompType::GT;
    filter1->expression.values = {"1993"};

    mat1->children.push_back(filter1);

    return result;
}

// Physical plan for multi-join query with filters (3 joins: lineorder-dates-part-supplier)
// Query: SELECT SUM(lo_revenue) FROM lineorder JOIN dates JOIN part JOIN supplier
//        WHERE p_category = 'MFGR#12' AND s_region = 'AMERICA'
inline std::shared_ptr<PhysicalPlanNode> physicalPlanMultiJoinExample() {
    // Node 0: AGGREGATE (SUM)
    auto agg = std::make_shared<PhysicalPlanNode>();
    agg->node_type = PhysicalNodeType::AGGREGATE;
    agg->base_columns = {{"intermediate", "i10", PlanColumnType::INTEGER}};
    agg->result_columns = {{"result", "sum", PlanColumnType::INTEGER}};
    agg->expression.agg_specs = {AggSpec{PlanAggFunc::SUM, std::nullopt, std::nullopt, false}};

    // Node 1: MATERIALIZE lo_revenue with i9_o (outer poslist from join3)
    auto mat_revenue = std::make_shared<PhysicalPlanNode>();
    mat_revenue->node_type = PhysicalNodeType::MATERIALIZE;
    mat_revenue->base_columns = {{"lineorder", "lo_revenue", PlanColumnType::INTEGER}, 
                                  {"intermediate", "i9_o", PlanColumnType::POSLIST}};
    mat_revenue->result_columns = {{"intermediate", "i10", PlanColumnType::INTEGER}};

    agg->children.push_back(mat_revenue);

    // Node 2: HASHJOIN (join3: lineorder-supplier)
    auto join3 = std::make_shared<PhysicalPlanNode>();
    join3->node_type = PhysicalNodeType::HASHJOIN;
    join3->base_columns = {{"intermediate", "i8", PlanColumnType::INTEGER}, 
                           {"intermediate", "i7", PlanColumnType::INTEGER}};
    join3->result_columns = {{"intermediate", "i9_i", PlanColumnType::POSLIST}, 
                             {"intermediate", "i9_o", PlanColumnType::POSLIST}};

    mat_revenue->children.push_back(join3);

    // Node 3: MATERIALIZE s_suppkey with i6 (filter poslist)
    auto mat_ssuppkey = std::make_shared<PhysicalPlanNode>();
    mat_ssuppkey->node_type = PhysicalNodeType::MATERIALIZE;
    mat_ssuppkey->base_columns = {{"supplier", "s_suppkey", PlanColumnType::INTEGER}, 
                                   {"intermediate", "i6", PlanColumnType::POSLIST}};
    mat_ssuppkey->result_columns = {{"intermediate", "i8", PlanColumnType::INTEGER}};

    join3->children.push_back(mat_ssuppkey);

    // Node 4: FILTER on s_region = 'AMERICA'
    auto filter_supplier = std::make_shared<PhysicalPlanNode>();
    filter_supplier->node_type = PhysicalNodeType::FILTER;
    filter_supplier->base_columns = {{"supplier", "s_region", PlanColumnType::STRING}};
    filter_supplier->result_columns = {{"intermediate", "i6", PlanColumnType::POSLIST}};
    filter_supplier->expression.comp_type = PlanCompType::EQ;
    filter_supplier->expression.values = {"AMERICA"};

    mat_ssuppkey->children.push_back(filter_supplier);

    // Node 5: MATERIALIZE lo_suppkey with i5_o (outer poslist from join2)
    auto mat_losuppkey = std::make_shared<PhysicalPlanNode>();
    mat_losuppkey->node_type = PhysicalNodeType::MATERIALIZE;
    mat_losuppkey->base_columns = {{"lineorder", "lo_suppkey", PlanColumnType::INTEGER}, 
                                    {"intermediate", "i5_o", PlanColumnType::POSLIST}};
    mat_losuppkey->result_columns = {{"intermediate", "i7", PlanColumnType::INTEGER}};

    join3->children.push_back(mat_losuppkey);

    // Node 6: HASHJOIN (join2: lineorder-part)
    auto join2 = std::make_shared<PhysicalPlanNode>();
    join2->node_type = PhysicalNodeType::HASHJOIN;
    join2->base_columns = {{"intermediate", "i4", PlanColumnType::INTEGER}, 
                           {"intermediate", "i3", PlanColumnType::INTEGER}};
    join2->result_columns = {{"intermediate", "i5_i", PlanColumnType::POSLIST}, 
                             {"intermediate", "i5_o", PlanColumnType::POSLIST}};

    mat_losuppkey->children.push_back(join2);

    // Node 7: MATERIALIZE p_partkey with i2 (filter poslist)
    auto mat_ppartkey = std::make_shared<PhysicalPlanNode>();
    mat_ppartkey->node_type = PhysicalNodeType::MATERIALIZE;
    mat_ppartkey->base_columns = {{"part", "p_partkey", PlanColumnType::INTEGER}, 
                                   {"intermediate", "i2", PlanColumnType::POSLIST}};
    mat_ppartkey->result_columns = {{"intermediate", "i4", PlanColumnType::INTEGER}};

    join2->children.push_back(mat_ppartkey);

    // Node 8: FILTER on p_category = 'MFGR#12'
    auto filter_part = std::make_shared<PhysicalPlanNode>();
    filter_part->node_type = PhysicalNodeType::FILTER;
    filter_part->base_columns = {{"part", "p_category", PlanColumnType::STRING}};
    filter_part->result_columns = {{"intermediate", "i2", PlanColumnType::POSLIST}};
    filter_part->expression.comp_type = PlanCompType::EQ;
    filter_part->expression.values = {"MFGR#12"};

    mat_ppartkey->children.push_back(filter_part);

    // Node 9: MATERIALIZE lo_partkey with i1_o (outer poslist from join1)
    auto mat_lopartkey = std::make_shared<PhysicalPlanNode>();
    mat_lopartkey->node_type = PhysicalNodeType::MATERIALIZE;
    mat_lopartkey->base_columns = {{"lineorder", "lo_partkey", PlanColumnType::INTEGER}, 
                                    {"intermediate", "i1_o", PlanColumnType::POSLIST}};
    mat_lopartkey->result_columns = {{"intermediate", "i3", PlanColumnType::INTEGER}};

    join2->children.push_back(mat_lopartkey);

    // Node 10: HASHJOIN (join1: lineorder-dates)
    auto join1 = std::make_shared<PhysicalPlanNode>();
    join1->node_type = PhysicalNodeType::HASHJOIN;
    join1->base_columns = {{"dates", "d_datekey", PlanColumnType::INTEGER}, 
                           {"lineorder", "lo_orderdate", PlanColumnType::INTEGER}};
    join1->result_columns = {{"intermediate", "i1_i", PlanColumnType::POSLIST}, 
                             {"intermediate", "i1_o", PlanColumnType::POSLIST}};

    mat_lopartkey->children.push_back(join1);

    return agg;
}

#endif // PHYSICAL_PLAN_STRUCTS_HPP
