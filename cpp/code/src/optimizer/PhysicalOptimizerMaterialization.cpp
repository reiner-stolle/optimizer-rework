#include "optimizer/PhysicalOptimizer.hpp"
#include <stdexcept>
#include <limits>

// ============================================================================
// Helper: isBaseTableChainMaterialization
// ============================================================================

/**
 * Checks whether a MaterializedInfo represents a direct base-table column
 * materialized through the poslist chain (as opposed to a derived/computed
 * value such as a rematerialized GROUPBY key).
 *
 * Base-table chain materializations have is_base=true on their first
 * base_column and are produced by a MATERIALIZE node.
 */
static bool isBaseTableChainMaterialization(const MaterializedInfo& info) {
    if (!info.producer_node) {
        return false;
    }
    if (info.producer_node->node_type != PhysicalNodeType::MATERIALIZE) {
        return false;
    }
    if (info.producer_node->base_columns.empty()) {
        return false;
    }
    // We mark base columns with is_base=true when building a base-table MATERIALIZE.
    if (info.producer_node->base_columns[0].is_base.has_value()) {
        return info.producer_node->base_columns[0].is_base.value();
    }
    return false;
}

// ============================================================================
// Intermediate-Result Lookup (shared by both ensureMaterialized variants)
// ============================================================================

std::optional<MaterializedInfo> PhysicalOptimizer::lookupIntermediateResult(
    const Column& col,
    const TransformContext& context) const {

    // Check alias first if present
    if (col.alias.has_value() && !col.alias->empty()) {
        auto it = context.intermediate_results.find(*col.alias);
        if (it != context.intermediate_results.end()) {
            return it->second;
        }
    }

    // Check by column name in intermediate results
    auto int_it = context.intermediate_results.find(col.column_name);
    if (int_it != context.intermediate_results.end()) {
        return int_it->second;
    }

    return std::nullopt;
}

// ============================================================================
// Materialization (Optimized Chain Variant)
// ============================================================================

/**
 * Materializes a base column using the optimized single-step strategy.
 *
 * Instead of iteratively materializing through each poslist in the chain,
 * this variant resolves intermediate poslist-to-poslist mappings lazily
 * (e.g. mapping a join poslist through a prior filter poslist) and then
 * materializes the base column exactly once using the final (latest)
 * position list. Results are cached by table.column to avoid duplication.
 *
 * The cache distinguishes between base-table chain materializations
 * (invalidated when the chain grows) and derived values (always reused).
 */
