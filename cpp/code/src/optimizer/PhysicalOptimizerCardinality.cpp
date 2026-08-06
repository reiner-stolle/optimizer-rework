#include "optimizer/PhysicalOptimizer.hpp"

// ============================================================================
// Cardinality Helper Functions
// ============================================================================

/**
 * Returns the hard-coded row count for a Star Schema Benchmark (SSB)
 * table at scale factor 1. Returns 1000 for unknown tables as a
 * conservative fallback.
 */
static size_t getSSBCardinality(const std::string& table_name) {
    if (table_name == "lineorder") return 6000000;
    if (table_name == "customer") return 30000;
    if (table_name == "part") return 200000;
    if (table_name == "supplier") return 2000;
    if (table_name == "date" || table_name == "ddate") return 2556;
    return 1000;
}

size_t PhysicalOptimizer::getCardinality(const std::string& table_name) {
    // FUTURE WORK: Replace with runtime metadata lookup to get actual table cardinalities.
    // Currently hard-coded for SSB scale factor 1.
    return getSSBCardinality(table_name);
}
