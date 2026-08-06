#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "LogicalPlanNode.hpp"

#include "SQLParser.h"
#include "util/sqlhelper.h"

using namespace hsql;

struct TranslationContext;

// --- Aggregation detection ---

struct AggPattern
{
    PlanAggFunc func{PlanAggFunc::COUNT};

    bool is_star = false;
    bool is_col = false;
    Column col;

    bool is_sum_mul = false;
    bool is_sum_sub = false;
    Column leftCol, rightCol;

    std::string alias;
};

std::optional<AggPattern> matchAgg(
    const Expr *e,
    const std::unordered_map<std::string, std::string> &aliasToBase);

// --- Aggregate alias generation ---

extern const std::unordered_map<PlanAggFunc, std::string> kAggFuncToName;
extern const std::unordered_map<std::string, PlanAggFunc> kNameToAggFunc;

std::string generateAggAlias(TranslationContext &ctx, PlanAggFunc func);
std::string generateAggAlias(TranslationContext &ctx, const std::string &funcName);

// --- SELECT analysis ---

struct AggWithName
{
    AggPattern pattern;
    std::string name;
};

struct SelectAnalysis
{
    bool hasAgg = false;
    std::vector<Column> projections;
    std::vector<AggWithName> aggregates;
    std::vector<AggSpec> aggSpecs;
};

SelectAnalysis analyzeSelectClause(
    const SelectStatement *sel,
    const std::unordered_map<std::string, std::string> &aliasToBase,
    TranslationContext &ctx);

// --- Predicate grouping ---

struct PredicateGroups
{
    std::vector<std::shared_ptr<LogicalPlanNode>> joinPreds;
    std::vector<std::shared_ptr<LogicalPlanNode>> selectionPreds;
};

PredicateGroups extractPredicates(
    const Expr *whereClause,
    const std::unordered_map<std::string, std::string> &aliasToBase);

// --- Plan construction helpers ---

void initializeSelectContext(const SelectStatement *sel, TranslationContext &ctx);

std::shared_ptr<LogicalPlanNode> buildSelectInputTree(
    const std::unordered_map<std::string, std::shared_ptr<LogicalPlanNode>> &scanByTable,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &joinPreds,
    const std::vector<std::string> &jsonJoinOrder);

void applySelectionPredicates(
    std::shared_ptr<LogicalPlanNode> &root,
    const std::vector<std::shared_ptr<LogicalPlanNode>> &selectionPreds);

std::vector<Column> collectGroupByColumns(
    const SelectStatement *sel,
    const std::unordered_map<std::string, std::string> &aliasToBase);

void applyLimitOffset(std::shared_ptr<LogicalPlanNode> &root, const SelectStatement *sel);

std::shared_ptr<LogicalPlanNode> applyMapNodes(
    const std::shared_ptr<LogicalPlanNode> &inputRoot,
    SelectAnalysis &selectAnalysis);

std::shared_ptr<LogicalPlanNode> buildSelectOutputTree(
    const std::shared_ptr<LogicalPlanNode> &inputRoot,
    const std::vector<Column> &groupByCols,
    SelectAnalysis &selectAnalysis,
    bool needsAggNode);

std::string findAggAlias(
    const std::vector<AggWithName> &aggregates,
    PlanAggFunc func, bool isStar, const Column *col);

void ensureDefaultAggregate(SelectAnalysis &selectAnalysis);

void applySortNode(
    std::shared_ptr<LogicalPlanNode> &root,
    const SelectStatement *sel,
    const std::vector<AggWithName> &aggregates,
    const std::unordered_map<std::string, std::string> &aliasToBase,
    TranslationContext &ctx);

// --- Main SELECT builder ---

std::shared_ptr<LogicalPlanNode> buildPlanForSelect(
    const SelectStatement *sel,
    const std::vector<std::string> &jsonJoinOrder,
    TranslationContext &ctx);
