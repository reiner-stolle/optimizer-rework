#include "SQLToLogicalPlanTranslator/SchemaResolver.hpp"
#include "SQLToLogicalPlanTranslator/TranslatorHelpers.hpp"

// --- Table name resolution from column prefix ---

const std::vector<std::pair<std::string, std::string>> kPrefixToTable = {
    // SSB
    {"lo_", "lineorder"},
    {"d_", "dates"},
    {"c_", "customer"},
    {"s_", "supplier"},
    {"p_", "part"},
    // IMDB
    {"chn_", "char_name"},
    {"ci_", "cast_info"},
    {"cn_", "company_name"},
    {"ct_", "company_type"},
    {"mc_", "movie_companies"},
    {"rt_", "role_type"},
    {"t_", "title"},
};

std::string resolveTableName(const Column &col)
{
    if (!col.table_name.empty())
        return col.table_name;

    for (const auto &[prefix, table] : kPrefixToTable)
    {
        if (col.column_name.rfind(prefix, 0) == 0)
            return table;
    }
    return "";
}

// --- Column type resolution ---

const std::unordered_map<std::string, SSBTableSchema> kSSBSchema = {
    {"date",
     {{"d_datekey", "d_year", "d_yearmonth", "d_yearmonthnum",
       "d_quarter", "d_quarternum", "d_month", "d_monthnum",
       "d_weeknum", "d_daynum", "d_daynumofweek", "d_daynumofyear"},
      PlanColumnType::INTEGER,
      PlanColumnType::STRING}},
    {"customer",
     {{"c_custkey", "c_nationkey", "c_regionkey", "c_mktsegment"},
      PlanColumnType::STRING,
      PlanColumnType::INTEGER}},
    {"supplier",
     {{"s_suppkey", "s_nationkey", "s_regionkey"},
      PlanColumnType::INTEGER,
      PlanColumnType::STRING}},
    {"part",
     {{"p_partkey", "p_size", "p_retailprice"},
      PlanColumnType::INTEGER,
      PlanColumnType::STRING}},
    {"lineorder",
     {{"lo_orderkey", "lo_linenumber", "lo_custkey", "lo_partkey",
       "lo_suppkey", "lo_orderdate", "lo_orderpriority", "lo_shippriority",
       "lo_quantity", "lo_extendedprice", "lo_ordtotalprice", "lo_discount",
       "lo_tax", "lo_commitdate", "lo_receiptdate", "lo_shipmode",
       "lo_shipinstruct", "lo_revenue", "lo_supplycost"},
      PlanColumnType::INTEGER,
      PlanColumnType::STRING}},
};

const std::unordered_set<std::string> kIMDBIntegerColumns = {
    "id", "production_year", "episode_nr", "season_nr", "nr_order"};

PlanColumnType resolveColumnType(const std::string &tableName, const std::string &columnName)
{
    std::string lowerColumn = toLower(columnName);
    std::string lowerTable = toLower(tableName);

    auto it = kSSBSchema.find(lowerTable);
    if (it != kSSBSchema.end())
    {
        const auto &schema = it->second;
        return schema.columns.count(lowerColumn) ? schema.listedType : schema.defaultType;
    }

    if (kIMDBIntegerColumns.count(lowerColumn))
        return PlanColumnType::INTEGER;

    if (lowerColumn.size() >= 3 &&
        lowerColumn.compare(lowerColumn.size() - 3, 3, "_id") == 0)
        return PlanColumnType::INTEGER;

    return PlanColumnType::STRING;
}
