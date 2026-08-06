#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "LogicalPlanNode.hpp"

// --- Table name resolution from column prefix ---

extern const std::vector<std::pair<std::string, std::string>> kPrefixToTable;

std::string resolveTableName(const Column &col);

// --- Column type resolution ---

struct SSBTableSchema
{
    std::unordered_set<std::string> columns;
    PlanColumnType listedType;
    PlanColumnType defaultType;
};

extern const std::unordered_map<std::string, SSBTableSchema> kSSBSchema;
extern const std::unordered_set<std::string> kIMDBIntegerColumns;

PlanColumnType resolveColumnType(const std::string &tableName, const std::string &columnName);
