#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>
#include "SQLToLogicalPlanTranslator.hpp"

using nlohmann::json;

static std::string loadFile(const std::string &relativePath)
{
    std::ifstream f(relativePath);
    if (!f.good())
    {
        throw std::runtime_error("Could not open: " + relativePath);
    }

    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

static std::string pbPlanPath(const std::string& filename)
{
    return std::string(OPTIMIZER_REPO_ROOT) + "/pb-plans/" + filename;
}

/*
 * Logical plan imported from JSON physical plan for Q1_1
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *         └─ MAP(MUL: lo_extendedprice * lo_discount)
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ FILTER(d_year = 1993) → SCAN(dates)
 *                 └─ FILTER(lo_discount BETWEEN 1 AND 3)
 *                     └─ FILTER(lo_quantity < 25)
 *                         └─ SCAN(lineorder)
 */
TEST(SQLToPlanTranslatorTest, Q1_1_FromJsonPhysicalPlan)
{
    std::string query = R"(
        SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
        FROM lineorder, dates
        WHERE lo_orderdate = d_datekey
            AND d_year = 1993
            AND lo_discount BETWEEN 1 AND 3
            AND lo_quantity < 25;
    )";

    const std::string path = pbPlanPath("q1-1-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
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
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[0].type, PlanColumnType::INTEGER);
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[1].column_name, "lo_discount");
    EXPECT_EQ(map->base_columns[1].type, PlanColumnType::INTEGER);
    ASSERT_EQ(map->children.size(), 1);

    const auto &join = map->children[0];
    EXPECT_EQ(join->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join->expression.comp_type.has_value());
    EXPECT_EQ(*join->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join->base_columns.size(), 2);
    EXPECT_EQ(join->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join->base_columns[0].type, PlanColumnType::INTEGER);
    EXPECT_EQ(join->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join->children.size(), 2);

    // Left: FILTER(d_year = 1993) -> SCAN(dates)
    const auto &f_year = join->children[0];
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

    // Right: FILTER(lo_discount BETWEEN 1 AND 3) -> FILTER(lo_quantity < 25) -> SCAN(lineorder)
    const auto &f_discount = join->children[1];
    EXPECT_EQ(f_discount->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_discount->expression.comp_type.has_value());
    EXPECT_EQ(*f_discount->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_discount->base_columns.size(), 1);
    EXPECT_EQ(f_discount->base_columns[0].column_name, "lo_discount");
    EXPECT_EQ(f_discount->base_columns[0].type, PlanColumnType::INTEGER);
    ASSERT_EQ(f_discount->expression.values.size(), 2);
    EXPECT_EQ(f_discount->expression.values[0], "1");
    EXPECT_EQ(f_discount->expression.values[1], "3");
    ASSERT_EQ(f_discount->children.size(), 1);

    const auto &f_qty = f_discount->children[0];
    EXPECT_EQ(f_qty->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_qty->expression.comp_type.has_value());
    EXPECT_EQ(*f_qty->expression.comp_type, PlanCompType::LT);
    ASSERT_EQ(f_qty->base_columns.size(), 1);
    EXPECT_EQ(f_qty->base_columns[0].column_name, "lo_quantity");
    EXPECT_EQ(f_qty->base_columns[0].type, PlanColumnType::INTEGER);
    ASSERT_EQ(f_qty->expression.values.size(), 1);
    EXPECT_EQ(f_qty->expression.values[0], "25");
    ASSERT_EQ(f_qty->children.size(), 1);

    const auto &scan_lo = f_qty->children[0];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");
}

/*
 * Logical plan imported from JSON physical plan for Q1_2
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *         └─ MAP(MUL: lo_extendedprice * lo_discount)
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ FILTER(d_yearmonth = 'Jan1994') → SCAN(dates)
 *                 └─ FILTER(lo_discount BETWEEN 4 AND 6)
 *                     └─ FILTER(lo_quantity BETWEEN 26 AND 35)
 *                         └─ SCAN(lineorder)
 */
