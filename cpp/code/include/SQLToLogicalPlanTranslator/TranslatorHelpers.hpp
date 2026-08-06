#pragma once

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LogicalPlanNode.hpp"

#include "SQLParser.h"
#include "util/sqlhelper.h"

using namespace hsql;

// --- TranslationContext ---

struct TranslationContext
{
    std::unordered_map<std::string, std::string> aliasToTable;
    std::unordered_map<PlanAggFunc, int> aggAliasCounters;
};

// --- String helpers ---

bool iequals(const char *a, const char *b);
std::string toLower(const std::string &s);

// --- SQL expression helpers ---

const Expr *unwrapSimpleFunction(const Expr *e);
std::optional<PlanStringOp> stringOpFromFunctionName(const char *name);
std::optional<PlanStringOp> unwrapStringOp(const Expr *e, const Expr **unwrapped);
std::optional<PlanCompType> mapCompOp(OperatorType t);

// --- Tree utilities ---

std::unordered_set<std::string> toTableSet(const std::vector<std::string> &v);
bool isSubset(const std::unordered_set<std::string> &need,
              const std::unordered_set<std::string> &have);

std::unordered_set<std::string> getPredicateTables(const std::shared_ptr<LogicalPlanNode> &p);
std::vector<std::string> getProvidedTables(const std::shared_ptr<LogicalPlanNode> &node);

std::shared_ptr<LogicalPlanNode> clonePlanSubtree(const std::shared_ptr<LogicalPlanNode> &node);
std::shared_ptr<LogicalPlanNode> clonePredicateTemplate(const std::shared_ptr<LogicalPlanNode> &pred);
void collectUnionLeaves(const std::shared_ptr<LogicalPlanNode> &n,
                        std::vector<std::shared_ptr<LogicalPlanNode>> &out);

// --- Table name matching helpers ---

bool namesMatch(const std::string &a, const std::string &b);
std::string normalizeTableToken(const std::string &name);

// --- Predicate target helpers ---

std::string getFilterTableName(const std::shared_ptr<LogicalPlanNode> &filterNode);
bool allLeavesSameTable(const std::vector<std::shared_ptr<LogicalPlanNode>> &leaves,
                        std::string &tableOut);
std::string getPredicateTargetTable(const std::shared_ptr<LogicalPlanNode> &node);

// --- Alias collection ---

void collectAliases(const TableRef *from,
                    std::unordered_map<std::string, std::string> &aliasToBase);

// --- Selection pushdown ---

void pushDownSelectionPredicate(std::shared_ptr<LogicalPlanNode> &node,
                                std::shared_ptr<LogicalPlanNode> predicate);

// --- Column conversion ---

Column toColumnFromExpr(const Expr *e,
                        const std::unordered_map<std::string, std::string> &aliasToBase);

// --- Literal helpers ---

std::string exprToLiteral(const Expr *e);
bool appendLiteralValue(std::vector<std::string> &values, const Expr *e);