MaterializedInfo PhysicalOptimizer::ensureMaterializedOptimized(
    Column& base_col,
    TransformContext& context) {

    // First check if this is an intermediate/computed result (by name or alias)
    auto lookup = lookupIntermediateResult(base_col, context);
    if (lookup.has_value()) {
        return *lookup;
    }

    // Base table column key (cache)
    const std::string table = base_col.table_name;
    const std::string key = table + "." + base_col.column_name;

    // If this table has no poslist chain, nothing to do (scan/base column)
    const auto chain_it = context.table_poslist_chain.find(table);
    if (chain_it == context.table_poslist_chain.end() || chain_it->second.empty()) {
        MaterializedInfo base_info;
        base_col.is_base = true;
        base_info.col = base_col;
        base_info.producer_node = nullptr;
        base_info.chain_index = -1;
        return base_info;
    }

    auto& chain_vec = context.table_poslist_chain[table];

    /** Lazy join-poslist mapping: when a table has multiple chained poslists
     * (e.g. filter then join), we must compose them so that the final poslist
     * maps directly from base-table row IDs to the narrowed result set.
     * This inserts MATERIALIZE nodes that resolve poslist-to-poslist indirection.
    */
    if (chain_vec.size() >= 2) {
        for (size_t i = 1; i < chain_vec.size(); ++i) {
            auto& cur = chain_vec[i];
            auto& prev = chain_vec[i - 1];

            if (cur.producer_node && (cur.producer_node->node_type == PhysicalNodeType::HASHJOIN ||
                                       cur.producer_node->node_type == PhysicalNodeType::FILTER)) {
                auto mat_node = std::make_shared<PhysicalPlanNode>();
                mat_node->node_type = PhysicalNodeType::MATERIALIZE;
                mat_node->base_columns = {prev.poslist_col};   // poslist-to-poslist mapping
                mat_node->index = cur.poslist_col;

                if (prev.producer_node) mat_node->children.push_back(prev.producer_node);
                if (cur.producer_node)  mat_node->children.push_back(cur.producer_node);

                int mid = context.nextIntermediateName();
                std::string mapped_name = "i" + std::to_string(mid);
                Column mapped_col(context.intermediate_table_name, mapped_name, PlanColumnType::POSLIST);
                mat_node->result_columns = {mapped_col};
                mat_node->node_id = mid;

                context.poslist_provenance[mapped_name] = table;

                cur.poslist_col = mapped_col;
                cur.producer_node = mat_node;
            }
        }
    }

    const int target_chain_index = static_cast<int>(chain_vec.size()) - 1;
    const auto& final_pos = chain_vec.back();

    // Reuse cached materialization if it is already based on the current final poslist
    auto cache_it = context.materialized_columns.find(key);
    if (cache_it != context.materialized_columns.end()) {
        /** If the cached entry is a *derived* value (e.g., GROUPBY key rematerialized
         * using idx_ext), it is NOT tied to the table's poslist chain and must be
         * preferred unconditionally.
        */
        if (!isBaseTableChainMaterialization(cache_it->second)) {
            return cache_it->second;
        }

        // Otherwise it's a base-table column materialized under a chain index. Reuse it iff it matches the current final chain index.
        if (cache_it->second.chain_index == target_chain_index) {
            return cache_it->second;
        }
    }

    // Materialize the base column ONLY ONCE using the latest (final) position list.
    base_col.is_base = true;

    auto mat_node = createMaterializeNode(base_col, final_pos.poslist_col, context,
                                          {final_pos.producer_node});

    MaterializedInfo out_info;
    out_info.col = mat_node->result_columns[0];
    out_info.producer_node = mat_node;
    out_info.chain_index = target_chain_index;

    context.materialized_columns[key] = out_info;
    return out_info;
}

// ============================================================================
// Materialization (Chained Variant)
// ============================================================================

/**
 * Dispatches to the optimized or chained materialization strategy based on
 * the optimize_chains flag.
 *
 * Chained variant: iteratively materializes a base column through each
 * poslist in the table's chain, starting from the last cached position.
 * Each step produces a new intermediate column that feeds into the next.
 */
MaterializedInfo PhysicalOptimizer::ensureMaterialized(
    Column& base_col,
    TransformContext& context) {
    if (context.optimize_chains) {
        return ensureMaterializedOptimized(base_col, context);
    }

    // First check if this is an intermediate/computed result (by name or column alias)
    auto lookup = lookupIntermediateResult(base_col, context);
    if (lookup.has_value()) {
        return *lookup;
    }

    std::string table = base_col.table_name;
    std::string key = table + "." + base_col.column_name;

    MaterializedInfo current_info;
    if (context.materialized_columns.count(key)) {
        current_info = context.materialized_columns[key];
    } else {
        Column base_col_copy = base_col;
        base_col_copy.is_base = true;
        current_info = {base_col_copy, nullptr, -1};
    }

    const auto& chain = context.table_poslist_chain[table];

    for (size_t i = current_info.chain_index + 1; i < chain.size(); ++i) {
        const auto& pos_info = chain[i];

        auto mat_node = createMaterializeNode(
            current_info.col, pos_info.poslist_col, context,
            {current_info.producer_node, pos_info.producer_node});

        current_info.col = mat_node->result_columns[0];
        current_info.producer_node = mat_node;
        current_info.chain_index = i;
    }

    context.materialized_columns[key] = current_info;
    return current_info;
}

// ============================================================================
// Helper: Register Intermediate Results
// ============================================================================

