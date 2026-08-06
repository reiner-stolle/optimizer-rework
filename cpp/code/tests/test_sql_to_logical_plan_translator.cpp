#include <gtest/gtest.h>
#include "SQLToLogicalPlanTranslator.hpp"
#include "LogicalPlanNode.hpp"

/*
 * Logical plan for SimpleSelect:
 *
 *   PROJECTION(d_day)
 *     └─ FILTER(d_year > 1993)
 *          └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, SimpleSelect)
{
    std::string query = R"(
        SELECT d_day
        FROM dates
        WHERE d_year > 1993;
    )";
    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(d_day)
    const auto &root = plans[0];
    EXPECT_EQ(root->node_type, LogicalNodeType::PROJECTION);
    ASSERT_FALSE(root->projected_columns.empty());
    EXPECT_EQ(root->projected_columns[0].column_name, "d_day");
    ASSERT_EQ(root->children.size(), 1);

    //  └─ FILTER(d_year > 1993)
    const auto &filter = root->children[0];
    EXPECT_EQ(filter->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(filter->expression.comp_type.has_value());
    EXPECT_EQ(*filter->expression.comp_type, PlanCompType::GT);
    ASSERT_EQ(filter->base_columns.size(), 1);
    EXPECT_EQ(filter->base_columns[0].column_name, "d_year");
    ASSERT_EQ(filter->expression.values.size(), 1);
    EXPECT_EQ(filter->expression.values[0], "1993");
    ASSERT_EQ(filter->children.size(), 1);

    //     └─ SCAN(dates)
    const auto &scan = filter->children[0];
    EXPECT_EQ(scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan->base_table, "dates");
    ASSERT_TRUE(scan->children.empty());
}

/*
 * Logical plan for:
 *
 *   PROJECTION(name)
 *     └─ FILTER(name LIKE '%ni%')
 *          └─ SCAN(badge)
 */
TEST(SQLToPlanTranslatorTest, LowerLikePredicateSetsStringOp)
{
    std::string query = R"(
        SELECT b.name
        FROM badge AS b
        WHERE LOWER(b.name) LIKE LOWER('%ni%');
    )";
    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    const auto &proj = plans[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1);
    EXPECT_EQ(proj->projected_columns[0].column_name, "name");
    EXPECT_EQ(proj->projected_columns[0].table_name, "b"); // alias preserved
    ASSERT_EQ(proj->children.size(), 1);

    const auto &filter = proj->children[0];
    EXPECT_EQ(filter->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(filter->expression.comp_type.has_value());
    EXPECT_EQ(*filter->expression.comp_type, PlanCompType::LIKE);
    ASSERT_TRUE(filter->expression.string_op.has_value());
    EXPECT_EQ(*filter->expression.string_op, PlanStringOp::LOWER);
    ASSERT_EQ(filter->base_columns.size(), 1);
    EXPECT_EQ(filter->base_columns[0].column_name, "name");
    EXPECT_EQ(filter->base_columns[0].table_name, "b");
    ASSERT_EQ(filter->expression.values.size(), 1);
    EXPECT_EQ(filter->expression.values[0], "%ni%");
    ASSERT_EQ(filter->children.size(), 1);

    const auto &scan = filter->children[0];
    EXPECT_EQ(scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan->base_table, "b");
    ASSERT_TRUE(scan->children.empty());
}

/*
 * Logical plan for:
 *
 *   PROJECTION(name)
 *     └─ FILTER(name LIKE '%NI%') [string_op=UPPER]
 *          └─ SCAN(badge)
 */
TEST(SQLToPlanTranslatorTest, UpperLikePredicateSetsStringOp)
{
    std::string query = R"(
        SELECT b.name
        FROM badge AS b
        WHERE UPPER(b.name) LIKE UPPER('%Ni%');
    )";
    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    const auto &proj = plans[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1);
    EXPECT_EQ(proj->projected_columns[0].column_name, "name");
    EXPECT_EQ(proj->projected_columns[0].table_name, "b");
    ASSERT_EQ(proj->children.size(), 1);

    const auto &filter = proj->children[0];
    EXPECT_EQ(filter->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(filter->expression.comp_type.has_value());
    EXPECT_EQ(*filter->expression.comp_type, PlanCompType::LIKE);
    ASSERT_TRUE(filter->expression.string_op.has_value());
    EXPECT_EQ(*filter->expression.string_op, PlanStringOp::UPPER);
    ASSERT_EQ(filter->base_columns.size(), 1);
    EXPECT_EQ(filter->base_columns[0].column_name, "name");
    EXPECT_EQ(filter->base_columns[0].table_name, "b");
    ASSERT_EQ(filter->expression.values.size(), 1);
    EXPECT_EQ(filter->expression.values[0], "%Ni%");
    ASSERT_EQ(filter->children.size(), 1);

    const auto &scan = filter->children[0];
    EXPECT_EQ(scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan->base_table, "b");
    ASSERT_TRUE(scan->children.empty());
}

/*
 * Logical plan for:
 *
 *   LIMIT(all, offset=5)
 *     └─ PROJECTION(badge.name)
 *          └─ SCAN(badge)
 */
TEST(SQLToPlanTranslatorTest, OffsetOnlyCreatesLimitNode)
{
    std::string query = R"(
        SELECT b.name
        FROM badge AS b
        OFFSET 5;
    )";
    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);

    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "name");
    EXPECT_EQ(proj->projected_columns[0].table_name, "b"); // alias preserved
    // LIMIT offset=5
    ASSERT_FALSE(proj->expression.limit_count.has_value());
    ASSERT_TRUE(proj->expression.limit_offset.has_value());
    EXPECT_EQ(proj->expression.limit_offset.value(), 5);
    ASSERT_EQ(proj->children.size(), 1u);

    const auto &scan = proj->children[0];
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan->base_table, "b"); // alias preserved
    ASSERT_TRUE(scan->children.empty());
}

/*
 * Logical plan for:
 *
 *   LIMIT(5, offset=5)
 *     └─ PROJECTION(badge.name)
 *          └─ SCAN(badge)
 */
TEST(SQLToPlanTranslatorTest, LimitAndOffsetCreateLimitNode)
{
    std::string query = R"(
        SELECT b.name
        FROM badge AS b
        LIMIT 5 OFFSET 5;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);

    const auto &root = plans[0];
    ASSERT_NE(root, nullptr);

    ASSERT_EQ(root->children.size(), 1u);
    ASSERT_TRUE(root->expression.limit_count.has_value());
    EXPECT_EQ(root->expression.limit_count.value(), 5);
    ASSERT_TRUE(root->expression.limit_offset.has_value());
    EXPECT_EQ(root->expression.limit_offset.value(), 5);

    const auto &proj = root;
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);

    ASSERT_EQ(proj->projected_columns.size(), 1u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "name");
    EXPECT_EQ(proj->projected_columns[0].table_name, "b"); // alias preserved

    ASSERT_EQ(proj->children.size(), 1u);

    const auto &scan = proj->children[0];
    ASSERT_NE(scan, nullptr);
    EXPECT_EQ(scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan->base_table, "b");
    ASSERT_TRUE(scan->children.empty());
}

/*
 * Logical plan for:
 *
 *   PROJECTION(lo_orderkey)
 *     └─ SETOPERATION(UNION)
 *         ├─ FILTER(lo_quantity < 25)
 *         │    └─ SCAN(lineorder)
 *         └─ FILTER(lo_quantity < 25)
 *              └─ SCAN(lineorder)
 *
 * Note: Although both branches are syntactically identical, the OR is still represented as a UNION. This may be optimized later.
 */