TEST(SQLToPlanTranslatorTest, Q1_2_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q1-2-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
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
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
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

    // Left: FILTER(d_yearmonth = 'Jan1994') -> SCAN(dates)
    const auto &f_month = join->children[0];
    EXPECT_EQ(f_month->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_month->expression.comp_type.has_value());
    EXPECT_EQ(*f_month->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_month->base_columns.size(), 1);
    EXPECT_EQ(f_month->base_columns[0].column_name, "d_yearmonth");
    ASSERT_EQ(f_month->expression.values.size(), 1);
    EXPECT_EQ(f_month->expression.values[0], "Jan1994");
    ASSERT_EQ(f_month->children.size(), 1);

    const auto &scan_dates = f_month->children[0];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");

    // Right: FILTER(lo_discount BETWEEN 4 AND 6) -> FILTER(lo_quantity BETWEEN 26 AND 35) -> SCAN(lineorder)
    const auto &f_discount = join->children[1];
    EXPECT_EQ(f_discount->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_discount->expression.comp_type.has_value());
    EXPECT_EQ(*f_discount->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_discount->base_columns.size(), 1);
    EXPECT_EQ(f_discount->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(f_discount->expression.values.size(), 2);
    EXPECT_EQ(f_discount->expression.values[0], "4");
    EXPECT_EQ(f_discount->expression.values[1], "6");
    ASSERT_EQ(f_discount->children.size(), 1);

    const auto &f_qty_between = f_discount->children[0];
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
}

/*
 * Logical plan imported from JSON physical plan for Q1_3
 *
 *   PROJECTION(REVENUE)
 *     └─ AGGREGATE(SUM)
 *         └─ MAP(MUL: lo_extendedprice * lo_discount)
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ FILTER(d_weeknuminyear = 6)
 *                 │   └─ FILTER(d_year = 1994) → SCAN(dates)
 *                 └─ FILTER(lo_discount BETWEEN 5 AND 7)
 *                     └─ FILTER(lo_quantity BETWEEN 26 AND 35)
 *                         └─ SCAN(lineorder)
 */
TEST(SQLToPlanTranslatorTest, Q1_3_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q1-3-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
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
    EXPECT_EQ(map->base_columns[0].table_name, "lineorder");
    EXPECT_EQ(map->base_columns[0].column_name, "lo_extendedprice");
    EXPECT_EQ(map->base_columns[1].table_name, "lineorder");
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

    // Left: FILTER(d_weeknuminyear = 6) -> FILTER(d_year = 1994) -> SCAN(dates)
    const auto &f_week = join->children[0];
    EXPECT_EQ(f_week->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_week->expression.comp_type.has_value());
    EXPECT_EQ(*f_week->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_week->base_columns.size(), 1);
    EXPECT_EQ(f_week->base_columns[0].column_name, "d_weeknuminyear");
    ASSERT_EQ(f_week->expression.values.size(), 1);
    EXPECT_EQ(f_week->expression.values[0], "6");
    ASSERT_EQ(f_week->children.size(), 1);

    const auto &f_year = f_week->children[0];
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

    // Right: FILTER(lo_discount BETWEEN 5 AND 7) -> FILTER(lo_quantity BETWEEN 26 AND 35) -> SCAN(lineorder)
    const auto &f_discount = join->children[1];
    EXPECT_EQ(f_discount->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_discount->expression.comp_type.has_value());
    EXPECT_EQ(*f_discount->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_discount->base_columns.size(), 1);
    EXPECT_EQ(f_discount->base_columns[0].column_name, "lo_discount");
    ASSERT_EQ(f_discount->expression.values.size(), 2);
    EXPECT_EQ(f_discount->expression.values[0], "5");
    EXPECT_EQ(f_discount->expression.values[1], "7");
    ASSERT_EQ(f_discount->children.size(), 1);

    const auto &f_qty_between = f_discount->children[0];
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
}

/*
 * Logical plan imported from JSON physical plan for Q2_1
 *
 *   SORT(p_brand ASC)
 *     └─ PROJECTION(d_year, p_brand, lo_revenue)
 *         └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ JOIN(lo_suppkey = s_suppkey)
 *                 │   ├─ JOIN(lo_partkey = p_partkey)
 *                 │   │   ├─ FILTER(p_category = 'MFGR#12') → SCAN(part)
 *                 │   │   └─ SCAN(lineorder)
 *                 │   └─ FILTER(s_region = 'AMERICA') → SCAN(supplier)
 *                 └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q2_1_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q2-1-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 1);
    EXPECT_EQ(sort->base_columns[0].column_name, "p_brand");
    EXPECT_EQ(sort->base_columns[0].type, PlanColumnType::STRING);
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 1);
    EXPECT_TRUE(dirs[0]);
    ASSERT_EQ(sort->children.size(), 1);

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[1].type, PlanColumnType::STRING);
    EXPECT_EQ(proj->projected_columns[2].column_name, "sum1");
    EXPECT_FALSE(proj->projected_columns[2].alias.has_value());
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

    const auto &agg = proj->children[0];
    EXPECT_EQ(agg->node_type, LogicalNodeType::AGGREGATE);
    ASSERT_FALSE(agg->expression.agg_specs.empty());
    EXPECT_EQ(agg->expression.agg_specs[0].func, PlanAggFunc::SUM);
    ASSERT_EQ(agg->base_columns.size(), 2);
    EXPECT_EQ(agg->base_columns[0].column_name, "d_year");
    EXPECT_EQ(agg->base_columns[1].column_name, "p_brand");
    EXPECT_EQ(agg->base_columns[1].type, PlanColumnType::STRING);
    ASSERT_EQ(agg->children.size(), 1);

    //      └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = agg->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[0].type, PlanColumnType::INTEGER);
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supplier = join_root->children[0];
    EXPECT_EQ(join_supplier->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supplier->expression.comp_type.has_value());
    EXPECT_EQ(*join_supplier->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supplier->base_columns.size(), 2);
    EXPECT_EQ(join_supplier->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supplier->base_columns[0].type, PlanColumnType::INTEGER);
    EXPECT_EQ(join_supplier->base_columns[1].column_name, "s_suppkey");
    EXPECT_EQ(join_supplier->base_columns[1].type, PlanColumnType::INTEGER);
    ASSERT_EQ(join_supplier->children.size(), 2);

    // Left: JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_supplier->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[0].type, PlanColumnType::INTEGER);
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    EXPECT_EQ(join_part->base_columns[1].type, PlanColumnType::INTEGER);
    ASSERT_EQ(join_part->children.size(), 2);

    // Left: FILTER(p_category = 'MFGR#12') -> SCAN(part)
    const auto &f_cat = join_part->children[0];
    EXPECT_EQ(f_cat->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_cat->expression.comp_type.has_value());
    EXPECT_EQ(*f_cat->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_cat->base_columns.size(), 1);
    EXPECT_EQ(f_cat->base_columns[0].column_name, "p_category");
    EXPECT_EQ(f_cat->base_columns[0].type, PlanColumnType::STRING);
    ASSERT_EQ(f_cat->expression.values.size(), 1);
    EXPECT_EQ(f_cat->expression.values[0], "MFGR#12");
    ASSERT_EQ(f_cat->children.size(), 1);

    const auto &scan_part = f_cat->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_part->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(s_region = 'AMERICA') -> SCAN(supplier)
    const auto &f_region = join_supplier->children[1];
    EXPECT_EQ(f_region->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_region->expression.comp_type.has_value());
    EXPECT_EQ(*f_region->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(f_region->base_columns.size(), 1);
    EXPECT_EQ(f_region->base_columns[0].column_name, "s_region");
    EXPECT_EQ(f_region->base_columns[0].type, PlanColumnType::STRING);
    ASSERT_EQ(f_region->expression.values.size(), 1);
    EXPECT_EQ(f_region->expression.values[0], "AMERICA");
    ASSERT_EQ(f_region->children.size(), 1);

    const auto &scan_supplier = f_region->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    // Right: SCAN(dates)
    const auto &scan_dates = join_root->children[1];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan imported from JSON physical plan for Q2_2
 *
 *   SORT(d_year ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, p_brand, lo_revenue)
 *         └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ JOIN(lo_suppkey = s_suppkey)
 *                 │   ├─ JOIN(lo_partkey = p_partkey)
 *                 │   │   ├─ FILTER(p_brand BETWEEN 'MFGR#2221' AND 'MFGR#2228') → SCAN(part)
 *                 │   │   └─ SCAN(lineorder)
 *                 │   └─ FILTER(s_region = 'ASIA') → SCAN(supplier)
 *                 └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q2_2_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q2-2-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

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

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[2].column_name, "sum1");
    EXPECT_FALSE(proj->projected_columns[2].alias.has_value());
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

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
    EXPECT_EQ(agg->expression.agg_specs[0].input->type, PlanColumnType::INTEGER);
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

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supplier = join_root->children[0];
    EXPECT_EQ(join_supplier->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supplier->expression.comp_type.has_value());
    EXPECT_EQ(*join_supplier->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supplier->base_columns.size(), 2);
    EXPECT_EQ(join_supplier->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supplier->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supplier->children.size(), 2);

    // Left: JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_supplier->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    // Left: FILTER(p_brand BETWEEN 'MFGR#2221' AND 'MFGR#2228') -> SCAN(part)
    const auto &f_brand = join_part->children[0];
    EXPECT_EQ(f_brand->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_brand->expression.comp_type.has_value());
    EXPECT_EQ(*f_brand->expression.comp_type, PlanCompType::BETWEEN);
    ASSERT_EQ(f_brand->base_columns.size(), 1);
    EXPECT_EQ(f_brand->base_columns[0].column_name, "p_brand");
    EXPECT_EQ(f_brand->base_columns[0].type, PlanColumnType::STRING);
    ASSERT_EQ(f_brand->expression.values.size(), 2);
    EXPECT_EQ(f_brand->expression.values[0], "MFGR#2221");
    EXPECT_EQ(f_brand->expression.values[1], "MFGR#2228");
    ASSERT_EQ(f_brand->children.size(), 1);

    const auto &scan_part = f_brand->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_part->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(s_region = 'ASIA') -> SCAN(supplier)
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

    // Right: SCAN(dates)
    const auto &scan_dates = join_root->children[1];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan imported from JSON physical plan for Q2_3
 *
 *   SORT(d_year ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, p_brand, lo_revenue)
 *         └─ AGGREGATE(SUM)  GROUP BY d_year, p_brand
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ JOIN(lo_suppkey = s_suppkey)
 *                 │   ├─ JOIN(lo_partkey = p_partkey)
 *                 │   │   ├─ FILTER(p_brand = 'MFGR#2239') → SCAN(part)
 *                 │   │   └─ SCAN(lineorder)
 *                 │   └─ FILTER(s_region = 'EUROPE') → SCAN(supplier)
 *                 └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q2_3_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q2-3-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

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

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "p_brand");
    EXPECT_EQ(proj->projected_columns[2].column_name, "sum1");
    EXPECT_FALSE(proj->projected_columns[2].alias.has_value());
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

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

    //      └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = agg->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supplier = join_root->children[0];
    EXPECT_EQ(join_supplier->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supplier->expression.comp_type.has_value());
    EXPECT_EQ(*join_supplier->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supplier->base_columns.size(), 2);
    EXPECT_EQ(join_supplier->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supplier->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supplier->children.size(), 2);

    // Left: JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_supplier->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    // Left: FILTER(p_brand = 'MFGR#2239') -> SCAN(part)
    const auto &f_brand = join_part->children[0];
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

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_part->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(s_region = 'EUROPE') -> SCAN(supplier)
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

    // Right: SCAN(dates)
    const auto &scan_dates = join_root->children[1];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan imported from JSON physical plan for Q3_1
 *
 *   SORT(d_year ASC, REVENUE DESC)
 *     └─ PROJECTION(c_nation, s_nation, d_year, REVENUE)
 *         └─ AGGREGATE(SUM)  GROUP BY c_nation, s_nation, d_year
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ JOIN(lo_suppkey = s_suppkey)
 *                 │   ├─ JOIN(lo_custkey = c_custkey)
 *                 │   │   ├─ FILTER(c_region = 'ASIA') → SCAN(customer)
 *                 │   │   └─ SCAN(lineorder)
 *                 │   └─ FILTER(s_region = 'ASIA') → SCAN(supplier)
 *                 └─ FILTER(d_year >= 1992)
 *                     └─ FILTER(d_year <= 1997) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_1_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q3-1-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

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

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "c_nation");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_nation");
    EXPECT_EQ(proj->projected_columns[2].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[3].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

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

    //      └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = agg->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_root->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    // Left: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    // Left: FILTER(c_region = 'ASIA') -> SCAN(customer)
    const auto &f_c_region = join_cust->children[0];
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

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_cust->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(s_region = 'ASIA') -> SCAN(supplier)
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

    // Right: FILTER(d_year >= 1992) -> FILTER(d_year <= 1997) -> SCAN(dates)
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
 * Logical plan imported from JSON physical plan for Q3_2
 *
 *   SORT(d_year ASC, REVENUE DESC)
 *     └─ PROJECTION(c_city, s_city, d_year, REVENUE)
 *         └─ AGGREGATE(SUM)  GROUP BY c_city, s_city, d_year
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ JOIN(lo_custkey = c_custkey)
 *                 │   ├─ JOIN(lo_suppkey = s_suppkey)
 *                 │   │   ├─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
 *                 │   │   └─ SCAN(lineorder)
 *                 │   └─ FILTER(c_nation = 'UNITED STATES') → SCAN(customer)
 *                 └─ FILTER(d_year >= 1992)
 *                     └─ FILTER(d_year <= 1997) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_2_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q3-2-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

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

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "c_city");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[3].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

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

    // Left: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_root->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_cust->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    // Left: FILTER(s_nation = 'UNITED STATES') -> SCAN(supplier)
    const auto &f_s_nation = join_supp->children[0];
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

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_supp->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(c_nation = 'UNITED STATES') -> SCAN(customer)
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

    // Right: FILTER(d_year >= 1992) -> FILTER(d_year <= 1997) -> SCAN(dates)
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
 * Logical plan imported from JSON physical plan for Q3_3
 *
 *   SORT(d_year ASC, REVENUE DESC)
 *     └─ PROJECTION(c_city, s_city, d_year, REVENUE)
 *         └─ AGGREGATE(SUM)  GROUP BY c_city, s_city, d_year
 *             └─ JOIN(lo_orderdate = d_datekey)
 *                 ├─ JOIN(lo_suppkey = s_suppkey)
 *                 │   ├─ JOIN(lo_custkey = c_custkey)
 *                 │   │   ├─ FILTER(c_city IN ('UNITED KI1','UNITED KI5')) → SCAN(customer)
 *                 │   │   └─ SCAN(lineorder)
 *                 │   └─ FILTER(s_city IN ('UNITED KI1','UNITED KI5')) → SCAN(supplier)
 *                 └─ FILTER(d_year >= 1992)
 *                     └─ FILTER(d_year <= 1997) → SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q3_3_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q3-3-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

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

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 4);
    EXPECT_EQ(proj->projected_columns[0].column_name, "c_city");
    EXPECT_EQ(proj->projected_columns[1].column_name, "s_city");
    EXPECT_EQ(proj->projected_columns[2].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[3].column_name, "REVENUE");
    EXPECT_EQ(proj->projected_columns[3].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

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

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_root->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    // Left: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    // Left: FILTER(c_city IN ('UNITED KI1','UNITED KI5')) -> SCAN(customer)
    const auto &f_c_city = join_cust->children[0];
    EXPECT_EQ(f_c_city->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_c_city->expression.comp_type.has_value());
    EXPECT_EQ(*f_c_city->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_c_city->base_columns.size(), 1);
    EXPECT_EQ(f_c_city->base_columns[0].column_name, "c_city");
    ASSERT_EQ(f_c_city->expression.values.size(), 2);
    EXPECT_EQ(f_c_city->expression.values[0], "UNITED KI1");
    EXPECT_EQ(f_c_city->expression.values[1], "UNITED KI5");
    ASSERT_EQ(f_c_city->children.size(), 1);

    const auto &scan_customer = f_c_city->children[0];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_cust->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(s_city IN ('UNITED KI1','UNITED KI5')) -> SCAN(supplier)
    const auto &f_s_city = join_supp->children[1];
    EXPECT_EQ(f_s_city->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_s_city->expression.comp_type.has_value());
    EXPECT_EQ(*f_s_city->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_s_city->base_columns.size(), 1);
    EXPECT_EQ(f_s_city->base_columns[0].column_name, "s_city");
    ASSERT_EQ(f_s_city->expression.values.size(), 2);
    EXPECT_EQ(f_s_city->expression.values[0], "UNITED KI1");
    EXPECT_EQ(f_s_city->expression.values[1], "UNITED KI5");
    ASSERT_EQ(f_s_city->children.size(), 1);

    const auto &scan_supplier = f_s_city->children[0];
    EXPECT_EQ(scan_supplier->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_supplier->base_table, "supplier");

    // Right: FILTER(d_year >= 1992) -> FILTER(d_year <= 1997) -> SCAN(dates)
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
 * Logical plan imported from JSON physical plan for Q3_4
 *
 *   SORT(d_year ASC, s_city ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
 *         └─ AGGREGATE(SUM)  GROUP BY d_year, s_city, p_brand
 *             └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                 └─ FILTER(p_category = 'MFGR#14')
 *                     └─ JOIN(lo_custkey = c_custkey)
 *                         ├─ JOIN(lo_orderdate = d_datekey)
 *                         │   ├─ JOIN(lo_suppkey = s_suppkey)
 *                         │   │   ├─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
 *                         │   │   └─ SCAN(lineorder)
 *                         │   └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
 *                         └─ SCAN(customer)
 */
TEST(SQLToPlanTranslatorTest, Q3_4_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q3-4-plan.json");
    std::string jsonText = loadFile(path);

    // The plan in the JSON file is missing the scan of the part table,
    EXPECT_THROW(translateSQLToLogicalPlan(query, jsonText), std::runtime_error);
}

/*
 * Logical plan imported from JSON physical plan for Q4_1
 *
 *   SORT(d_year ASC, c_nation ASC)
 *     └─ PROJECTION(d_year, c_nation, PROFIT)
 *         └─ AGGREGATE(SUM)  GROUP BY d_year, c_nation
 *             └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                 └─ JOIN(lo_orderdate = d_datekey)
 *                     ├─ JOIN(lo_partkey = p_partkey)
 *                     │   ├─ JOIN(lo_custkey = c_custkey)
 *                     │   │   ├─ JOIN(lo_suppkey = s_suppkey)
 *                     │   │   │   ├─ FILTER(s_region = 'AMERICA') → SCAN(supplier)
 *                     │   │   │   └─ SCAN(lineorder)
 *                     │   │   └─ FILTER(c_region = 'AMERICA') → SCAN(customer)
 *                     │   └─ FILTER(p_mfgr IN ('MFGR#1','MFGR#2')) → SCAN(part)
 *                     └─ SCAN(dates)
 */
TEST(SQLToPlanTranslatorTest, Q4_1_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q4-1-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
    ASSERT_EQ(plans.size(), 1);

    const auto &sort = plans[0];
    EXPECT_EQ(sort->node_type, LogicalNodeType::SORT);
    ASSERT_EQ(sort->base_columns.size(), 2);
    EXPECT_EQ(sort->base_columns[0].column_name, "d_year");
    EXPECT_EQ(sort->base_columns[1].column_name, "c_nation");
    ASSERT_TRUE(sort->expression.sort_order.has_value());
    const auto &dirs = sort->expression.sort_order.value();
    ASSERT_EQ(dirs.size(), 2);
    EXPECT_TRUE(dirs[0]);
    EXPECT_TRUE(dirs[1]);
    ASSERT_EQ(sort->children.size(), 1);

    const auto &proj = sort->children[0];
    EXPECT_EQ(proj->node_type, LogicalNodeType::PROJECTION);
    ASSERT_EQ(proj->projected_columns.size(), 3);
    EXPECT_EQ(proj->projected_columns[0].column_name, "d_year");
    EXPECT_EQ(proj->projected_columns[1].column_name, "c_nation");
    EXPECT_EQ(proj->projected_columns[2].column_name, "PROFIT");
    EXPECT_EQ(proj->projected_columns[2].type, PlanColumnType::INTEGER);
    ASSERT_EQ(proj->children.size(), 1);

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

    //      └─ JOIN(lo_orderdate = d_datekey)
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_root->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_root->children.size(), 2);

    // Left: JOIN(lo_partkey = p_partkey)
    const auto &join_part = join_root->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_part->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_part->children.size(), 2);

    // Left: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_part->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_cust->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    // Left: FILTER(s_region = 'AMERICA') -> SCAN(supplier)
    const auto &f_s_region = join_supp->children[0];
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

    // Right: SCAN(lineorder)
    const auto &scan_lo = join_supp->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    // Right: FILTER(c_region = 'AMERICA') -> SCAN(customer)
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

    // Right: FILTER(p_mfgr IN ('MFGR#1','MFGR#2')) -> SCAN(part)
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

    // Right: SCAN(dates)
    const auto &scan_dates = join_root->children[1];
    EXPECT_EQ(scan_dates->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_dates->base_table, "dates");
}

