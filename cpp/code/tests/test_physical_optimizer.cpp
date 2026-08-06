#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "LogicalPlanNode.hpp"
#include "optimizer/PhysicalOptimizer.hpp"

static std::shared_ptr<LogicalPlanNode> makeLimitPlan(const std::vector<std::string>& values)
{
    auto scan = std::make_shared<LogicalPlanNode>();
    scan->node_type = LogicalNodeType::SCAN;
    scan->base_table = "badge";

    auto proj = std::make_shared<LogicalPlanNode>();
    proj->node_type = LogicalNodeType::PROJECTION;
    proj->projected_columns = {{"badge", "name", PlanColumnType::STRING}};
    proj->children.push_back(scan);

    if (!values.empty() && values[0] != "ALL") {
        proj->expression.limit_count = std::stoi(values[0]);
    }
    if (values.size() > 1) {
        proj->expression.limit_offset = std::stoi(values[1]);
    }

    return proj;
}

TEST(PhysicalOptimizerTest, LimitAndOffsetStoredInResult)
{
    auto logical_root = makeLimitPlan({"10", "3"});

    std::shared_ptr<PhysicalPlanNode> physical_plan;
    ASSERT_NO_THROW({
        PhysicalOptimizer optimizer(logical_root);
        physical_plan = optimizer.optimize();
    });

    ASSERT_NE(physical_plan, nullptr);
    EXPECT_EQ(physical_plan->node_type, PhysicalNodeType::RESULT);
    ASSERT_TRUE(physical_plan->expression.limit_count.has_value());
    EXPECT_EQ(physical_plan->expression.limit_count.value(), 10);
    ASSERT_TRUE(physical_plan->expression.limit_offset.has_value());
    EXPECT_EQ(physical_plan->expression.limit_offset.value(), 3);
}

TEST(PhysicalOptimizerTest, OffsetOnlySkipsLimitCount)
{
    auto logical_root = makeLimitPlan({"ALL", "7"});

    std::shared_ptr<PhysicalPlanNode> physical_plan;
    ASSERT_NO_THROW({
        PhysicalOptimizer optimizer(logical_root);
        physical_plan = optimizer.optimize();
    });

    ASSERT_NE(physical_plan, nullptr);
    EXPECT_EQ(physical_plan->node_type, PhysicalNodeType::RESULT);
    EXPECT_FALSE(physical_plan->expression.limit_count.has_value());
    ASSERT_TRUE(physical_plan->expression.limit_offset.has_value());
    EXPECT_EQ(physical_plan->expression.limit_offset.value(), 7);
}