TEST(SQLToPlanTranslatorTest, OrRequiresSetSemanticsNotUnionAll)
{
    std::string query = R"(
        SELECT lo_orderkey
        FROM lineorder
        WHERE lo_quantity < 25 OR lo_quantity < 25;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(lo_orderkey)
    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1);
    EXPECT_EQ(proj->projected_columns[0].column_name, "lo_orderkey");
    ASSERT_EQ(proj->children.size(), 1);

    //  └─ SETOPERATION(UNION)
    const auto &u = proj->children[0];
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->node_type, LogicalNodeType::SETOPERATION);
    ASSERT_TRUE(u->expression.logical_rel_op.has_value());
    EXPECT_EQ(*u->expression.logical_rel_op, PlanLogicalRelOp::UNION);
    ASSERT_EQ(u->children.size(), 2);

    // Child 0: FILTER(lo_quantity < 25) -> SCAN(lineorder)
    const auto &f0 = u->children[0];
    ASSERT_NE(f0, nullptr);
    EXPECT_EQ(f0->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f0->expression.comp_type.has_value());
    EXPECT_EQ(*f0->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f0->base_columns.size(), 1);
    EXPECT_EQ(f0->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f0->expression.values.size(), 1);
    EXPECT_EQ(f0->expression.values[0], "25");
    ASSERT_EQ(f0->children.size(), 1);

    const auto &s0 = f0->children[0];
    ASSERT_NE(s0, nullptr);
    EXPECT_EQ(s0->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(s0->base_table, "lineorder");
    ASSERT_TRUE(s0->children.empty());

    // Child 1: FILTER(lo_quantity < 25) -> SCAN(lineorder)
    const auto &f1 = u->children[1];
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(f1->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f1->expression.comp_type.has_value());
    EXPECT_EQ(*f1->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f1->base_columns.size(), 1);
    EXPECT_EQ(f1->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f1->expression.values.size(), 1);
    EXPECT_EQ(f1->expression.values[0], "25");
    ASSERT_EQ(f1->children.size(), 1);

    const auto &s1 = f1->children[0];
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(s1->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(s1->base_table, "lineorder");
    ASSERT_TRUE(s1->children.empty());
}

/*
 * Logical plan for:
 *
 *   PROJECTION(lo_orderkey)
 *     └─ SETOPERATION(UNION)
 *         ├─ FILTER(lo_discount BETWEEN 1 AND 3)
 *         │    └─ FILTER(lo_shipmode = 'AIR')
 *         │         └─ SCAN(lineorder)
 *         └─ FILTER(lo_quantity < 25)
 *              └─ FILTER(lo_shipmode = 'AIR')
 *                   └─ SCAN(lineorder)
 */
TEST(SQLToPlanTranslatorTest, AndPredicateIsPushedIntoBothUnionBranches)
{
    std::string query = R"(
        SELECT lo_orderkey
        FROM lineorder
        WHERE lo_shipmode = 'AIR'
          AND (lo_discount BETWEEN 1 AND 3 OR lo_quantity < 25);
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(lo_orderkey)
    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1);
    EXPECT_EQ(proj->projected_columns[0].column_name, "lo_orderkey");
    ASSERT_EQ(proj->children.size(), 1);

    //  └─ SETOPERATION(UNION)
    const auto &u = proj->children[0];
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->node_type, LogicalNodeType::SETOPERATION);
    ASSERT_TRUE(u->expression.logical_rel_op.has_value());
    EXPECT_EQ(*u->expression.logical_rel_op, PlanLogicalRelOp::UNION);
    ASSERT_EQ(u->children.size(), 2);

    // ------------------------
    // Branch 0:
    //   FILTER(lo_discount BETWEEN 1 AND 3)
    //     └─ FILTER(lo_shipmode = 'AIR')
    //          └─ SCAN(lineorder)
    // ------------------------
    const auto &b0_top = u->children[0];
    ASSERT_NE(b0_top, nullptr);
    EXPECT_EQ(b0_top->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(b0_top->expression.comp_type.has_value());
    EXPECT_EQ(*b0_top->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(b0_top->base_columns.size(), 1);
    EXPECT_EQ(b0_top->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(b0_top->expression.values.size(), 2);
    EXPECT_EQ(b0_top->expression.values[0], "1");
    EXPECT_EQ(b0_top->expression.values[1], "3");
    ASSERT_EQ(b0_top->children.size(), 1);

    const auto &b0_ship = b0_top->children[0];
    ASSERT_NE(b0_ship, nullptr);
    EXPECT_EQ(b0_ship->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(b0_ship->expression.comp_type.has_value());
    EXPECT_EQ(*b0_ship->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(b0_ship->base_columns.size(), 1);
    EXPECT_EQ(b0_ship->base_columns[0].column_name, "lo_shipmode");
    ASSERT_EQ(b0_ship->expression.values.size(), 1);
    EXPECT_EQ(b0_ship->expression.values[0], "AIR");
    ASSERT_EQ(b0_ship->children.size(), 1);

    const auto &b0_scan = b0_ship->children[0];
    ASSERT_NE(b0_scan, nullptr);
    EXPECT_EQ(b0_scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(b0_scan->base_table, "lineorder");
    ASSERT_TRUE(b0_scan->children.empty());

    // ------------------------
    // Branch 1:
    //   FILTER(lo_quantity < 25)
    //     └─ FILTER(lo_shipmode = 'AIR')
    //          └─ SCAN(lineorder)
    // ------------------------
    const auto &b1_top = u->children[1];
    ASSERT_NE(b1_top, nullptr);
    EXPECT_EQ(b1_top->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(b1_top->expression.comp_type.has_value());
    EXPECT_EQ(*b1_top->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(b1_top->base_columns.size(), 1);
    EXPECT_EQ(b1_top->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(b1_top->expression.values.size(), 1);
    EXPECT_EQ(b1_top->expression.values[0], "25");
    ASSERT_EQ(b1_top->children.size(), 1);

    const auto &b1_ship = b1_top->children[0];
    ASSERT_NE(b1_ship, nullptr);
    EXPECT_EQ(b1_ship->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(b1_ship->expression.comp_type.has_value());
    EXPECT_EQ(*b1_ship->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(b1_ship->base_columns.size(), 1);
    EXPECT_EQ(b1_ship->base_columns[0].column_name, "lo_shipmode");
    ASSERT_EQ(b1_ship->expression.values.size(), 1);
    EXPECT_EQ(b1_ship->expression.values[0], "AIR");
    ASSERT_EQ(b1_ship->children.size(), 1);

    const auto &b1_scan = b1_ship->children[0];
    ASSERT_NE(b1_scan, nullptr);
    EXPECT_EQ(b1_scan->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(b1_scan->base_table, "lineorder");
    ASSERT_TRUE(b1_scan->children.empty());
}

/*
 * Logical plan for:
 *
 *   PROJECTION(lo_orderkey)
 *     └─ SETOPERATION(UNION)
 *         ├─ FILTER(lo_discount BETWEEN 1 AND 3) → SCAN(lineorder)
 *         ├─ FILTER(lo_quantity < 25)            → SCAN(lineorder)
 *         └─ FILTER(lo_tax > 0)                  → SCAN(lineorder)
 */
TEST(SQLToPlanTranslatorTest, MultipleOrBecomesUnionChainOrNaryUnion)
{
    std::string query = R"(
        SELECT lo_orderkey
        FROM lineorder
        WHERE lo_discount BETWEEN 1 AND 3
           OR lo_quantity < 25
           OR lo_tax > 0;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(lo_orderkey)
    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "lo_orderkey");
    ASSERT_EQ(proj->children.size(), 1u);

    //  └─ SETOPERATION(UNION) with 3 children
    const auto &u = proj->children[0];
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(u->node_type, LogicalNodeType::SETOPERATION);
    ASSERT_TRUE(u->expression.logical_rel_op.has_value());
    EXPECT_EQ(*u->expression.logical_rel_op, PlanLogicalRelOp::UNION);
    ASSERT_EQ(u->children.size(), 3u);

    // Child 0: FILTER(lo_discount BETWEEN 1 AND 3) → SCAN(lineorder)
    const auto &f0 = u->children[0];
    ASSERT_NE(f0, nullptr);
    EXPECT_EQ(f0->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f0->expression.comp_type.has_value());
    EXPECT_EQ(*f0->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f0->base_columns.size(), 1u);
    EXPECT_EQ(f0->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(f0->expression.values.size(), 2u);
    EXPECT_EQ(f0->expression.values[0], "1");
    EXPECT_EQ(f0->expression.values[1], "3");
    ASSERT_EQ(f0->children.size(), 1u);

    const auto &s0 = f0->children[0];
    ASSERT_NE(s0, nullptr);
    EXPECT_EQ(s0->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(s0->base_table, "lineorder");

    // Child 1: FILTER(lo_quantity < 25) → SCAN(lineorder)
    const auto &f1 = u->children[1];
    ASSERT_NE(f1, nullptr);
    EXPECT_EQ(f1->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f1->expression.comp_type.has_value());
    EXPECT_EQ(*f1->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f1->base_columns.size(), 1u);
    EXPECT_EQ(f1->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f1->expression.values.size(), 1u);
    EXPECT_EQ(f1->expression.values[0], "25");
    ASSERT_EQ(f1->children.size(), 1u);

    const auto &s1 = f1->children[0];
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(s1->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(s1->base_table, "lineorder");

    // Child 2: FILTER(lo_tax > 0) → SCAN(lineorder)
    const auto &f2 = u->children[2];
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f2->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f2->expression.comp_type.has_value());
    EXPECT_EQ(*f2->expression.comp_type, PlanCompType::GT);
    ASSERT_EQ(f2->base_columns.size(), 1u);
    EXPECT_EQ(f2->base_columns[0].column_name, "lo_tax");
    ASSERT_EQ(f2->expression.values.size(), 1u);
    EXPECT_EQ(f2->expression.values[0], "0");
    ASSERT_EQ(f2->children.size(), 1u);

    const auto &s2 = f2->children[0];
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(s2->base_table, "lineorder");
}

/*
 * Logical plan for:
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)                         -- result alias: REVENUE
 *         └─ MAP(MUL: lo_extendedprice * lo_discount)
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ LEFT  branch (lineorder side):
 *                 │     SETOPERATION(UNION)
 *                 │       ├─ FILTER(lo_discount BETWEEN 1 AND 3) → SCAN(lineorder)
 *                 │       └─ FILTER(lo_quantity < 25)            → SCAN(lineorder)
 *                 └─ RIGHT branch (dates side):
 *                       FILTER(d_year = 1993) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, OrDoesNotLeakPredicatesAcrossJoinSides)
{
    std::string query = R"(
        SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
        FROM lineorder, dates
        WHERE lo_orderdate = d_datekey
          AND d_year = 1993
          AND (lo_discount BETWEEN 1 AND 3 OR lo_quantity < 25);
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(REVENUE)
    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "REVENUE");
    ASSERT_EQ(proj->children.size(), 1u);

    //  └─ AGGREGATE(SUM) alias REVENUE
    const auto &agg = proj->children[0];
    ASSERT_NE(agg, nullptr);
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_EQ(agg->children.size(), 1u);

    //      └─ MAP(MUL: lo_extendedprice * lo_discount)
    const auto &map = agg->children[0];
    ASSERT_NE(map, nullptr);
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::MUL);
    ASSERT_EQ(map->base_columns.size(), 2u);
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_discount");
    ASSERT_EQ(map->children.size(), 1u);

    //          └─ JOIN(lo_orderdate = d_datekey)
    const auto &join = map->children[0];
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join->expression.comp_type.has_value());
    EXPECT_EQ(*join->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join->base_columns.size(), 2u);
    EXPECT_EQ(join->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join->children.size(), 2u);

    // LEFT: SETOPERATION(UNION)
    const auto &left = join->children[0];
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->node_type, LogicalNodeType::SETOPERATION);
    ASSERT_TRUE(left->expression.logical_rel_op.has_value());
    EXPECT_EQ(*left->expression.logical_rel_op, PlanLogicalRelOp::UNION);
    ASSERT_EQ(left->children.size(), 2u);

    // LEFT child 0: FILTER(lo_discount BETWEEN 1 AND 3) → SCAN(lineorder)
    const auto &f_between = left->children[0];
    ASSERT_NE(f_between, nullptr);
    EXPECT_EQ(f_between->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_between->expression.comp_type.has_value());
    EXPECT_EQ(*f_between->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_between->base_columns.size(), 1u);
    EXPECT_EQ(f_between->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(f_between->expression.values.size(), 2u);
    EXPECT_EQ(f_between->expression.values[0], "1");
    EXPECT_EQ(f_between->expression.values[1], "3");
    ASSERT_EQ(f_between->children.size(), 1u);

    const auto &scan_lo_0 = f_between->children[0];
    ASSERT_NE(scan_lo_0, nullptr);
    EXPECT_EQ(scan_lo_0->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo_0->base_table, "lineorder");

    // LEFT child 1: FILTER(lo_quantity < 25) → SCAN(lineorder)
    const auto &f_lt = left->children[1];
    ASSERT_NE(f_lt, nullptr);
    EXPECT_EQ(f_lt->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_lt->expression.comp_type.has_value());
    EXPECT_EQ(*f_lt->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f_lt->base_columns.size(), 1u);
    EXPECT_EQ(f_lt->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f_lt->expression.values.size(), 1u);
    EXPECT_EQ(f_lt->expression.values[0], "25");
    ASSERT_EQ(f_lt->children.size(), 1u);

    const auto &scan_lo_1 = f_lt->children[0];
    ASSERT_NE(scan_lo_1, nullptr);
    EXPECT_EQ(scan_lo_1->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo_1->base_table, "lineorder");

    // RIGHT: FILTER(d_year = 1993) → SCAN(dates)
    const auto &right = join->children[1];
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(right->expression.comp_type.has_value());
    EXPECT_EQ(*right->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(right->base_columns.size(), 1u);
    EXPECT_EQ(right->base_columns[0].column_name, "d_year");
    ASSERT_EQ(right->expression.values.size(), 1u);
    EXPECT_EQ(right->expression.values[0], "1993");
    ASSERT_EQ(right->children.size(), 1u);

    const auto &scan_dates = right->children[0];
    ASSERT_NE(scan_dates, nullptr);
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for:
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *         └─ MAP(MUL: lo_extendedprice * lo_discount)
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ FILTER(d_year = 1993) → SCAN(dates)
 *                 └─ SETOPERATION(UNION)
 *                     ├─ FILTER(lo_discount BETWEEN 1 AND 3) → SCAN(lineorder)
 *                     └─ FILTER(lo_quantity < 25)            → SCAN(lineorder)
 */
TEST(SQLToPlanTranslatorTest, OrIsRepresentedAsUnionSetOperationNode)
{
    std::string query = R"(
        SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
        FROM lineorder, dates
        WHERE lo_orderdate = d_datekey
          AND d_year = 1993
          AND (
                lo_discount BETWEEN 1 AND 3
                OR lo_quantity < 25
              );
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    const auto &proj = plans[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 1);
    EXPECT_EQ(proj->projected_columns[0].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[0].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_EQ(agg->children.size(), 1);

    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::MUL);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_discount");
    ASSERT_EQ(map->children.size(), 1);

    const auto &join = map->children[0];
    EXPECT_EQ(join->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join->expression.comp_type.has_value());
    EXPECT_EQ(*join->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join->base_columns.size(), 2);
    EXPECT_EQ(join->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join->children.size(), 2);

    // Left: SETOPERATION(UNION) with two FILTER children (each over lineorder)
    const auto &u_or = join->children[0];
    EXPECT_EQ(u_or->node_type, LogicalNodeType::SETOPERATION);
    ASSERT_TRUE(u_or->expression.logical_rel_op.has_value());
    EXPECT_EQ(*u_or->expression.logical_rel_op, PlanLogicalRelOp::UNION);
    ASSERT_EQ(u_or->children.size(), 2);

    // Child 0: lo_discount BETWEEN 1 AND 3 -> SCAN(lineorder)
    const auto &f_between = u_or->children[0];
    EXPECT_EQ(f_between->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_between->expression.comp_type.has_value());
    EXPECT_EQ(*f_between->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_between->base_columns.size(), 1);
    EXPECT_EQ(f_between->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(f_between->expression.values.size(), 2);
    EXPECT_EQ(f_between->expression.values[0], "1");
    EXPECT_EQ(f_between->expression.values[1], "3");
    ASSERT_EQ(f_between->children.size(), 1);

    const auto &scan_lo_0 = f_between->children[0];
    EXPECT_EQ(scan_lo_0->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo_0->base_table, "lineorder");

    // Child 1: lo_quantity < 25 -> SCAN(lineorder)
    const auto &f_lt = u_or->children[1];
    EXPECT_EQ(f_lt->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_lt->expression.comp_type.has_value());
    EXPECT_EQ(*f_lt->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f_lt->base_columns.size(), 1);
    EXPECT_EQ(f_lt->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f_lt->expression.values.size(), 1);
    EXPECT_EQ(f_lt->expression.values[0], "25");
    ASSERT_EQ(f_lt->children.size(), 1);

    const auto &scan_lo_1 = f_lt->children[0];
    EXPECT_EQ(scan_lo_1->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo_1->base_table, "lineorder");

    // Right: FILTER(d_year = 1993) -> SCAN(dates)
    const auto &f_year = join->children[1];
    EXPECT_EQ(f_year->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year->expression.comp_type.has_value());
    EXPECT_EQ(*f_year->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_year->base_columns.size(), 1);
    EXPECT_EQ(f_year->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year->expression.values.size(), 1);
    EXPECT_EQ(f_year->expression.values[0], "1993");
    ASSERT_EQ(f_year->children.size(), 1);

    const auto &scan_dates = f_year->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q1_1:
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *          └─ MAP( lo_extendedprice * lo_discount )
 *               └─ JOIN( lo_orderdate = d_datekey )
 *                    ├─ LEFT  branch (lineorder side):
 *                    │     FILTER(lo_quantity < 25)
 *                    │       └─ FILTER(lo_discount BETWEEN 1 AND 3)
 *                    │             └─ SCAN(lineorder)
 *                    └─ RIGHT branch (dates side):
 *                          FILTER(d_year = 1993)
 *                             └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q1_1)
{
    std::string query = R"(
        SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
        FROM lineorder, dates
        WHERE lo_orderdate = d_datekey
            AND d_year = 1993
            AND lo_discount BETWEEN 1 AND 3
            AND lo_quantity < 25;
    )";
    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(REVENUE)
    const auto &root = plans[0];
    EXPECT_EQ(root->node_type, LogicalNodeType::PROJECTION);
    ASSERT_FALSE(root->projected_columns.empty());
    EXPECT_EQ(root->projected_columns[0].column_name, "REVENUE");
    EXPECT_EQ(root->projected_columns[0].type, PlanColumnType::INTEGER);

    //  └─ AGGREGATE(SUM)
    ASSERT_EQ(root->children.size(), 1);
    const auto &agg = root->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_TRUE(agg->projected_columns.empty());
    ASSERT_EQ(agg->children.size(), 1);

    //     └─ MAP(MUL: lo_extendedprice * lo_discount)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::MUL);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_discount");
    ASSERT_EQ(map->children.size(), 1);

    //         └─ JOIN(lo_orderdate = d_datekey)
    const auto &join = map->children[0];
    EXPECT_EQ(join->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join->expression.comp_type.has_value());
    EXPECT_EQ(*join->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join->base_columns.size(), 2);
    EXPECT_EQ(join->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join->children.size(), 2);

    //             LEFT  branch: FILTER(lo_discount BETWEEN 1 AND 3) → FILTER(lo_quantity < 25) → SCAN(lineorder)
    const auto &left = join->children[0];
    EXPECT_EQ(left->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(left->expression.comp_type.has_value());
    EXPECT_EQ(*left->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(left->base_columns.size(), 1);
    EXPECT_EQ(left->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(left->expression.values.size(), 2);
    EXPECT_EQ(left->expression.values[0], "1");
    EXPECT_EQ(left->expression.values[1], "3");
    ASSERT_EQ(left->children.size(), 1);

    const auto &f_qty = left->children[0];
    EXPECT_EQ(f_qty->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qty->expression.comp_type.has_value());
    EXPECT_EQ(*f_qty->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f_qty->base_columns.size(), 1);
    EXPECT_EQ(f_qty->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f_qty->expression.values.size(), 1);
    EXPECT_EQ(f_qty->expression.values[0], "25");
    ASSERT_EQ(f_qty->children.size(), 1);

    const auto &scan_lo = f_qty->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //             RIGHT branch: FILTER(d_year = 1993) → SCAN(dates)
    const auto &right = join->children[1];
    EXPECT_EQ(right->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(right->expression.comp_type.has_value());
    EXPECT_EQ(*right->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(right->base_columns.size(), 1);
    EXPECT_EQ(right->base_columns[0].column_name, "d_year");
    ASSERT_EQ(right->expression.values.size(), 1);
    EXPECT_EQ(right->expression.values[0], "1993");
    ASSERT_EQ(right->children.size(), 1);

    const auto &scan_dates = right->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q1_2:
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *          └─ MAP( lo_extendedprice * lo_discount )
 *               └─ JOIN( lo_orderdate = d_datekey )
 *                    ├─ LEFT  branch (lineorder):
 *                    │     FILTER(lo_discount BETWEEN 4 AND 6)
 *                    │       └─ FILTER(lo_quantity BETWEEN 26 AND 35)
 *                    │             └─ SCAN(lineorder)
 *                    └─ RIGHT branch (dates):
 *                          FILTER(d_yearmonth = 'Jan1994')
 *                             └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q1_2)
{
    std::string query = R"(
        SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
        FROM lineorder, dates
        WHERE
            lo_orderdate = d_datekey
            AND d_yearmonth = 'Jan1994'
            AND lo_discount BETWEEN 4 AND 6
            AND lo_quantity BETWEEN 26 AND 35;
    )";
    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(REVENUE)
    const auto &root = plans[0];
    EXPECT_EQ(root->node_type, LogicalNodeType::PROJECTION);
    ASSERT_FALSE(root->projected_columns.empty());
    EXPECT_EQ(root->projected_columns[0].column_name, "REVENUE");

    //  └─ AGGREGATE(SUM)
    ASSERT_EQ(root->children.size(), 1);
    const auto &agg = root->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_EQ(agg->children.size(), 1);

    //     └─ MAP(MUL: lo_extendedprice * lo_discount)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::MUL);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_discount");
    ASSERT_EQ(map->children.size(), 1);

    //         └─ JOIN(lo_orderdate = d_datekey)
    const auto &join = map->children[0];
    EXPECT_EQ(join->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join->expression.comp_type.has_value());
    EXPECT_EQ(*join->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join->base_columns.size(), 2);
    EXPECT_EQ(join->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join->children.size(), 2);

    //             LEFT branch: FILTER(lo_discount BETWEEN 4 AND 6) → FILTER(lo_quantity BETWEEN 26 AND 35) → SCAN(lineorder)
    const auto &left = join->children[0];
    EXPECT_EQ(left->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(left->expression.comp_type.has_value());
    EXPECT_EQ(*left->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(left->base_columns.size(), 1);
    EXPECT_EQ(left->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(left->expression.values.size(), 2);
    EXPECT_EQ(left->expression.values[0], "4");
    EXPECT_EQ(left->expression.values[1], "6");
    ASSERT_EQ(left->children.size(), 1);

    const auto &f_qty_between = left->children[0];
    EXPECT_EQ(f_qty_between->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qty_between->expression.comp_type.has_value());
    EXPECT_EQ(*f_qty_between->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_qty_between->base_columns.size(), 1);
    EXPECT_EQ(f_qty_between->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f_qty_between->expression.values.size(), 2);
    EXPECT_EQ(f_qty_between->expression.values[0], "26");
    EXPECT_EQ(f_qty_between->expression.values[1], "35");
    ASSERT_EQ(f_qty_between->children.size(), 1);

    const auto &scan_lo = f_qty_between->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //             RIGHT branch: FILTER(d_yearmonth = 'Jan1994') → SCAN(dates)
    const auto &right = join->children[1];
    EXPECT_EQ(right->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(right->expression.comp_type.has_value());
    EXPECT_EQ(*right->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(right->base_columns.size(), 1);
    EXPECT_EQ(right->base_columns[0].column_name, "d_yearmonth");
    ASSERT_EQ(right->expression.values.size(), 1);
    EXPECT_EQ(right->expression.values[0], "Jan1994");
    ASSERT_EQ(right->children.size(), 1);

    const auto &scan_dates = right->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q1_3:
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *          └─ MAP( lo_extendedprice * lo_discount )
 *               └─ JOIN( lo_orderdate = d_datekey )
 *                    ├─ LEFT  branch (lineorder):
 *                    │     FILTER(lo_discount BETWEEN 5 AND 7)
 *                    │       └─ FILTER(lo_quantity BETWEEN 26 AND 35)
 *                    │             └─ SCAN(lineorder)
 *                    └─ RIGHT branch (dates):
 *                          FILTER(d_weeknuminyear = 6)
 *                            └─ FILTER(d_year = 1994)
 *                                  └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q1_3)
{
    std::string query = R"(
        SELECT
            SUM(lo_extendedprice * lo_discount) AS REVENUE
        FROM lineorder, dates
        WHERE
            lo_orderdate = d_datekey
            AND d_weeknuminyear = 6
            AND d_year = 1994
            AND lo_discount BETWEEN 5 AND 7
            AND lo_quantity BETWEEN 26 AND 35;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // PROJECTION(REVENUE)
    const auto &root = plans[0];
    EXPECT_EQ(root->node_type, LogicalNodeType::PROJECTION);
    ASSERT_FALSE(root->projected_columns.empty());
    EXPECT_EQ(root->projected_columns[0].column_name, "REVENUE");

    //  └─ AGGREGATE(SUM)
    ASSERT_EQ(root->children.size(), 1);
    const auto &agg = root->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_EQ(agg->children.size(), 1);

    //     └─ MAP(MUL: lo_extendedprice * lo_discount)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::MUL);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_discount");
    ASSERT_EQ(map->children.size(), 1);

    //         └─ JOIN(lo_orderdate = d_datekey)
    const auto &join = map->children[0];
    EXPECT_EQ(join->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join->expression.comp_type.has_value());
    EXPECT_EQ(*join->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join->base_columns.size(), 2);
    EXPECT_EQ(join->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join->children.size(), 2);

    //             LEFT branch: FILTER(lo_discount BETWEEN 5 AND 7) → FILTER(lo_quantity BETWEEN 26 AND 35) → SCAN(lineorder)
    const auto &left = join->children[0];
    EXPECT_EQ(left->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(left->expression.comp_type.has_value());
    EXPECT_EQ(*left->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(left->base_columns.size(), 1);
    EXPECT_EQ(left->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(left->expression.values.size(), 2);
    EXPECT_EQ(left->expression.values[0], "5");
    EXPECT_EQ(left->expression.values[1], "7");
    ASSERT_EQ(left->children.size(), 1);

    const auto &f_qty_between = left->children[0];
    EXPECT_EQ(f_qty_between->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qty_between->expression.comp_type.has_value());
    EXPECT_EQ(*f_qty_between->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_qty_between->base_columns.size(), 1);
    EXPECT_EQ(f_qty_between->base_columns[0].column_name, "lo_quantity");
    ASSERT_EQ(f_qty_between->expression.values.size(), 2);
    EXPECT_EQ(f_qty_between->expression.values[0], "26");
    EXPECT_EQ(f_qty_between->expression.values[1], "35");
    ASSERT_EQ(f_qty_between->children.size(), 1);

    const auto &scan_lo = f_qty_between->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //             RIGHT branch: FILTER(d_weeknuminyear = 6) → FILTER(d_year = 1994) → SCAN(dates)
    const auto &right = join->children[1];
    EXPECT_EQ(right->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(right->expression.comp_type.has_value());
    EXPECT_EQ(*right->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(right->base_columns.size(), 1);
    EXPECT_EQ(right->base_columns[0].column_name, "d_weeknuminyear");
    ASSERT_EQ(right->expression.values.size(), 1);
    EXPECT_EQ(right->expression.values[0], "6");
    ASSERT_EQ(right->children.size(), 1);

    const auto &f_year = right->children[0];
    EXPECT_EQ(f_year->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year->expression.comp_type.has_value());
    EXPECT_EQ(*f_year->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_year->base_columns.size(), 1);
    EXPECT_EQ(f_year->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year->expression.values.size(), 1);
    EXPECT_EQ(f_year->expression.values[0], "1994");
    ASSERT_EQ(f_year->children.size(), 1);

    const auto &scan_dates = f_year->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q2_1:
 *
 *   SORT(p_brand)
 *     └─ PROJECTION(d_year, p_brand, lo_revenue)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
 *               └─ JOIN(lo_suppkey = s_suppkey)                  -- top join
 *                    ├─ JOIN(lo_partkey = p_partkey)
 *                    │     ├─ JOIN(lo_orderdate = d_datekey)
 *                    │     │     ├─ SCAN(lineorder)
 *                    │     │     └─ SCAN(dates)
 *                    │     └─ FILTER(p_category = 'MFGR#12')
 *                    │           └─ SCAN(part)
 *                    └─ FILTER(s_region = 'AMERICA')
 *                          └─ SCAN(supplier)
 */
TEST(SQLToPlanTranslatorTest, Q2_1)
{
    std::string query = R"(
        SELECT SUM(lo_revenue), d_year, p_brand
        FROM lineorder, dates, part, supplier
        WHERE
            lo_orderdate = d_datekey
            AND lo_partkey = p_partkey
            AND lo_suppkey = s_suppkey
            AND p_category = 'MFGR#12'
            AND s_region = 'AMERICA'
        GROUP BY d_year, p_brand
        ORDER BY p_brand;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(p_brand)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 1);
    EXPECT_EQ(sort->base_columns[0].column_name, "p_brand");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 1);
    EXPECT_TRUE(dirs[0]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(d_year, p_brand, lo_revenue)
    const auto &root = sort->children[0];
    EXPECT_EQ(root->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(root->projected_columns.size(), 3);
    EXPECT_EQ(root->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(root->projected_columns[1].column_name, "p_brand");
    EXPECT_EQ(root->projected_columns[2].column_name, "sum1");
    EXPECT_FALSE(root->projected_columns[2].alias.has_value());
    EXPECT_EQ(root->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(root->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
    const auto &agg = root->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_EQ(agg->base_columns.size(), 2);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "p_brand");
    ASSERT_TRUE(agg->expression.agg_specs[0].input.has_value());
    EXPECT_EQ(agg->expression.agg_specs[0].input->table_name, "lineorder");
    EXPECT_EQ(agg->expression.agg_specs[0].input->column_name, "lo_revenue");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ JOIN(lo_suppkey = s_suppkey)   -- top join
    const auto &join_supplier = agg->children[0];
    EXPECT_EQ(join_supplier->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supplier->expression.comp_type.has_value());
    EXPECT_EQ(*join_supplier->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supplier->base_columns.size(), 2);
    EXPECT_EQ(join_supplier->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supplier->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supplier->children.size(), 2);

    //          LEFT branch: JOIN(lo_partkey = p_partkey) …
    const auto &join_part = join_supplier->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //              LEFT: JOIN(lo_orderdate = d_datekey)
    const auto &join_dates = join_part->children[0];
    EXPECT_EQ(join_dates->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_dates->expression.comp_type.has_value());
    EXPECT_EQ(*join_dates->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_dates->base_columns.size(), 2);
    EXPECT_EQ(join_dates->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_dates->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_dates->children.size(), 2);

    //                  ├─ SCAN(lineorder)
    EXPECT_EQ(join_dates->children[0]->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(join_dates->children[0]->base_table, "lineorder");

    //                  └─ SCAN(dates)
    EXPECT_EQ(join_dates->children[1]->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(join_dates->children[1]->base_table, "dates");

    //              RIGHT: FILTER(p_category = 'MFGR#12') → SCAN(part)
    const auto &f_cat = join_part->children[1];
    EXPECT_EQ(f_cat->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_cat->expression.comp_type.has_value());
    EXPECT_EQ(*f_cat->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_cat->base_columns.size(), 1);
    EXPECT_EQ(f_cat->base_columns[0].column_name, "p_category");
    ASSERT_EQ(f_cat->expression.values.size(), 1);
    EXPECT_EQ(f_cat->expression.values[0], "MFGR#12");
    ASSERT_EQ(f_cat->children.size(), 1);

    const auto &scan_part = f_cat->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    //          RIGHT branch: FILTER(s_region = 'AMERICA') → SCAN(supplier)
    const auto &f_region = join_supplier->children[1];
    EXPECT_EQ(f_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_region->base_columns.size(), 1);
    EXPECT_EQ(f_region->base_columns[0].column_name, "s_region");
    ASSERT_EQ(f_region->expression.values.size(), 1);
    EXPECT_EQ(f_region->expression.values[0], "AMERICA");
    ASSERT_EQ(f_region->children.size(), 1);

    const auto &scan_supplier = f_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");
}

/*
 * Logical plan for Q2_2:
 *
 *   SORT(d_year, p_brand)
 *     └─ PROJECTION(d_year, p_brand, lo_revenue)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
 *               └─ JOIN(lo_suppkey = s_suppkey)                  -- top join
 *                    ├─ JOIN(lo_partkey = p_partkey)
 *                    │     ├─ JOIN(lo_orderdate = d_datekey)
 *                    │     │     ├─ SCAN(lineorder)
 *                    │     │     └─ SCAN(dates)
 *                    │     └─ FILTER(p_brand BETWEEN 'MFGR#2221' AND 'MFGR#2228')
 *                    │           └─ SCAN(part)
 *                    └─ FILTER(s_region = 'ASIA')
 *                          └─ SCAN(supplier)
 */
TEST(SQLToPlanTranslatorTest, Q2_2)
{
    std::string query = R"(
        SELECT SUM(lo_revenue), d_year, p_brand
        FROM lineorder, dates, part, supplier
        WHERE
            lo_orderdate = d_datekey
            AND lo_partkey = p_partkey
            AND lo_suppkey = s_suppkey
            AND p_brand BETWEEN 'MFGR#2221' AND 'MFGR#2228'
            AND s_region = 'ASIA'
        GROUP BY d_year, p_brand
        ORDER BY d_year, p_brand;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year, p_brand)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "p_brand");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(d_year, p_brand, lo_revenue)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[2].column_name, "sum1");
    EXPECT_FALSE(proj->projected_columns[2].alias.has_value());
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_EQ(agg->base_columns.size(), 2);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "p_brand");
    ASSERT_TRUE(agg->expression.agg_specs[0].input.has_value());
    EXPECT_EQ(agg->expression.agg_specs[0].input->table_name, "lineorder");
    EXPECT_EQ(agg->expression.agg_specs[0].input->column_name, "lo_revenue");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ JOIN(lo_suppkey = s_suppkey)
    const auto &join_supplier = agg->children[0];
    EXPECT_EQ(join_supplier->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supplier->expression.comp_type.has_value());
    EXPECT_EQ(*join_supplier->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supplier->base_columns.size(), 2);
    EXPECT_EQ(join_supplier->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supplier->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supplier->children.size(), 2);

    //          LEFT branch: JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_supplier->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //              LEFT: JOIN(lo_orderdate = d_datekey)
    const auto &join_dates = join_part->children[0];
    EXPECT_EQ(join_dates->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_dates->expression.comp_type.has_value());
    EXPECT_EQ(*join_dates->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_dates->base_columns.size(), 2);
    EXPECT_EQ(join_dates->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_dates->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_dates->children.size(), 2);

    //                  ├─ SCAN(lineorder)
    EXPECT_EQ(join_dates->children[0]->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(join_dates->children[0]->base_table, "lineorder");

    //                  └─ SCAN(dates)
    EXPECT_EQ(join_dates->children[1]->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(join_dates->children[1]->base_table, "dates");

    //              RIGHT: FILTER(p_brand BETWEEN 'MFGR#2221' AND 'MFGR#2228') → SCAN(part)
    const auto &f_brand = join_part->children[1];
    EXPECT_EQ(f_brand->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_brand->expression.comp_type.has_value());
    EXPECT_EQ(*f_brand->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_brand->base_columns.size(), 1);
    EXPECT_EQ(f_brand->base_columns[0].column_name, "p_brand");
    ASSERT_EQ(f_brand->expression.values.size(), 2);
    EXPECT_EQ(f_brand->expression.values[0], "MFGR#2221");
    EXPECT_EQ(f_brand->expression.values[1], "MFGR#2228");
    ASSERT_EQ(f_brand->children.size(), 1);

    const auto &scan_part = f_brand->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    //          RIGHT branch: FILTER(s_region = 'ASIA') → SCAN(supplier)
    const auto &f_region = join_supplier->children[1];
    EXPECT_EQ(f_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_region->base_columns.size(), 1);
    EXPECT_EQ(f_region->base_columns[0].column_name, "s_region");
    ASSERT_EQ(f_region->expression.values.size(), 1);
    EXPECT_EQ(f_region->expression.values[0], "ASIA");
    ASSERT_EQ(f_region->children.size(), 1);

    const auto &scan_supplier = f_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");
}

/*
 * Logical plan for Q2_3:
 *
 *   SORT(d_year, p_brand)
 *     └─ PROJECTION(d_year, p_brand, lo_revenue)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
 *               └─ JOIN(lo_suppkey = s_suppkey)                  -- top join
 *                    ├─ JOIN(lo_partkey = p_partkey)
 *                    │     ├─ JOIN(lo_orderdate = d_datekey)
 *                    │     │     ├─ SCAN(lineorder)
 *                    │     │     └─ SCAN(dates)
 *                    │     └─ FILTER(p_brand = 'MFGR#2239')
 *                    │           └─ SCAN(part)
 *                    └─ FILTER(s_region = 'EUROPE')
 *                          └─ SCAN(supplier)
 */
TEST(SQLToPlanTranslatorTest, Q2_3)
{
    std::string query = R"(
        SELECT SUM(lo_revenue), d_year, p_brand
        FROM lineorder, dates, part, supplier
        WHERE
            lo_orderdate = d_datekey
            AND lo_partkey = p_partkey
            AND lo_suppkey = s_suppkey
            AND p_brand = 'MFGR#2239'
            AND s_region = 'EUROPE'
        GROUP BY d_year, p_brand
        ORDER BY d_year, p_brand;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year, p_brand)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "p_brand");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(d_year, p_brand, lo_revenue)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[2].column_name, "sum1");
    EXPECT_FALSE(proj->projected_columns[2].alias.has_value());
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_EQ(agg->base_columns.size(), 2);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "p_brand");
    ASSERT_TRUE(agg->expression.agg_specs[0].input.has_value());
    EXPECT_EQ(agg->expression.agg_specs[0].input->table_name, "lineorder");
    EXPECT_EQ(agg->expression.agg_specs[0].input->column_name, "lo_revenue");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ JOIN(lo_suppkey = s_suppkey)   -- top join
    const auto &join_supplier = agg->children[0];
    EXPECT_EQ(join_supplier->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supplier->expression.comp_type.has_value());
    EXPECT_EQ(*join_supplier->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supplier->base_columns.size(), 2);
    EXPECT_EQ(join_supplier->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supplier->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supplier->children.size(), 2);

    // LEFT branch: JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_supplier->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //   LEFT: JOIN(lo_orderdate = d_datekey)
    const auto &join_dates = join_part->children[0];
    EXPECT_EQ(join_dates->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_dates->expression.comp_type.has_value());
    EXPECT_EQ(*join_dates->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_dates->base_columns.size(), 2);
    EXPECT_EQ(join_dates->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_dates->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_dates->children.size(), 2);

    //       ├─ SCAN(lineorder)
    EXPECT_EQ(join_dates->children[0]->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(join_dates->children[0]->base_table, "lineorder");

    //       └─ SCAN(dates)
    EXPECT_EQ(join_dates->children[1]->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(join_dates->children[1]->base_table, "dates");

    //   RIGHT: FILTER(p_brand = 'MFGR#2239') → SCAN(part)
    const auto &f_brand = join_part->children[1];
    EXPECT_EQ(f_brand->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_brand->expression.comp_type.has_value());
    EXPECT_EQ(*f_brand->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_brand->base_columns.size(), 1);
    EXPECT_EQ(f_brand->base_columns[0].column_name, "p_brand");
    ASSERT_EQ(f_brand->expression.values.size(), 1);
    EXPECT_EQ(f_brand->expression.values[0], "MFGR#2239");
    ASSERT_EQ(f_brand->children.size(), 1);

    const auto &scan_part = f_brand->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    // RIGHT branch: FILTER(s_region = 'EUROPE') → SCAN(supplier)
    const auto &f_region = join_supplier->children[1];
    EXPECT_EQ(f_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_region->base_columns.size(), 1);
    EXPECT_EQ(f_region->base_columns[0].column_name, "s_region");
    ASSERT_EQ(f_region->expression.values.size(), 1);
    EXPECT_EQ(f_region->expression.values[0], "EUROPE");
    ASSERT_EQ(f_region->children.size(), 1);

    const auto &scan_supplier = f_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");
}

/*
 * Logical plan for Q3_1:
 *
 *   SORT(d_year ASC, REVENUE DESC)
 *     └─ PROJECTION(c_nation, s_nation, d_year, REVENUE)
 *          └─ AGGREGATE(SUM)  GROUP BY c_nation, s_nation, d_year
 *               └─ JOIN(lo_orderdate = d_datekey)           -- root join
 *                    ├─ JOIN(lo_suppkey = s_suppkey)
 *                    │    ├─ JOIN(lo_custkey = c_custkey)
 *                    │    │    ├─ SCAN(lineorder)
 *                    │    │    └─ FILTER(c_region = 'ASIA') → SCAN(customer)
 *                    │    └─ FILTER(s_region = 'ASIA')
 *                    │         └─ SCAN(supplier)
 *                    └─ FILTER(d_year >= 1992)
 *                         └─ FILTER(d_year <= 1997)
 *                              └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_1)
{
    std::string query = R"(
        SELECT
            c_nation,
            s_nation,
            d_year,
            SUM(lo_revenue) AS REVENUE
        FROM customer, lineorder, supplier, dates
        WHERE
            lo_custkey  = c_custkey
            AND lo_suppkey   = s_suppkey
            AND lo_orderdate = d_datekey
            AND c_region = 'ASIA'
            AND s_region = 'ASIA'
            AND d_year >= 1992
            AND d_year <= 1997
        GROUP BY c_nation, s_nation, d_year
        ORDER BY d_year ASC, REVENUE DESC;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year, REVENUE)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "REVENUE");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_FALSE(dirs[1]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(c_nation, s_nation, d_year, REVENUE)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "c_nation");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_nation");
    EXPECT_EQ(proj->projected_columns[2].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[3].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].alias, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY c_nation, s_nation, d_year
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_TRUE(agg->expression.agg_specs[0].input.has_value());
    EXPECT_EQ(agg->expression.agg_specs[0].input->table_name, "lineorder");
    EXPECT_EQ(agg->expression.agg_specs[0].input->column_name, "lo_revenue");

    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "c_nation");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_nation");
    EXPECT_EQ(agg->base_columns[2].column_name, "d_year");
    ASSERT_EQ(agg->children.size(), 1);

    //      └─ JOIN(lo_orderdate = d_datekey)  (root join)
    const auto &join_root = agg->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // LEFT: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_root->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //   LEFT of that: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    //       ├─ SCAN(lineorder)
    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //       └─ FILTER(c_region = 'ASIA') → SCAN(customer)
    const auto &f_c_region = join_cust->children[1];
    EXPECT_EQ(f_c_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_c_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_c_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_c_region->base_columns.size(), 1);
    EXPECT_EQ(f_c_region->base_columns[0].column_name, "c_region");
    ASSERT_EQ(f_c_region->expression.values.size(), 1);
    EXPECT_EQ(f_c_region->expression.values[0], "ASIA");
    ASSERT_EQ(f_c_region->children.size(), 1);
    const auto &scan_customer = f_c_region->children[0];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //   RIGHT of join_supp: FILTER(s_region = 'ASIA') → SCAN(supplier)
    const auto &f_s_region = join_supp->children[1];
    EXPECT_EQ(f_s_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_s_region->base_columns.size(), 1);
    EXPECT_EQ(f_s_region->base_columns[0].column_name, "s_region");
    ASSERT_EQ(f_s_region->expression.values.size(), 1);
    EXPECT_EQ(f_s_region->expression.values[0], "ASIA");
    ASSERT_EQ(f_s_region->children.size(), 1);
    const auto &scan_supplier = f_s_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    // RIGHT branch: FILTER(d_year >= 1992) → FILTER(d_year <= 1997) → SCAN(dates)
    const auto &f_year_ge = join_root->children[1];
    EXPECT_EQ(f_year_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_year_ge->base_columns.size(), 1);
    EXPECT_EQ(f_year_ge->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_ge->expression.values.size(), 1);
    EXPECT_EQ(f_year_ge->expression.values[0], "1992");
    ASSERT_EQ(f_year_ge->children.size(), 1);

    const auto &f_year_le = f_year_ge->children[0];
    EXPECT_EQ(f_year_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_year_le->base_columns.size(), 1);
    EXPECT_EQ(f_year_le->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_le->expression.values.size(), 1);
    EXPECT_EQ(f_year_le->expression.values[0], "1997");
    ASSERT_EQ(f_year_le->children.size(), 1);

    const auto &scan_dates = f_year_le->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q3_2:
 *
 *   SORT(d_year ASC, REVENUE DESC)
 *     └─ PROJECTION(c_city, s_city, d_year, REVENUE)
 *          └─ AGGREGATE(SUM)  GROUP BY c_city, s_city, d_year
 *               └─ JOIN(lo_orderdate = d_datekey)
 *                    ├─ JOIN(lo_suppkey = s_suppkey)
 *                    │    ├─ JOIN(lo_custkey = c_custkey)
 *                    │    │    ├─ SCAN(lineorder)
 *                    │    │    └─ FILTER(c_nation = 'UNITED STATES') → SCAN(customer)
 *                    │    └─ FILTER(s_nation = 'UNITED STATES')
 *                    │         └─ SCAN(supplier)
 *                    └─ FILTER(d_year >= 1992)
 *                         └─ FILTER(d_year <= 1997)
 *                              └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_2)
{
    std::string query = R"(
        SELECT
            c_city,
            s_city,
            d_year,
            SUM(lo_revenue) AS REVENUE
        FROM customer, lineorder, supplier, dates
        WHERE
            lo_custkey   = c_custkey
            AND lo_suppkey   = s_suppkey
            AND lo_orderdate = d_datekey
            AND c_nation = 'UNITED STATES'
            AND s_nation = 'UNITED STATES'
            AND d_year >= 1992
            AND d_year <= 1997
        GROUP BY c_city, s_city, d_year
        ORDER BY d_year ASC, REVENUE DESC;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year, REVENUE)
    const auto &sort = plans[0];
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "REVENUE");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_FALSE(dirs[1]);

    //  └─ PROJECTION(c_city, s_city, d_year, REVENUE)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "c_city");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[3].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].alias, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY c_city, s_city, d_year
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_TRUE(agg->expression.agg_specs[0].input.has_value());
    EXPECT_EQ(agg->expression.agg_specs[0].input->table_name, "lineorder");
    EXPECT_EQ(agg->expression.agg_specs[0].input->column_name, "lo_revenue");

    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "c_city");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_city");
    EXPECT_EQ(agg->base_columns[2].column_name, "d_year");
    ASSERT_EQ(agg->children.size(), 1);

    //      └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = agg->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // LEFT: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_root->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //   LEFT: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    //       ├─ SCAN(lineorder)
    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //       └─ FILTER(c_nation = 'UNITED STATES') → SCAN(customer)
    const auto &f_c_nation = join_cust->children[1];
    EXPECT_EQ(f_c_nation->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_c_nation->expression.comp_type.has_value());
    EXPECT_EQ(*f_c_nation->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_c_nation->base_columns.size(), 1);
    EXPECT_EQ(f_c_nation->base_columns[0].column_name, "c_nation");
    ASSERT_EQ(f_c_nation->expression.values.size(), 1);
    EXPECT_EQ(f_c_nation->expression.values[0], "UNITED STATES");
    ASSERT_EQ(f_c_nation->children.size(), 1);
    const auto &scan_customer = f_c_nation->children[0];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //   RIGHT: FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
    const auto &f_s_nation = join_supp->children[1];
    EXPECT_EQ(f_s_nation->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_nation->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_nation->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_s_nation->base_columns.size(), 1);
    EXPECT_EQ(f_s_nation->base_columns[0].column_name, "s_nation");
    ASSERT_EQ(f_s_nation->expression.values.size(), 1);
    EXPECT_EQ(f_s_nation->expression.values[0], "UNITED STATES");
    ASSERT_EQ(f_s_nation->children.size(), 1);
    const auto &scan_supplier = f_s_nation->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    // RIGHT: FILTER(d_year >= 1992) → FILTER(d_year <= 1997) → SCAN(dates)
    const auto &f_year_ge = join_root->children[1];
    EXPECT_EQ(f_year_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_year_ge->base_columns.size(), 1);
    EXPECT_EQ(f_year_ge->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_ge->expression.values.size(), 1);
    EXPECT_EQ(f_year_ge->expression.values[0], "1992");
    ASSERT_EQ(f_year_ge->children.size(), 1);

    const auto &f_year_le = f_year_ge->children[0];
    EXPECT_EQ(f_year_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_year_le->base_columns.size(), 1);
    EXPECT_EQ(f_year_le->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_le->expression.values.size(), 1);
    EXPECT_EQ(f_year_le->expression.values[0], "1997");
    ASSERT_EQ(f_year_le->children.size(), 1);

    const auto &scan_dates = f_year_le->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q3_3:
 *
 *   SORT(d_year ASC, REVENUE DESC)
 *     └─ PROJECTION(c_city, s_city, d_year, REVENUE)
 *          └─ AGGREGATE(SUM)  GROUP BY c_city, s_city, d_year
 *               └─ JOIN(lo_orderdate = d_datekey)
 *                    ├─ JOIN(lo_suppkey = s_suppkey)
 *                    │    ├─ JOIN(lo_custkey = c_custkey)
 *                    │    │    ├─ SCAN(lineorder)
 *                    │    │    └─ FILTER(c_city IN ('UNITED KI1','UNITED KI5')) → SCAN(customer)
 *                    │    └─ FILTER(s_city IN ('UNITED KI1','UNITED KI5'))
 *                    │         └─ SCAN(supplier)
 *                    └─ FILTER(d_year >= 1992)
 *                         └─ FILTER(d_year <= 1997)
 *                              └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_3)
{
    std::string query = R"(
        SELECT
            c_city,
            s_city,
            d_year,
            SUM(lo_revenue) AS REVENUE
        FROM customer, lineorder, supplier, dates
        WHERE
            lo_custkey   = c_custkey
            AND lo_suppkey   = s_suppkey
            AND lo_orderdate = d_datekey
            AND (
                c_city = 'UNITED KI1'
                OR c_city = 'UNITED KI5'
            )
            AND (
                s_city = 'UNITED KI1'
                OR s_city = 'UNITED KI5'
            )
            AND d_year >= 1992
            AND d_year <= 1997
        GROUP BY c_city, s_city, d_year
        ORDER BY d_year ASC, REVENUE DESC;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year ASC, REVENUE DESC)
    const auto &sort = plans[0];
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "REVENUE");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_FALSE(dirs[1]);

    //  └─ PROJECTION(c_city, s_city, d_year, REVENUE)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "c_city");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[3].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].alias, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY c_city, s_city, d_year
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "REVENUE");
    ASSERT_TRUE(agg->expression.agg_specs[0].input.has_value());
    EXPECT_EQ(agg->expression.agg_specs[0].input->table_name, "lineorder");
    EXPECT_EQ(agg->expression.agg_specs[0].input->column_name, "lo_revenue");

    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "c_city");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_city");
    EXPECT_EQ(agg->base_columns[2].column_name, "d_year");
    ASSERT_EQ(agg->children.size(), 1);

    //      └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = agg->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // LEFT: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_root->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //   LEFT: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    //       ├─ SCAN(lineorder)
    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //       └─ FILTER(c_city IN (...)) → SCAN(customer)
    const auto &f_c_city_in = join_cust->children[1];
    EXPECT_EQ(f_c_city_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_c_city_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_c_city_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_c_city_in->base_columns.size(), 1);
    EXPECT_EQ(f_c_city_in->base_columns[0].column_name, "c_city");
    ASSERT_EQ(f_c_city_in->expression.values.size(), 2);
    EXPECT_EQ(f_c_city_in->expression.values[0], "UNITED KI1");
    EXPECT_EQ(f_c_city_in->expression.values[1], "UNITED KI5");
    ASSERT_EQ(f_c_city_in->children.size(), 1);
    const auto &scan_customer = f_c_city_in->children[0];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //   RIGHT: FILTER(s_city IN (...)) → SCAN(supplier)
    const auto &f_s_city_in = join_supp->children[1];
    EXPECT_EQ(f_s_city_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_city_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_city_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_s_city_in->base_columns.size(), 1);
    EXPECT_EQ(f_s_city_in->base_columns[0].column_name, "s_city");
    ASSERT_EQ(f_s_city_in->expression.values.size(), 2);
    EXPECT_EQ(f_s_city_in->expression.values[0], "UNITED KI1");
    EXPECT_EQ(f_s_city_in->expression.values[1], "UNITED KI5");
    ASSERT_EQ(f_s_city_in->children.size(), 1);
    const auto &scan_supplier = f_s_city_in->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    // RIGHT: FILTER(d_year >= 1992) → FILTER(d_year <= 1997) → SCAN(dates)
    const auto &f_year_ge = join_root->children[1];
    EXPECT_EQ(f_year_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_year_ge->base_columns.size(), 1);
    EXPECT_EQ(f_year_ge->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_ge->expression.values.size(), 1);
    EXPECT_EQ(f_year_ge->expression.values[0], "1992");
    ASSERT_EQ(f_year_ge->children.size(), 1);

    const auto &f_year_le = f_year_ge->children[0];
    EXPECT_EQ(f_year_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_year_le->base_columns.size(), 1);
    EXPECT_EQ(f_year_le->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_le->expression.values.size(), 1);
    EXPECT_EQ(f_year_le->expression.values[0], "1997");
    ASSERT_EQ(f_year_le->children.size(), 1);

    const auto &scan_dates = f_year_le->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q3_4:
 *   SORT(d_year ASC, s_city ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, s_city, p_brand
 *               └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                    └─ JOIN(lo_orderdate = d_datekey)
 *                         ├─ JOIN(lo_partkey = p_partkey)
 *                         │    ├─ JOIN(lo_suppkey = s_suppkey)
 *                         │    │    ├─ JOIN(lo_custkey = c_custkey)
 *                         │    │    │    ├─ SCAN(lineorder)
 *                         │    │    │    └─ SCAN(customer)
 *                         │    │    └─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
 *                         │    └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
 *                         └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_4)
{
    std::string query = R"(
        SELECT
            d_year,
            s_city,
            p_brand,
            SUM(lo_revenue - lo_supplycost) AS PROFIT
        FROM dates, customer, supplier, part, lineorder
        WHERE
            lo_custkey = c_custkey
            AND lo_suppkey = s_suppkey
            AND lo_partkey = p_partkey
            AND lo_orderdate = d_datekey
            AND s_nation = 'UNITED STATES'
            AND (
                d_year = 1997
                OR d_year = 1998
            )
            AND p_category = 'MFGR#14'
        GROUP BY d_year, s_city, p_brand
        ORDER BY d_year, s_city, p_brand;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year ASC, s_city ASC, p_brand ASC)
    const auto &sort = plans[0];
    ASSERT_EQ(sort->base_columns.size(), 3);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "s_city");
    EXPECT_EQ(sort->base_columns[2].column_name, "p_brand");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 3);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    EXPECT_TRUE(dirs[2]);

    //  └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[3].column_name, "PROFIT");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM) GROUP BY d_year, s_city, p_brand
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "PROFIT");

    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_city");
    EXPECT_EQ(agg->base_columns[2].column_name, "p_brand");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ MAP(SUB: lo_revenue - lo_supplycost)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::SUB);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_revenue");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_supplycost");
    ASSERT_EQ(map->children.size(), 1);

    //              └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    //                  ├─ JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_root->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //                  │    ├─ JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_part->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //                  │    │    ├─ JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    const auto &scan_customer = join_cust->children[1];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //                  │    │    └─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
    const auto &f_s_nation = join_supp->children[1];
    EXPECT_EQ(f_s_nation->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_nation->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_nation->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_s_nation->base_columns.size(), 1);
    EXPECT_EQ(f_s_nation->base_columns[0].column_name, "s_nation");
    ASSERT_EQ(f_s_nation->expression.values.size(), 1);
    EXPECT_EQ(f_s_nation->expression.values[0], "UNITED STATES");
    ASSERT_EQ(f_s_nation->children.size(), 1);

    const auto &scan_supplier = f_s_nation->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    //                  │    └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
    const auto &f_cat = join_part->children[1];
    EXPECT_EQ(f_cat->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_cat->expression.comp_type.has_value());
    EXPECT_EQ(*f_cat->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_cat->base_columns.size(), 1);
    EXPECT_EQ(f_cat->base_columns[0].column_name, "p_category");
    ASSERT_EQ(f_cat->expression.values.size(), 1);
    EXPECT_EQ(f_cat->expression.values[0], "MFGR#14");
    ASSERT_EQ(f_cat->children.size(), 1);

    const auto &scan_part = f_cat->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    //                  └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
    const auto &f_year_in = join_root->children[1];
    EXPECT_EQ(f_year_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_year_in->base_columns.size(), 1);
    EXPECT_EQ(f_year_in->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_in->expression.values.size(), 2);
    EXPECT_EQ(f_year_in->expression.values[0], "1997");
    EXPECT_EQ(f_year_in->expression.values[1], "1998");
    ASSERT_EQ(f_year_in->children.size(), 1);

    const auto &scan_dates = f_year_in->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q3_4 Multi-Aggregate:
 *
 *   SORT(d_year ASC, s_city ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, s_city, p_brand, profit, total_revenue, total_cost)
 *          └─ AGGREGATE(SUM, SUM, SUM) GROUP BY d_year, s_city, p_brand
 *               └─ MAP(SUB: lo_revenue - lo_supplycost) [optional]
 *                    └─ JOIN(...)
 */
TEST(SQLToPlanTranslatorTest, Q3_4_MultiAggregate)
{
    std::string query = R"(
        SELECT
            d_year,
            s_city,
            p_brand,
            SUM(lo_revenue - lo_supplycost) AS profit,
            SUM(lo_revenue) AS total_revenue,
            SUM(lo_supplycost) AS total_cost
        FROM dates, customer, supplier, part, lineorder
        WHERE
            lo_custkey = c_custkey
            AND lo_suppkey = s_suppkey
            AND lo_partkey = p_partkey
            AND lo_orderdate = d_datekey
            AND s_nation = 'UNITED STATES'
            AND (
                d_year = 1997
                OR d_year = 1998
            )
            AND p_category = 'MFGR#14'
        GROUP BY d_year, s_city, p_brand
        ORDER BY d_year, s_city, p_brand;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year ASC, s_city ASC, p_brand ASC)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 3);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "s_city");
    EXPECT_EQ(sort->base_columns[2].column_name, "p_brand");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 3);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    EXPECT_TRUE(dirs[2]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(d_year, s_city, p_brand, profit, total_revenue, total_cost)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 6);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[3].column_name, "profit");
    EXPECT_EQ(proj->projected_columns[4].column_name, "total_revenue");
    EXPECT_EQ(proj->projected_columns[5].column_name, "total_cost");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    EXPECT_EQ(proj->projected_columns[4].type, PlanColumnType::INTEGER);
    EXPECT_EQ(proj->projected_columns[5].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM, SUM, SUM) GROUP BY d_year, s_city, p_brand
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_city");
    EXPECT_EQ(agg->base_columns[2].column_name, "p_brand");
    ASSERT_EQ(agg->expression.agg_specs.size(), 3u);
    ASSERT_EQ(agg->children.size(), 1);

    const auto &agg_specs = agg->expression.agg_specs;
    EXPECT_EQ(agg_specs.size(), 3u);
    EXPECT_EQ(agg_specs[0].result_alias, "profit");
    EXPECT_EQ(agg_specs[0].func, PlanAggFunc::SUM);
    EXPECT_EQ(agg_specs[1].result_alias, "total_revenue");
    EXPECT_EQ(agg_specs[1].func, PlanAggFunc::SUM);
    EXPECT_EQ(agg_specs[2].result_alias, "total_cost");
    EXPECT_EQ(agg_specs[2].func, PlanAggFunc::SUM);

    //          └─ MAP(...) [optional]
    const LogicalPlanNode *belowAgg = agg->children[0].get();
    ASSERT_NE(belowAgg, nullptr);
    if (belowAgg->node_type == LogicalNodeType::MAP)
    {
        ASSERT_TRUE(belowAgg->expression.arith_op.has_value());
        EXPECT_EQ(*belowAgg->expression.arith_op, PlanArithOp::SUB);
        ASSERT_EQ(belowAgg->children.size(), 1);
        belowAgg = belowAgg->children[0].get();
        ASSERT_NE(belowAgg, nullptr);
    }

    //              └─ JOIN(...)
    EXPECT_EQ(belowAgg->node_type, LogicalNodeType::JOIN);
    ASSERT_EQ(belowAgg->children.size(), 2);
}

/*
 * Logical plan for Q4_1
 *
 *   SORT(d_year ASC, c_nation ASC)
 *     └─ PROJECTION(d_year, c_nation, PROFIT)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, c_nation
 *               └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                    └─ JOIN(lo_orderdate = d_datekey)
 *                         ├─ JOIN(lo_partkey = p_partkey)
 *                         │    ├─ JOIN(lo_suppkey = s_suppkey)
 *                         │    │    ├─ JOIN(lo_custkey = c_custkey)
 *                         │    │    │    ├─ SCAN(lineorder)
 *                         │    │    │    └─ FILTER(c_region = 'AMERICA') → SCAN(customer)
 *                         │    │    └─ FILTER(s_region = 'AMERICA') → SCAN(supplier)
 *                         │    └─ FILTER(p_mfgr IN ('MFGR#1','MFGR#2')) → SCAN(part)
 *                         └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q4_1)
{
    std::string query = R"(
        SELECT
            d_year,
            c_nation,
            SUM(lo_revenue - lo_supplycost) AS PROFIT
        FROM dates, customer, supplier, part, lineorder
        WHERE
            lo_custkey   = c_custkey
            AND lo_suppkey   = s_suppkey
            AND lo_partkey   = p_partkey
            AND lo_orderdate = d_datekey
            AND c_region = 'AMERICA'
            AND s_region = 'AMERICA'
            AND (
                p_mfgr = 'MFGR#1'
                OR p_mfgr = 'MFGR#2'
            )
        GROUP BY d_year, c_nation
        ORDER BY d_year, c_nation;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year ASC, c_nation ASC)
    const auto &sort = plans[0];
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "c_nation");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);

    //  └─ PROJECTION(d_year, c_nation, PROFIT)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "c_nation");
    EXPECT_EQ(proj->projected_columns[2].column_name, "PROFIT");
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY d_year, c_nation
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "PROFIT");

    ASSERT_EQ(agg->base_columns.size(), 2);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "c_nation");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ MAP(SUB: lo_revenue - lo_supplycost)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::SUB);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_revenue");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_supplycost");
    ASSERT_EQ(map->children.size(), 1);

    //              └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    //                  ├─ JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_root->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //                  │    ├─ JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_part->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //                  │    │    ├─ JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    const auto &f_c_region = join_cust->children[1];
    EXPECT_EQ(f_c_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_c_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_c_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_c_region->base_columns.size(), 1);
    EXPECT_EQ(f_c_region->base_columns[0].column_name, "c_region");
    ASSERT_EQ(f_c_region->expression.values.size(), 1);
    EXPECT_EQ(f_c_region->expression.values[0], "AMERICA");
    ASSERT_EQ(f_c_region->children.size(), 1);

    const auto &scan_customer = f_c_region->children[0];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //                  │    │    └─ FILTER(s_region = 'AMERICA') → SCAN(supplier)
    const auto &f_s_region = join_supp->children[1];
    EXPECT_EQ(f_s_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_s_region->base_columns.size(), 1);
    EXPECT_EQ(f_s_region->base_columns[0].column_name, "s_region");
    ASSERT_EQ(f_s_region->expression.values.size(), 1);
    EXPECT_EQ(f_s_region->expression.values[0], "AMERICA");
    ASSERT_EQ(f_s_region->children.size(), 1);

    const auto &scan_supplier = f_s_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    //                  │    └─ FILTER(p_mfgr IN ('MFGR#1','MFGR#2')) → SCAN(part)
    const auto &f_mfgr_in = join_part->children[1];
    EXPECT_EQ(f_mfgr_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_mfgr_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_mfgr_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_mfgr_in->base_columns.size(), 1);
    EXPECT_EQ(f_mfgr_in->base_columns[0].column_name, "p_mfgr");
    ASSERT_EQ(f_mfgr_in->expression.values.size(), 2);
    EXPECT_EQ(f_mfgr_in->expression.values[0], "MFGR#1");
    EXPECT_EQ(f_mfgr_in->expression.values[1], "MFGR#2");
    ASSERT_EQ(f_mfgr_in->children.size(), 1);

    const auto &scan_part = f_mfgr_in->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    //                  └─ SCAN(dates)
    const auto &scan_dates = join_root->children[1];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q4_2:
 *
 *   SORT(d_year ASC, s_nation ASC, p_category ASC)
 *     └─ PROJECTION(d_year, s_nation, p_category, PROFIT)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, s_nation, p_category
 *               └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                    └─ JOIN(lo_orderdate = d_datekey)
 *                         ├─ JOIN(lo_partkey = p_partkey)
 *                         │    ├─ JOIN(lo_suppkey = s_suppkey)
 *                         │    │    ├─ JOIN(lo_custkey = c_custkey)
 *                         │    │    │    ├─ SCAN(lineorder)
 *                         │    │    │    └─ FILTER(c_region = 'AMERICA') → SCAN(customer)
 *                         │    │    └─ FILTER(s_region = 'AMERICA') → SCAN(supplier)
 *                         │    └─ FILTER(p_mfgr IN ('MFGR#1','MFGR#2')) → SCAN(part)
 *                         └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q4_2)
{
    std::string query = R"(
        SELECT
            d_year,
            s_nation,
            p_category,
            SUM(lo_revenue - lo_supplycost) AS PROFIT
        FROM dates, customer, supplier, part, lineorder
        WHERE
            lo_custkey   = c_custkey
            AND lo_suppkey   = s_suppkey
            AND lo_partkey   = p_partkey
            AND lo_orderdate = d_datekey
            AND c_region = 'AMERICA'
            AND s_region = 'AMERICA'
            AND (
                d_year = 1997
                OR d_year = 1998
            )
            AND (
                p_mfgr = 'MFGR#1'
                OR p_mfgr = 'MFGR#2'
            )
        GROUP BY d_year, s_nation, p_category
        ORDER BY d_year, s_nation, p_category;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year ASC, s_nation ASC, p_category ASC)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 3);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "s_nation");
    EXPECT_EQ(sort->base_columns[2].column_name, "p_category");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 3);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    EXPECT_TRUE(dirs[2]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(d_year, s_nation, p_category, PROFIT)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_nation");
    EXPECT_EQ(proj->projected_columns[2].column_name, "p_category");
    EXPECT_EQ(proj->projected_columns[3].column_name, "PROFIT");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY d_year, s_nation, p_category
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "PROFIT");

    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_nation");
    EXPECT_EQ(agg->base_columns[2].column_name, "p_category");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ MAP(SUB: lo_revenue - lo_supplycost)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::SUB);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_revenue");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_supplycost");
    ASSERT_EQ(map->children.size(), 1);

    //              └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    //                  ├─ JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_root->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //                  │    ├─ JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_part->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //                  │    │    ├─ JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    const auto &f_c_region = join_cust->children[1];
    EXPECT_EQ(f_c_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_c_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_c_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_c_region->base_columns.size(), 1);
    EXPECT_EQ(f_c_region->base_columns[0].column_name, "c_region");
    ASSERT_EQ(f_c_region->expression.values.size(), 1);
    EXPECT_EQ(f_c_region->expression.values[0], "AMERICA");
    ASSERT_EQ(f_c_region->children.size(), 1);

    const auto &scan_customer = f_c_region->children[0];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //                  │    │    └─ FILTER(s_region = 'AMERICA') → SCAN(supplier)
    const auto &f_s_region = join_supp->children[1];
    EXPECT_EQ(f_s_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_s_region->base_columns.size(), 1);
    EXPECT_EQ(f_s_region->base_columns[0].column_name, "s_region");
    ASSERT_EQ(f_s_region->expression.values.size(), 1);
    EXPECT_EQ(f_s_region->expression.values[0], "AMERICA");
    ASSERT_EQ(f_s_region->children.size(), 1);

    const auto &scan_supplier = f_s_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    //                  │    └─ FILTER(p_mfgr IN ('MFGR#1', 'MFGR#2')) → SCAN(part)
    const auto &f_mfgr_in = join_part->children[1];
    EXPECT_EQ(f_mfgr_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_mfgr_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_mfgr_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_mfgr_in->base_columns.size(), 1);
    EXPECT_EQ(f_mfgr_in->base_columns[0].column_name, "p_mfgr");
    ASSERT_EQ(f_mfgr_in->expression.values.size(), 2);
    EXPECT_EQ(f_mfgr_in->expression.values[0], "MFGR#1");
    EXPECT_EQ(f_mfgr_in->expression.values[1], "MFGR#2");
    ASSERT_EQ(f_mfgr_in->children.size(), 1);

    const auto &scan_part = f_mfgr_in->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    //                  └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
    const auto &f_year_in = join_root->children[1];
    EXPECT_EQ(f_year_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_year_in->base_columns.size(), 1);
    EXPECT_EQ(f_year_in->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_in->expression.values.size(), 2);
    EXPECT_EQ(f_year_in->expression.values[0], "1997");
    EXPECT_EQ(f_year_in->expression.values[1], "1998");
    ASSERT_EQ(f_year_in->children.size(), 1);

    const auto &scan_dates = f_year_in->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for Q4_3:
 *
 *   SORT(d_year ASC, s_city ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, s_city, p_brand
 *               └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                    └─ JOIN(lo_orderdate = d_datekey)
 *                         ├─ JOIN(lo_partkey = p_partkey)
 *                         │    ├─ JOIN(lo_suppkey = s_suppkey)
 *                         │    │    ├─ JOIN(lo_custkey = c_custkey)
 *                         │    │    │    ├─ SCAN(lineorder)
 *                         │    │    │    └─ SCAN(customer)
 *                         │    │    └─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
 *                         │    └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
 *                         └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q4_3)
{
    std::string query = R"(
        SELECT
            d_year,
            s_city,
            p_brand,
            SUM(lo_revenue - lo_supplycost) AS PROFIT
        FROM dates, customer, supplier, part, lineorder
        WHERE
            lo_custkey   = c_custkey
            AND lo_suppkey   = s_suppkey
            AND lo_partkey   = p_partkey
            AND lo_orderdate = d_datekey
            AND s_nation = 'UNITED STATES'
            AND (
                d_year = 1997
                OR d_year = 1998
            )
            AND p_category = 'MFGR#14'
        GROUP BY d_year, s_city, p_brand
        ORDER BY d_year, s_city, p_brand;
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1);

    // SORT(d_year ASC, s_city ASC, p_brand ASC)
    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 3);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "s_city");
    EXPECT_EQ(sort->base_columns[2].column_name, "p_brand");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 3);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    EXPECT_TRUE(dirs[2]);
    ASSERT_EQ(sort->children.size(), 1);

    //  └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[3].column_name, "PROFIT");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    //      └─ AGGREGATE(SUM)  GROUP BY d_year, s_city, p_brand
    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "PROFIT");

    ASSERT_EQ(agg->base_columns.size(), 3);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "s_city");
    EXPECT_EQ(agg->base_columns[2].column_name, "p_brand");
    ASSERT_EQ(agg->children.size(), 1);

    //          └─ MAP(SUB: lo_revenue - lo_supplycost)
    const auto &map = agg->children[0];
    EXPECT_EQ(map->node_type, LogicalNodeType::MAP);
    ASSERT_TRUE(map->expression.arith_op.has_value());
    EXPECT_EQ(*map->expression.arith_op, PlanArithOp::SUB);
    ASSERT_EQ(map->base_columns.size(), 2);
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_revenue");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_supplycost");
    ASSERT_EQ(map->children.size(), 1);

    //              └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    //                  ├─ JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_root->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    //                  │    ├─ JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_part->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //                  │    │    ├─ JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    const auto &scan_lo = join_cust->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    const auto &scan_customer = join_cust->children[1];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //                  │    │    └─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
    const auto &f_s_nat = join_supp->children[1];
    EXPECT_EQ(f_s_nat->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_nat->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_nat->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_s_nat->base_columns.size(), 1);
    EXPECT_EQ(f_s_nat->base_columns[0].column_name, "s_nation");
    ASSERT_EQ(f_s_nat->expression.values.size(), 1);
    EXPECT_EQ(f_s_nat->expression.values[0], "UNITED STATES");
    ASSERT_EQ(f_s_nat->children.size(), 1);

    const auto &scan_supplier = f_s_nat->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    //                  │    └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
    const auto &f_p_cat = join_part->children[1];
    EXPECT_EQ(f_p_cat->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_p_cat->expression.comp_type.has_value());
    EXPECT_EQ(*f_p_cat->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_p_cat->base_columns.size(), 1);
    EXPECT_EQ(f_p_cat->base_columns[0].column_name, "p_category");
    ASSERT_EQ(f_p_cat->expression.values.size(), 1);
    EXPECT_EQ(f_p_cat->expression.values[0], "MFGR#14");
    ASSERT_EQ(f_p_cat->children.size(), 1);

    const auto &scan_part = f_p_cat->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    //                  └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
    const auto &f_year_in = join_root->children[1];
    EXPECT_EQ(f_year_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_year_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_year_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_year_in->base_columns.size(), 1);
    EXPECT_EQ(f_year_in->base_columns[0].column_name, "d_year");
    ASSERT_EQ(f_year_in->expression.values.size(), 2);
    EXPECT_EQ(f_year_in->expression.values[0], "1997");
    EXPECT_EQ(f_year_in->expression.values[1], "1998");
    ASSERT_EQ(f_year_in->children.size(), 1);

    const auto &scan_dates = f_year_in->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan for 000e6d88a61ab15d3bd71dd9e538e5e5fbaaf754:
 *
 *   LIMIT 100
 *     └─ SORT(count DESC)
 *          └─ PROJECTION(badge.name, count)
 *               └─ AGGREGATE(COUNT)  GROUP BY badge.name
 *                    └─ JOIN(account.id = so_user.account_id)
 *                         ├─ FILTER(account.website_url LIKE '%')
 *                         │    └─ SCAN(account)
 *                         └─ FILTER(question.id = tag_question.question_id)
 *                              └─ FILTER(question.owner_user_id = so_user.id)
 *                                   └─ JOIN(site.site_id = question.site_id)
 *                                        ├─ FILTER(tag.id = tag_question.tag_id)
 *                                        │    └─ JOIN(site.site_id = tag_question.site_id)
 *                                        │         ├─ JOIN(site.site_id = tag.site_id)
 *                                        │         │    ├─ FILTER(badge.user_id = so_user.id)
 *                                        │         │    │    └─ JOIN(site.site_id = badge.site_id)
 *                                        │         │    │         ├─ JOIN(site.site_id = so_user.site_id)
 *                                        │         │    │         │    ├─ FILTER(site.site_name IN ('stackoverflow','superuser'))
 *                                        │         │    │         │    │    └─ SCAN(site)
 *                                        │         │    │         │    └─ SCAN(so_user)
 *                                        │         │    │         └─ SCAN(badge)
 *                                        │         │    └─ FILTER(tag.name IN ('binding','makefile','replace'))
 *                                        │         │         └─ SCAN(tag)
 *                                        │         └─ SCAN(tag_question)
 *                                        └─ FILTER(question.score >= 10)
 *                                             └─ FILTER(question.score <= 1000)
 *                                                  └─ SCAN(question)
 */

TEST(SQLToPlanTranslatorTest, 000e6d88a61ab15d3bd71dd9e538e5e5fbaaf754)
{
    const std::string query = R"(
        SELECT b1.name, count(*)
        FROM
            site as s,
            so_user as u1,
            tag as t1,
            tag_question as tq1,
            question as q1,
            badge as b1,
            account as acc
        WHERE
            s.site_id = u1.site_id
            AND s.site_id = b1.site_id
            AND s.site_id = t1.site_id
            AND s.site_id = tq1.site_id
            AND s.site_id = q1.site_id
            AND t1.id = tq1.tag_id
            AND q1.id = tq1.question_id
            AND q1.owner_user_id = u1.id
            AND acc.id = u1.account_id
            AND b1.user_id = u1.id
            AND (q1.score >= 10)
            AND (q1.score <= 1000)
            AND (s.site_name in('stackoverflow', 'superuser'))
            AND (t1.name in('binding', 'makefile', 'replace'))
            AND (acc.website_url like('%'))
        GROUP BY b1.name
        ORDER BY count(*) DESC
        LIMIT 100
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);

    //  └─ SORT order_by count DESC
    const auto &sort = plans[0];
    ASSERT_NE(sort, nullptr);
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->children.size(), 1u);
    ASSERT_EQ(sort->base_columns.size(), 1u);
    EXPECT_EQ(sort->base_columns[0].column_name, "count1");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    ASSERT_EQ(sort->expression.sort_order->size(), 1u);
    EXPECT_FALSE((*sort->expression.sort_order)[0]); // DESC -> usually false in your tests
    // LIMIT 100
    ASSERT_TRUE(sort->expression.limit_count.has_value());
    EXPECT_EQ(sort->expression.limit_count.value(), 100);

    //      └─ PROJECTION(badge.name, count)
    const auto &proj = sort->children[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->children.size(), 1u);
    ASSERT_EQ(proj->projected_columns.size(), 2u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "name");
    EXPECT_EQ(proj->projected_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(proj->projected_columns[1].column_name, "count1");

    //          └─ AGGREGATE agg=COUNT  GROUP BY badge.name
    const auto &agg = proj->children[0];
    ASSERT_NE(agg, nullptr);
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_EQ(agg->children.size(), 1u);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::COUNT);
    ASSERT_EQ(agg->base_columns.size(), 1u);
    EXPECT_EQ(agg->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(agg->base_columns[0].column_name, "name");

    //              └─ JOIN(account.id = so_user.account_id)
    const auto &join_acc = agg->children[0];
    ASSERT_NE(join_acc, nullptr);
    EXPECT_EQ(join_acc->node_type, LogicalNodeType::JOIN);
    ASSERT_EQ(join_acc->children.size(), 2u);
    ASSERT_TRUE(join_acc->expression.comp_type.has_value());
    EXPECT_EQ(*join_acc->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_acc->base_columns.size(), 2u);
    EXPECT_EQ(join_acc->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(join_acc->base_columns[0].column_name, "id");
    EXPECT_EQ(join_acc->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_acc->base_columns[1].column_name, "account_id");

    // left: FILTER(account.website_url LIKE '%') -> SCAN(account)
    const auto &f_acc_like = join_acc->children[0];
    ASSERT_NE(f_acc_like, nullptr);
    EXPECT_EQ(f_acc_like->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_acc_like->expression.comp_type.has_value());
    EXPECT_EQ(*f_acc_like->expression.comp_type, PlanCompType::LIKE);
    ASSERT_EQ(f_acc_like->base_columns.size(), 1u);
    EXPECT_EQ(f_acc_like->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(f_acc_like->base_columns[0].column_name, "website_url");
    ASSERT_EQ(f_acc_like->expression.values.size(), 1u);
    EXPECT_EQ(f_acc_like->expression.values[0], "%");
    ASSERT_EQ(f_acc_like->children.size(), 1u);

    const auto &scan_acc = f_acc_like->children[0];
    ASSERT_NE(scan_acc, nullptr);
    EXPECT_EQ(scan_acc->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_acc->base_table, "acc");
    ASSERT_TRUE(scan_acc->children.empty());

    // right: FILTER(question.id = tag_question.question_id)
    const auto &f_qid_tqid = join_acc->children[1];
    ASSERT_NE(f_qid_tqid, nullptr);
    EXPECT_EQ(f_qid_tqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_tqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_tqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_tqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_tqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_tqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_tqid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_qid_tqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_tqid->children.size(), 1u);

    //     └─ FILTER(question.owner_user_id = so_user.id)
    const auto &f_owner_uid = f_qid_tqid->children[0];
    ASSERT_NE(f_owner_uid, nullptr);
    EXPECT_EQ(f_owner_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_owner_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_owner_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_owner_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_owner_uid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_owner_uid->base_columns[0].column_name, "owner_user_id");
    EXPECT_EQ(f_owner_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_owner_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_owner_uid->children.size(), 1u);

    //         └─ JOIN(site.site_id = question.site_id)
    const auto &join_s_q = f_owner_uid->children[0];
    ASSERT_NE(join_s_q, nullptr);
    EXPECT_EQ(join_s_q->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_q->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_q->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_q->base_columns.size(), 2u);
    EXPECT_EQ(join_s_q->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_q->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_q->base_columns[1].getBaseTableName(), "question");
    EXPECT_EQ(join_s_q->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_q->children.size(), 2u);

    // left child: FILTER(tag.id = tag_question.tag_id)
    const auto &f_tid_tqtid = join_s_q->children[0];
    ASSERT_NE(f_tid_tqtid, nullptr);
    EXPECT_EQ(f_tid_tqtid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tid_tqtid->expression.comp_type.has_value());
    EXPECT_EQ(*f_tid_tqtid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_tid_tqtid->base_columns.size(), 2u);
    EXPECT_EQ(f_tid_tqtid->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tid_tqtid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].column_name, "tag_id");
    ASSERT_EQ(f_tid_tqtid->children.size(), 1u);

    //   └─ JOIN(site.site_id = tag_question.site_id)
    const auto &join_s_tq = f_tid_tqtid->children[0];
    ASSERT_NE(join_s_tq, nullptr);
    EXPECT_EQ(join_s_tq->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_tq->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_tq->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_tq->base_columns.size(), 2u);
    EXPECT_EQ(join_s_tq->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_tq->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_tq->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(join_s_tq->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_tq->children.size(), 2u);

    // left: JOIN(site.site_id = tag.site_id)
    const auto &join_s_t = join_s_tq->children[0];
    ASSERT_NE(join_s_t, nullptr);
    EXPECT_EQ(join_s_t->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_t->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_t->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_t->base_columns.size(), 2u);
    EXPECT_EQ(join_s_t->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_t->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_t->base_columns[1].getBaseTableName(), "tag");
    EXPECT_EQ(join_s_t->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_t->children.size(), 2u);

    // left of that: FILTER(badge.user_id = so_user.id)
    const auto &f_badge_uid = join_s_t->children[0];
    ASSERT_NE(f_badge_uid, nullptr);
    EXPECT_EQ(f_badge_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_badge_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_badge_uid->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_uid->base_columns[0].column_name, "user_id");
    EXPECT_EQ(f_badge_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_badge_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_badge_uid->children.size(), 1u);

    //   └─ JOIN(site.site_id = badge.site_id)
    const auto &join_s_b = f_badge_uid->children[0];
    ASSERT_NE(join_s_b, nullptr);
    EXPECT_EQ(join_s_b->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_b->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_b->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_b->base_columns.size(), 2u);
    EXPECT_EQ(join_s_b->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_b->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_b->base_columns[1].getBaseTableName(), "badge");
    EXPECT_EQ(join_s_b->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_b->children.size(), 2u);

    // left: JOIN(site.site_id = so_user.site_id)
    const auto &join_s_u = join_s_b->children[0];
    ASSERT_NE(join_s_u, nullptr);
    EXPECT_EQ(join_s_u->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_u->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_u->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_u->base_columns.size(), 2u);
    EXPECT_EQ(join_s_u->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_u->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_u->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_s_u->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_u->children.size(), 2u);

    // left: FILTER(site.site_name IN (...)) -> SCAN(site)
    const auto &f_site_in = join_s_u->children[0];
    ASSERT_NE(f_site_in, nullptr);
    EXPECT_EQ(f_site_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_site_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_site_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_site_in->base_columns.size(), 1u);
    EXPECT_EQ(f_site_in->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(f_site_in->base_columns[0].column_name, "site_name");
    ASSERT_EQ(f_site_in->expression.values.size(), 2u);
    EXPECT_EQ(f_site_in->expression.values[0], "stackoverflow");
    EXPECT_EQ(f_site_in->expression.values[1], "superuser");
    ASSERT_EQ(f_site_in->children.size(), 1u);

    const auto &scan_site = f_site_in->children[0];
    ASSERT_NE(scan_site, nullptr);
    EXPECT_EQ(scan_site->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_site->base_table, "s");
    ASSERT_TRUE(scan_site->children.empty());

    // right: SCAN(so_user)
    const auto &scan_user = join_s_u->children[1];
    ASSERT_NE(scan_user, nullptr);
    EXPECT_EQ(scan_user->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_user->base_table, "u1");
    ASSERT_TRUE(scan_user->children.empty());

    // right child of join_s_b: SCAN(badge)
    const auto &scan_badge = join_s_b->children[1];
    ASSERT_NE(scan_badge, nullptr);
    EXPECT_EQ(scan_badge->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_badge->base_table, "b1");
    ASSERT_TRUE(scan_badge->children.empty());

    // right child of join_s_t: FILTER(tag.name IN (...)) -> SCAN(tag)
    const auto &f_tag_in = join_s_t->children[1];
    ASSERT_NE(f_tag_in, nullptr);
    EXPECT_EQ(f_tag_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tag_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_tag_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_tag_in->base_columns.size(), 1u);
    EXPECT_EQ(f_tag_in->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tag_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_tag_in->expression.values.size(), 3u);
    EXPECT_EQ(f_tag_in->expression.values[0], "binding");
    EXPECT_EQ(f_tag_in->expression.values[1], "makefile");
    EXPECT_EQ(f_tag_in->expression.values[2], "replace");
    ASSERT_EQ(f_tag_in->children.size(), 1u);

    const auto &scan_tag = f_tag_in->children[0];
    ASSERT_NE(scan_tag, nullptr);
    EXPECT_EQ(scan_tag->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tag->base_table, "t1");
    ASSERT_TRUE(scan_tag->children.empty());

    // right child of join_s_tq: SCAN(tag_question)
    const auto &scan_tq = join_s_tq->children[1];
    ASSERT_NE(scan_tq, nullptr);
    EXPECT_EQ(scan_tq->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tq->base_table, "tq1");
    ASSERT_TRUE(scan_tq->children.empty());

    // right child of join_s_q: FILTER(question.score >= 10) -> FILTER(question.score <= 1000) -> SCAN(question)
    const auto &f_score_ge = join_s_q->children[1];
    ASSERT_NE(f_score_ge, nullptr);
    EXPECT_EQ(f_score_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_score_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_score_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_score_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_score_ge->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_score_ge->base_columns[0].column_name, "score");
    ASSERT_EQ(f_score_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_score_ge->expression.values[0], "10");
    ASSERT_EQ(f_score_ge->children.size(), 1u);

    const auto &f_score_le = f_score_ge->children[0];
    ASSERT_NE(f_score_le, nullptr);
    EXPECT_EQ(f_score_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_score_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_score_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_score_le->base_columns.size(), 1u);
    EXPECT_EQ(f_score_le->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_score_le->base_columns[0].column_name, "score");
    ASSERT_EQ(f_score_le->expression.values.size(), 1u);
    EXPECT_EQ(f_score_le->expression.values[0], "1000");
    ASSERT_EQ(f_score_le->children.size(), 1u);

    const auto &scan_q = f_score_le->children[0];
    ASSERT_NE(scan_q, nullptr);
    EXPECT_EQ(scan_q->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_q->base_table, "q1");
    ASSERT_TRUE(scan_q->children.empty());
}

/*
 * Logical plan for 1a5f715c24b5e043923be9c9c3649381b37d2b57:
 *
 *   PROJECTION(count)
 *     └─ AGGREGATE(COUNT)  alias=count
 *          └─ JOIN(account.id = so_user.account_id)
 *               ├─ FILTER(account.website_url LIKE '%')
 *               │    └─ SCAN(account)
 *               └─ FILTER(question.id = tag_question.question_id)
 *                    └─ FILTER(question.owner_user_id = so_user.id)
 *                         └─ JOIN(site.site_id = question.site_id)
 *                              ├─ FILTER(tag.id = tag_question.tag_id)
 *                              │    └─ JOIN(site.site_id = tag_question.site_id)
 *                              │         ├─ JOIN(site.site_id = tag.site_id)
 *                              │         │    ├─ FILTER(badge.user_id = so_user.id)
 *                              │         │    │    └─ JOIN(site.site_id = badge.site_id)
 *                              │         │    │         ├─ JOIN(site.site_id = so_user.site_id)
 *                              │         │    │         │    ├─ FILTER(site.site_name = 'stackoverflow')
 *                              │         │    │         │    │    └─ SCAN(site)
 *                              │         │    │         │    └─ SCAN(so_user)
 *                              │         │    │         └─ SCAN(badge)
 *                              │         │    └─ FILTER(tag.name IN ('latex','sum'))
 *                              │         │         └─ SCAN(tag)
 *                              │         └─ SCAN(tag_question)
 *                              └─ FILTER(question.view_count >= 100)
 *                                   └─ FILTER(question.view_count <= 100000)
 *                                        └─ SCAN(question)
 */

TEST(SQLToPlanTranslatorTest, 1a5f715c24b5e043923be9c9c3649381b37d2b57)
{
    const std::string query = R"(
        SELECT COUNT(*)
        FROM
            site as s,
            so_user as u1,
            tag as t1,
            tag_question as tq1,
            question as q1,
            badge as b1,
            account as acc
        WHERE
            s.site_id = u1.site_id
            AND s.site_id = b1.site_id
            AND s.site_id = t1.site_id
            AND s.site_id = tq1.site_id
            AND s.site_id = q1.site_id
            AND t1.id = tq1.tag_id
            AND q1.id = tq1.question_id
            AND q1.owner_user_id = u1.id
            AND acc.id = u1.account_id
            AND b1.user_id = u1.id
            AND (q1.view_count >= 100)
            AND (q1.view_count <= 100000)
            AND s.site_name = 'stackoverflow'
            AND (t1.name in('latex', 'sum'))
            AND (acc.website_url like('%'))
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);
    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);

    // PROJECTION(count)
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->children.size(), 1u);
    ASSERT_EQ(proj->projected_columns.size(), 1u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "count1");

    //  └─ AGGREGATE(COUNT) alias=count
    const auto &agg = proj->children[0];
    ASSERT_NE(agg, nullptr);
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_EQ(agg->children.size(), 1u);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::COUNT);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "count1");

    //      └─ JOIN(account.id = so_user.account_id)
    const auto &join_acc = agg->children[0];
    ASSERT_NE(join_acc, nullptr);
    EXPECT_EQ(join_acc->node_type, LogicalNodeType::JOIN);
    ASSERT_EQ(join_acc->children.size(), 2u);
    ASSERT_TRUE(join_acc->expression.comp_type.has_value());
    EXPECT_EQ(*join_acc->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_acc->base_columns.size(), 2u);
    EXPECT_EQ(join_acc->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(join_acc->base_columns[0].column_name, "id");
    EXPECT_EQ(join_acc->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_acc->base_columns[1].column_name, "account_id");

    // left: FILTER(account.website_url LIKE '%') -> SCAN(account)
    const auto &f_acc_like = join_acc->children[0];
    ASSERT_NE(f_acc_like, nullptr);
    EXPECT_EQ(f_acc_like->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_acc_like->expression.comp_type.has_value());
    EXPECT_EQ(*f_acc_like->expression.comp_type, PlanCompType::LIKE);
    ASSERT_EQ(f_acc_like->base_columns.size(), 1u);
    EXPECT_EQ(f_acc_like->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(f_acc_like->base_columns[0].column_name, "website_url");
    ASSERT_EQ(f_acc_like->expression.values.size(), 1u);
    EXPECT_EQ(f_acc_like->expression.values[0], "%");
    ASSERT_EQ(f_acc_like->children.size(), 1u);

    const auto &scan_acc = f_acc_like->children[0];
    ASSERT_NE(scan_acc, nullptr);
    EXPECT_EQ(scan_acc->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_acc->base_table, "acc");
    ASSERT_TRUE(scan_acc->children.empty());

    // right: FILTER(question.id = tag_question.question_id)
    const auto &f_qid_tqid = join_acc->children[1];
    ASSERT_NE(f_qid_tqid, nullptr);
    EXPECT_EQ(f_qid_tqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_tqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_tqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_tqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_tqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_tqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_tqid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_qid_tqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_tqid->children.size(), 1u);

    //   └─ FILTER(question.owner_user_id = so_user.id)
    const auto &f_owner_uid = f_qid_tqid->children[0];
    ASSERT_NE(f_owner_uid, nullptr);
    EXPECT_EQ(f_owner_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_owner_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_owner_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_owner_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_owner_uid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_owner_uid->base_columns[0].column_name, "owner_user_id");
    EXPECT_EQ(f_owner_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_owner_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_owner_uid->children.size(), 1u);

    //     └─ JOIN(site.site_id = question.site_id)
    const auto &join_s_q = f_owner_uid->children[0];
    ASSERT_NE(join_s_q, nullptr);
    EXPECT_EQ(join_s_q->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_q->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_q->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_q->base_columns.size(), 2u);
    EXPECT_EQ(join_s_q->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_q->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_q->base_columns[1].getBaseTableName(), "question");
    EXPECT_EQ(join_s_q->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_q->children.size(), 2u);

    // left: FILTER(tag.id = tag_question.tag_id)
    const auto &f_tid_tqtid = join_s_q->children[0];
    ASSERT_NE(f_tid_tqtid, nullptr);
    EXPECT_EQ(f_tid_tqtid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tid_tqtid->expression.comp_type.has_value());
    EXPECT_EQ(*f_tid_tqtid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_tid_tqtid->base_columns.size(), 2u);
    EXPECT_EQ(f_tid_tqtid->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tid_tqtid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].column_name, "tag_id");
    ASSERT_EQ(f_tid_tqtid->children.size(), 1u);

    //   └─ JOIN(site.site_id = tag_question.site_id)
    const auto &join_s_tq = f_tid_tqtid->children[0];
    ASSERT_NE(join_s_tq, nullptr);
    EXPECT_EQ(join_s_tq->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_tq->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_tq->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_tq->base_columns.size(), 2u);
    EXPECT_EQ(join_s_tq->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_tq->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_tq->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(join_s_tq->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_tq->children.size(), 2u);

    // left: JOIN(site.site_id = tag.site_id)
    const auto &join_s_t = join_s_tq->children[0];
    ASSERT_NE(join_s_t, nullptr);
    EXPECT_EQ(join_s_t->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_t->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_t->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_t->base_columns.size(), 2u);
    EXPECT_EQ(join_s_t->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_t->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_t->base_columns[1].getBaseTableName(), "tag");
    EXPECT_EQ(join_s_t->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_t->children.size(), 2u);

    // left: FILTER(badge.user_id = so_user.id)
    const auto &f_badge_uid = join_s_t->children[0];
    ASSERT_NE(f_badge_uid, nullptr);
    EXPECT_EQ(f_badge_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_badge_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_badge_uid->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_uid->base_columns[0].column_name, "user_id");
    EXPECT_EQ(f_badge_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_badge_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_badge_uid->children.size(), 1u);

    //   └─ JOIN(site.site_id = badge.site_id)
    const auto &join_s_b = f_badge_uid->children[0];
    ASSERT_NE(join_s_b, nullptr);
    EXPECT_EQ(join_s_b->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_b->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_b->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_b->base_columns.size(), 2u);
    EXPECT_EQ(join_s_b->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_b->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_b->base_columns[1].getBaseTableName(), "badge");
    EXPECT_EQ(join_s_b->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_b->children.size(), 2u);

    // left: JOIN(site.site_id = so_user.site_id)
    const auto &join_s_u = join_s_b->children[0];
    ASSERT_NE(join_s_u, nullptr);
    EXPECT_EQ(join_s_u->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_u->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_u->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_u->base_columns.size(), 2u);
    EXPECT_EQ(join_s_u->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_u->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_u->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_s_u->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_u->children.size(), 2u);

    // left: FILTER(site.site_name = 'stackoverflow') -> SCAN(site)
    const auto &f_site_eq = join_s_u->children[0];
    ASSERT_NE(f_site_eq, nullptr);
    EXPECT_EQ(f_site_eq->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_site_eq->expression.comp_type.has_value());
    EXPECT_EQ(*f_site_eq->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_site_eq->base_columns.size(), 1u);
    EXPECT_EQ(f_site_eq->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(f_site_eq->base_columns[0].column_name, "site_name");
    ASSERT_EQ(f_site_eq->expression.values.size(), 1u);
    EXPECT_EQ(f_site_eq->expression.values[0], "stackoverflow");
    ASSERT_EQ(f_site_eq->children.size(), 1u);

    const auto &scan_site = f_site_eq->children[0];
    ASSERT_NE(scan_site, nullptr);
    EXPECT_EQ(scan_site->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_site->base_table, "s");
    ASSERT_TRUE(scan_site->children.empty());

    // right: SCAN(so_user)
    const auto &scan_user = join_s_u->children[1];
    ASSERT_NE(scan_user, nullptr);
    EXPECT_EQ(scan_user->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_user->base_table, "u1");
    ASSERT_TRUE(scan_user->children.empty());

    // right child of join_s_b: SCAN(badge)
    const auto &scan_badge = join_s_b->children[1];
    ASSERT_NE(scan_badge, nullptr);
    EXPECT_EQ(scan_badge->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_badge->base_table, "b1");
    ASSERT_TRUE(scan_badge->children.empty());

    // right child of join_s_t: FILTER(tag.name IN ('latex','sum')) -> SCAN(tag)
    const auto &f_tag_in = join_s_t->children[1];
    ASSERT_NE(f_tag_in, nullptr);
    EXPECT_EQ(f_tag_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tag_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_tag_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_tag_in->base_columns.size(), 1u);
    EXPECT_EQ(f_tag_in->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tag_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_tag_in->expression.values.size(), 2u);
    EXPECT_EQ(f_tag_in->expression.values[0], "latex");
    EXPECT_EQ(f_tag_in->expression.values[1], "sum");
    ASSERT_EQ(f_tag_in->children.size(), 1u);

    const auto &scan_tag = f_tag_in->children[0];
    ASSERT_NE(scan_tag, nullptr);
    EXPECT_EQ(scan_tag->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tag->base_table, "t1");
    ASSERT_TRUE(scan_tag->children.empty());

    // right child of join_s_tq: SCAN(tag_question)
    const auto &scan_tq = join_s_tq->children[1];
    ASSERT_NE(scan_tq, nullptr);
    EXPECT_EQ(scan_tq->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tq->base_table, "tq1");
    ASSERT_TRUE(scan_tq->children.empty());

    // right child of join_s_q: FILTER(question.view_count >= 100) -> FILTER(question.view_count <= 100000) -> SCAN(question)
    const auto &f_vc_ge = join_s_q->children[1];
    ASSERT_NE(f_vc_ge, nullptr);
    EXPECT_EQ(f_vc_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_vc_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_ge->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_ge->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_ge->expression.values[0], "100");
    ASSERT_EQ(f_vc_ge->children.size(), 1u);

    const auto &f_vc_le = f_vc_ge->children[0];
    ASSERT_NE(f_vc_le, nullptr);
    EXPECT_EQ(f_vc_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_vc_le->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_le->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_le->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_le->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_le->expression.values[0], "100000");
    ASSERT_EQ(f_vc_le->children.size(), 1u);

    const auto &scan_q = f_vc_le->children[0];
    ASSERT_NE(scan_q, nullptr);
    EXPECT_EQ(scan_q->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_q->base_table, "q1");
    ASSERT_TRUE(scan_q->children.empty());
}

/*
 * Logical plan for 2f557625e3f5cb546234f92b3e6c0a212203b211:
 *
 *   LIMIT 100
 *     └─ SORT(count DESC)
 *          └─ PROJECTION(account.location, count)
 *               └─ AGGREGATE(COUNT) GROUP BY account.location
 *                    └─ JOIN(account.id = so_user.account_id)
 *                         ├─ SCAN(account)
 *                         └─ FILTER(badge.user_id = so_user.id)
 *                              └─ JOIN(site.site_id = badge.site_id)
 *                                   ├─ FILTER(question.id = tag_question.question_id)
 *                                   │    └─ FILTER(tag.id = tag_question.tag_id)
 *                                   │         └─ JOIN(site.site_id = tag_question.site_id)
 *                                   │              ├─ JOIN(site.site_id = tag.site_id)
 *                                   │              │    ├─ FILTER(question.id = answer.question_id)
 *                                   │              │    │    └─ FILTER(answer.owner_user_id = so_user.id)
 *                                   │              │    │         └─ JOIN(site.site_id = answer.site_id)
 *                                   │              │    │              ├─ JOIN(site.site_id = so_user.site_id)
 *                                   │              │    │              │    ├─ JOIN(site.site_id = question.site_id)
 *                                   │              │    │              │    │    ├─ FILTER(site.site_name IN (...))
 *                                   │              │    │              │    │    │    └─ SCAN(site)
 *                                   │              │    │              │    │    └─ FILTER(question.view_count ...)
 *                                   │              │    │              │    │         └─ SCAN(question)
 *                                   │              │    │              │    └─ FILTER(so_user.downvotes ...)
 *                                   │              │    │              │         └─ SCAN(so_user)
 *                                   │              │    │              └─ SCAN(answer)
 *                                   │              │    └─ FILTER(tag.name IN (...))
 *                                   │              │         └─ SCAN(tag)
 *                                   │              └─ SCAN(tag_question)
 *                                   └─ FILTER(badge.name IN (...))
 *                                        └─ SCAN(badge)
 */

TEST(SQLToPlanTranslatorTest, 2f557625e3f5cb546234f92b3e6c0a212203b211)
{
    const std::string query = R"(
        SELECT acc.location, count(*)
        FROM
            site as s,
            so_user as u1,
            question as q1,
            answer as a1,
            tag as t1,
            tag_question as tq1,
            badge as b,
            account as acc
        WHERE
            s.site_id = q1.site_id
            AND s.site_id = u1.site_id
            AND s.site_id = a1.site_id
            AND s.site_id = t1.site_id
            AND s.site_id = tq1.site_id
            AND s.site_id = b.site_id
            AND q1.id = tq1.question_id
            AND q1.id = a1.question_id
            AND a1.owner_user_id = u1.id
            AND t1.id = tq1.tag_id
            AND b.user_id = u1.id
            AND acc.id = u1.account_id
            AND (s.site_name in ('apple','drupal','english','ru','tex'))
            AND (t1.name in ('beamer','equations'))
            AND (q1.view_count >= 100)
            AND (q1.view_count <= 100000)
            AND (u1.downvotes >= 10)
            AND (u1.downvotes <= 100000)
            AND (b.name in ('Excavator','Explainer','Pundit','Tag Editor'))
        GROUP BY acc.location
        ORDER BY COUNT(*)
        DESC
        LIMIT 100
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);

    //  └─ SORT(order_by count DESC)
    const auto &sort = plans[0];
    ASSERT_NE(sort, nullptr);
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->children.size(), 1u);
    ASSERT_EQ(sort->base_columns.size(), 1u);
    EXPECT_EQ(sort->base_columns[0].column_name, "count1");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    ASSERT_EQ(sort->expression.sort_order->size(), 1u);
    EXPECT_FALSE((*sort->expression.sort_order)[0]); // DESC
    // LIMIT 100
    ASSERT_TRUE(sort->expression.limit_count.has_value());
    EXPECT_EQ(sort->expression.limit_count.value(), 100);

    //      └─ PROJECTION(account.location, count)
    const auto &proj = sort->children[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->children.size(), 1u);
    ASSERT_EQ(proj->projected_columns.size(), 2u);
    EXPECT_EQ(proj->projected_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(proj->projected_columns[0].column_name, "location");
    EXPECT_EQ(proj->projected_columns[1].column_name, "count1");

    //          └─ AGGREGATE(COUNT) GROUP BY account.location
    const auto &agg = proj->children[0];
    ASSERT_NE(agg, nullptr);
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_EQ(agg->children.size(), 1u);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::COUNT);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "count1");
    ASSERT_EQ(agg->base_columns.size(), 1u);
    EXPECT_EQ(agg->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(agg->base_columns[0].column_name, "location");

    //              └─ JOIN(account.id = so_user.account_id)
    const auto &join_acc = agg->children[0];
    ASSERT_NE(join_acc, nullptr);
    EXPECT_EQ(join_acc->node_type, LogicalNodeType::JOIN);
    ASSERT_EQ(join_acc->children.size(), 2u);
    ASSERT_TRUE(join_acc->expression.comp_type.has_value());
    EXPECT_EQ(*join_acc->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_acc->base_columns.size(), 2u);
    EXPECT_EQ(join_acc->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(join_acc->base_columns[0].column_name, "id");
    EXPECT_EQ(join_acc->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_acc->base_columns[1].column_name, "account_id");

    // left: SCAN(account)
    const auto &scan_acc = join_acc->children[0];
    ASSERT_NE(scan_acc, nullptr);
    EXPECT_EQ(scan_acc->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_acc->base_table, "acc");
    ASSERT_TRUE(scan_acc->children.empty());

    // right: FILTER(badge.user_id = so_user.id)
    const auto &f_badge_uid = join_acc->children[1];
    ASSERT_NE(f_badge_uid, nullptr);
    EXPECT_EQ(f_badge_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_badge_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_badge_uid->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_uid->base_columns[0].column_name, "user_id");
    EXPECT_EQ(f_badge_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_badge_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_badge_uid->children.size(), 1u);

    // (From here on, we match the remaining skeleton exactly as printed)

    //   └─ JOIN(site.site_id = badge.site_id)
    const auto &join_s_b = f_badge_uid->children[0];
    ASSERT_NE(join_s_b, nullptr);
    EXPECT_EQ(join_s_b->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_b->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_b->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_b->base_columns.size(), 2u);
    EXPECT_EQ(join_s_b->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_b->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_b->base_columns[1].getBaseTableName(), "badge");
    EXPECT_EQ(join_s_b->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_b->children.size(), 2u);

    // left: FILTER(question.id = tag_question.question_id)
    const auto &f_qid_tqid = join_s_b->children[0];
    ASSERT_NE(f_qid_tqid, nullptr);
    EXPECT_EQ(f_qid_tqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_tqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_tqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_tqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_tqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_tqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_tqid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_qid_tqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_tqid->children.size(), 1u);

    //  └─ FILTER(tag.id = tag_question.tag_id)
    const auto &f_tid_tqtid = f_qid_tqid->children[0];
    ASSERT_NE(f_tid_tqtid, nullptr);
    EXPECT_EQ(f_tid_tqtid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tid_tqtid->expression.comp_type.has_value());
    EXPECT_EQ(*f_tid_tqtid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_tid_tqtid->base_columns.size(), 2u);
    EXPECT_EQ(f_tid_tqtid->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tid_tqtid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].column_name, "tag_id");
    ASSERT_EQ(f_tid_tqtid->children.size(), 1u);

    //    └─ JOIN(site.site_id = tag_question.site_id)
    const auto &join_s_tq = f_tid_tqtid->children[0];
    ASSERT_NE(join_s_tq, nullptr);
    EXPECT_EQ(join_s_tq->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_tq->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_tq->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_tq->base_columns.size(), 2u);
    EXPECT_EQ(join_s_tq->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_tq->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_tq->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(join_s_tq->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_tq->children.size(), 2u);

    // left: JOIN(site.site_id = tag.site_id)
    const auto &join_s_t = join_s_tq->children[0];
    ASSERT_NE(join_s_t, nullptr);
    EXPECT_EQ(join_s_t->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_t->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_t->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_t->base_columns.size(), 2u);
    EXPECT_EQ(join_s_t->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_t->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_t->base_columns[1].getBaseTableName(), "tag");
    EXPECT_EQ(join_s_t->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_t->children.size(), 2u);

    // left: FILTER(question.id = answer.question_id)
    const auto &f_qid_aqid = join_s_t->children[0];
    ASSERT_NE(f_qid_aqid, nullptr);
    EXPECT_EQ(f_qid_aqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_aqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_aqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_aqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_aqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_aqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_aqid->base_columns[1].getBaseTableName(), "answer");
    EXPECT_EQ(f_qid_aqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_aqid->children.size(), 1u);

    //  └─ FILTER(answer.owner_user_id = so_user.id)
    const auto &f_a_owner = f_qid_aqid->children[0];
    ASSERT_NE(f_a_owner, nullptr);
    EXPECT_EQ(f_a_owner->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_a_owner->expression.comp_type.has_value());
    EXPECT_EQ(*f_a_owner->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_a_owner->base_columns.size(), 2u);
    EXPECT_EQ(f_a_owner->base_columns[0].getBaseTableName(), "answer");
    EXPECT_EQ(f_a_owner->base_columns[0].column_name, "owner_user_id");
    EXPECT_EQ(f_a_owner->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_a_owner->base_columns[1].column_name, "id");
    ASSERT_EQ(f_a_owner->children.size(), 1u);

    //     └─ JOIN(site.site_id = answer.site_id)
    const auto &join_s_a = f_a_owner->children[0];
    ASSERT_NE(join_s_a, nullptr);
    EXPECT_EQ(join_s_a->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_a->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_a->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_a->base_columns.size(), 2u);
    EXPECT_EQ(join_s_a->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_a->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_a->base_columns[1].getBaseTableName(), "answer");
    EXPECT_EQ(join_s_a->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_a->children.size(), 2u);

    // left: JOIN(site.site_id = so_user.site_id)
    const auto &join_s_u = join_s_a->children[0];
    ASSERT_NE(join_s_u, nullptr);
    EXPECT_EQ(join_s_u->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_u->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_u->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_u->base_columns.size(), 2u);
    EXPECT_EQ(join_s_u->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_u->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_u->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_s_u->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_u->children.size(), 2u);

    // left: JOIN(site.site_id = question.site_id)
    const auto &join_s_q = join_s_u->children[0];
    ASSERT_NE(join_s_q, nullptr);
    EXPECT_EQ(join_s_q->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_q->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_q->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_q->base_columns.size(), 2u);
    EXPECT_EQ(join_s_q->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_q->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_q->base_columns[1].getBaseTableName(), "question");
    EXPECT_EQ(join_s_q->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_q->children.size(), 2u);

    // left: FILTER(site.site_name IN (...)) -> SCAN(site)
    const auto &f_site_in = join_s_q->children[0];
    ASSERT_NE(f_site_in, nullptr);
    EXPECT_EQ(f_site_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_site_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_site_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_site_in->base_columns.size(), 1u);
    EXPECT_EQ(f_site_in->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(f_site_in->base_columns[0].column_name, "site_name");
    ASSERT_EQ(f_site_in->expression.values.size(), 5u);
    EXPECT_EQ(f_site_in->expression.values[0], "apple");
    EXPECT_EQ(f_site_in->expression.values[1], "drupal");
    EXPECT_EQ(f_site_in->expression.values[2], "english");
    EXPECT_EQ(f_site_in->expression.values[3], "ru");
    EXPECT_EQ(f_site_in->expression.values[4], "tex");
    ASSERT_EQ(f_site_in->children.size(), 1u);

    const auto &scan_site = f_site_in->children[0];
    ASSERT_NE(scan_site, nullptr);
    EXPECT_EQ(scan_site->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_site->base_table, "s");
    ASSERT_TRUE(scan_site->children.empty());

    // right: FILTER(question.view_count >= 100) -> FILTER(question.view_count <= 100000) -> SCAN(question)
    const auto &f_vc_ge = join_s_q->children[1];
    ASSERT_NE(f_vc_ge, nullptr);
    EXPECT_EQ(f_vc_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_vc_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_ge->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_ge->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_ge->expression.values[0], "100");
    ASSERT_EQ(f_vc_ge->children.size(), 1u);

    const auto &f_vc_le = f_vc_ge->children[0];
    ASSERT_NE(f_vc_le, nullptr);
    EXPECT_EQ(f_vc_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_vc_le->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_le->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_le->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_le->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_le->expression.values[0], "100000");
    ASSERT_EQ(f_vc_le->children.size(), 1u);

    const auto &scan_q = f_vc_le->children[0];
    ASSERT_NE(scan_q, nullptr);
    EXPECT_EQ(scan_q->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_q->base_table, "q1");
    ASSERT_TRUE(scan_q->children.empty());

    // right: FILTER(so_user.downvotes >= 10) -> FILTER(so_user.downvotes <= 100000) -> SCAN(so_user)
    const auto &f_down_ge = join_s_u->children[1];
    ASSERT_NE(f_down_ge, nullptr);
    EXPECT_EQ(f_down_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_down_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_down_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_down_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_down_ge->base_columns[0].getBaseTableName(), "so_user");
    EXPECT_EQ(f_down_ge->base_columns[0].column_name, "downvotes");
    ASSERT_EQ(f_down_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_down_ge->expression.values[0], "10");
    ASSERT_EQ(f_down_ge->children.size(), 1u);

    const auto &f_down_le = f_down_ge->children[0];
    ASSERT_NE(f_down_le, nullptr);
    EXPECT_EQ(f_down_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_down_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_down_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_down_le->base_columns.size(), 1u);
    EXPECT_EQ(f_down_le->base_columns[0].getBaseTableName(), "so_user");
    EXPECT_EQ(f_down_le->base_columns[0].column_name, "downvotes");
    ASSERT_EQ(f_down_le->expression.values.size(), 1u);
    EXPECT_EQ(f_down_le->expression.values[0], "100000");
    ASSERT_EQ(f_down_le->children.size(), 1u);

    const auto &scan_u = f_down_le->children[0];
    ASSERT_NE(scan_u, nullptr);
    EXPECT_EQ(scan_u->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_u->base_table, "u1");
    ASSERT_TRUE(scan_u->children.empty());

    // right child of join_s_a: SCAN(answer)
    const auto &scan_a = join_s_a->children[1];
    ASSERT_NE(scan_a, nullptr);
    EXPECT_EQ(scan_a->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_a->base_table, "a1");
    ASSERT_TRUE(scan_a->children.empty());

    // right child of join_s_t: FILTER(tag.name IN ('beamer','equations')) -> SCAN(tag)
    const auto &f_tag_in = join_s_t->children[1];
    ASSERT_NE(f_tag_in, nullptr);
    EXPECT_EQ(f_tag_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tag_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_tag_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_tag_in->base_columns.size(), 1u);
    EXPECT_EQ(f_tag_in->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tag_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_tag_in->expression.values.size(), 2u);
    EXPECT_EQ(f_tag_in->expression.values[0], "beamer");
    EXPECT_EQ(f_tag_in->expression.values[1], "equations");
    ASSERT_EQ(f_tag_in->children.size(), 1u);

    const auto &scan_tag = f_tag_in->children[0];
    ASSERT_NE(scan_tag, nullptr);
    EXPECT_EQ(scan_tag->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tag->base_table, "t1");
    ASSERT_TRUE(scan_tag->children.empty());

    // right child of join_s_tq: SCAN(tag_question)
    const auto &scan_tq = join_s_tq->children[1];
    ASSERT_NE(scan_tq, nullptr);
    EXPECT_EQ(scan_tq->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tq->base_table, "tq1");
    ASSERT_TRUE(scan_tq->children.empty());

    // right child of f_badge_uid chain bottom: FILTER(badge.name IN (...)) -> SCAN(badge)
    const auto &f_badge_name_in = join_s_b->children[1];
    ASSERT_NE(f_badge_name_in, nullptr);
    EXPECT_EQ(f_badge_name_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_name_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_name_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_badge_name_in->base_columns.size(), 1u);
    EXPECT_EQ(f_badge_name_in->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_name_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_badge_name_in->expression.values.size(), 4u);
    EXPECT_EQ(f_badge_name_in->expression.values[0], "Excavator");
    EXPECT_EQ(f_badge_name_in->expression.values[1], "Explainer");
    EXPECT_EQ(f_badge_name_in->expression.values[2], "Pundit");
    EXPECT_EQ(f_badge_name_in->expression.values[3], "Tag Editor");
    ASSERT_EQ(f_badge_name_in->children.size(), 1u);

    const auto &scan_badge = f_badge_name_in->children[0];
    ASSERT_NE(scan_badge, nullptr);
    EXPECT_EQ(scan_badge->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_badge->base_table, "b");
    ASSERT_TRUE(scan_badge->children.empty());
}

/*
 * Logical plan for 8a462a4e71f6fd5c0aff0ae4817a16bbb2cfb37f:
 *
 *   LIMIT 100
 *     └─ SORT(count DESC)
 *          └─ PROJECTION(account.location, count)
 *               └─ AGGREGATE(COUNT) GROUP BY account.location
 *                    └─ JOIN(account.id = so_user.account_id)
 *                         ├─ SCAN(account)
 *                         └─ FILTER(badge.user_id = so_user.id)
 *                              └─ JOIN(site.site_id = badge.site_id)
 *                                   ├─ FILTER(question.id = tag_question.question_id)
 *                                   │    └─ FILTER(tag.id = tag_question.tag_id)
 *                                   │         └─ JOIN(site.site_id = tag_question.site_id)
 *                                   │              ├─ JOIN(site.site_id = tag.site_id)
 *                                   │              │    ├─ FILTER(question.id = answer.question_id)
 *                                   │              │    │    └─ FILTER(answer.owner_user_id = so_user.id)
 *                                   │              │    │         └─ JOIN(site.site_id = answer.site_id)
 *                                   │              │    │              ├─ JOIN(site.site_id = so_user.site_id)
 *                                   │              │    │              │    ├─ JOIN(site.site_id = question.site_id)
 *                                   │              │    │              │    │    ├─ FILTER(site.site_name IN ('stackoverflow','superuser'))
 *                                   │              │    │              │    │    │    └─ SCAN(site)
 *                                   │              │    │              │    │    └─ FILTER(question.view_count >= 0)
 *                                   │              │    │              │    │         └─ FILTER(question.view_count <= 100)
 *                                   │              │    │              │    │              └─ SCAN(question)
 *                                   │              │    │              │    └─ FILTER(so_user.downvotes >= 0)
 *                                   │              │    │              │         └─ FILTER(so_user.downvotes <= 10)
 *                                   │              │    │              │              └─ SCAN(so_user)
 *                                   │              │    │              └─ SCAN(answer)
 *                                   │              │    └─ FILTER(tag.name IN (...12...))
 *                                   │              │         └─ SCAN(tag)
 *                                   │              └─ SCAN(tag_question)
 *                                   └─ FILTER(badge.name IN (...5...))
 *                                        └─ SCAN(badge)
 */

TEST(SQLToPlanTranslatorTest, 8a462a4e71f6fd5c0aff0ae4817a16bbb2cfb37f)
{
    const std::string query = R"(
        SELECT acc.location, count(*)
        FROM
            site as s,
            so_user as u1,
            question as q1,
            answer as a1,
            tag as t1,
            tag_question as tq1,
            badge as b,
            account as acc
        WHERE
            s.site_id = q1.site_id
            AND s.site_id = u1.site_id
            AND s.site_id = a1.site_id
            AND s.site_id = t1.site_id
            AND s.site_id = tq1.site_id
            AND s.site_id = b.site_id
            AND q1.id = tq1.question_id
            AND q1.id = a1.question_id
            AND a1.owner_user_id = u1.id
            AND t1.id = tq1.tag_id
            AND b.user_id = u1.id
            AND acc.id = u1.account_id
            AND (s.site_name in ('stackoverflow','superuser'))
            AND (t1.name in ('abstract-class','admob','anaconda','applet','cloud','list-comprehension','lodash','microsoft-graph','router','tidyverse','union','uri'))
            AND (q1.view_count >= 0)
            AND (q1.view_count <= 100)
            AND (u1.downvotes >= 0)
            AND (u1.downvotes <= 10)
            AND (b.name in ('Documentation User','Lifejacket','Populist','Proofreader','Pundit'))
        GROUP BY acc.location
        ORDER BY COUNT(*)
        DESC
        LIMIT 100
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);

    //  └─ SORT(order_by count DESC)
    const auto &sort = plans[0];
    ASSERT_NE(sort, nullptr);
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->children.size(), 1u);
    ASSERT_EQ(sort->base_columns.size(), 1u);
    EXPECT_EQ(sort->base_columns[0].column_name, "count1");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    ASSERT_EQ(sort->expression.sort_order->size(), 1u);
    EXPECT_FALSE((*sort->expression.sort_order)[0]); // DESC
    // LIMIT 100
    ASSERT_TRUE(sort->expression.limit_count.has_value());
    EXPECT_EQ(sort->expression.limit_count.value(), 100);

    //      └─ PROJECTION(account.location, count)
    const auto &proj = sort->children[0];
    ASSERT_NE(proj, nullptr);
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->children.size(), 1u);
    ASSERT_EQ(proj->projected_columns.size(), 2u);
    EXPECT_EQ(proj->projected_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(proj->projected_columns[0].column_name, "location");
    EXPECT_EQ(proj->projected_columns[1].column_name, "count1");

    //          └─ AGGREGATE(COUNT) GROUP BY account.location
    const auto &agg = proj->children[0];
    ASSERT_NE(agg, nullptr);
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_EQ(agg->children.size(), 1u);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::COUNT);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "count1");
    ASSERT_EQ(agg->base_columns.size(), 1u);
    EXPECT_EQ(agg->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(agg->base_columns[0].column_name, "location");

    //              └─ JOIN(account.id = so_user.account_id)
    const auto &join_acc = agg->children[0];
    ASSERT_NE(join_acc, nullptr);
    EXPECT_EQ(join_acc->node_type, LogicalNodeType::JOIN);
    ASSERT_EQ(join_acc->children.size(), 2u);
    ASSERT_TRUE(join_acc->expression.comp_type.has_value());
    EXPECT_EQ(*join_acc->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_acc->base_columns.size(), 2u);
    EXPECT_EQ(join_acc->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(join_acc->base_columns[0].column_name, "id");
    EXPECT_EQ(join_acc->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_acc->base_columns[1].column_name, "account_id");

    // left: SCAN(account)
    const auto &scan_acc = join_acc->children[0];
    ASSERT_NE(scan_acc, nullptr);
    EXPECT_EQ(scan_acc->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_acc->base_table, "acc");
    ASSERT_TRUE(scan_acc->children.empty());

    // right: FILTER(badge.user_id = so_user.id)
    const auto &f_badge_uid = join_acc->children[1];
    ASSERT_NE(f_badge_uid, nullptr);
    EXPECT_EQ(f_badge_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_badge_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_badge_uid->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_uid->base_columns[0].column_name, "user_id");
    EXPECT_EQ(f_badge_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_badge_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_badge_uid->children.size(), 1u);

    //   └─ JOIN(site.site_id = badge.site_id)
    const auto &join_s_b = f_badge_uid->children[0];
    ASSERT_NE(join_s_b, nullptr);
    EXPECT_EQ(join_s_b->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_b->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_b->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_b->base_columns.size(), 2u);
    EXPECT_EQ(join_s_b->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_b->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_b->base_columns[1].getBaseTableName(), "badge");
    EXPECT_EQ(join_s_b->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_b->children.size(), 2u);

    // left: FILTER(question.id = tag_question.question_id)
    const auto &f_qid_tqid = join_s_b->children[0];
    ASSERT_NE(f_qid_tqid, nullptr);
    EXPECT_EQ(f_qid_tqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_tqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_tqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_tqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_tqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_tqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_tqid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_qid_tqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_tqid->children.size(), 1u);

    //   └─ FILTER(tag.id = tag_question.tag_id)
    const auto &f_tid_tqtid = f_qid_tqid->children[0];
    ASSERT_NE(f_tid_tqtid, nullptr);
    EXPECT_EQ(f_tid_tqtid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tid_tqtid->expression.comp_type.has_value());
    EXPECT_EQ(*f_tid_tqtid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_tid_tqtid->base_columns.size(), 2u);
    EXPECT_EQ(f_tid_tqtid->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tid_tqtid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].column_name, "tag_id");
    ASSERT_EQ(f_tid_tqtid->children.size(), 1u);

    //       └─ JOIN(site.site_id = tag_question.site_id)
    const auto &join_s_tq = f_tid_tqtid->children[0];
    ASSERT_NE(join_s_tq, nullptr);
    EXPECT_EQ(join_s_tq->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_tq->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_tq->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_tq->base_columns.size(), 2u);
    EXPECT_EQ(join_s_tq->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_tq->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_tq->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(join_s_tq->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_tq->children.size(), 2u);

    // left: JOIN(site.site_id = tag.site_id)
    const auto &join_s_t = join_s_tq->children[0];
    ASSERT_NE(join_s_t, nullptr);
    EXPECT_EQ(join_s_t->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_t->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_t->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_t->base_columns.size(), 2u);
    EXPECT_EQ(join_s_t->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_t->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_t->base_columns[1].getBaseTableName(), "tag");
    EXPECT_EQ(join_s_t->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_t->children.size(), 2u);

    // left: FILTER(question.id = answer.question_id)
    const auto &f_qid_aqid = join_s_t->children[0];
    ASSERT_NE(f_qid_aqid, nullptr);
    EXPECT_EQ(f_qid_aqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_aqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_aqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_aqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_aqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_aqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_aqid->base_columns[1].getBaseTableName(), "answer");
    EXPECT_EQ(f_qid_aqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_aqid->children.size(), 1u);

    //   └─ FILTER(answer.owner_user_id = so_user.id)
    const auto &f_a_owner = f_qid_aqid->children[0];
    ASSERT_NE(f_a_owner, nullptr);
    EXPECT_EQ(f_a_owner->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_a_owner->expression.comp_type.has_value());
    EXPECT_EQ(*f_a_owner->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_a_owner->base_columns.size(), 2u);
    EXPECT_EQ(f_a_owner->base_columns[0].getBaseTableName(), "answer");
    EXPECT_EQ(f_a_owner->base_columns[0].column_name, "owner_user_id");
    EXPECT_EQ(f_a_owner->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_a_owner->base_columns[1].column_name, "id");
    ASSERT_EQ(f_a_owner->children.size(), 1u);

    //      └─ JOIN(site.site_id = answer.site_id)
    const auto &join_s_a = f_a_owner->children[0];
    ASSERT_NE(join_s_a, nullptr);
    EXPECT_EQ(join_s_a->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_a->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_a->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_a->base_columns.size(), 2u);
    EXPECT_EQ(join_s_a->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_a->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_a->base_columns[1].getBaseTableName(), "answer");
    EXPECT_EQ(join_s_a->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_a->children.size(), 2u);

    // left: JOIN(site.site_id = so_user.site_id)
    const auto &join_s_u = join_s_a->children[0];
    ASSERT_NE(join_s_u, nullptr);
    EXPECT_EQ(join_s_u->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_u->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_u->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_u->base_columns.size(), 2u);
    EXPECT_EQ(join_s_u->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_u->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_u->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_s_u->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_u->children.size(), 2u);

    // left: JOIN(site.site_id = question.site_id)
    const auto &join_s_q = join_s_u->children[0];
    ASSERT_NE(join_s_q, nullptr);
    EXPECT_EQ(join_s_q->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_q->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_q->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_q->base_columns.size(), 2u);
    EXPECT_EQ(join_s_q->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_q->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_q->base_columns[1].getBaseTableName(), "question");
    EXPECT_EQ(join_s_q->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_q->children.size(), 2u);

    // left: FILTER(site.site_name IN ('stackoverflow','superuser')) -> SCAN(site)
    const auto &f_site_in = join_s_q->children[0];
    ASSERT_NE(f_site_in, nullptr);
    EXPECT_EQ(f_site_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_site_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_site_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_site_in->base_columns.size(), 1u);
    EXPECT_EQ(f_site_in->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(f_site_in->base_columns[0].column_name, "site_name");
    ASSERT_EQ(f_site_in->expression.values.size(), 2u);
    EXPECT_EQ(f_site_in->expression.values[0], "stackoverflow");
    EXPECT_EQ(f_site_in->expression.values[1], "superuser");
    ASSERT_EQ(f_site_in->children.size(), 1u);

    const auto &scan_site = f_site_in->children[0];
    ASSERT_NE(scan_site, nullptr);
    EXPECT_EQ(scan_site->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_site->base_table, "s");
    ASSERT_TRUE(scan_site->children.empty());

    // right: FILTER(question.view_count >= 0) -> FILTER(question.view_count <= 100) -> SCAN(question)
    const auto &f_vc_ge = join_s_q->children[1];
    ASSERT_NE(f_vc_ge, nullptr);
    EXPECT_EQ(f_vc_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_vc_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_ge->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_ge->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_ge->expression.values[0], "0");
    ASSERT_EQ(f_vc_ge->children.size(), 1u);

    const auto &f_vc_le = f_vc_ge->children[0];
    ASSERT_NE(f_vc_le, nullptr);
    EXPECT_EQ(f_vc_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_vc_le->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_le->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_le->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_le->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_le->expression.values[0], "100");
    ASSERT_EQ(f_vc_le->children.size(), 1u);

    const auto &scan_q = f_vc_le->children[0];
    ASSERT_NE(scan_q, nullptr);
    EXPECT_EQ(scan_q->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_q->base_table, "q1");
    ASSERT_TRUE(scan_q->children.empty());

    // right: FILTER(so_user.downvotes >= 0) -> FILTER(so_user.downvotes <= 10) -> SCAN(so_user)
    const auto &f_down_ge = join_s_u->children[1];
    ASSERT_NE(f_down_ge, nullptr);
    EXPECT_EQ(f_down_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_down_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_down_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_down_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_down_ge->base_columns[0].getBaseTableName(), "so_user");
    EXPECT_EQ(f_down_ge->base_columns[0].column_name, "downvotes");
    ASSERT_EQ(f_down_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_down_ge->expression.values[0], "0");
    ASSERT_EQ(f_down_ge->children.size(), 1u);

    const auto &f_down_le = f_down_ge->children[0];
    ASSERT_NE(f_down_le, nullptr);
    EXPECT_EQ(f_down_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_down_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_down_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_down_le->base_columns.size(), 1u);
    EXPECT_EQ(f_down_le->base_columns[0].getBaseTableName(), "so_user");
    EXPECT_EQ(f_down_le->base_columns[0].column_name, "downvotes");
    ASSERT_EQ(f_down_le->expression.values.size(), 1u);
    EXPECT_EQ(f_down_le->expression.values[0], "10");
    ASSERT_EQ(f_down_le->children.size(), 1u);

    const auto &scan_u = f_down_le->children[0];
    ASSERT_NE(scan_u, nullptr);
    EXPECT_EQ(scan_u->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_u->base_table, "u1");
    ASSERT_TRUE(scan_u->children.empty());

    // right child of join_s_a: SCAN(answer)
    const auto &scan_a = join_s_a->children[1];
    ASSERT_NE(scan_a, nullptr);
    EXPECT_EQ(scan_a->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_a->base_table, "a1");
    ASSERT_TRUE(scan_a->children.empty());

    // right child of join_s_t: FILTER(tag.name IN (...12...)) -> SCAN(tag)
    const auto &f_tag_in = join_s_t->children[1];
    ASSERT_NE(f_tag_in, nullptr);
    EXPECT_EQ(f_tag_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tag_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_tag_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_tag_in->base_columns.size(), 1u);
    EXPECT_EQ(f_tag_in->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tag_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_tag_in->expression.values.size(), 12u);
    EXPECT_EQ(f_tag_in->expression.values[0], "abstract-class");
    EXPECT_EQ(f_tag_in->expression.values[1], "admob");
    EXPECT_EQ(f_tag_in->expression.values[2], "anaconda");
    EXPECT_EQ(f_tag_in->expression.values[3], "applet");
    EXPECT_EQ(f_tag_in->expression.values[4], "cloud");
    EXPECT_EQ(f_tag_in->expression.values[5], "list-comprehension");
    EXPECT_EQ(f_tag_in->expression.values[6], "lodash");
    EXPECT_EQ(f_tag_in->expression.values[7], "microsoft-graph");
    EXPECT_EQ(f_tag_in->expression.values[8], "router");
    EXPECT_EQ(f_tag_in->expression.values[9], "tidyverse");
    EXPECT_EQ(f_tag_in->expression.values[10], "union");
    EXPECT_EQ(f_tag_in->expression.values[11], "uri");
    ASSERT_EQ(f_tag_in->children.size(), 1u);

    const auto &scan_tag = f_tag_in->children[0];
    ASSERT_NE(scan_tag, nullptr);
    EXPECT_EQ(scan_tag->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tag->base_table, "t1");
    ASSERT_TRUE(scan_tag->children.empty());

    // right child of join_s_tq: SCAN(tag_question)
    const auto &scan_tq = join_s_tq->children[1];
    ASSERT_NE(scan_tq, nullptr);
    EXPECT_EQ(scan_tq->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tq->base_table, "tq1");
    ASSERT_TRUE(scan_tq->children.empty());

    // right branch of join_s_b: FILTER(badge.name IN (...5...)) -> SCAN(badge)
    const auto &f_badge_name_in = join_s_b->children[1];
    ASSERT_NE(f_badge_name_in, nullptr);
    EXPECT_EQ(f_badge_name_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_name_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_name_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_badge_name_in->base_columns.size(), 1u);
    EXPECT_EQ(f_badge_name_in->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_name_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_badge_name_in->expression.values.size(), 5u);
    EXPECT_EQ(f_badge_name_in->expression.values[0], "Documentation User");
    EXPECT_EQ(f_badge_name_in->expression.values[1], "Lifejacket");
    EXPECT_EQ(f_badge_name_in->expression.values[2], "Populist");
    EXPECT_EQ(f_badge_name_in->expression.values[3], "Proofreader");
    EXPECT_EQ(f_badge_name_in->expression.values[4], "Pundit");
    ASSERT_EQ(f_badge_name_in->children.size(), 1u);

    const auto &scan_badge = f_badge_name_in->children[0];
    ASSERT_NE(scan_badge, nullptr);
    EXPECT_EQ(scan_badge->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_badge->base_table, "b");
    ASSERT_TRUE(scan_badge->children.empty());
}

/*
 * Logical plan for 1a82e99b3a66b745a15706cebd215a3514871eaa:
 *
 *   PROJECTION(count)
 *     └─ AGGREGATE(COUNT) alias=count
 *          └─ JOIN(account.id = so_user.account_id)
 *               ├─ SCAN(account)
 *               └─ FILTER(badge.user_id = so_user.id)
 *                    └─ JOIN(site.site_id = badge.site_id)
 *                         ├─ FILTER(question.id = tag_question.question_id)
 *                         │    └─ FILTER(tag.id = tag_question.tag_id)
 *                         │         └─ JOIN(site.site_id = tag_question.site_id)
 *                         │              ├─ JOIN(site.site_id = tag.site_id)
 *                         │              │    ├─ FILTER(question.id = answer.question_id)
 *                         │              │    │    └─ FILTER(answer.owner_user_id = so_user.id)
 *                         │              │    │         └─ JOIN(site.site_id = answer.site_id)
 *                         │              │    │              ├─ JOIN(site.site_id = so_user.site_id)
 *                         │              │    │              │    ├─ JOIN(site.site_id = question.site_id)
 *                         │              │    │              │    │    ├─ FILTER(site.site_name IN ('academia','blender','chemistry','ell','security'))
 *                         │              │    │              │    │    │    └─ SCAN(site)
 *                         │              │    │              │    │    └─ FILTER(question.view_count >= 100)
 *                         │              │    │              │    │         └─ FILTER(question.view_count <= 100000)
 *                         │              │    │              │    │              └─ SCAN(question)
 *                         │              │    │              │    └─ FILTER(so_user.downvotes >= 10)
 *                         │              │    │              │         └─ FILTER(so_user.downvotes <= 100000)
 *                         │              │    │              │              └─ SCAN(so_user)
 *                         │              │    │              └─ SCAN(answer)
 *                         │              │    └─ FILTER(tag.name IN (...10...))
 *                         │              │         └─ SCAN(tag)
 *                         │              └─ SCAN(tag_question)
 *                         └─ FILTER(badge.name LIKE '%ni%')   // LOWER(..) captured via string_op
 *                              └─ SCAN(badge)
 */

TEST(SQLToPlanTranslatorTest, 1a82e99b3a66b745a15706cebd215a3514871eaa)
{
    const std::string query = R"(
        SELECT COUNT(*)
        FROM
            site as s,
            so_user as u1,
            question as q1,
            answer as a1,
            tag as t1,
            tag_question as tq1,
            badge as b,
            account as acc
        WHERE
            s.site_id = q1.site_id
            AND s.site_id = u1.site_id
            AND s.site_id = a1.site_id
            AND s.site_id = t1.site_id
            AND s.site_id = tq1.site_id
            AND s.site_id = b.site_id
            AND q1.id = tq1.question_id
            AND q1.id = a1.question_id
            AND a1.owner_user_id = u1.id
            AND t1.id = tq1.tag_id
            AND b.user_id = u1.id
            AND acc.id = u1.account_id
            AND (s.site_name in ('academia','blender','chemistry','ell','security'))
            AND (t1.name in ('career-path','computer-science','experimental-chemistry','export','homework','http','nomenclature','sentence-meaning','wifi','xss'))
            AND (q1.view_count >= 100)
            AND (q1.view_count <= 100000)
            AND (u1.downvotes >= 10)
            AND (u1.downvotes <= 100000)
            AND (LOWER(b.name) LIKE LOWER('%ni%'))
    )";

    auto plans = translateSQLToLogicalPlan(query);
    ASSERT_EQ(plans.size(), 1u);
    const auto &proj = plans[0];
    ASSERT_NE(proj, nullptr);

    // PROJECTION(count)
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->children.size(), 1u);
    ASSERT_EQ(proj->projected_columns.size(), 1u);
    EXPECT_EQ(proj->projected_columns[0].column_name, "count1");

    //  └─ AGGREGATE(COUNT) alias=count
    const auto &agg = proj->children[0];
    ASSERT_NE(agg, nullptr);
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_EQ(agg->children.size(), 1u);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::COUNT);
    ASSERT_TRUE(agg->expression.agg_specs[0].result_alias.has_value());
    EXPECT_EQ(*agg->expression.agg_specs[0].result_alias, "count1");

    //      └─ JOIN(account.id = so_user.account_id)
    const auto &join_acc = agg->children[0];
    ASSERT_NE(join_acc, nullptr);
    EXPECT_EQ(join_acc->node_type, LogicalNodeType::JOIN);
    ASSERT_EQ(join_acc->children.size(), 2u);
    ASSERT_TRUE(join_acc->expression.comp_type.has_value());
    EXPECT_EQ(*join_acc->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_acc->base_columns.size(), 2u);
    EXPECT_EQ(join_acc->base_columns[0].getBaseTableName(), "account");
    EXPECT_EQ(join_acc->base_columns[0].column_name, "id");
    EXPECT_EQ(join_acc->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_acc->base_columns[1].column_name, "account_id");

    // left: SCAN(account)
    const auto &scan_acc = join_acc->children[0];
    ASSERT_NE(scan_acc, nullptr);
    EXPECT_EQ(scan_acc->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_acc->base_table, "acc");
    ASSERT_TRUE(scan_acc->children.empty());

    // right: FILTER(badge.user_id = so_user.id)
    const auto &f_badge_uid = join_acc->children[1];
    ASSERT_NE(f_badge_uid, nullptr);
    EXPECT_EQ(f_badge_uid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_uid->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_uid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_badge_uid->base_columns.size(), 2u);
    EXPECT_EQ(f_badge_uid->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_uid->base_columns[0].column_name, "user_id");
    EXPECT_EQ(f_badge_uid->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_badge_uid->base_columns[1].column_name, "id");
    ASSERT_EQ(f_badge_uid->children.size(), 1u);

    //   └─ JOIN(site.site_id = badge.site_id)
    const auto &join_s_b = f_badge_uid->children[0];
    ASSERT_NE(join_s_b, nullptr);
    EXPECT_EQ(join_s_b->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_b->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_b->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_b->base_columns.size(), 2u);
    EXPECT_EQ(join_s_b->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_b->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_b->base_columns[1].getBaseTableName(), "badge");
    EXPECT_EQ(join_s_b->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_b->children.size(), 2u);

    // left: FILTER(question.id = tag_question.question_id)
    const auto &f_qid_tqid = join_s_b->children[0];
    ASSERT_NE(f_qid_tqid, nullptr);
    EXPECT_EQ(f_qid_tqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_tqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_tqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_tqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_tqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_tqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_tqid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_qid_tqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_tqid->children.size(), 1u);

    //   └─ FILTER(tag.id = tag_question.tag_id)
    const auto &f_tid_tqtid = f_qid_tqid->children[0];
    ASSERT_NE(f_tid_tqtid, nullptr);
    EXPECT_EQ(f_tid_tqtid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tid_tqtid->expression.comp_type.has_value());
    EXPECT_EQ(*f_tid_tqtid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_tid_tqtid->base_columns.size(), 2u);
    EXPECT_EQ(f_tid_tqtid->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tid_tqtid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(f_tid_tqtid->base_columns[1].column_name, "tag_id");
    ASSERT_EQ(f_tid_tqtid->children.size(), 1u);

    //       └─ JOIN(site.site_id = tag_question.site_id)
    const auto &join_s_tq = f_tid_tqtid->children[0];
    ASSERT_NE(join_s_tq, nullptr);
    EXPECT_EQ(join_s_tq->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_tq->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_tq->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_tq->base_columns.size(), 2u);
    EXPECT_EQ(join_s_tq->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_tq->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_tq->base_columns[1].getBaseTableName(), "tag_question");
    EXPECT_EQ(join_s_tq->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_tq->children.size(), 2u);

    // left: JOIN(site.site_id = tag.site_id)
    const auto &join_s_t = join_s_tq->children[0];
    ASSERT_NE(join_s_t, nullptr);
    EXPECT_EQ(join_s_t->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_t->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_t->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_t->base_columns.size(), 2u);
    EXPECT_EQ(join_s_t->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_t->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_t->base_columns[1].getBaseTableName(), "tag");
    EXPECT_EQ(join_s_t->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_t->children.size(), 2u);

    // left: FILTER(question.id = answer.question_id)
    const auto &f_qid_aqid = join_s_t->children[0];
    ASSERT_NE(f_qid_aqid, nullptr);
    EXPECT_EQ(f_qid_aqid->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qid_aqid->expression.comp_type.has_value());
    EXPECT_EQ(*f_qid_aqid->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_qid_aqid->base_columns.size(), 2u);
    EXPECT_EQ(f_qid_aqid->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_qid_aqid->base_columns[0].column_name, "id");
    EXPECT_EQ(f_qid_aqid->base_columns[1].getBaseTableName(), "answer");
    EXPECT_EQ(f_qid_aqid->base_columns[1].column_name, "question_id");
    ASSERT_EQ(f_qid_aqid->children.size(), 1u);

    //   └─ FILTER(answer.owner_user_id = so_user.id)
    const auto &f_a_owner = f_qid_aqid->children[0];
    ASSERT_NE(f_a_owner, nullptr);
    EXPECT_EQ(f_a_owner->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_a_owner->expression.comp_type.has_value());
    EXPECT_EQ(*f_a_owner->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_a_owner->base_columns.size(), 2u);
    EXPECT_EQ(f_a_owner->base_columns[0].getBaseTableName(), "answer");
    EXPECT_EQ(f_a_owner->base_columns[0].column_name, "owner_user_id");
    EXPECT_EQ(f_a_owner->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(f_a_owner->base_columns[1].column_name, "id");
    ASSERT_EQ(f_a_owner->children.size(), 1u);

    //      └─ JOIN(site.site_id = answer.site_id)
    const auto &join_s_a = f_a_owner->children[0];
    ASSERT_NE(join_s_a, nullptr);
    EXPECT_EQ(join_s_a->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_a->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_a->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_a->base_columns.size(), 2u);
    EXPECT_EQ(join_s_a->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_a->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_a->base_columns[1].getBaseTableName(), "answer");
    EXPECT_EQ(join_s_a->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_a->children.size(), 2u);

    // left: JOIN(site.site_id = so_user.site_id)
    const auto &join_s_u = join_s_a->children[0];
    ASSERT_NE(join_s_u, nullptr);
    EXPECT_EQ(join_s_u->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_u->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_u->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_u->base_columns.size(), 2u);
    EXPECT_EQ(join_s_u->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_u->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_u->base_columns[1].getBaseTableName(), "so_user");
    EXPECT_EQ(join_s_u->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_u->children.size(), 2u);

    // left: JOIN(site.site_id = question.site_id)
    const auto &join_s_q = join_s_u->children[0];
    ASSERT_NE(join_s_q, nullptr);
    EXPECT_EQ(join_s_q->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_s_q->expression.comp_type.has_value());
    EXPECT_EQ(*join_s_q->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_s_q->base_columns.size(), 2u);
    EXPECT_EQ(join_s_q->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(join_s_q->base_columns[0].column_name, "site_id");
    EXPECT_EQ(join_s_q->base_columns[1].getBaseTableName(), "question");
    EXPECT_EQ(join_s_q->base_columns[1].column_name, "site_id");
    ASSERT_EQ(join_s_q->children.size(), 2u);

    // left: FILTER(site.site_name IN ('academia','blender','chemistry','ell','security')) -> SCAN(site)
    const auto &f_site_in = join_s_q->children[0];
    ASSERT_NE(f_site_in, nullptr);
    EXPECT_EQ(f_site_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_site_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_site_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_site_in->base_columns.size(), 1u);
    EXPECT_EQ(f_site_in->base_columns[0].getBaseTableName(), "site");
    EXPECT_EQ(f_site_in->base_columns[0].column_name, "site_name");
    ASSERT_EQ(f_site_in->expression.values.size(), 5u);
    EXPECT_EQ(f_site_in->expression.values[0], "academia");
    EXPECT_EQ(f_site_in->expression.values[1], "blender");
    EXPECT_EQ(f_site_in->expression.values[2], "chemistry");
    EXPECT_EQ(f_site_in->expression.values[3], "ell");
    EXPECT_EQ(f_site_in->expression.values[4], "security");
    ASSERT_EQ(f_site_in->children.size(), 1u);

    const auto &scan_site = f_site_in->children[0];
    ASSERT_NE(scan_site, nullptr);
    EXPECT_EQ(scan_site->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_site->base_table, "s");
    ASSERT_TRUE(scan_site->children.empty());

    // right: FILTER(question.view_count >= 100) -> FILTER(question.view_count <= 100000) -> SCAN(question)
    const auto &f_vc_ge = join_s_q->children[1];
    ASSERT_NE(f_vc_ge, nullptr);
    EXPECT_EQ(f_vc_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_vc_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_ge->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_ge->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_ge->expression.values[0], "100");
    ASSERT_EQ(f_vc_ge->children.size(), 1u);

    const auto &f_vc_le = f_vc_ge->children[0];
    ASSERT_NE(f_vc_le, nullptr);
    EXPECT_EQ(f_vc_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_vc_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_vc_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_vc_le->base_columns.size(), 1u);
    EXPECT_EQ(f_vc_le->base_columns[0].getBaseTableName(), "question");
    EXPECT_EQ(f_vc_le->base_columns[0].column_name, "view_count");
    ASSERT_EQ(f_vc_le->expression.values.size(), 1u);
    EXPECT_EQ(f_vc_le->expression.values[0], "100000");
    ASSERT_EQ(f_vc_le->children.size(), 1u);

    const auto &scan_q = f_vc_le->children[0];
    ASSERT_NE(scan_q, nullptr);
    EXPECT_EQ(scan_q->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_q->base_table, "q1");
    ASSERT_TRUE(scan_q->children.empty());

    // right child of join_s_u: FILTER(so_user.downvotes >= 10) -> FILTER(so_user.downvotes <= 100000) -> SCAN(so_user)
    const auto &f_down_ge = join_s_u->children[1];
    ASSERT_NE(f_down_ge, nullptr);
    EXPECT_EQ(f_down_ge->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_down_ge->expression.comp_type.has_value());
    EXPECT_EQ(*f_down_ge->expression.comp_type, PlanCompType::GE);
    ASSERT_EQ(f_down_ge->base_columns.size(), 1u);
    EXPECT_EQ(f_down_ge->base_columns[0].getBaseTableName(), "so_user");
    EXPECT_EQ(f_down_ge->base_columns[0].column_name, "downvotes");
    ASSERT_EQ(f_down_ge->expression.values.size(), 1u);
    EXPECT_EQ(f_down_ge->expression.values[0], "10");
    ASSERT_EQ(f_down_ge->children.size(), 1u);

    const auto &f_down_le = f_down_ge->children[0];
    ASSERT_NE(f_down_le, nullptr);
    EXPECT_EQ(f_down_le->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_down_le->expression.comp_type.has_value());
    EXPECT_EQ(*f_down_le->expression.comp_type, PlanCompType::LE);
    ASSERT_EQ(f_down_le->base_columns.size(), 1u);
    EXPECT_EQ(f_down_le->base_columns[0].getBaseTableName(), "so_user");
    EXPECT_EQ(f_down_le->base_columns[0].column_name, "downvotes");
    ASSERT_EQ(f_down_le->expression.values.size(), 1u);
    EXPECT_EQ(f_down_le->expression.values[0], "100000");
    ASSERT_EQ(f_down_le->children.size(), 1u);

    const auto &scan_u = f_down_le->children[0];
    ASSERT_NE(scan_u, nullptr);
    EXPECT_EQ(scan_u->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_u->base_table, "u1");
    ASSERT_TRUE(scan_u->children.empty());

    // right child of join_s_a: SCAN(answer)
    const auto &scan_a = join_s_a->children[1];
    ASSERT_NE(scan_a, nullptr);
    EXPECT_EQ(scan_a->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_a->base_table, "a1");
    ASSERT_TRUE(scan_a->children.empty());

    // right child of join_s_t: FILTER(tag.name IN (...10...)) -> SCAN(tag)
    const auto &f_tag_in = join_s_t->children[1];
    ASSERT_NE(f_tag_in, nullptr);
    EXPECT_EQ(f_tag_in->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_tag_in->expression.comp_type.has_value());
    EXPECT_EQ(*f_tag_in->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_tag_in->base_columns.size(), 1u);
    EXPECT_EQ(f_tag_in->base_columns[0].getBaseTableName(), "tag");
    EXPECT_EQ(f_tag_in->base_columns[0].column_name, "name");
    ASSERT_EQ(f_tag_in->expression.values.size(), 10u);
    EXPECT_EQ(f_tag_in->expression.values[0], "career-path");
    EXPECT_EQ(f_tag_in->expression.values[1], "computer-science");
    EXPECT_EQ(f_tag_in->expression.values[2], "experimental-chemistry");
    EXPECT_EQ(f_tag_in->expression.values[3], "export");
    EXPECT_EQ(f_tag_in->expression.values[4], "homework");
    EXPECT_EQ(f_tag_in->expression.values[5], "http");
    EXPECT_EQ(f_tag_in->expression.values[6], "nomenclature");
    EXPECT_EQ(f_tag_in->expression.values[7], "sentence-meaning");
    EXPECT_EQ(f_tag_in->expression.values[8], "wifi");
    EXPECT_EQ(f_tag_in->expression.values[9], "xss");
    ASSERT_EQ(f_tag_in->children.size(), 1u);

    const auto &scan_tag = f_tag_in->children[0];
    ASSERT_NE(scan_tag, nullptr);
    EXPECT_EQ(scan_tag->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tag->base_table, "t1");
    ASSERT_TRUE(scan_tag->children.empty());

    // right child of join_s_tq: SCAN(tag_question)
    const auto &scan_tq = join_s_tq->children[1];
    ASSERT_NE(scan_tq, nullptr);
    EXPECT_EQ(scan_tq->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_tq->base_table, "tq1");
    ASSERT_TRUE(scan_tq->children.empty());

    // right child of join_s_b: FILTER(badge.name LIKE '%ni%') -> SCAN(badge)
    const auto &f_badge_like = join_s_b->children[1];
    ASSERT_NE(f_badge_like, nullptr);
    EXPECT_EQ(f_badge_like->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_badge_like->expression.comp_type.has_value());
    EXPECT_EQ(*f_badge_like->expression.comp_type, PlanCompType::LIKE);
    ASSERT_TRUE(f_badge_like->expression.string_op.has_value());
    EXPECT_EQ(*f_badge_like->expression.string_op, PlanStringOp::LOWER);
    ASSERT_EQ(f_badge_like->base_columns.size(), 1u);
    EXPECT_EQ(f_badge_like->base_columns[0].getBaseTableName(), "badge");
    EXPECT_EQ(f_badge_like->base_columns[0].column_name, "name");
    ASSERT_EQ(f_badge_like->expression.values.size(), 1u);
    EXPECT_EQ(f_badge_like->expression.values[0], "%ni%");
    ASSERT_EQ(f_badge_like->children.size(), 1u);

    const auto &scan_badge = f_badge_like->children[0];
    ASSERT_NE(scan_badge, nullptr);
    EXPECT_EQ(scan_badge->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_badge->base_table, "b");
    ASSERT_TRUE(scan_badge->children.empty());
}

TEST(SQLToPlanTranslatorTest, InvalidSQL)
{
    std::string query = "SELCT * FORM table;";
    EXPECT_THROW(translateSQLToLogicalPlan(query), std::runtime_error);
}
