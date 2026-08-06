#ifndef PHYSICAL_PLAN_Node_HPP
#define PHYSICAL_PLAN_Node_HPP

#include "LogicalPlanNode.hpp"
#include "WorkItem.pb.h"

enum class PhysicalNodeType {
    FILTER,
    IDXSCAN,
    NLJ,
    HASHJOIN,
    MERGEJOIN,
    AGGREGATE,
    SETOPERATION,
    SORT,
    MAP,
    MATERIALIZE,
    GROUPBY,
    INTERMEDIATE,
    RESULT
};

struct PhysicalPlanNode {
    PhysicalNodeType node_type;
    int node_id;
    // Input columns. Semantics depend on node_type:
    //   HASHJOIN: [0]=inner key, [1]=outer key
    //   FILTER:            [0]=column to filter on
    //   MATERIALIZE:       [0]=column to materialize
    //   MAP:               arithmetic operands
    //   SETOPERATION:      position lists to combine
    //   SORT:              columns to sort by
    //   GROUPBY:           group-by key columns
    //   AGGREGATE:         column to aggregate
    std::vector<Column> base_columns;
    std::optional<Column> aggregationColumn;  // For single-agg GROUPBY: the column being aggregated
    std::optional<Column> clusterColumn;      // For multi-agg AGGREGATE nodes: the cluster column from GROUPBY
    Expression expression;
    std::vector<Column> result_columns;
    std::vector<std::shared_ptr<PhysicalPlanNode> > children;
    std::optional<std::string> resultName;    // Only set on RESULT nodes; names the output result set
    std::optional<Column> index;              // For MATERIALIZE: the position list used for indirection. For RESULT: optional sort index.
};


#endif // PHYSICAL_PLAN_Node_HPP