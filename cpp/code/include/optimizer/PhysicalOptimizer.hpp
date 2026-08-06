#ifndef PHYSICAL_OPTIMIZER_HPP
#define PHYSICAL_OPTIMIZER_HPP

#include "LogicalPlanNode.hpp"
#include "PhysicalPlanNode.hpp"
#include <map>
#include <optional>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include <random>

// ============================================================================
// Helper Structures for Transformation Context
// ============================================================================

struct PosListInfo {
    Column poslist_col;
    std::shared_ptr<PhysicalPlanNode> producer_node;
};

struct MaterializedInfo {
    Column col;
    std::shared_ptr<PhysicalPlanNode> producer_node;
    int chain_index; // -1 for base column
};

/**
 * Tracks the current state during logical-to-physical transformation.
 * This allows us to pass information up the tree during post-order traversal
 * without requiring multiple tree walks.
 */
struct TransformContext {
    // Maps "tablename_filter" keys to the corresponding filter position list columns
    std::map<std::string, Column> available_poslists;

    std::string intermediate_table_name = "intermediate";
    
    // Track which table each poslist comes from (important for join operations)
    std::map<std::string, std::string> poslist_provenance;
    
    // Track physical nodes that produce filter poslists (for connecting materialization chains)
    std::map<std::string, std::shared_ptr<PhysicalPlanNode>> filter_nodes;

    // Ordered chain of position lists for each table. Each entry narrows the
    // row set further (filter → join → post-join filter). Materialization walks
    // this chain to resolve base columns to concrete values.
    std::map<std::string, std::vector<PosListInfo>> table_poslist_chain;

    // Cache for materialized columns to avoid duplicating chains
    std::map<std::string, MaterializedInfo> materialized_columns;

    // Track intermediate results by name or alias (for computed columns like REVENUE)
    // Key examples: "REVENUE", "i12345"
    std::map<std::string, MaterializedInfo> intermediate_results;

    // Track projected columns for the final RESULT node
    std::vector<MaterializedInfo> projected_columns;
    
    // LIMIT information to be applied to RESULT node
    std::optional<int64_t> limit_count;
    std::optional<int64_t> limit_offset;

    // When true, uses the optimized single-materialization strategy;
    // when false, uses step-by-step chained materialization.
    bool optimize_chains = true;

    // When true, optimizes HASHJOIN nodes to produce only the needed output poslist(s).
    bool optimize_join_outputs = false;
    
    // Generates a random integer ID used to name intermediate columns (e.g. "i42").
    // Random IDs (rather than sequential) avoid collisions when multiple optimizer
    // instances run concurrently on different query fragments.
    int nextIntermediateName() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 999999);
        return dis(gen);
    }
};

/**
 * Classifies what a transformed subtree produces.
 */
enum class TransformResultType {
    UNKNOWN,
    SCAN,
    POSLIST,
    JOINED,
    MATERIALIZED,
    AGGREGATE,
    GROUPBY,
};

/**
 * Result of transforming a logical subtree into physical operations.
 * Contains the root of the physical subtree and metadata about what it produces.
 */
struct TransformResult {
    std::shared_ptr<PhysicalPlanNode> physical_root;
    
    // What this subtree produces (could be poslist, materialized column, etc.)
    Column output_column;
    
    // For joins: both inner and outer position lists
    std::optional<Column> secondary_output;
    
    // Primary source table for this subtree
    std::string source_table;
    
    // All tables reachable from this subtree
    std::set<std::string> tables_involved;
    
    TransformResultType result_type = TransformResultType::UNKNOWN;
};

class PhysicalOptimizer {
    public:
    PhysicalOptimizer(std::shared_ptr<LogicalPlanNode> logical_plan_root, bool optimize_chains = false, bool optimize_join_outputs = false, std::string intermediate_table_name = "intermediate", bool demo = false, int vis_nr = 0);
    ~PhysicalOptimizer();

    std::shared_ptr<PhysicalPlanNode> optimize();

    private:
    int vis_nr = 0;

    bool demo = false;

    std::shared_ptr<PhysicalPlanNode> physical_plan;
    std::string intermediate_table_name;

    // Main transformation entry point
    bool ConvertToPhys(std::shared_ptr<LogicalPlanNode> logical_plan_root, bool optimize_chains, bool optimize_join_outputs);
    
    // Core transformation logic - recursively transforms nodes in post-order
    TransformResult transformNode(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);
    
    // Node type specific handlers
    TransformResult handleScan(
        std::shared_ptr<LogicalPlanNode> logical_node);
    
    TransformResult handleFilter(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);
    
    TransformResult handleJoin(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);
    
    TransformResult handleMap(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);
    
    TransformResult handleAggregate(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);

    TransformResult handleGroupBy(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context, 
        TransformResult& child_result);
    
    TransformResult handleProjection(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);
    
    TransformResult handleSetOperation(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);

