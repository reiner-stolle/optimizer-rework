#ifndef GRAPH_VISUALIZER_HPP
#define GRAPH_VISUALIZER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <fstream>
#include <sstream>
#include <memory>
#include <unordered_map>

#include "LogicalPlanNode.hpp"

class GraphvizLogicalVisualizer {
private:
    int node_id_counter_;
    std::unordered_map<const LogicalPlanNode *, int> visited_nodes_;

    std::string nodeTypeToString(LogicalNodeType type);
    std::string compTypeToString(PlanCompType type);
    std::string aggFuncToString(PlanAggFunc type);
    std::string arithOpToString(PlanArithOp type);
    std::string logicalRelOpToString(PlanLogicalRelOp type);
    std::string formatColumn(const Column &col);
    std::string formatColumns(const std::vector<Column> &cols);
    int traverse(const LogicalPlanNode &node, std::stringstream &ss);

public:
    void visualize(const LogicalPlanNode &root, const std::string &output_filename);
};

#endif // GRAPH_VISUALIZER_HPP