/**
 * Registers a computed/aggregated intermediate result in the context so that
 * downstream nodes can find it via ensureMaterialized.
 * Registers by the given name and by the column's alias (if present).
 */
void PhysicalOptimizer::registerIntermediateResult(
    const std::string& name,
    const Column& col,
    const std::shared_ptr<PhysicalPlanNode>& producer,
    TransformContext& context) {
    
    MaterializedInfo info;
    info.col = col;
    info.producer_node = producer;
    info.chain_index = std::numeric_limits<int>::max(); // Sentinel: computed/derived result, not tied to any poslist chain position

    context.intermediate_results[name] = info;

    if (col.alias.has_value() && !col.alias->empty()) {
        context.intermediate_results[*col.alias] = info;
    }
}

/**
 * Resolves the input column for an aggregate operation.
 * If the AggSpec has an explicit input column, materializes it through
 * the poslist chain. Otherwise, falls back to the child_result's output.
 */
PhysicalOptimizer::AggInputInfo PhysicalOptimizer::resolveAggregateInput(
    const AggSpec& agg_spec,
    const TransformResult& child_result,
    TransformContext& context) {
    
    if (agg_spec.input.has_value()) {
        Column base_col = *agg_spec.input;
        MaterializedInfo mat_info = ensureMaterialized(base_col, context);
        return {mat_info.col, mat_info.producer_node};
    }
    // No specific column — use child's output directly (e.g., COUNT(*) or MAP result)
    return {child_result.output_column, child_result.physical_root};
}

/**
 * Materializes group-by key columns using the given index poslist from the
 * GROUPBY node. Updates the materialized_columns cache so that downstream
 * nodes (e.g., PROJECTION) see the post-grouped versions.
 */
void PhysicalOptimizer::materializeGroupedKeys(
    const std::vector<MaterializedInfo>& group_mats,
    const std::vector<Column>& group_cols_logical,
    const Column& idx_poslist,
    const std::shared_ptr<PhysicalPlanNode>& groupby_node,
    TransformContext& context) {
    
    for (size_t i = 0; i < group_cols_logical.size(); ++i) {
        auto mat_node = createMaterializeNode(
            group_mats[i].col, idx_poslist, context,
            {groupby_node, group_mats[i].producer_node});

        std::string key = group_cols_logical[i].table_name + "." + group_cols_logical[i].column_name;

        MaterializedInfo new_info;
        new_info.col = mat_node->result_columns[0];
        new_info.producer_node = mat_node;
        new_info.chain_index = std::numeric_limits<int>::max(); // Sentinel: derived result, not part of the poslist chain

        context.materialized_columns[key] = new_info;
    }
}

// ============================================================================
// Helper: Create MATERIALIZE Node
// ============================================================================

/**
 * Creates a MATERIALIZE physical node.
 * Materializes a column using a position list.
 * 
 * @param column_to_materialize The base column from a table
 * @param poslist The position list to use for materialization
 * @param context Transformation context
 * @param children Optional child nodes to attach (producers of the inputs)
 * @return Physical MATERIALIZE node
 */
std::shared_ptr<PhysicalPlanNode> PhysicalOptimizer::createMaterializeNode(
    const Column& column_to_materialize,
    const Column& poslist,
    TransformContext& context,
    const std::vector<std::shared_ptr<PhysicalPlanNode>>& children) {
    
    auto mat_node = std::make_shared<PhysicalPlanNode>();
    mat_node->node_type = PhysicalNodeType::MATERIALIZE;
    
    mat_node->base_columns = {column_to_materialize};
    mat_node->index = poslist;
    
    for (const auto& child : children) {
        if (child) mat_node->children.push_back(child);
    }
    
    // Output: materialized intermediate column
    int id = context.nextIntermediateName();
    std::string intermediate_name = "i" + std::to_string(id);
    Column output_col(context.intermediate_table_name, intermediate_name, column_to_materialize.type);
    mat_node->result_columns = {output_col};
    mat_node->node_id = id;
    
    return mat_node;
}
