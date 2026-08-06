#include "SQLToLogicalPlanTranslator.hpp"
#include "SQLToLogicalPlanTranslator/TranslatorHelpers.hpp"
#include "SQLToLogicalPlanTranslator/SelectPlanBuilder.hpp"
#include "SQLToLogicalPlanTranslator/JoinTreeBuilder.hpp"

#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <stdexcept>
#include "optimizer/GraphvizLogicalVisualizer.hpp"

using nlohmann::json;

#include "SQLParser.h"

using namespace hsql;

static std::string sanitizeJsonForNaN(std::string text) {
    const std::string needle = "NaN";
    const std::string replacement = "null";
    std::size_t pos = 0;

    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return text;
}

static std::optional<json> parsePlanJson(const std::optional<nlohmann::json> &opts) {
    if (!opts.has_value())
        return std::nullopt;

    try {
        if (opts->is_string()) {
            auto sanitized = sanitizeJsonForNaN(opts->get<std::string>());
            return json::parse(sanitized);
        }

        if (opts->is_object() || opts->is_array())
            return opts;
    } catch (const std::exception &) {
        return std::nullopt;
    }

    return std::nullopt;
}

// public:
// Parses SQL and returns one LogicalPlanNode root per statement.
std::vector<std::shared_ptr<LogicalPlanNode> > translateSQLToLogicalPlan(
    const std::string &sql, std::optional<nlohmann::json> opts, bool demo, int vis_nr) {
    std::vector<std::string> jsonJoinOrder;
    if (auto planJson = parsePlanJson(opts))
        jsonJoinOrder = extractJoinOrderFromJson(*planJson);

    SQLParserResult result;
    SQLParser::parse(sql.c_str(), &result);

    if (!result.isValid()) {
        throw std::runtime_error(
            std::string("Invalid SQL: ") + result.errorMsg() +
            " (L" + std::to_string(result.errorLine()) +
            ":" + std::to_string(result.errorColumn()) + ")");
    }

    std::vector<std::shared_ptr<LogicalPlanNode> > plans;
    plans.reserve(result.size());

    for (size_t i = 0; i < result.size(); ++i) {
        const SQLStatement *stmt = result.getStatement(i);
        std::shared_ptr<LogicalPlanNode> currentPlan;

        if (stmt->type() == kStmtSelect) {
            TranslationContext ctx;
            currentPlan = buildPlanForSelect(
                static_cast<const SelectStatement *>(stmt),
                jsonJoinOrder,
                ctx);
        } else {
            currentPlan = std::make_shared<LogicalPlanNode>();
            currentPlan->node_type = LogicalNodeType::PROJECTION;
        }

        plans.push_back(currentPlan);

        if (demo && currentPlan) {
            GraphvizLogicalVisualizer visualizer;
            std::string basename = "demo_logical_plan_" + std::to_string(i) + "_" + std::to_string(vis_nr);
            visualizer.visualize(*currentPlan, basename);

            std::string cmd = "dot -Tpng " + basename + ".dot -o " + basename + ".png";
            std::system(cmd.c_str());
            std::cout << "Logical plan visualized: " << basename + ".png" << std::endl;
        }
    }
    return plans;
}
