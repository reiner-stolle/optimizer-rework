# SQL-to-Logical-Plan Translator — Developer Documentation

## Table of Contents

1. [Overview](#1-overview)
2. [Architecture](#2-architecture)
3. [Key Concepts](#3-key-concepts)
   - 3.1 [LogicalPlanNode — The Tree Node](#31-logicalplannode--the-tree-node)
   - 3.2 [Logical Operators](#32-logical-operators)
   - 3.3 [Column and Expression](#33-column-and-expression)
   - 3.4 [Predicate Classification](#34-predicate-classification)
4. [Walkthrough: SSB Q1.1](#4-walkthrough-ssb-q11)
   - 4.1 [SQL Input](#41-sql-input)
   - 4.2 [Step-by-Step Plan Construction](#42-step-by-step-plan-construction)
   - 4.3 [Resulting Logical Plan Tree](#43-resulting-logical-plan-tree)
5. [Walkthrough: SSB Q4.1](#5-walkthrough-ssb-q41)
   - 5.1 [SQL Input](#51-sql-input)
   - 5.2 [Join Tree Construction](#52-join-tree-construction)
   - 5.3 [Selection Predicate Placement](#53-selection-predicate-placement)
   - 5.4 [Output Layer](#54-output-layer)
   - 5.5 [Resulting Logical Plan Tree](#55-resulting-logical-plan-tree)
6. [Translation Pipeline](#6-translation-pipeline)
   - 6.1 [Entry Point](#61-entry-point)
   - 6.2 [buildPlanForSelect — The Eight Steps](#62-buildplanforselect--the-eight-steps)
   - 6.3 [TranslationContext — Shared State](#63-translationcontext--shared-state)
7. [Module Details](#7-module-details)
   - 7.1 [SchemaResolver](#71-schemaresolver)
   - 7.2 [TranslatorHelpers](#72-translatorhelpers)
   - 7.3 [PredicateBuilder](#73-predicatebuilder)
   - 7.4 [JoinTreeBuilder](#74-jointreebuilder)
   - 7.5 [SelectPlanBuilder](#75-selectplanbuilder)
8. [Predicate Pushdown Deep-Dive](#8-predicate-pushdown-deep-dive)
   - 8.1 [The Algorithm](#81-the-algorithm)
   - 8.2 [SETOPERATION Predicates](#82-setoperation-predicates)
   - 8.3 [UNION Distribution](#83-union-distribution)
9. [File Layout](#9-file-layout)
---

## 1. Overview

The **SQL-to-Logical-Plan Translator** converts a SQL string into a *logical plan
tree* — a tree of relational algebra operators (SCAN, FILTER, JOIN, MAP,
AGGREGATE, SORT, PROJECTION) that represents what the query computes, independent
of how it will physically execute.

This logical plan is the input to the [Physical Optimizer](../PhysicalOptimizer/PhysicalOptimizer.md),
which maps it to a concrete physical DAG for column-store execution.

The public entry point is a single function:

```cpp
#include "SQLToLogicalPlanTranslator.hpp"

// Returns one LogicalPlanNode root per SQL statement.
std::vector<std::shared_ptr<LogicalPlanNode>>
    translateSQLToLogicalPlan(const std::string &sql,
                              std::optional<nlohmann::json> opts = std::nullopt);
```

The optional `opts` parameter accepts a JSON physical plan whose leaf order is
used to control the join tree shape (see [Section 7.4](#74-jointreebuilder)).

**Example:**

```cpp
auto plans = translateSQLToLogicalPlan(
    "SELECT SUM(lo_revenue) FROM lineorder WHERE lo_discount BETWEEN 1 AND 3");
printLogicalPlan(*plans[0]);
```

---

## 2. Architecture

```
translateSQLToLogicalPlan()
        │
        ▼
SQLToLogicalPlanTranslator.cpp   ← parse SQL, extract JSON join order, dispatch
        │
        ▼
SelectPlanBuilder                ← orchestrates the plan-construction pipeline
        │
        ├──▶ JoinTreeBuilder     ← constructs the join tree (DSU or JSON-ordered)
        │
        ├──▶ PredicateBuilder    ← converts WHERE clause to plan nodes
        │
        └──▶ SchemaResolver      ← table-name and column-type lookups
```
---

## 3. Key Concepts

### 3.1 LogicalPlanNode — The Tree Node

Every operator in the logical plan is a `LogicalPlanNode`:

```cpp
struct LogicalPlanNode { 
    LogicalNodeType node_type;                              // What kind of operator this is
    std::string base_table;                                 // Used by SCAN: the table name
    std::vector<Column> base_columns;                       // Operand columns (MAP inputs, join keys, filter column, …)
    Expression expression;                                  // Operator-specific parameters
    std::vector<Column> projected_columns;                  // Used by PROJECTION: output columns
    std::vector<std::shared_ptr<LogicalPlanNode>> children; // Child subtrees
};
```

### 3.2 Logical Operators

| Operator | Children | Key fields | What it does |
|----------|----------|------------|--------------|
| `SCAN` | 0 | `base_table` | Represents reading a base table |
| `FILTER` | 1 | `base_columns[0]`, `expression.comp_type`, `expression.values` | Selects rows matching a predicate |
| `SETOPERATION` | 2+ | `expression.logical_rel_op` | Combines filter results (UNION/INTERSECTION/NEGATION) |
| `JOIN` | 2 | `base_columns[0,1]`, `expression.comp_type` (always EQ) | Equi-join on two columns |
| `MAP` | 1 | `base_columns[0,1]`, `expression.arith_op` | Computes an arithmetic expression |
| `AGGREGATE` | 1 | `base_columns` (group-by keys), `expression.agg_specs` | Aggregation, optionally with GROUP BY |
| `SORT` | 1 | `base_columns`, `expression.sort_order` | Orders rows by one or more columns |
| `PROJECTION` | 1 | `projected_columns` | Selects the final output columns |

### 3.3 Column and Expression

**`Column`** represents a reference to one column:

```cpp
struct Column {
    std::string table_name;                   // Table name or alias used in query
    std::string column_name;
    PlanColumnType type;                      // INTEGER, FLOAT, STRING, POSLIST, …
    std::optional<std::string> alias;         // e.g. "REVENUE" from "SUM(...) AS REVENUE"
    std::optional<std::string> base_table;    // Actual table when table_name is an alias
    std::optional<bool> is_base;
};
```

**`Expression`** is a tagged union — only the field relevant to the node's type
is set:

| Node type | Relevant Expression field |
|-----------|---------------------------|
| FILTER / JOIN | `comp_type` (`LT`, `LE`, `EQ`, `GE`, `GT`, `NE`, `BETWEEN`, `IN`, `LIKE`) |
| FILTER (values) | `values` — list of literal strings (e.g. `["1", "3"]` for BETWEEN) |
| SETOPERATION | `logical_rel_op` (`UNION`, `INTERSECTION`, `NEGATION`) |
| MAP | `arith_op` (`ADD`, `SUB`, `MUL`, `DIV`, `MOD`) |
| AGGREGATE | `agg_specs` — vector of `AggSpec` (one per aggregation function) |
| SORT | `sort_order` — `vector<bool>` (true = ascending, parallel with `base_columns`) |
| PROJECTION / SCAN | `limit_count`, `limit_offset` (stored on the PROJECTION root) |

### 3.4 Predicate Classification

Every predicate in the WHERE clause is classified into one of two groups:

- **Join predicate**: both sides reference a column (`col1 = col2` across two tables)
  → becomes a JOIN node in the join tree
- **Selection predicate**: one side is a column, the other a literal (or IN-list)
  → becomes a FILTER or SETOPERATION node, pushed down to the appropriate subtree

---

## 4. Walkthrough: SSB Q1.1

Let's trace through a complete example using Star Schema Benchmark query 1.1.

### 4.1 SQL Input

```sql
SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
FROM lineorder, dates
WHERE lo_orderdate = d_datekey
  AND d_year = 1993
  AND lo_discount BETWEEN 1 AND 3
  AND lo_quantity < 25;
```

### 4.2 Step-by-Step Plan Construction

**1. Parse SQL** — the `hyrise/sql-parser` library parses the query into an AST.

**2. Split WHERE clause into conjuncts** — `splitConjuncts()` flattens AND-chains:

```
[lo_orderdate = d_datekey]      ← join predicate
[d_year = 1993]                 ← selection on dates
[lo_discount BETWEEN 1 AND 3]   ← selection on lineorder
[lo_quantity < 25]              ← selection on lineorder
```

**3. Classify predicates** — `buildPredicateNode()` turns each conjunct into a plan node:

```
JOIN [lo_orderdate = d_datekey]           → join predicate (both sides are columns)
FILTER [d_year = 1993]                    → selection predicate
FILTER [lo_discount BETWEEN 1 AND 3]      → selection predicate
FILTER [lo_quantity < 25]                 → selection predicate
```

**4. Build join tree** (`buildJoinTree` via DSU) — starts with two SCAN leaves, then
applies the single join predicate:

```
JOIN [lo_orderdate = d_datekey]
├── SCAN [lineorder]
└── SCAN [dates]
```

**5. Push down selection predicates** (`applySelectionPredicates`) — each predicate
is pushed as deep as possible by `pushDownSelectionPredicate()`:

- `d_year = 1993` → right child of JOIN covers `{dates}` → pushed onto SCAN[dates]
- `lo_discount BETWEEN 1 AND 3` → left child covers `{lineorder}` → pushed onto SCAN[lineorder]
- `lo_quantity < 25` → also lineorder → pushed through the existing FILTER, attaches below it

Result:

```
JOIN [lo_orderdate = d_datekey]
├── FILTER [lo_discount BETWEEN 1 AND 3]
│     └── FILTER [lo_quantity < 25]
│           └── SCAN [lineorder]
└── FILTER [d_year = 1993]
      └── SCAN [dates]
```

**6. Analyze SELECT clause** — `analyzeSelectClause()` detects `SUM(lo_extendedprice * lo_discount)`:

```
hasAgg = true
aggregates = [{SUM, is_sum_mul=true, leftCol=lo_extendedprice, rightCol=lo_discount, alias="REVENUE"}]
```

Because `is_sum_mul` is true, the aggregation contains an inline multiplication.
This triggers `applyMapNodes()` to insert a MAP node before the AGGREGATE.

**7. Build output tree** (`buildSelectOutputTree`) — with `needsAggNode = true`:

```
PROJECTION [REVENUE]
  AGGREGATE [SUM] (agg_specs → input=nullopt, result_alias="REVENUE")
    MAP [lo_extendedprice * lo_discount] (arith_op=MUL)
      <join+filter tree from step 5>
```

**8. Apply SORT and LIMIT** — no ORDER BY or LIMIT in Q1.1, so nothing is added.

### 4.3 Resulting Logical Plan Tree


```
PROJECTION [REVENUE]
  AGGREGATE [SUM]
    MAP [lo_extendedprice * lo_discount]
      JOIN [lo_orderdate = d_datekey]
        FILTER [lo_discount BETWEEN 1 AND 3]
          FILTER [lo_quantity < 25]
            SCAN [lineorder]
        FILTER [d_year = 1993]
          SCAN [dates]
```

---

## 5. Walkthrough: SSB Q4.1

```sql
SELECT d_year, c_nation,
       SUM(lo_revenue - lo_supplycost) AS PROFIT
FROM dates, customer, supplier, part, lineorder
WHERE lo_custkey  = c_custkey
  AND lo_suppkey  = s_suppkey
  AND lo_partkey  = p_partkey
  AND lo_orderdate = d_datekey
  AND c_region = 'AMERICA'
  AND s_region = 'AMERICA'
  AND (p_mfgr = 'MFGR#1' OR p_mfgr = 'MFGR#2')
GROUP BY d_year, c_nation
ORDER BY d_year, c_nation;
```

### 5.1 SQL Input

Five tables, four equi-joins, three selection predicates (one with OR).

### 5.2 Join Tree Construction

`splitConjuncts()` extracts 7 conjuncts. The join predicates are classified by
`buildPredicateNode()` (both sides are column refs → JOIN node):

```
JOIN [lo_custkey  = c_custkey]
JOIN [lo_suppkey  = s_suppkey]
JOIN [lo_partkey  = p_partkey]
JOIN [lo_orderdate = d_datekey]
```

The **DSU-based join builder** processes these in order. It starts with five
isolated SCAN nodes and merges components as each join predicate is applied:

| Step | Join | Component after merge |
|------|------|-----------------------|
| 1 | `lo_custkey = c_custkey` | JOIN(SCAN[lineorder], SCAN[customer]) |
| 2 | `lo_suppkey = s_suppkey` | JOIN(prev, SCAN[supplier]) |
| 3 | `lo_partkey = p_partkey` | JOIN(prev, SCAN[part]) |
| 4 | `lo_orderdate = d_datekey` | JOIN(prev, SCAN[dates]) |

All five tables end up in a single connected component — a left-deep join tree.

### 5.3 Selection Predicate Placement

Three selection predicates are pushed down:

| Predicate | Table | Placement |
|-----------|-------|-----------|
| `c_region = 'AMERICA'` | customer | Above SCAN[customer] |
| `s_region = 'AMERICA'` | supplier | Above SCAN[supplier] |
| `p_mfgr = 'MFGR#1' OR p_mfgr = 'MFGR#2'` | part | Above SCAN[part] |

The OR predicate receives special handling in `buildOrPredicate()`: since both
branches compare the same column (`p_mfgr`) to different literals, they are
merged into a single `FILTER [p_mfgr IN ('MFGR#1', 'MFGR#2')]` node rather
than a SETOPERATION.

### 5.4 Output Layer

`analyzeSelectClause()` detects:

- **Projections**: `d_year`, `c_nation` (plain column refs)
- **Aggregate**: `SUM(lo_revenue - lo_supplycost)` with `is_sum_sub = true` → MAP[SUB] + AGGREGATE[SUM]
- **Group-by keys**: `d_year`, `c_nation` (stored in `AGGREGATE.base_columns`)

`applySortNode()` appends a SORT node for `ORDER BY d_year, c_nation`.


### 5.5 Resulting Logical Plan Tree

```
SORT [d_year ASC, c_nation ASC]
  PROJECTION [d_year, c_nation, PROFIT]
    AGGREGATE [GROUP BY d_year, c_nation; SUM]
      MAP [lo_revenue - lo_supplycost]
        JOIN [lo_orderdate = d_datekey]
          JOIN [lo_partkey = p_partkey]
            JOIN [lo_suppkey = s_suppkey]
              JOIN [lo_custkey = c_custkey]
                SCAN [lineorder]
                FILTER [c_region = 'AMERICA']
                  SCAN [customer]
              FILTER [s_region = 'AMERICA']
                SCAN [supplier]
            FILTER [p_mfgr IN ('MFGR#1', 'MFGR#2')]
              SCAN [part]
          SCAN [dates]
```

---

## 6. Translation Pipeline

### 6.1 Entry Point

`translateSQLToLogicalPlan()` in [SQLToLogicalPlanTranslator.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator.cpp):

1. If `opts` is present, parses it as JSON and extracts a join order via
   `extractJoinOrderFromJson()`.
2. Calls the `hyrise/sql-parser` to produce an AST.
3. Fails fast on syntax errors.
4. For each `SELECT` statement, calls `buildPlanForSelect()` with a fresh
   `TranslationContext`. Non-SELECT statements receive a placeholder
   PROJECTION node.

### 6.2 buildPlanForSelect

`buildPlanForSelect()` in [SelectPlanBuilder.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator/SelectPlanBuilder.cpp)
orchestrates the full pipeline:

```
Step 1  initializeSelectContext()      Populate aliasToTable from FROM clause
Step 2  buildScanMap()                 Create a SCAN node for each table
Step 3  extractPredicates()            Split WHERE into join / selection predicates
Step 4  buildSelectInputTree()         Build join tree; apply JSON order if valid
Step 5  applySelectionPredicates()     Push filters down (SETs first, then scalars)
Step 6  collectGroupByColumns()        Collect GROUP BY column list
Step 7  analyzeSelectClause()          Detect aggregations, projections, aliases
Step 8  buildSelectOutputTree()        Add MAP, AGGREGATE, PROJECTION nodes
         applySortNode()               Add SORT if ORDER BY present
         applyLimitOffset()            Store LIMIT/OFFSET on the root
```

### 6.3 TranslationContext — Shared State

`TranslationContext` threads state through the pipeline:

| Field | Type | Purpose |
|-------|------|---------|
| `aliasToTable` | `map<string, string>` | Maps query alias → actual table name (e.g. `"lo"` → `"lineorder"`) |
| `aggAliasCounters` | `map<PlanAggFunc, int>` | Auto-increments alias suffixes for unnamed aggregates (`sum1`, `sum2`, …) |

---

## 7. Module Details

### 7.1 SchemaResolver

**Files:** [SchemaResolver.hpp](../../cpp/code/include/SQLToLogicalPlanTranslator/SchemaResolver.hpp) /
[SchemaResolver.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator/SchemaResolver.cpp)

The schema resolver provides two pure lookup services:

- **`resolveTableName(column)`** — infers the table a column belongs to from its
  prefix. If the column has no explicit table prefix, the resolver looks up the
  column name against the known SSB schema to find its home table.
- **`resolveColumnType(table, column)`** — returns the `PlanColumnType`
  (`INTEGER`, `FLOAT`, `STRING`) for a given `table.column` pair.

Both functions operate on a hard-coded SSB schema. Extending to other schemas
requires updating the lookup tables in `SchemaResolver.cpp`.

### 7.2 TranslatorHelpers

**Files:** [TranslatorHelpers.hpp](../../cpp/code/include/SQLToLogicalPlanTranslator/TranslatorHelpers.hpp) /
[TranslatorHelpers.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator/TranslatorHelpers.cpp)

A collection of utility functions used by all other modules:

| Function | Purpose |
|----------|---------|
| `toColumnFromExpr(expr, aliasMap)` | Converts an AST column expression to a `Column`, resolving aliases and types |
| `pushDownSelectionPredicate(root, pred)` | Recursively pushes a predicate to the deepest valid node (see [Section 8](#8-predicate-pushdown-deep-dive)) |
| `splitConjuncts(expr, out)` | Flattens a nested AND-tree into a flat list of conjuncts |
| `collectAliases(from, map)` | Walks the FROM clause to build the `alias → base_table` map |
| `getProvidedTables(node)` | Returns all base tables reachable from a subtree |
| `getPredicateTables(pred)` | Returns which tables a predicate node references |
| `clonePlanSubtree(node)` | Deep-copies a plan subtree (used during UNION distribution) |
| `iequals(a, b)` | Case-insensitive string comparison |
| `namesMatch(a, b)` | Plural-tolerant table name comparison (`dates` ≈ `date`) |

### 7.3 PredicateBuilder

**Files:** [PredicateBuilder.hpp](../../cpp/code/include/SQLToLogicalPlanTranslator/PredicateBuilder.hpp) /
[PredicateBuilder.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator/PredicateBuilder.cpp)

`buildPredicateNode()` is the main dispatcher. It inspects each AST predicate
expression and routes to the appropriate builder:

| Predicate form | Builder | Output node |
|----------------|---------|-------------|
| `col = col` (two different tables) | `buildJoinPredicate()` | `JOIN` node |
| `col BETWEEN lo AND hi` | `buildBetweenPredicate()` | `FILTER [BETWEEN]` with `values = [lo, hi]` |
| `col IN (v1, v2, …)` | `buildInPredicate()` | `FILTER [IN]` with literal list |
| `col OP literal` | `buildComparisonPredicate()` | `FILTER [LT/LE/EQ/GE/GT/NE/LIKE]` |
| `(col=v1 OR col=v2)` same column | `buildOrPredicate()` → folds to IN | `FILTER [IN]` |
| `(pred1 OR pred2)` same table | `buildOrPredicate()` → SETOPERATION | `SETOPERATION [UNION]` |

**OR optimization** — `collectOrEqualsSameColumn()` tries to recognize the
pattern `col = v1 OR col = v2 OR …`. If all branches compare the *same* column
to literals, the result is a single `FILTER [IN]` node, which is more efficient
than a SETOPERATION tree. If branches differ, a `SETOPERATION [UNION]` is
created instead.

**String functions** — `LOWER(col)` and `UPPER(col)` wrapping a column reference
are transparently unwrapped by `unwrapSimpleFunction()` and the string operation
is recorded in `expression.string_op`.

### 7.4 JoinTreeBuilder

**Files:** [JoinTreeBuilder.hpp](../../cpp/code/include/SQLToLogicalPlanTranslator/JoinTreeBuilder.hpp) /
[JoinTreeBuilder.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator/JoinTreeBuilder.cpp)

Two strategies for building the join tree from SCAN nodes and join predicates:

#### DSU-based (default)

`buildJoinTree()` uses a **Disjoint Set Union** data structure. Each table
starts as its own component. For every join predicate, the two referenced tables
are merged into a combined JOIN node. The representative node after all merges
is the join tree root.

If a predicate connects two tables already in the same component (a cycle), the
predicate is demoted to an `extraJoinFilter` — a FILTER node pushed down into
the tree after construction.

```
Initial:   {dates} {customer} {supplier} {part} {lineorder}
After J1:  {lineorder + customer}  {supplier}  {part}  {dates}
After J2:  {lineorder + customer + supplier}  {part}  {dates}
After J3:  {lineorder + customer + supplier + part}  {dates}
After J4:  {lineorder + customer + supplier + part + dates}  ← single root
```

#### JSON-ordered

`buildJoinTreeFromOrder()` uses a caller-supplied join order (extracted from a
JSON physical plan via `extractJoinOrderFromJson()`). It builds a **left-deep**
tree by processing tables in the given sequence, selecting the matching join
predicate at each step.

If the JSON order covers all tables and every adjacent pair has a matching join
predicate, the result is used. Otherwise the translator falls back to the
DSU-based builder.

`extractJoinOrderFromJson()` reads SCAN leaf positions from a JSON plan tree via
DFS, sorting leaves deepest-first (left-most in the join tree), and deduplicates
table names.

### 7.5 SelectPlanBuilder

**Files:** [SelectPlanBuilder.hpp](../../cpp/code/include/SQLToLogicalPlanTranslator/SelectPlanBuilder.hpp) /
[SelectPlanBuilder.cpp](../../cpp/code/src/SQLToLogicalPlanTranslator/SelectPlanBuilder.cpp)

The orchestrator. Key internal functions:

| Function | Role |
|----------|------|
| `buildPlanForSelect()` | Top-level pipeline (8 steps) |
| `analyzeSelectClause()` | Detects aggregations and plain projections; returns `SelectAnalysis` |
| `matchAgg()` | Pattern-matches an AST expression against known aggregate forms |
| `applyMapNodes()` | Inserts MAP nodes for `SUM(a * b)` and `SUM(a - b)` patterns |
| `buildSelectOutputTree()` | Assembles MAP → AGGREGATE → PROJECTION stack |
| `applySortNode()` | Appends SORT node for ORDER BY |
| `applyLimitOffset()` | Stores LIMIT/OFFSET in the root's `expression` |
| `ensureDefaultAggregate()` | If GROUP BY is present but no agg function, inserts `COUNT(*)` |

**`analyzeSelectClause()` — aggregation patterns**

`matchAgg()` recognises:

| SQL form | Pattern flags set |
|----------|-------------------|
| `COUNT(*)` | `is_star = true` |
| `SUM(col)` | `is_col = true` |
| `SUM(a * b)` | `is_sum_mul = true`; `leftCol`, `rightCol` set |
| `SUM(a - b)` | `is_sum_sub = true`; `leftCol`, `rightCol` set |

When `is_sum_mul` or `is_sum_sub` is detected, `applyMapNodes()` inserts a MAP
node before the AGGREGATE and clears `AggSpec.input` (the MAP output becomes
the implicit input to the aggregation).

---

## 8. Predicate Pushdown Deep-Dive

### 8.1 The Algorithm

`pushDownSelectionPredicate(node, predicate)` recursively traverses the plan
tree to place a predicate at the deepest subtree that provides all the tables
the predicate references.

```
neededTables ← getPredicateTables(predicate)

if node is SCAN:
    if node.base_table ∈ neededTables and |neededTables| == 1:
        attach predicate above node
    else:
        attach predicate at current position (cross-table filter)

if node is JOIN:
    leftTables ← getProvidedTables(node.children[0])
    rightTables ← getProvidedTables(node.children[1])
    if neededTables ⊆ leftTables:
        recurse into left child
    elif neededTables ⊆ rightTables:
        recurse into right child
    else:
        attach predicate above JOIN (cross-join condition)

if node is FILTER:
    recurse into its only child
```

Attaching a predicate means: make the current node the child of the predicate
node, and replace the current node pointer with the predicate node.

**Why push SETOPERATION predicates first?**

`applySelectionPredicates()` processes `SETOPERATION`-typed predicates before
simple `FILTER` predicates. A SETOPERATION (e.g. a UNION of two filters on the
same table) expands into multiple children during pushdown. Placing it before
individual FILTERs ensures the UNION is attached at the correct level before
subsequent filters try to recurse through it.

### 8.2 SETOPERATION Predicates

A SETOPERATION predicate (e.g. `UNION` of two FILTERs) is pushed down by
attaching each of its leaf-filter children to the target node individually:

```
Original SETOPERATION:         After attachment:
  UNION                          UNION
    FILTER [p_mfgr='MFGR#1']       FILTER [p_mfgr='MFGR#1']
    FILTER [p_mfgr='MFGR#2']         SCAN [part]
                                   FILTER [p_mfgr='MFGR#2']
                                     SCAN [part]
```

The target subtree is cloned (`clonePlanSubtree`) once per branch so each
branch gets its own independent copy.

> **In practice** this case rarely occurs: `buildOrPredicate()` already folds
> `col = v1 OR col = v2` patterns into a single `FILTER [IN]` node. A residual
> SETOPERATION only appears when the OR branches cannot be merged (e.g. they
> involve different predicates beyond simple equality).

### 8.3 UNION Distribution

If the current node is itself a `SETOPERATION [UNION]` and the incoming
predicate is *not* a SETOPERATION, the predicate is **distributed** into each
branch of the UNION:

```cpp
// Distribute a scalar filter across a UNION tree
for each branch of UNION:
    push a clone of the predicate into that branch
```

This ensures filters are not placed above a UNION when they could be evaluated
more selectively inside each branch.

---
