#include <gtest/gtest.h>
#include "SQLParser.h"

TEST(SQLParserTest, ValidQuery)
{
    std::string query = "SELECT * FROM test;";
    hsql::SQLParserResult result;
    hsql::SQLParser::parse(query, &result);
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(result.size(), 1);
}

TEST(SQLParserTest, InvalidQuery)
{
    std::string query = "SELCT FROM";
    hsql::SQLParserResult result;
    hsql::SQLParser::parse(query, &result);
    EXPECT_FALSE(result.isValid());
}