#ifndef GRAPHVIZ_PHYSICAL_VISUALIZER_HPP
#define GRAPHVIZ_PHYSICAL_VISUALIZER_HPP

#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include "PhysicalPlanNode.hpp"

class GraphvizPhysicalVisualizer {
private:
    int node_id_counter_;
    std::unordered_map<const PhysicalPlanNode *, int> visited_nodes_;
    std::unordered_map<std::string, int> base_col_node_ids_;

    std::string phyTypeToString(PhysicalNodeType type);
    std::string compTypeToString(PlanCompType type);
    std::string aggFuncToString(PlanAggFunc type);
    std::string arithOpToString(PlanArithOp type);
    std::string logicalRelOpToString(PlanLogicalRelOp type);
    std::string formatColumns(const std::vector<Column> &cols);
    int traverse(const std::shared_ptr<PhysicalPlanNode> &node, std::stringstream &ss);

public:
    void visualize(const std::shared_ptr<PhysicalPlanNode> &root, const std::string &output_filename);
};

#endif // GRAPHVIZ_PHYSICAL_VISUALIZER_HPP
