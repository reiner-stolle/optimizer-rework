#pragma once
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <memory>

enum class LogicalNodeType {
    SCAN,
    FILTER,
    PROJECTION,
    JOIN,
    AGGREGATE,
    MAP,
    SETOPERATION,
    SORT
};

enum class PlanStringOp {
    UPPER,
    LOWER
};

enum class PlanCompType {
    LT,
    LE,
    EQ,
    GE,
    GT,
    NE,
    BETWEEN,
    IN,
    LIKE
};

enum class PlanAggFunc {
    COUNT,
    SUM,
    MIN,
    MAX,
    AVG,
};

enum class PlanLogicalRelOp {
    UNION,
    INTERSECTION,
    NEGATION,
};

enum class PlanArithOp {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
};

enum class PlanColumnType {
    INTEGER,
    FLOAT,
    STRING,
    BITMASK,
    POSLIST,
    PAIR_POSLIST,
};

struct Column {
    std::string table_name;       // Table name or table alias used in query (e.g., "s" from "site AS s")
    std::string column_name;
    PlanColumnType type;
    std::optional<std::string> alias;       // Column alias for display (e.g., "test" from "SELECT col AS test")
    std::optional<std::string> base_table;  // Actual table name when table_name is an alias (e.g., "site" when table_name is "s")
    std::optional<bool> is_base;

    Column() = default;

    Column(std::string t, std::string c,
           PlanColumnType ty = PlanColumnType::STRING,
           std::optional<std::string> col_alias = std::nullopt,
           std::optional<std::string> base_tbl = std::nullopt)
        : table_name(std::move(t)), column_name(std::move(c)), type(ty), 
          alias(std::move(col_alias)), base_table(std::move(base_tbl)) {
    }

    // Helper to get the actual table name for data access
    std::string getBaseTableName() const {
        return base_table.value_or(table_name);
    }
};

struct AggSpec {
    PlanAggFunc func{PlanAggFunc::COUNT};
    std::optional<Column> input;
    std::optional<std::string> result_alias;
    bool is_star{false};
};

// Expression carries exactly one "kind" of operator depending on the node:
struct Expression {
    std::optional<PlanCompType> comp_type; // FILTER/JOIN
    std::vector<AggSpec> agg_specs; // AGGREGATE (multi)
    std::optional<PlanLogicalRelOp> logical_rel_op; // SETOP
    std::optional<PlanArithOp> arith_op; // MAP
    std::optional<std::vector<bool> > sort_order;
    // true = ascending, false = descending (i-th entry corresponds to i-th base_column)
    std::vector<std::string> values; // literal strings (e.g., ["1","3"])
    std::optional<int> limit_count;
    std::optional<int> limit_offset;
    std::optional<PlanStringOp> string_op;
};

struct LogicalPlanNode {
    LogicalNodeType node_type{LogicalNodeType::SCAN};
    std::string base_table; // SCAN
    std::vector<Column> base_columns; // Operator input columns (semantics vary by node_type)
    Expression expression; // FILTER/JOIN/AGGREGATE/MAP/SETOP/SORT conditions
    std::vector<Column> projected_columns; // PROJECTION outputs
    std::vector<std::shared_ptr<LogicalPlanNode> > children;
};

// Optional: Debug output
void printLogicalPlan(const LogicalPlanNode &n, int depth = 0);