    // Extracted sub-handlers for handleFilter
    TransformResult handleCrossTableFilter(
        std::shared_ptr<LogicalPlanNode> logical_node,
        const TransformResult& child_result,
        const std::string& source_table,
        TransformContext& context);

    // Extracted sub-handlers for handleAggregate (no GROUP BY)
    TransformResult createSingleAggregateNode(
        std::shared_ptr<LogicalPlanNode> logical_node,
        const TransformResult& child_result,
        TransformContext& context);

    TransformResult createParallelAggregateNodes(
        const std::vector<AggSpec>& agg_specs,
        const TransformResult& child_result,
        TransformContext& context);

    // Extracted sub-handlers for handleGroupBy
    TransformResult handleGroupByMultiAgg(
        std::shared_ptr<LogicalPlanNode> logical_node,
        const std::shared_ptr<PhysicalPlanNode>& groupby_node,
        const std::vector<MaterializedInfo>& group_mats,
        const std::vector<Column>& group_cols_logical,
        const Column& idx_col,
        const Column& cluster_col,
        TransformResult& child_result,
        TransformContext& context);

    TransformResult handleGroupBySingleAgg(
        std::shared_ptr<LogicalPlanNode> logical_node,
        const std::shared_ptr<PhysicalPlanNode>& groupby_node,
        AggSpec* first_agg,
        const std::vector<MaterializedInfo>& group_mats,
        const std::vector<Column>& group_cols_logical,
        const std::string& groupby_res_name,
        const Column& idx_ext_col,
        TransformResult& child_result,
        TransformContext& context);
    
    TransformResult handleSort(
        std::shared_ptr<LogicalPlanNode> logical_node,
        TransformContext& context);
    
    // Helper functions
    std::shared_ptr<PhysicalPlanNode> createMaterializeNode(
        const Column& column_to_materialize,
        const Column& poslist,
        TransformContext& context,
        const std::vector<std::shared_ptr<PhysicalPlanNode>>& children = {});
    
    // Creates a SETOPERATION physical node to combine position lists
    // Used by both handleFilter (for implicit INTERSECTION) and handleSetOperation (for explicit UNION/INTERSECTION)
    TransformResult createSetOperationNode(
        const std::vector<TransformResult>& child_results,
        PlanLogicalRelOp op_type,
        TransformContext& context);

    MaterializedInfo ensureMaterialized(
        Column& base_col,
        TransformContext& context);

    MaterializedInfo ensureMaterializedOptimized(
        Column& base_col,
        TransformContext& context);

    // Checks context.intermediate_results for a match by alias or column name.
    // Returns the matching MaterializedInfo if found, or std::nullopt.
    std::optional<MaterializedInfo> lookupIntermediateResult(
        const Column& col,
        const TransformContext& context) const;

    // Builds the final RESULT node that wraps the transformed plan.
    // Handles sorted vs. unsorted cases, LIMIT propagation, and child deduplication.
    std::shared_ptr<PhysicalPlanNode> buildResultNode(
        const TransformResult& result,
        TransformContext& context);

    // Registers a computed/aggregated result in context.intermediate_results
    // so it can be found later by ensureMaterialized. Registers by name and alias.
    void registerIntermediateResult(
        const std::string& name,
        const Column& col,
        const std::shared_ptr<PhysicalPlanNode>& producer,
        TransformContext& context);

    // Resolves the input column and producer node for an aggregate operation.
    // If the agg_spec has an explicit input, materializes it; otherwise falls
    // back to the child_result's output column.
    struct AggInputInfo { Column col; std::shared_ptr<PhysicalPlanNode> producer; };
    AggInputInfo resolveAggregateInput(
        const AggSpec& agg_spec,
        const TransformResult& child_result,
        TransformContext& context);

    // Materializes group-by key columns using the given index poslist (idx or idx_ext)
    // and updates the materialized_columns cache.
    void materializeGroupedKeys(
        const std::vector<MaterializedInfo>& group_mats,
        const std::vector<Column>& group_cols_logical,
        const Column& idx_poslist,
        const std::shared_ptr<PhysicalPlanNode>& groupby_node,
        TransformContext& context);

    // Post-processing: single-output join optimization
    // Collects all column references consumed anywhere in the physical plan DAG.
    static void collectReferencedColumns(
        const std::shared_ptr<PhysicalPlanNode>& node,
        std::set<PhysicalPlanNode*>& visited,
        std::set<std::string>& referenced);

    // Optimizes HASHJOIN nodes to produce only the needed output poslist(s)
    // when only one of the two output position lists is consumed downstream.
    static void optimizeJoinOutputs(
        const std::shared_ptr<PhysicalPlanNode>& node,
        std::set<PhysicalPlanNode*>& visited,
        const std::set<std::string>& referenced);

    // Returns the hard-coded SSB cardinality for a given table name.
    // Used by handleJoin to decide inner/outer swap order.
    static size_t getCardinality(const std::string& table_name);
};

#endif // PHYSICAL_OPTIMIZER_HPP