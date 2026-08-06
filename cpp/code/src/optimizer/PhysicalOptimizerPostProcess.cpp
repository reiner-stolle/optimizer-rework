#include "optimizer/PhysicalOptimizer.hpp"
#include <set>

// ============================================================================
// Post-Processing: Single-Output Join Optimization
// ============================================================================

/**
 * Recursively collects all column identifiers (table.column) that are
 * *consumed* anywhere in the physical plan DAG.  A column is "consumed" if
 * it appears in a node's base_columns, index, aggregationColumn, or
 * clusterColumn.  result_columns are *produced*, not consumed, so they are
 * intentionally skipped.
 */
void PhysicalOptimizer::collectReferencedColumns(
    const std::shared_ptr<PhysicalPlanNode>& node,
    std::set<PhysicalPlanNode*>& visited,
    std::set<std::string>& referenced) {
    if (!node || visited.count(node.get())) return;
    visited.insert(node.get());

    auto addCol = [&](const Column& c) {
        if (!c.table_name.empty() || !c.column_name.empty())
            referenced.insert(c.table_name + "." + c.column_name);
    };

    for (const auto& c : node->base_columns) addCol(c);
    if (node->index.has_value()) addCol(*node->index);
    if (node->aggregationColumn.has_value()) addCol(*node->aggregationColumn);
    if (node->clusterColumn.has_value()) addCol(*node->clusterColumn);

    for (const auto& child : node->children)
        collectReferencedColumns(child, visited, referenced);
}

/**
 * Walks the physical plan DAG and optimizes HASHJOIN nodes that only need
 * one of their two output position lists.  The node remains a HASHJOIN
 * (the operator is functionally unchanged) but its result_columns are
 * trimmed to the single consumed poslist, signalling the executor to skip
 * allocating and writing the unused output.
 *
 * For a HASHJOIN with result_columns = {inner_poslist, outer_poslist}:
 *   - If only inner is referenced  → keep inner poslist only
 *   - If only outer  is referenced → keep outer  poslist only
 *   - If both or neither           → leave unchanged
 */
void PhysicalOptimizer::optimizeJoinOutputs(
    const std::shared_ptr<PhysicalPlanNode>& node,
    std::set<PhysicalPlanNode*>& visited,
    const std::set<std::string>& referenced) {
    if (!node || visited.count(node.get())) return;
    visited.insert(node.get());

    for (const auto& child : node->children)
        optimizeJoinOutputs(child, visited, referenced);

    if (node->node_type != PhysicalNodeType::HASHJOIN) return;
    if (node->result_columns.size() != 2) return;

    const std::string inner_key = node->result_columns[0].table_name + "." +
                                  node->result_columns[0].column_name;
    const std::string outer_key = node->result_columns[1].table_name + "." +
                                  node->result_columns[1].column_name;

    bool inner_used = referenced.count(inner_key) > 0;
    bool outer_used = referenced.count(outer_key) > 0;

    if (inner_used && outer_used) return;   // both needed → full join
    if (!inner_used && !outer_used) throw std::runtime_error("Post-processing pass: Neither join column referenced (should not happen)");

    // Trim result_columns to the single consumed poslist.
    if (inner_used) {
        node->result_columns = {node->result_columns[0]};
    } else {
        node->result_columns = {node->result_columns[1]};
    }
}