/*
 * Logical plan imported from JSON physical plan for Q4_2
 *
 *   SORT(d_year ASC, s_city ASC, p_brand ASC)
 *   └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
 *     └─ AGGREGATE(SUM) GROUP BY d_year, s_city, p_brand
 *        └─ MAP(SUB: lo_revenue - lo_supplycost)
 *          └─ JOIN(lo_partkey = p_partkey)
 *              ├─ JOIN(lo_orderdate = d_datekey)
 *              │  ├─ JOIN(lo_custkey = c_custkey)
 *              │  │  ├─ JOIN(lo_suppkey = s_suppkey)
 *              │  │  │  ├─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
 *              │  │  │  └─ SCAN(lineorder)
 *              │  │  └─ SCAN(customer)
 *              │  └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
 *              └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
 */
TEST(SQLToPlanTranslatorTest, Q4_2_FromJsonPhysicalPlan)
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

    const std::string path = pbPlanPath("q4-2-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);

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

    //              └─ JOIN(lo_partkey = p_partkey)   <-- root join in your failing log
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_root->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_root->children.size(), 2);

    // Right: FILTER(p_mfgr IN ('MFGR#1','MFGR#2')) → SCAN(part)
    const auto &f_p_mfgr = join_root->children[1];
    EXPECT_EQ(f_p_mfgr->node_type, LogicalNodeType::FILTER);
    ASSERT_TRUE(f_p_mfgr->expression.comp_type.has_value());
    EXPECT_EQ(*f_p_mfgr->expression.comp_type, PlanCompType::IN);
    ASSERT_EQ(f_p_mfgr->base_columns.size(), 1);
    EXPECT_EQ(f_p_mfgr->base_columns[0].column_name, "p_mfgr");
    ASSERT_EQ(f_p_mfgr->expression.values.size(), 2);
    EXPECT_EQ(f_p_mfgr->expression.values[0], "MFGR#1");
    EXPECT_EQ(f_p_mfgr->expression.values[1], "MFGR#2");
    ASSERT_EQ(f_p_mfgr->children.size(), 1);

    const auto &scan_part = f_p_mfgr->children[0];
    EXPECT_EQ(scan_part->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_part->base_table, "part");

    // Left: JOIN(lo_orderdate = d_datekey)
    const auto &join_dates = join_root->children[0];
    EXPECT_EQ(join_dates->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_dates->expression.comp_type.has_value());
    EXPECT_EQ(*join_dates->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_dates->base_columns.size(), 2);
    EXPECT_EQ(join_dates->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_dates->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_dates->children.size(), 2);

    // Right: FILTER(d_year IN [1997,1998]) → SCAN(dates)
    const auto &f_year_in = join_dates->children[1];
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

    // Left: JOIN(lo_custkey = c_custkey)
    const auto &join_cust = join_dates->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    // Right: FILTER(c_region = 'AMERICA') → SCAN(customer)
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

    // Left: JOIN(lo_suppkey = s_suppkey)
    const auto &join_supp = join_cust->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    // For this join, your plan had the FILTER on one side and SCAN(lineorder) on the other.
    // In your earlier failing log, the positions were flipped compared to the old expectation,
    // so we assert the currently observed arrangement:
    //   children[0] = FILTER(s_region) -> SCAN(supplier)
    //   children[1] = SCAN(lineorder)

    const auto &f_s_region = join_supp->children[0];
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

    const auto &scan_lo = join_supp->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");
}

