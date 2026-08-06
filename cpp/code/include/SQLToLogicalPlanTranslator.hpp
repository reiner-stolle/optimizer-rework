#pragma once
#include <string>
#include <vector>
#include <memory>
#include "LogicalPlanNode.hpp"
#include <optional>
#include <nlohmann/json.hpp>

// Throws std::runtime_error for invalid SQL
std::vector<std::shared_ptr<LogicalPlanNode>>
translateSQLToLogicalPlan(const std::string &sql,
                          std::optional<nlohmann::json> opts = std::nullopt, bool demo = false, int vis_nr = 0);