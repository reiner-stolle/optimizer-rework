#include <LogicalPlanStructs.hpp>

class LogicalOptimzer {
    public:
    LogicalOptimzer();
    ~LogicalOptimzer();

    LogicalPlanNode optimize(LogicalPlanNode plan);

    private:
    

};