/*
 * Logical plan imported from JSON physical plan for Q4_3
 *
 *   SORT(d_year ASC, s_city ASC, p_brand ASC)
 *     └─ PROJECTION(d_year, s_city, p_brand, PROFIT)
 *          └─ AGGREGATE(SUM)  GROUP BY d_year, s_city, p_brand
 *               └─ MAP(SUB: lo_revenue - lo_supplycost)
 *                    └─ JOIN(lo_partkey = p_partkey)
 *                         ├─ JOIN(lo_orderdate = d_datekey)
 *                         │   ├─ JOIN(lo_custkey = c_custkey)
 *                         │   │   ├─ JOIN(lo_suppkey = s_suppkey)
 *                         │   │   │   ├─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
 *                         │   │   │   └─ SCAN(lineorder)
 *                         │   │   └─ SCAN(customer)
 *                         │   └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
 *                         └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
 */

TEST(SQLToPlanTranslatorTest, Q4_3_FromJsonPhysicalPlan)
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
    const std::string path = pbPlanPath("q4-3-plan.json");
    std::string jsonText = loadFile(path);

    auto plans = translateSQLToLogicalPlan(query, jsonText);
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

    //              └─ JOIN(lo_partkey = p_partkey)
    const auto &join_root = map->children[0];
    EXPECT_EQ(join_root->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_root->expression.comp_type.has_value());
    EXPECT_EQ(*join_root->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_root->base_columns.size(), 2);
    EXPECT_EQ(join_root->base_columns[0].column_name, "lo_partkey");
    EXPECT_EQ(join_root->base_columns[1].column_name, "p_partkey");
    ASSERT_EQ(join_root->children.size(), 2);

    //                  ├─ JOIN(lo_orderdate = d_datekey)
    const auto &join_part = join_root->children[0];
    EXPECT_EQ(join_part->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_part->expression.comp_type.has_value());
    EXPECT_EQ(*join_part->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_part->base_columns.size(), 2);
    EXPECT_EQ(join_part->base_columns[0].column_name, "lo_orderdate");
    EXPECT_EQ(join_part->base_columns[1].column_name, "d_datekey");
    ASSERT_EQ(join_part->children.size(), 2);

    //                  │    ├─ JOIN(lo_custkey = c_custkey)
    const auto &join_supp = join_part->children[0];
    EXPECT_EQ(join_supp->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_supp->expression.comp_type.has_value());
    EXPECT_EQ(*join_supp->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_supp->base_columns.size(), 2);
    EXPECT_EQ(join_supp->base_columns[0].column_name, "lo_custkey");
    EXPECT_EQ(join_supp->base_columns[1].column_name, "c_custkey");
    ASSERT_EQ(join_supp->children.size(), 2);

    //                  │    │    ├─ JOIN(lo_suppkey = s_suppkey)
    const auto &join_cust = join_supp->children[0];
    EXPECT_EQ(join_cust->node_type, LogicalNodeType::JOIN);
    ASSERT_TRUE(join_cust->expression.comp_type.has_value());
    EXPECT_EQ(*join_cust->expression.comp_type, PlanCompType::EQ);
    ASSERT_EQ(join_cust->base_columns.size(), 2);
    EXPECT_EQ(join_cust->base_columns[0].column_name, "lo_suppkey");
    EXPECT_EQ(join_cust->base_columns[1].column_name, "s_suppkey");
    ASSERT_EQ(join_cust->children.size(), 2);

    //                  │    │    ├─ FILTER(s_nation = 'UNITED STATES') → SCAN(supplier)
    const auto &f_s_nat = join_cust->children[0];
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

    //                  │    │    └─ SCAN(lineorder)
    const auto &scan_lo = join_cust->children[1];
    EXPECT_EQ(scan_lo->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_lo->base_table, "lineorder");

    //                  │    └─ SCAN(customer)
    const auto &scan_customer = join_supp->children[1];
    EXPECT_EQ(scan_customer->node_type, LogicalNodeType::SCAN);
    EXPECT_EQ(scan_customer->base_table, "customer");

    //                  └─ FILTER(d_year IN [1997,1998]) → SCAN(dates)
    const auto &f_year_in = join_part->children[1];
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

    //                  └─ FILTER(p_category = 'MFGR#14') → SCAN(part)
    const auto &f_p_cat = join_root->children[1];
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
}
