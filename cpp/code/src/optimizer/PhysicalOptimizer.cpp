#include "optimizer/PhysicalOptimizer.hpp"
#include <stdexcept>
#include <iostream>
#include <map>
#include <set>

#include "optimizer/GraphvizPhysicalVisualizer.hpp"

// ============================================================================
// Constructor & Public Interface
// ============================================================================

PhysicalOptimizer::PhysicalOptimizer(std::shared_ptr<LogicalPlanNode> logical_plan_root, bool optimize_chains, bool optimize_join_outputs, std::string intermediate_table_name, bool demo, int vis_nr)
    : intermediate_table_name(intermediate_table_name), demo(demo), vis_nr(vis_nr) {
    if (!ConvertToPhys(logical_plan_root, optimize_chains, optimize_join_outputs)) {
        throw std::runtime_error("Conversion to physical plan failed!");
    }
}

PhysicalOptimizer::~PhysicalOptimizer() = default;

std::shared_ptr<PhysicalPlanNode> PhysicalOptimizer::optimize() {
    return physical_plan;
}

// ============================================================================
// Main Transformation Entry Point
// ============================================================================

bool PhysicalOptimizer::ConvertToPhys(std::shared_ptr<LogicalPlanNode> logical_plan_root, bool optimize_chains, bool optimize_join_outputs) {
    if (!logical_plan_root) return false;
    
    TransformContext context;
    context.intermediate_table_name = this->intermediate_table_name;
    context.optimize_chains = optimize_chains;
    context.optimize_join_outputs = optimize_join_outputs;
    
    try {
        TransformResult result = transformNode(logical_plan_root, context);
        
        // If we have projected columns, wrap in RESULT node
        if (!context.projected_columns.empty()) {
            physical_plan = buildResultNode(result, context);
        } else {
            physical_plan = result.physical_root;
        }
        
        // ================================================================
        // Post-processing: Optimize join outputs
        // A HASHJOIN can be told to produce only one of its two
        // output poslists (_i or _o) when only one is consumed downstream.
        // The operator remains a HASHJOIN but produces fewer outputs.
        // ================================================================
        if (physical_plan && optimize_join_outputs) {
            std::set<std::string> referenced;
            std::set<PhysicalPlanNode*> visited;
            collectReferencedColumns(physical_plan, visited, referenced);
            visited.clear();
            optimizeJoinOutputs(physical_plan, visited, referenced);
        }

        if (demo && physical_plan) {
            GraphvizPhysicalVisualizer visualizer;
            std::string basename = "demo_physical_plan_" + std::to_string(vis_nr);

            visualizer.visualize(physical_plan, basename);
            std::string cmd = "dot -Tpng " + basename + ".dot -o " + basename + ".png";
            std::system(cmd.c_str());

            std::cout << "Physical plan visualized: " << basename + ".png" << std::endl;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Transformation failed: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Core Transformation Logic - Post-Order Traversal
// ============================================================================

/**
 * Main recursive transformation function.
 * Uses post-order traversal: process children first, then parent.
 * This allows us to know what position lists are available when processing joins/aggregates.
 */
TransformResult PhysicalOptimizer::transformNode(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    if (!logical_node) {
        throw std::runtime_error("Null logical node encountered");
    }
    
    // Extract LIMIT/OFFSET if present and store in context. These will be applied to the final RESULT node.
    if (logical_node->expression.limit_count.has_value()) {
        context.limit_count = logical_node->expression.limit_count;
    }
    if (logical_node->expression.limit_offset.has_value()) {
        context.limit_offset = logical_node->expression.limit_offset;
    }
    
    switch (logical_node->node_type) {
        case LogicalNodeType::SCAN:
            return handleScan(logical_node);
        
        case LogicalNodeType::FILTER:
            return handleFilter(logical_node, context);
        
        case LogicalNodeType::JOIN:
            return handleJoin(logical_node, context);
        
        case LogicalNodeType::MAP:
            return handleMap(logical_node, context);
        
        case LogicalNodeType::AGGREGATE:
            return handleAggregate(logical_node, context);
        
        case LogicalNodeType::PROJECTION:
            return handleProjection(logical_node, context);
        
        case LogicalNodeType::SETOPERATION:
            return handleSetOperation(logical_node, context);
        
        case LogicalNodeType::SORT:
            return handleSort(logical_node, context);
        
        default:
            throw std::runtime_error("Unknown logical node type");
    }
}

// ============================================================================
// Node Type Handlers
// ============================================================================

/**
 * SCAN handler:
 * In physical plan, SCAN nodes don't exist explicitly.
 * We simply mark that the table is available for filtering/joins.
 * Return a "dummy" result indicating table availability.
 */
TransformResult PhysicalOptimizer::handleScan(
    std::shared_ptr<LogicalPlanNode> logical_node) {
    
    TransformResult result;
    result.source_table = logical_node->base_table;
    result.tables_involved.insert(logical_node->base_table);
    result.result_type = TransformResultType::SCAN;
    
    return result;
}

/**
 * FILTER handler:
 * Creates a FILTER physical node that produces a position list.
 * Input: column from a table (determined via getTableForColumn)
 * Output: position list (POSLIST type)
 * 
 * If child is also a FILTER on the same table, we need to create a SETOPERATION
 * to combine the position lists (e.g., INTERSECTION for AND conditions).
 * 
 * Special case: If FILTER has 2 base_columns from different tables, it's really
 * a JOIN condition and should be handled as a JOIN.
 */
TransformResult PhysicalOptimizer::handleFilter(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    TransformResult result;
    
    if (logical_node->children.empty()) {
        throw std::runtime_error("FILTER node must have a child");
    }
    
    TransformResult child_result = transformNode(logical_node->children[0], context);
    
    std::string source_table;
    if (!logical_node->base_columns.empty()) {
        source_table = logical_node->base_columns[0].table_name;
    }

    // Cross-table filter (2 base_columns from different tables)
    if (logical_node->base_columns.size() >= 2) {
        std::string table1 = logical_node->base_columns[0].table_name;
        std::string table2 = logical_node->base_columns[1].table_name;
        if (!table1.empty() && !table2.empty() && table1 != table2) {
            return handleCrossTableFilter(logical_node, child_result, source_table, context);
        }
    }

    // Single-table filter
    logical_node->base_columns[0].is_base = true;
    
    auto filter_node = std::make_shared<PhysicalPlanNode>();
    filter_node->node_type = PhysicalNodeType::FILTER;
    filter_node->base_columns = logical_node->base_columns;
    filter_node->expression = logical_node->expression;

    int id = context.nextIntermediateName();
    std::string intermediate_name = "i" + std::to_string(id);
    Column output_col(context.intermediate_table_name, intermediate_name, PlanColumnType::POSLIST);
    filter_node->result_columns = {output_col};
    context.poslist_provenance[intermediate_name] = source_table;
    filter_node->node_id = id;
    
    result.tables_involved = child_result.tables_involved;
    if (!source_table.empty()) {
        result.tables_involved.insert(source_table);
    }
    
    // If child is also a FILTER on the SAME TABLE, combine via SETOPERATION
    if (child_result.physical_root && 
        child_result.physical_root->node_type == PhysicalNodeType::FILTER &&
        child_result.source_table == source_table) {
        
        TransformResult this_filter_result;
        this_filter_result.physical_root = filter_node;
        this_filter_result.output_column = output_col;
        this_filter_result.source_table = source_table;
        this_filter_result.tables_involved = result.tables_involved;
        this_filter_result.result_type = TransformResultType::POSLIST;
        
        std::vector<TransformResult> filter_results = {child_result, this_filter_result};
        return createSetOperationNode(filter_results, PlanLogicalRelOp::INTERSECTION, context);
    }

    // No combination needed
    context.filter_nodes[source_table + "_filter"] = filter_node;
    context.available_poslists[source_table + "_filter"] = output_col;
    
    result.physical_root = filter_node;
    result.output_column = output_col;
    result.source_table = source_table;
    result.result_type = TransformResultType::POSLIST;
    context.table_poslist_chain[source_table].push_back({output_col, filter_node});
    
    return result;
}

/**
 * Handles cross-table (post-join) FILTER with base_columns from different tables.
 * Materializes both columns through their poslist chains, creates a FILTER on
 * the materialized values, and registers the poslist for all involved tables.
 */
TransformResult PhysicalOptimizer::handleCrossTableFilter(
    std::shared_ptr<LogicalPlanNode> logical_node,
    const TransformResult& child_result,
    const std::string& source_table,
    TransformContext& context) {

    Column left_col = logical_node->base_columns[0];
    MaterializedInfo left_mat = ensureMaterialized(left_col, context);

    Column right_col = logical_node->base_columns[1];
    MaterializedInfo right_mat = ensureMaterialized(right_col, context);

    auto filter_node = std::make_shared<PhysicalPlanNode>();
    filter_node->node_type = PhysicalNodeType::FILTER;
    filter_node->base_columns = {left_mat.col, right_mat.col};
    filter_node->expression = logical_node->expression;

    if (left_mat.producer_node) filter_node->children.push_back(left_mat.producer_node);
    if (right_mat.producer_node) filter_node->children.push_back(right_mat.producer_node);

    int id = context.nextIntermediateName();
    std::string intermediate_name = "i" + std::to_string(id);
    Column output_col(context.intermediate_table_name, intermediate_name, PlanColumnType::POSLIST);
    filter_node->result_columns = {output_col};
    filter_node->node_id = id;
    context.poslist_provenance[intermediate_name] = logical_node->base_columns[0].table_name;

    // Register the filter poslist for ALL involved tables
    for (const auto& tbl : child_result.tables_involved) {
        context.table_poslist_chain[tbl].push_back({output_col, filter_node});
    }

    // Invalidate cached materializations for all involved tables
    std::vector<std::string> keys_to_erase;
    for (const auto& entry : context.materialized_columns) {
        for (const auto& tbl : child_result.tables_involved) {
            if (entry.first.rfind(tbl + ".", 0) == 0) {
                keys_to_erase.push_back(entry.first);
                break;
            }
        }
    }
    for (const auto& k : keys_to_erase) {
        context.materialized_columns.erase(k);
    }

    TransformResult result;
    result.physical_root = filter_node;
    result.output_column = output_col;
    result.source_table = source_table;
    result.result_type = TransformResultType::POSLIST;
    result.tables_involved = child_result.tables_involved;
    result.tables_involved.insert(logical_node->base_columns[0].table_name);
    result.tables_involved.insert(logical_node->base_columns[1].table_name);
    return result;
}

/**
 * JOIN handler:
 * Transforms logical JOIN into physical HASHJOIN with materialization.
 * 
 * Handles cases:
 * 1. JOIN → SCAN + SCAN (no filters – full scan)
 * 2. JOIN → FILTER + SCAN (one side filtered)
 * 3. JOIN → FILTER + FILTER (both sides filtered – typical with selection pushdown)
 * 
 * Note: Filters are always pre-applied (before JOIN) in the logical plan due to selection pushdown.
 */
TransformResult PhysicalOptimizer::handleJoin(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    TransformResult result;
    
    // Step 1: Process children
    if (logical_node->children.empty()) {
        throw std::runtime_error("JOIN node must have at least one child");
    }
    
    TransformResult left_result = transformNode(logical_node->children[0], context);
    TransformResult right_result = transformNode(logical_node->children[1], context);

    std::string left_join_col_table = logical_node->base_columns[0].table_name;
    std::string right_join_col_table = logical_node->base_columns[1].table_name;
    
    // Step 2: Materialize join keys using ensureMaterialized, which handles chaining if needed
    Column left_join_col = logical_node->base_columns[0];
    MaterializedInfo left_mat_info = ensureMaterialized(left_join_col, context);
    
    Column right_join_col = logical_node->base_columns[1];
    MaterializedInfo right_mat_info = ensureMaterialized(right_join_col, context);
    
    // Step 3: Create HASHJOIN node
    // FUTURE WORK: replace getCardinality with a runtime metadata lookup for swapping decision
    bool swap_inputs = getCardinality(left_join_col_table) > getCardinality(right_join_col_table);
    auto join_node = std::make_shared<PhysicalPlanNode>();
    join_node->node_type = PhysicalNodeType::HASHJOIN;
    join_node->expression = logical_node->expression;
    
    if (swap_inputs) {
        // Inner: Right, Outer: Left
        join_node->base_columns = {right_mat_info.col, left_mat_info.col};

        if (right_mat_info.producer_node) join_node->children.push_back(right_mat_info.producer_node);
        if (left_mat_info.producer_node)  join_node->children.push_back(left_mat_info.producer_node);
    } else {
        // Inner: Left, Outer: Right
        join_node->base_columns = {left_mat_info.col, right_mat_info.col};

        if (left_mat_info.producer_node)  join_node->children.push_back(left_mat_info.producer_node);
        if (right_mat_info.producer_node) join_node->children.push_back(right_mat_info.producer_node);
    }
    
    // Step 4: Track provenance and register poslists
    std::string inner_table, outer_table;
    if (swap_inputs) {
        inner_table = right_join_col_table;
        outer_table = left_join_col_table;
    } else {
        inner_table = left_join_col_table;
        outer_table = right_join_col_table;
    }

    // Generate inner and outer position list names
    int id = context.nextIntermediateName();
    std::string inner_name = "i" + std::to_string(id) + "_i";
    std::string outer_name = "i" + std::to_string(id) + "_o";
    Column inner_col(context.intermediate_table_name, inner_name, PlanColumnType::POSLIST);
    Column outer_col(context.intermediate_table_name, outer_name, PlanColumnType::POSLIST);
    join_node->node_id = id;
    
    join_node->result_columns = {inner_col, outer_col};
    
    context.poslist_provenance[inner_name] = inner_table;
    context.poslist_provenance[outer_name] = outer_table;
    
    // Update chains for ALL tables involved in the respective subtrees
    // If swap_inputs: Left -> Outer, Right -> Inner
    // Else: Left -> Inner, Right -> Outer
    
    const auto& inner_tables = swap_inputs ? right_result.tables_involved : left_result.tables_involved;
    const auto& outer_tables = swap_inputs ? left_result.tables_involved : right_result.tables_involved;

    for (const auto& tbl : inner_tables) {
        context.table_poslist_chain[tbl].push_back({inner_col, join_node});
    }
    for (const auto& tbl : outer_tables) {
        context.table_poslist_chain[tbl].push_back({outer_col, join_node});
    }
    
    // Step 5: Set up result
    result.physical_root = join_node;
    result.output_column = inner_col;
    result.secondary_output = outer_col;
    result.result_type = TransformResultType::JOINED;
    result.tables_involved = left_result.tables_involved;
    result.tables_involved.insert(right_result.tables_involved.begin(),
                                   right_result.tables_involved.end());
    
    return result;
}

/**
 * MAP handler:
 * Creates a MAP physical node for arithmetic operations.
 * Input: Materialized columns (base_columns)
 * Output: Intermediate result column 
 * 
 * Important: MAP operates on materialized values, not position lists!
 * 
 * Two scenarios:
 * A) Columns have been filtered before join: Need chained materialization
 *    1. Materialize base_column using filter_poslist → intermediate
 *    2. Materialize intermediate using join_poslist → final
 * B) No pre-join filtering: Direct materialization
 *    1. Materialize base_column using join_poslist → final
 */
TransformResult PhysicalOptimizer::handleMap(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    TransformResult result;
    
    // Step 1: Process child to get available position lists
    if (logical_node->children.empty()) {
        throw std::runtime_error("MAP node must have a child");
    }
    
    TransformResult child_result = transformNode(logical_node->children[0], context);
    
    // Step 2: Create MAP physical node
    auto map_node = std::make_shared<PhysicalPlanNode>();
    map_node->node_type = PhysicalNodeType::MAP;
    
    // Base columns are the final materialized inputs
    for (auto& base_col : logical_node->base_columns) {
        MaterializedInfo mat_info = ensureMaterialized(base_col, context);
        map_node->base_columns.push_back(mat_info.col);
        if (mat_info.producer_node) {
            map_node->children.push_back(mat_info.producer_node);
        }
    }
    
    map_node->expression = logical_node->expression;
    
    // Generate intermediate result column
    int id = context.nextIntermediateName();
    std::string intermediate_name = "i" + std::to_string(id);
    Column output_col(context.intermediate_table_name, intermediate_name, PlanColumnType::INTEGER);
    map_node->node_id = id;
    
    map_node->result_columns = {output_col};
    
    // Register this intermediate result so it can be found by ensureMaterialized
    registerIntermediateResult(intermediate_name, output_col, map_node, context);
    
    // Step 3: Set up result
    result.physical_root = map_node;
    result.output_column = output_col;
    result.result_type = TransformResultType::MATERIALIZED;
    result.tables_involved = child_result.tables_involved;
    
    return result;
}

/**
 * AGGREGATE handler:
 * Creates physical node for aggregation (SUM, COUNT, etc.)
 * Input: Materialized column values
 * Output: Aggregated result
 * 
 * Handles cases:
 * 1. AGGREGATE → MAP (child already materialized columns)
 * 2. AGGREGATE → JOIN (need to materialize base column with join poslist)
 * 3. Multiple aggregations without GROUP BY → create parallel AGGREGATE nodes
 */
TransformResult PhysicalOptimizer::handleAggregate(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    if (logical_node->children.empty()) {
        throw std::runtime_error("AGGREGATE node must have a child");
    }
    
    TransformResult child_result = transformNode(logical_node->children[0], context);

    // A) GROUP BY present
    if (!logical_node->base_columns.empty()) {
        return handleGroupBy(logical_node, context, child_result);
    }
    
    // B) Simple aggregation(s) over all rows
    const auto& agg_specs = logical_node->expression.agg_specs;
    if (agg_specs.size() > 1) {
        return createParallelAggregateNodes(agg_specs, child_result, context);
    }
    return createSingleAggregateNode(logical_node, child_result, context);
}

/**
 * Creates separate AGGREGATE nodes for each aggregation spec (parallel execution).
 * Used when there are multiple aggregations without GROUP BY.
 */
TransformResult PhysicalOptimizer::createParallelAggregateNodes(
    const std::vector<AggSpec>& agg_specs,
    const TransformResult& child_result,
    TransformContext& context) {
    
    std::vector<std::shared_ptr<PhysicalPlanNode>> agg_nodes;
    
    for (const auto& agg_spec : agg_specs) {
        auto [agg_input_column, agg_input_node] = resolveAggregateInput(agg_spec, child_result, context);
        
        auto agg_node = std::make_shared<PhysicalPlanNode>();
        agg_node->node_type = PhysicalNodeType::AGGREGATE;
        agg_node->base_columns = {agg_input_column};
        agg_node->expression.agg_specs = {agg_spec};
        
        int id = context.nextIntermediateName();
        std::string agg_result_name = "i" + std::to_string(id);
        agg_node->node_id = id;
        Column output_col(context.intermediate_table_name, agg_result_name, PlanColumnType::INTEGER);
        if (agg_spec.result_alias.has_value()) {
            output_col.alias = agg_spec.result_alias;
        }
        agg_node->result_columns = {output_col};
        
        if (agg_input_node) {
            agg_node->children.push_back(agg_input_node);
        }
        
        registerIntermediateResult(agg_result_name, output_col, agg_node, context);
        agg_nodes.push_back(agg_node);
    }
    
    TransformResult result;
    result.physical_root = agg_nodes.empty() ? nullptr : agg_nodes[0];
    result.output_column = agg_nodes.empty() ? Column() : agg_nodes[0]->result_columns[0];
    result.result_type = TransformResultType::AGGREGATE;
    result.tables_involved = child_result.tables_involved;
    return result;
}

/**
 * Creates a single AGGREGATE physical node.
 * Handles input resolution for JOINED vs other child types.
 */
TransformResult PhysicalOptimizer::createSingleAggregateNode(
    std::shared_ptr<LogicalPlanNode> logical_node,
    const TransformResult& child_result,
    TransformContext& context) {
    
    const auto& agg_specs = logical_node->expression.agg_specs;
    const AggSpec *first_agg = agg_specs.empty() ? nullptr : &agg_specs.front();

    // Resolve input column
    Column agg_input_column;
    std::shared_ptr<PhysicalPlanNode> agg_input_node = nullptr;
    
    if (child_result.result_type == TransformResultType::JOINED) {
        if (first_agg && first_agg->input.has_value()) {
            Column base_col = *first_agg->input;
            MaterializedInfo mat_info = ensureMaterialized(base_col, context);
            agg_input_column = mat_info.col;
            agg_input_node = mat_info.producer_node;
        } else {
            agg_input_column = child_result.output_column;
            agg_input_node = child_result.physical_root;
        }
    } else {
        agg_input_column = child_result.output_column;
        agg_input_node = child_result.physical_root;
    }
    
    auto agg_node = std::make_shared<PhysicalPlanNode>();
    agg_node->node_type = PhysicalNodeType::AGGREGATE;
    agg_node->base_columns = {agg_input_column};
    agg_node->expression = logical_node->expression;
    
    int id = context.nextIntermediateName();
    std::string result_name = "i" + std::to_string(id);
    agg_node->node_id = id;
    Column output_col(context.intermediate_table_name, result_name, PlanColumnType::INTEGER);
    if (first_agg && first_agg->result_alias.has_value()) {
        output_col.alias = first_agg->result_alias;
    }
    agg_node->result_columns = {output_col};
    
    registerIntermediateResult(result_name, output_col, agg_node, context);
    
    if (agg_input_node) {
        agg_node->children.push_back(agg_input_node);
    }
    
    TransformResult result;
    result.physical_root = agg_node;
    result.output_column = output_col;
    result.result_type = TransformResultType::AGGREGATE;
    result.tables_involved = child_result.tables_involved;
    return result;
}

/**
 * GROUP BY handler:
 * Materializes group-by key columns, creates a GROUPBY physical node,
 * and dispatches to handleGroupByMultiAgg or handleGroupBySingleAgg
 * depending on how many aggregation specs are present.
 *
 * GROUPBY output columns use suffixes:
 *   _idx
 *   _idx_ext
 *   _cluster
 */
TransformResult PhysicalOptimizer::handleGroupBy(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context, 
    TransformResult& child_result) {
        
    // Step 1: Materialize group-by columns
    std::vector<Column> group_cols_logical = logical_node->base_columns;
    std::vector<MaterializedInfo> group_mats;
    for(auto& col : group_cols_logical) {
        group_mats.push_back(ensureMaterialized(col, context));
    }
    
    // Step 2: Create GROUPBY node
    auto groupby_node = std::make_shared<PhysicalPlanNode>();
    groupby_node->node_type = PhysicalNodeType::GROUPBY;
    
    for(const auto& info : group_mats) {
        groupby_node->base_columns.push_back(info.col);
        if(info.producer_node) groupby_node->children.push_back(info.producer_node);
    }

    int groupby_id = context.nextIntermediateName();
    std::string groupby_res_name = "i" + std::to_string(groupby_id);
    groupby_node->node_id = groupby_id;
    
    // Generate auxiliary column names
    Column idx_ext_col(context.intermediate_table_name, groupby_res_name + "_idx_ext", PlanColumnType::POSLIST);
    Column idx_col(context.intermediate_table_name, groupby_res_name + "_idx", PlanColumnType::POSLIST);
    Column cluster_col(context.intermediate_table_name, groupby_res_name + "_cluster", PlanColumnType::POSLIST);

    // Step 3: Dispatch to multi-agg or single-agg handler
    auto& agg_specs = logical_node->expression.agg_specs;
    if (agg_specs.size() > 1) {
        return handleGroupByMultiAgg(logical_node, groupby_node, group_mats,
                                     group_cols_logical, idx_col, cluster_col,
                                     child_result, context);
    }
    AggSpec *first_agg = agg_specs.empty() ? nullptr : &agg_specs.front();
    return handleGroupBySingleAgg(logical_node, groupby_node, first_agg, group_mats,
                                   group_cols_logical, groupby_res_name, idx_ext_col,
                                   child_result, context);
}

/**
 * GROUP BY with multiple aggregation columns.
 * Creates GROUPBY node WITHOUT aggregation column, then separate AGGREGATE nodes
 * for each agg spec (parallelizable).
 */
TransformResult PhysicalOptimizer::handleGroupByMultiAgg(
    std::shared_ptr<LogicalPlanNode> logical_node,
    const std::shared_ptr<PhysicalPlanNode>& groupby_node,
    const std::vector<MaterializedInfo>& group_mats,
    const std::vector<Column>& group_cols_logical,
    const Column& idx_col,
    const Column& cluster_col,
    TransformResult& child_result,
    TransformContext& context) {

    auto& agg_specs = logical_node->expression.agg_specs;

    // GROUPBY outputs only _idx and _cluster (no aggregation result)
    groupby_node->result_columns = {idx_col, cluster_col};
    groupby_node->expression = logical_node->expression;
    groupby_node->expression.agg_specs.clear();
    
    // Register MAP output if present so aggregations can find it
    if (child_result.physical_root && child_result.physical_root->node_type == PhysicalNodeType::MAP) {
        Column map_output = child_result.physical_root->result_columns[0];
        registerIntermediateResult(map_output.column_name, map_output, child_result.physical_root, context);
    }
    
    // Create a separate AGGREGATE node for each aggregation spec
    std::vector<std::shared_ptr<PhysicalPlanNode>> agg_nodes;
    
    for (size_t agg_idx = 0; agg_idx < agg_specs.size(); ++agg_idx) {
        AggSpec& agg_spec = agg_specs[agg_idx];
        
        auto agg_node = std::make_shared<PhysicalPlanNode>();
        agg_node->node_type = PhysicalNodeType::AGGREGATE;
        agg_node->index = idx_col;
        agg_node->clusterColumn = cluster_col;
        
        // Determine input column
        if (agg_spec.input.has_value()) {
            MaterializedInfo agg_mat = ensureMaterialized(*agg_spec.input, context);
            agg_node->base_columns = {agg_mat.col};
            if (agg_mat.producer_node) agg_node->children.push_back(agg_mat.producer_node);
        } else if (child_result.physical_root && child_result.physical_root->node_type == PhysicalNodeType::MAP) {
            agg_node->base_columns = {child_result.physical_root->result_columns[0]};
            agg_node->children.push_back(child_result.physical_root);
        } else {
            agg_node->base_columns = {};
        }
        
        agg_node->children.push_back(groupby_node);
        agg_node->expression.agg_specs = {agg_spec};
        
        int agg_id = context.nextIntermediateName();
        std::string agg_result_name = "i" + std::to_string(agg_id);
        Column agg_output_col(context.intermediate_table_name, agg_result_name, PlanColumnType::INTEGER);
        agg_node->node_id = agg_id;
        if (agg_spec.result_alias.has_value()) agg_output_col.alias = agg_spec.result_alias;
        agg_node->result_columns = {agg_output_col};
        
        // Register with _agg suffix to distinguish the aggregation result from the GROUPBY's internal output column
        Column agg_res_col(context.intermediate_table_name, agg_result_name + "_agg", PlanColumnType::INTEGER);
        if (agg_spec.result_alias.has_value()) agg_res_col.alias = agg_spec.result_alias;
        registerIntermediateResult(agg_result_name, agg_res_col, agg_node, context);
        
        agg_nodes.push_back(agg_node);
    }
    
    materializeGroupedKeys(group_mats, group_cols_logical, idx_col, groupby_node, context);
    
    TransformResult result;
    result.physical_root = agg_nodes.empty() ? groupby_node : agg_nodes[0];
    result.output_column = agg_nodes.empty() ? idx_col : agg_nodes[0]->result_columns[0];
    result.result_type = TransformResultType::GROUPBY;
    result.tables_involved = child_result.tables_involved;
    return result;
}

/**
 * GROUP BY with a single aggregation column (or no aggregation).
 * Includes the aggregation directly in the GROUPBY node.
 */
TransformResult PhysicalOptimizer::handleGroupBySingleAgg(
    std::shared_ptr<LogicalPlanNode> logical_node,
    const std::shared_ptr<PhysicalPlanNode>& groupby_node,
    AggSpec* first_agg,
    const std::vector<MaterializedInfo>& group_mats,
    const std::vector<Column>& group_cols_logical,
    const std::string& groupby_res_name,
    const Column& idx_ext_col,
    TransformResult& child_result,
    TransformContext& context) {

    if (child_result.physical_root && child_result.physical_root->node_type == PhysicalNodeType::MAP) {
        groupby_node->children.push_back(child_result.physical_root);
        groupby_node->aggregationColumn = child_result.physical_root->result_columns[0];
    }

    if (first_agg && first_agg->input.has_value()) {
        MaterializedInfo agg_mat = ensureMaterialized(*first_agg->input, context);
        groupby_node->aggregationColumn = agg_mat.col;
        if (agg_mat.producer_node) groupby_node->children.push_back(agg_mat.producer_node);
    } else if (first_agg->is_star && first_agg->func == PlanAggFunc::COUNT) {
        groupby_node->aggregationColumn = groupby_node->base_columns[0];
    }
    
    Column res_col(context.intermediate_table_name, groupby_res_name, PlanColumnType::INTEGER);
    if (first_agg && first_agg->result_alias.has_value()) {
        res_col.alias = first_agg->result_alias;
    }
    groupby_node->result_columns = {res_col};
    groupby_node->expression = logical_node->expression;
    
    Column agg_res_col(context.intermediate_table_name, groupby_res_name + "_agg", PlanColumnType::INTEGER);
    if (first_agg && first_agg->result_alias.has_value()) {
        agg_res_col.alias = first_agg->result_alias;
    }
    registerIntermediateResult(groupby_res_name, agg_res_col, groupby_node, context);
    
    materializeGroupedKeys(group_mats, group_cols_logical, idx_ext_col, groupby_node, context);
    
    TransformResult result;
    result.physical_root = groupby_node;
    result.output_column = res_col;
    result.result_type = TransformResultType::GROUPBY;
    result.tables_involved = child_result.tables_involved;
    return result;
}

/**
 * PROJECTION handler:
 * In physical plan, projection is often implicit or handled by final materialization.
 * This might just pass through or add a final materialization step.
 */
TransformResult PhysicalOptimizer::handleProjection(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    if (logical_node->children.empty()) {
        throw std::runtime_error("PROJECTION node must have a child");
    }
    
    TransformResult child_result = transformNode(logical_node->children[0], context);

    // Materialize each projected column and preserve aliases
    context.projected_columns.clear();
    for (auto& col : logical_node->projected_columns) {
        MaterializedInfo projected_col_info = ensureMaterialized(col, context);
        // Preserve the column alias from the logical plan (e.g., "AS test")
        if (col.alias.has_value() && !col.alias->empty()) {
            projected_col_info.col.alias = col.alias;
        } else {
        // If no alias was specified, use the column name as the display name
            projected_col_info.col.alias = col.column_name;
        }
        if (col.base_table.has_value() && !col.base_table->empty()) {
            projected_col_info.col.base_table = col.base_table;
        }
        context.projected_columns.push_back(projected_col_info);
    }

    return child_result;
}

/**
 * SETOPERATION handler:
 * Creates SETOPERATION physical node (UNION, INTERSECTION, etc.)
 * Used to combine multiple position lists, e.g., from multiple filters.
 * 
 * Example (AND - INTERSECTION): lo_discount BETWEEN 1 AND 3 AND lo_quantity < 25
 * Becomes: INTERSECTION of two filter results
 * 
 * Example (OR - UNION): lo_discount BETWEEN 1 AND 3 OR lo_quantity < 25
 * Becomes: UNION of two filter results
 */
TransformResult PhysicalOptimizer::handleSetOperation(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    // Step 1: Process all children (typically 2+ filters)
    if (logical_node->children.size() < 2) {
        throw std::runtime_error("SETOPERATION node must have at least 2 children");
    }
    
    std::vector<TransformResult> child_results;
    for (const auto& child : logical_node->children) {
        child_results.push_back(transformNode(child, context));
    }
    
    // Step 2: Get the operation type from the logical node
    PlanLogicalRelOp op_type = PlanLogicalRelOp::INTERSECTION; // Default
    if (logical_node->expression.logical_rel_op.has_value()) {
        op_type = *logical_node->expression.logical_rel_op;
    }
    
    // Step 3: Delegate to the helper method
    return createSetOperationNode(child_results, op_type, context);
}

/**
 * Helper: Creates a SETOPERATION physical node to combine position lists.
 * This is the centralized logic used by both:
 * - handleFilter (for implicit INTERSECTION when stacking filters on same table)
 * - handleSetOperation (for explicit UNION/INTERSECTION from logical plan)
 * 
 * @param child_results The results from processing child nodes (filters)
 * @param op_type The set operation type (UNION, INTERSECTION, etc.)
 * @param context The transformation context
 * @return TransformResult containing the SETOPERATION node
 */
TransformResult PhysicalOptimizer::createSetOperationNode(
    const std::vector<TransformResult>& child_results,
    PlanLogicalRelOp op_type,
    TransformContext& context) {
    
    TransformResult result;
    
    if (child_results.size() < 2) {
        throw std::runtime_error("SETOPERATION requires at least 2 inputs");
    }
    
    // Step 1: Create SETOPERATION physical node
    auto setop_node = std::make_shared<PhysicalPlanNode>();
    setop_node->node_type = PhysicalNodeType::SETOPERATION;
    
    // Base columns are the position lists to combine
    for (const auto& child_res : child_results) {
        setop_node->base_columns.push_back(child_res.output_column);
    }
    
    setop_node->expression.logical_rel_op = op_type;
    
    // Generate result position list
    int id = context.nextIntermediateName();
    std::string intermediate_name = "i" + std::to_string(id);
    Column output_col(context.intermediate_table_name, intermediate_name, PlanColumnType::POSLIST);
    setop_node->result_columns = {output_col};
    setop_node->node_id = id;
    
    for (const auto& child_res : child_results) {
        setop_node->children.push_back(child_res.physical_root);
    }
    
    // Step 2: Determine the source table from children
    std::string source_table = child_results[0].source_table;
    
    // Collect all tables involved from children
    result.tables_involved = child_results[0].tables_involved;
    for (size_t i = 1; i < child_results.size(); ++i) {
        result.tables_involved.insert(child_results[i].tables_involved.begin(),
                                       child_results[i].tables_involved.end());
    }
    
    /** Step 3: Track provenance and update poslist chain
     * Clear existing chain entries for the source table and register only the SETOPERATION result.
     * This is critical for both UNION and INTERSECTION operations so that subsequent
     * materializations use the combined poslist instead of individual filter poslists
    */
    context.poslist_provenance[intermediate_name] = source_table;
    if (!source_table.empty()) {
        context.table_poslist_chain[source_table].clear();
        context.table_poslist_chain[source_table].push_back({output_col, setop_node});
        
        // Also update available_poslists and filter_nodes for consistency
        context.filter_nodes[source_table + "_filter"] = setop_node;
        context.available_poslists[source_table + "_filter"] = output_col;
    }
    
    // Step 4: Set up result
    result.physical_root = setop_node;
    result.output_column = output_col;
    result.source_table = source_table;
    result.result_type = TransformResultType::POSLIST;
    
    return result;
}

/**
 * SORT handler:
 * Creates a SORT physical node that produces a sorted position list.
 *
 * Special cases:
 * - GROUPBY elision: If the SORT columns match the preceding GROUPBY's
 *   key columns exactly, the SORT is skipped because GROUPBY output is
 *   already grouped in the desired order.
 * - Aggregate references: ORDER BY clauses referencing aggregate results
 *   (e.g. ORDER BY COUNT(*)) are resolved by looking up the GROUPBY
 *   node's result column with a _cluster suffix.
 *
 * Output column uses a _idx suffix to denote a sorted index.
 */
TransformResult PhysicalOptimizer::handleSort(
    std::shared_ptr<LogicalPlanNode> logical_node,
    TransformContext& context) {
    
    // Step 1: Process child - the data to be sorted
    if (logical_node->children.empty()) {
        throw std::runtime_error("SORT node must have a child");
    }

    TransformResult child_result = transformNode(logical_node->children[0], context);

    // If the SORT columns match the GROUPBY key columns, the output is already ordered.
    if (
        child_result.result_type == TransformResultType::GROUPBY &&
        logical_node->base_columns.size() == logical_node->children[0]->children[0]->base_columns.size() &&
        std::equal(
            logical_node->base_columns.begin(), logical_node->base_columns.end(),
            logical_node->children[0]->children[0]->base_columns.begin(),
            [](const Column& a, const Column& b) {
                return a.table_name == b.table_name &&
                a.column_name == b.column_name &&
                    a.type == b.type;
            }
        )
    ) {
        return child_result;
    }

    // Step 2: Create SORT node
    auto sort_node = std::make_shared<PhysicalPlanNode>();
    sort_node->node_type = PhysicalNodeType::SORT;
    
    // Helper lambda to check if column name starts with an aggregate function prefix
    auto isAggregateRef = [](const std::string& name) -> bool {
        return name.rfind("count", 0) == 0 || 
               name.rfind("sum", 0) == 0 || 
               name.rfind("avg", 0) == 0 || 
               name.rfind("min", 0) == 0 || 
               name.rfind("max", 0) == 0;
    };

    for(auto& col : logical_node->base_columns) {
        // Special handling for aggregate function references (e.g., ORDER BY COUNT(*))
        if (col.table_name.empty() && isAggregateRef(col.column_name)) {
            
            // Look for the aggregate result - it should be the result from the child (groupby)
            if (child_result.physical_root && 
                child_result.physical_root->node_type == PhysicalNodeType::GROUPBY &&
                !child_result.physical_root->result_columns.empty()) {
                
                // Use the GROUPBY's result column with _cluster suffix.
                Column agg_result_col_cluster = child_result.physical_root->result_columns[0];
                agg_result_col_cluster.column_name = agg_result_col_cluster.column_name + "_cluster";
                sort_node->base_columns.push_back(agg_result_col_cluster);
                sort_node->children.push_back(child_result.physical_root);
                continue;
            }
        }
        
        // Normal column handling
        MaterializedInfo mat_info = ensureMaterialized(col, context);
        sort_node->base_columns.push_back(mat_info.col);
        if(mat_info.producer_node) sort_node->children.push_back(mat_info.producer_node);
    }
    
    sort_node->expression.sort_order = logical_node->expression.sort_order;

    // Output: Sorted position list
    int id = context.nextIntermediateName();
    std::string sort_poslist_name = "i" + std::to_string(id);
    Column output_col(context.intermediate_table_name, sort_poslist_name + "_idx", PlanColumnType::POSLIST);
    sort_node->result_columns = {output_col};
    sort_node->node_id = id;
    
    TransformResult result;
    result.physical_root = sort_node;
    result.output_column = output_col;
    result.result_type = TransformResultType::POSLIST;
    result.tables_involved = child_result.tables_involved;
    
    return result;
}

// ============================================================================
// RESULT Node Construction
// ============================================================================

/**
 * Builds the final RESULT node that wraps the transformed physical plan.
 * Handles sorted vs. unsorted cases, LIMIT propagation, and child deduplication.
 */
std::shared_ptr<PhysicalPlanNode> PhysicalOptimizer::buildResultNode(
    const TransformResult& result,
    TransformContext& context) {

    auto result_node = std::make_shared<PhysicalPlanNode>();
    result_node->node_type = PhysicalNodeType::RESULT;
    result_node->node_id = context.nextIntermediateName();

    if (intermediate_table_name == "intermediate") {
        result_node->resultName = "result";
    } else {
        result_node->resultName = "result_" + intermediate_table_name;
    }

    // Store LIMIT info in RESULT node if present
    if (context.limit_count.has_value()) {
        result_node->expression.limit_count = context.limit_count;
    }
    if (context.limit_offset.has_value()) {
        result_node->expression.limit_offset = context.limit_offset;
    }

    bool has_sort = result.physical_root && result.physical_root->node_type == PhysicalNodeType::SORT;

    if (has_sort) {
        // Case A: Sorted result
        // Connect the SORT as child of RESULT
        result_node->children.push_back(result.physical_root);
        
        // Use SORT's poslist as index
        result_node->index = result.output_column;

        for(const auto& info : context.projected_columns) {
            Column col = info.col;
            result_node->base_columns.push_back(col);
            
            // Add producer as child if not already present
            if (info.producer_node) {
                bool found = false;
                for(const auto& child : result_node->children) {
                    if (child == info.producer_node) {
                        found = true;
                        break;
                    }
                }
                if (!found) result_node->children.push_back(info.producer_node);
            }
            
            // Result schema
            Column res_col = info.col;
            result_node->result_columns.push_back(res_col);
        }
    } else {
        // Case B: Unsorted result (e.g. just GroupBy or Scan)
        for(const auto& info : context.projected_columns) {
            result_node->base_columns.push_back(info.col);
            if(info.producer_node) result_node->children.push_back(info.producer_node);
            
            // Result schema
            Column res_col = info.col;
            result_node->result_columns.push_back(res_col);
        }
    }

    return result_node;
}