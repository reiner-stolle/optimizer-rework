#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LogicalPlanNode.hpp"

#include "SQLParser.h"
#include "util/sqlhelper.h"

using namespace hsql;

// --- WHERE clause splitting ---

void splitConjuncts(const Expr *e, std::vector<const Expr *> &out);

bool collectOrEqualsSameColumn(const Expr *e,
                               Column &colOut,
                               std::vector<std::string> &valuesOut,
                               const std::unordered_map<std::string, std::string> &aliasToBase);

// --- Predicate node builders ---

std::shared_ptr<LogicalPlanNode> buildInPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase);

std::shared_ptr<LogicalPlanNode> buildOrPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase);

std::shared_ptr<LogicalPlanNode> buildJoinPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase);

std::shared_ptr<LogicalPlanNode> buildBetweenPredicate(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase);

std::shared_ptr<LogicalPlanNode> buildComparisonPredicate(
    const Expr *pred, PlanCompType cmp,
    const std::unordered_map<std::string, std::string> &aliasToBase);

std::shared_ptr<LogicalPlanNode> buildPredicateNode(
    const Expr *pred,
    const std::unordered_map<std::string, std::string> &aliasToBase);
