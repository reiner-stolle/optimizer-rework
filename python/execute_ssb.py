import sys, pathlib
root = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(root))            # so `generated` is importable
sys.path.insert(0, str(root / "generated"))  # so `QueryPlan_pb2` (top-level) resolves

import inspect

import threading
import datetime
import argparse

import msg
import client as tcp_client
import util
from generated import WorkResponse_pb2 as WorkResponse
from generated import NetworkRequests_pb2 as NetworkRequests
from generated import WorkItem_pb2 as WorkItem
from generated import QueryPlan_pb2 as QueryPlan

name_list = []
uuid_list = []
complete_tasks = []
query_bodies = None
uuid_condition = threading.Condition()
complete_condition = threading.Condition()

query_strings = {
    "q_1_1": """SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
                FROM lineorder, dates
                WHERE
                    lo_orderdate = d_datekey
                AND d_year = 1993
                AND lo_discount BETWEEN 1 AND 3
                AND lo_quantity < 25;""",
    "q_1_2": """SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
                FROM lineorder, dates
                WHERE
                    lo_orderdate = d_datekey
                AND d_yearmonth = 'Jan1994'
                AND lo_discount BETWEEN 4 AND 6
                AND lo_quantity BETWEEN 26 AND 35;""",
    "q_1_3": """SELECT SUM(lo_extendedprice * lo_discount) AS REVENUE
                FROM lineorder, dates
                WHERE
                    lo_orderdate = d_datekey
                AND d_weeknuminyear = 6
                AND d_year = 1994
                AND lo_discount BETWEEN 5 AND 7
                AND lo_quantity BETWEEN 26 AND 35;""",
    "q_2_1": """SELECT SUM(lo_revenue), d_year, p_brand
                FROM lineorder, dates, part, supplier
                WHERE
                    lo_orderdate = d_datekey
                AND lo_partkey = p_partkey
                AND lo_suppkey = s_suppkey
                AND p_category = 'MFGR#12'
                AND s_region = 'AMERICA'
                GROUP BY d_year, p_brand
                ORDER BY p_brand;""",
    "q_2_2": """SELECT SUM(lo_revenue), d_year, p_brand
                FROM lineorder, dates, part, supplier
                WHERE
                    lo_orderdate = d_datekey
                AND lo_partkey = p_partkey
                AND lo_suppkey = s_suppkey
                AND p_brand BETWEEN 'MFGR#2221' AND 'MFGR#2228'
                AND s_region = 'ASIA'
                GROUP BY d_year, p_brand
                ORDER BY d_year, p_brand;""",
    "q_2_3": """SELECT SUM(lo_revenue), d_year, p_brand
                FROM lineorder, dates, part, supplier
                WHERE
                    lo_orderdate = d_datekey
                AND lo_partkey = p_partkey
                AND lo_suppkey = s_suppkey
                AND p_brand = 'MFGR#2239'
                AND s_region = 'EUROPE'
                GROUP BY d_year, p_brand
                ORDER BY d_year, p_brand;""",
    "q_3_1": """SELECT c_nation, s_nation, d_year, SUM(lo_revenue) AS REVENUE
                FROM customer, lineorder, supplier, dates
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_orderdate = d_datekey
                AND c_region = 'ASIA'
                AND s_region = 'ASIA'
                AND d_year >= 1992
                AND d_year <= 1997
                GROUP BY c_nation, s_nation, d_year
                ORDER BY d_year ASC, REVENUE DESC;""",
    "q_3_2": """SELECT c_city, s_city, d_year, SUM(lo_revenue) AS REVENUE
                FROM customer, lineorder, supplier, dates
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_orderdate = d_datekey
                AND c_nation = 'UNITED STATES'
                AND s_nation = 'UNITED STATES'
                AND d_year >= 1992
                AND d_year <= 1997
                GROUP BY c_city, s_city, d_year
                ORDER BY d_year ASC, REVENUE DESC;""",
    "q_3_3": """SELECT c_city, s_city, d_year, SUM(lo_revenue) AS REVENUE
                FROM customer, lineorder, supplier, dates
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_orderdate = d_datekey
                AND (
                            c_city = 'UNITED KI1'
                        OR c_city = 'UNITED KI5'
                    )
                AND (
                            s_city = 'UNITED KI1'
                        OR s_city = 'UNITED KI5'
                    )
                AND d_year >= 1992
                AND d_year <= 1997
                GROUP BY c_city, s_city, d_year
                ORDER BY d_year ASC, REVENUE DESC;""",
    "q_3_4": """SELECT c_city, s_city, d_year, SUM(lo_revenue) AS REVENUE
                FROM customer, lineorder, supplier, dates
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_orderdate = d_datekey
                AND (
                            c_city = 'UNITED KI1'
                        OR c_city = 'UNITED KI5'
                    )
                AND (
                            s_city = 'UNITED KI1'
                        OR s_city = 'UNITED KI5'
                    )
                AND d_yearmonth = 'Dec1997'
                GROUP BY c_city, s_city, d_year
                ORDER BY d_year ASC, REVENUE DESC;""",
    "q_4_1": """SELECT d_year, c_nation, SUM(lo_revenue - lo_supplycost) AS PROFIT
                FROM dates, customer, supplier, part, lineorder
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_partkey = p_partkey
                AND lo_orderdate = d_datekey
                AND c_region = 'AMERICA'
                AND s_region = 'AMERICA'
                AND (
                            p_mfgr = 'MFGR#1'
                        OR p_mfgr = 'MFGR#2'
                    )
                GROUP BY d_year, c_nation
                ORDER BY d_year, c_nation;""",
    "q_4_2": """SELECT d_year, s_nation, p_category, SUM(lo_revenue - lo_supplycost) AS PROFIT
                FROM dates, customer, supplier, part, lineorder
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_partkey = p_partkey
                AND lo_orderdate = d_datekey
                AND c_region = 'AMERICA'
                AND s_region = 'AMERICA'
                AND (
                            d_year = 1997
                        OR d_year = 1998
                    )
                AND (
                            p_mfgr = 'MFGR#1'
                        OR p_mfgr = 'MFGR#2'
                    )
                GROUP BY d_year, s_nation, p_category
                ORDER BY d_year, s_nation, p_category;"""
    "q_4_3": """SELECT d_year, s_city, p_brand, SUM(lo_revenue - lo_supplycost) AS PROFIT
                FROM dates, customer, supplier, part, lineorder
                WHERE
                    lo_custkey = c_custkey
                AND lo_suppkey = s_suppkey
                AND lo_partkey = p_partkey
                AND lo_orderdate = d_datekey
                AND s_nation = 'UNITED STATES'
                AND (
                            d_year = 1997
                        OR d_year = 1998
                    )
                AND p_category = 'MFGR#14'
                GROUP BY d_year, s_city, p_brand
                ORDER BY d_year, s_city, p_brand;"""
}


# The `update_uuids` function is a callback function that is used to update the pairs for Unit
# name and their registered UUID. It takes a `TCPMessage` object as input, extracts the payload
# from the message, and then parses it into a `UuidForUnitResponse` object from the
# `NetworkRequests` protocol buffer.
def update_uuids(message: msg.TCPMessage):
    global name_list, uuid_list, uuid_condition
    response = NetworkRequests.UuidForUnitResponse()
    response.ParseFromString(message.payload)
    with uuid_condition:
        for name in response.names:
            name_list.append(name)
        for uuid in response.uuids:
            uuid_list.append(uuid)
        uuid_condition.notify_all()


# The `task_finished_cb` function is a callback function that is called when a task responds with its
# finished status. It takes a `TCPMessage` object as input, extracts the payload from the message, and
# then parses it into a `WorkResponse` object from the `WorkResponse` protocol buffer.
def task_finished_cb(message: msg.TCPMessage):
    global complete_condition, complete_tasks
    response = WorkResponse.WorkResponse()
    response.ParseFromString(message.payload)
    with complete_condition:
        # print(f"Received response: {response}")
        complete_tasks.append(f"{response.planId}_{response.itemId}")
        complete_condition.notify_all()


# The `get_timedelta` function calculates the time difference between two datetime objects `t_s`
# (start time) and `t_e` (end time). It returns a string representation of the time delta in seconds
# and microseconds. The function prints the time delta in seconds and microseconds before returning
# the total time in microseconds as a string.
def get_timedelta(t_s: datetime, t_e: datetime) -> str:
    delta = t_e - t_s
    delta_str = f"{delta.seconds} s {delta.microseconds} us"
    print(delta_str)
    return str(delta.seconds*1000000 + delta.microseconds)


def run_query(client: tcp_client.TCPClient, query: str) -> int:
    global query_bodies
    query: QueryPlan = query_bodies[query]()
    t_s = datetime.datetime.now()
    client.send_message(query)
    t_e = datetime.datetime.now()
    return get_timedelta(t_s, t_e)


def q_1_1(planId: int = 1) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="lineorder", inputColumn="lo_discount", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[1, 3]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="lineorder", inputColumn="lo_quantity", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_LT, filterArgVals=[25]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=[1993]))

    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=4, dependsOn=[1, 2], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_INTERSECTION,
        innerTable="intermediate", innerColumn="q_1_1_it_1", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_1_1_it_2", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_1_1_it_4", outputType=WorkItem.TYPE_BITMASK))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[4], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_1_it_4", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_5", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_1_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_6", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=7, dependsOn=[5, 6], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_1_1_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_1_1_it_5", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_7", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[4], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_1_it_4", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_extendedprice", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_8", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[4], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_1_it_4", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_discount", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_9", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=10, dependsOn=[7, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_1_it_7_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_1_1_it_8", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[7, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_1_it_7_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_1_1_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_11", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="map",
        planId=planId, itemId=12, dependsOn=[10, 11], operatorId=WorkItem.OP_MAP, operatorType=WorkItem.ARITH_MUL,
        inputTable="intermediate", inputColumn="q_1_1_it_10", inputType=WorkItem.TYPE_BITMASK,
        partnerTable="intermediate", partnerColumn="q_1_1_it_11", partnerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_1_it_12", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="aggregate",
        planId=planId, itemId=13, dependsOn=[12], operatorId=WorkItem.OP_AGGREGATE, aggregationFunction=WorkItem.AGG_SUM,
        inputTable="intermediate", inputColumn="q_1_1_it_12", inputType=WorkItem.TYPE_BITMASK,
        outputTable="result", outputColumn="q_1_1_it_13", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="result", 
        planId=planId, itemId=14, dependsOn=[13],
        resultTables=["result"], resultColumns=["q_1_1_it_13"], resultHeader=["revenue"], resultName="result_q_1_1"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_1_2(planId: int = 2) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="lineorder", inputColumn="lo_discount", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[4, 6]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="lineorder", inputColumn="lo_quantity", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[26, 35]))

    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_yearmonth", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_1_2_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["Jan1994"]))

    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=4, dependsOn=[1, 2], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_INTERSECTION,
        innerTable="intermediate", innerColumn="q_1_2_it_1", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_1_2_it_2", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_1_2_it_4", outputType=WorkItem.TYPE_BITMASK))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[4], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_2_it_4", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_5", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_2_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_6", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=7, dependsOn=[5, 6], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_1_2_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_1_2_it_5", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_7", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="matrialize",
        planId=planId, itemId=8, dependsOn=[4], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_2_it_4", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_extendedprice", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_8", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[4], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_2_it_4", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_discount", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_9", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=10, dependsOn=[7, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_2_it_7_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_1_2_it_8", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[7, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_2_it_7_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_1_2_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_11", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="map",
        planId=planId, itemId=12, dependsOn=[10, 11], operatorId=WorkItem.OP_MAP, operatorType=WorkItem.ARITH_MUL,
        inputTable="intermediate", inputColumn="q_1_2_it_10", inputType=WorkItem.TYPE_BITMASK,
        partnerTable="intermediate", partnerColumn="q_1_2_it_11", partnerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_2_it_12", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="aggregate",
        planId=planId, itemId=13, dependsOn=[12], operatorId=WorkItem.OP_AGGREGATE, aggregationFunction=WorkItem.AGG_SUM,
        inputTable="intermediate", inputColumn="q_1_2_it_12", inputType=WorkItem.TYPE_BITMASK,
        outputTable="result", outputColumn="q_1_2_it_13", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=14, dependsOn=[13],
        resultTables=["result"], resultColumns=["q_1_2_it_13"], resultHeader=["revenue"],resultName="result_q_1_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)
    

def q_1_3(planId: int = 3) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_weeknuminyear", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=[6]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_1_3_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=[1994]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="lineorder", inputColumn="lo_discount", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[5, 7]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=4, operatorId=WorkItem.OP_FILTER,
        inputTable="lineorder", inputColumn="lo_quantity", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_4", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[26, 35]))

    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=5, dependsOn=[1, 2], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_INTERSECTION,
        innerTable="intermediate", innerColumn="q_1_3_it_1", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_1_3_it_2", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_1_3_it_5", outputType=WorkItem.TYPE_BITMASK))

    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=6, dependsOn=[3, 4], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_INTERSECTION,
        innerTable="intermediate", innerColumn="q_1_3_it_3", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_1_3_it_4", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_1_3_it_6", outputType=WorkItem.TYPE_BITMASK))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_3_it_5", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_7", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_3_it_6", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_8", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=9, dependsOn=[7, 8], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_1_3_it_7", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_1_3_it_8", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_9", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=10, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_3_it_6", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_extendedprice", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_3_it_6", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_discount", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_11", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[9, 10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_3_it_9_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_1_3_it_10", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_12", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[9, 11], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_1_3_it_9_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_1_3_it_11", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_13", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="map",
        planId=planId, itemId=14, dependsOn=[12, 13], operatorId=WorkItem.OP_MAP, operatorType=WorkItem.ARITH_MUL,
        inputTable="intermediate", inputColumn="q_1_3_it_12", inputType=WorkItem.TYPE_BITMASK,
        partnerTable="intermediate", partnerColumn="q_1_3_it_13", partnerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_1_3_it_14", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="aggregate",
        planId=planId, itemId=15, dependsOn=[14], operatorId=WorkItem.OP_AGGREGATE, aggregationFunction=WorkItem.AGG_SUM,
        inputTable="intermediate", inputColumn="q_1_3_it_14", inputType=WorkItem.TYPE_BITMASK,
        outputTable="result", outputColumn="q_1_3_it_15", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=16, dependsOn=[14],
        resultTables=["result"], resultColumns=["q_1_3_it_15"], 
        resultHeader=["revenue"], resultName="result_q_1_3"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)
    
    
def q_2_1(planId: int = 4) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_category", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_2_1_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#12"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_2_1_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["AMERICA"]))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=3, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_3", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_4", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_brand", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_5", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=6, dependsOn=[3], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_2_1_it_3", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_partkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_6", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_6_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=90, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_6_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_7_ord", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=70, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_6_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_7_rev", outputType=WorkItem.TYPE_INTEGER))

    # Materialize filtered p_brand with join id list from lo_partkey (6_o) and p_partkey
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[5, 6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_6_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_5", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_12", outputType=WorkItem.TYPE_INTEGER))

    # Join s_suppkey == lo_suppkey
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=8, dependsOn=[4, 7], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_2_1_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_2_1_it_7", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_8", outputType=WorkItem.TYPE_INTEGER))

    # Project lo_orderdate according to join result
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[7, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_8_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_7_ord", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_9", outputType=WorkItem.TYPE_INTEGER))
    
    # Project lko_revenue according to join result
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=91, dependsOn=[70, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_8_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_7_rev", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_9_rev", outputType=WorkItem.TYPE_INTEGER))
    
    # Materialize filtered/joined p_brand with join id list from lo_suppkey
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[8, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_8_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_13", outputType=WorkItem.TYPE_INTEGER))

    # Join d_datekey 
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[9], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="dates", innerColumn="d_datekey", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_2_1_it_9", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_10_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=14, dependsOn=[10, 91], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_9_rev", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[10, 13], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_13", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_15", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=16, dependsOn=[11, 14, 15], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate"], 
        columns=["q_2_1_it_11", "q_2_1_it_15"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_2_1_it_16_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_2_1_it_16_cluster",
        aggregationTable="intermediate", aggregationColumn="q_2_1_it_14", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_2_1_it_16", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="matrialize",
        planId=planId, itemId=30, dependsOn=[11, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_16_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_11", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_d_year", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=31, dependsOn=[15, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_1_it_16_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_1_it_15", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_1_it_p_brand", outputType=WorkItem.TYPE_INTEGER))
        
    workItems.append(util.create_work_item(itemType="sort",
        planId=planId, itemId=18, dependsOn=[31], operatorId=WorkItem.OP_SORT,
        inputTables=["intermediate"], 
        inputColumns=["q_2_1_it_p_brand"],
        indexOutputTable="intermediate", indexOutputColumn="q_2_1_final_idx",
        sortOrder=[True]))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=19, dependsOn=[16, 18, 30, 31],
        resultTables=["intermediate","intermediate","intermediate"], 
        resultColumns=["q_2_1_it_16_agg","q_2_1_d_year","q_2_1_it_p_brand"], 
        resultHeader=["sum","d_year","p_brand"],
        resultIndexTable="intermediate", resultIndexColumn="q_2_1_final_idx",
        resultName="result_q_2_1"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)
    
    
def q_2_2(planId: int = 5) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_brand", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_2_2_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=["MFGR#2221", "MFGR#2228"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_2_2_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["ASIA"]))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=3, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_3", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_4", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_brand", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_5", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=6, dependsOn=[3], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_2_2_it_3", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_partkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_6", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_6_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=90, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_6_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_7_ord", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=70, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_6_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_7_rev", outputType=WorkItem.TYPE_INTEGER))

    # Materialize filtered p_brand with join id list from lo_partkey (6_o) and p_partkey
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[5, 6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_6_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_5", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_12", outputType=WorkItem.TYPE_INTEGER))

    # Join s_suppkey == lo_suppkey
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=8, dependsOn=[4, 7], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_2_2_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_2_2_it_7", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_8", outputType=WorkItem.TYPE_INTEGER))

    # Project lo_orderdate according to join result
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[7, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_8_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_7_ord", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_9", outputType=WorkItem.TYPE_INTEGER))
    
    # Project lko_revenue according to join result
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=91, dependsOn=[70, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_8_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_7_rev", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_9_rev", outputType=WorkItem.TYPE_INTEGER))
    
    # Materialize filtered/joined p_brand with join id list from lo_suppkey
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[8, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_8_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_13", outputType=WorkItem.TYPE_INTEGER))

    # Join d_datekey 
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[9], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="dates", innerColumn="d_datekey", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_2_2_it_9", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_10_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=14, dependsOn=[10, 91], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_9_rev", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[10, 13], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_13", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_15", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=16, dependsOn=[11, 14, 15], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate"], 
        columns=["q_2_2_it_11", "q_2_2_it_15"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_2_2_it_16_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_2_2_it_16_cluster",
        aggregationTable="intermediate", aggregationColumn="q_2_2_it_14", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_2_2_it_16", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="matrialize",
        planId=planId, itemId=17, dependsOn=[11, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_16_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_11", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_17", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[15, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_2_it_16_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_2_2_it_15", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_2_it_18", outputType=WorkItem.TYPE_INTEGER))
        

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=19, dependsOn=[16, 17, 18],
        resultTables=["intermediate","intermediate","intermediate"], 
        resultColumns=["q_2_2_it_16_agg","q_2_2_it_17","q_2_2_it_18"], 
        resultHeader=["sum","d_year","p_brand"],
        resultName="result_q_2_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)

    
def q_2_3(planId: int = 6) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_brand", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_2_3_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#2239"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_2_3_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["EUROPE"]))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=3, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_brand", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_3", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_4", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_5", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=6, dependsOn=[5], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_2_3_it_5", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_suppkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_6", outputType=WorkItem.TYPE_INTEGER))  

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_6_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_7", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_6_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_8", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_6_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_9", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[4, 7], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_2_3_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_2_3_it_7", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10, 3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_10_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_3", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[10, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_8", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_12", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[10, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_13", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=14, dependsOn=[13], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="dates", innerColumn="d_datekey", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_2_3_it_13", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_14", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[14], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_14_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_15", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[14, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_14_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_16", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[14, 11], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_14_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_11", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_17", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=18, dependsOn=[15, 17, 16], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate"], 
        columns=["q_2_3_it_15", "q_2_3_it_17"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_2_3_it_18_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_2_3_it_18_cluster",
        aggregationTable="intermediate", aggregationColumn="q_2_3_it_16", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_2_3_it_18", agregationResultColumnType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=19, dependsOn=[18, 17], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_18_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_17", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_19", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[18, 15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_2_3_it_18_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_2_3_it_15", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_2_3_it_20", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=21, dependsOn=[18, 19, 20], 
        resultTables=["intermediate","intermediate","intermediate"], 
        resultColumns=["q_2_3_it_18_agg","q_2_3_it_20","q_2_3_it_19"], 
        resultHeader=["sum","d_year","p_brand"],
        resultName="result_q_2_3"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_3_1(planId: int = 7) -> msg.TCPMessage:
    workItems = []
    # filter(c_region)
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_1_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["ASIA"]))
    
    # filter(s_region)
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_1_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["ASIA"]))
    
    # filter(d_year)
    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[1992, 1997]))

    # mat(c_custkey) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_4", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(c_nation) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_nation", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_5", outputType=WorkItem.TYPE_INTEGER))

    # mat(s_suppkey) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_6", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(s_nation) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_nation", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_9", outputType=WorkItem.TYPE_INTEGER))

    # join mat(c_custkey) with lo_custkey
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[4], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_1_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_custkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_10", outputType=WorkItem.TYPE_INTEGER))

    # mat(lo_orderdate, lo_suppkey, lo_revenue) using join(lo_custkey)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_12", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=50, dependsOn=[10, 5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_10_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_5", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_50", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=14, dependsOn=[6, 11], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_1_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_1_it_11", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[14, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_15", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[14, 13], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_13", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_16", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[14, 7], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_14_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_17", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[14, 50], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_50", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_18", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=19, dependsOn=[8, 15], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_1_it_8", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_1_it_15", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_19", outputType=WorkItem.TYPE_INTEGER))   
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[19, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_16", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_20", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=21, dependsOn=[19, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=22, dependsOn=[19, 18], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_18", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_22", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[19, 17], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_17", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_23", outputType=WorkItem.TYPE_INTEGER))
 
    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=24, dependsOn=[22, 23, 21, 19, 20], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate","intermediate"], 
        columns=["q_3_1_it_22", "q_3_1_it_23","q_3_1_it_21"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_3_1_it_19_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_3_1_it_19_cluster",
        aggregationTable="intermediate", aggregationColumn="q_3_1_it_20", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_3_1_it_24", agregationResultColumnType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=26, dependsOn=[19, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_21", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_26", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[19, 22], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_22", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_27", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=28, dependsOn=[19, 23], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_1_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_1_it_23", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_1_it_28", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="sort",
        planId=planId, itemId=29, dependsOn=[26, 24], operatorId=WorkItem.OP_SORT,
        inputTables=["intermediate","intermediate"], 
        inputColumns=["q_3_1_it_26","q_3_1_it_24_agg"],
        indexOutputTable="intermediate", indexOutputColumn="q_3_1_it_29",
        sortOrder=[True,False]))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=30, dependsOn=[24, 26, 27, 28, 29], 
        resultTables=["intermediate","intermediate","intermediate","intermediate"], 
        resultColumns=["q_3_1_it_27","q_3_1_it_28","q_3_1_it_26","q_3_1_it_24_agg"], 
        resultHeader=["c_nation","s_nation","d_year","revenue"],
        resultIndexTable="intermediate", resultIndexColumn="q_3_1_it_29",
        resultName="result_q_3_1"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_3_2(planId: int = 8) -> msg.TCPMessage:
    workItems = []
     # filter(c_region)
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_nation", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_2_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED STATES"]))
    
    # filter(s_region)
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_nation", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_2_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED STATES"]))
    
    # filter(d_year)
    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[1992, 1997]))

    # mat(c_custkey) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_4", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(c_nation) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_5", outputType=WorkItem.TYPE_INTEGER))

    # mat(s_suppkey) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_6", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(s_nation) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_9", outputType=WorkItem.TYPE_INTEGER))

    # join mat(c_custkey) with lo_custkey
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[4], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_2_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_custkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_10", outputType=WorkItem.TYPE_INTEGER))

    # mat(lo_orderdate, lo_suppkey, lo_revenue) using join(lo_custkey)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_12", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=50, dependsOn=[10, 5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_10_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_5", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_50", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=14, dependsOn=[6, 11], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_2_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_2_it_11", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[14, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_15", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[14, 13], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_13", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_16", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[14, 7], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_14_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_17", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[14, 50], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_50", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_18", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=19, dependsOn=[8, 15], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_2_it_8", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_2_it_15", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_19", outputType=WorkItem.TYPE_INTEGER)) 
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[19, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_16", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_20", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=21, dependsOn=[19, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=22, dependsOn=[19, 18], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_18", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_22", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[19, 17], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_17", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_23", outputType=WorkItem.TYPE_INTEGER))
        
    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=24, dependsOn=[19, 20, 21, 22, 23], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate","intermediate"], 
        columns=["q_3_2_it_22", "q_3_2_it_23","q_3_2_it_21"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_3_2_it_19_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_3_2_it_19_cluster",
        aggregationTable="intermediate", aggregationColumn="q_3_2_it_20", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_3_2_it_24", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=26, dependsOn=[19, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_21", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_26", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[19, 22], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_22", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_27", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=28, dependsOn=[19, 23], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_2_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_2_it_23", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_2_it_28", outputType=WorkItem.TYPE_INTEGER))
      
    workItems.append(util.create_work_item(itemType="sort",
        planId=planId, itemId=29, dependsOn=[24, 26], operatorId=WorkItem.OP_SORT,
        inputTables=["intermediate","intermediate"], 
        inputColumns=["q_3_2_it_26","q_3_2_it_24_agg"],
        indexOutputTable="intermediate", indexOutputColumn="q_3_2_it_29",
        sortOrder=[True,False]))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=30, dependsOn=[24, 26, 27, 28, 29], 
        resultTables=["intermediate","intermediate","intermediate","intermediate"], 
        resultColumns=["q_3_2_it_27","q_3_2_it_28","q_3_2_it_26","q_3_2_it_24_agg"], 
        resultHeader=["c_nation","s_nation","d_year","revenue"],
        resultIndexTable="intermediate", resultIndexColumn="q_3_2_it_29",
        resultName="result_q_3_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_3_3(planId: int = 9) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=100, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_3_it_100", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI1"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=101, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_3_it_101", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI5"]))
    
    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=1, dependsOn=[100, 101], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_UNION,
        innerTable="intermediate", innerColumn="q_3_3_it_100", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_3_3_it_101", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_3_3_it_1", outputType=WorkItem.TYPE_BITMASK))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=200, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_3_it_200", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI1"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=201, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_3_it_201", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI5"]))
    
    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=2, dependsOn=[200, 201], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_UNION,
        innerTable="intermediate", innerColumn="q_3_3_it_200", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_3_3_it_201", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_3_3_it_2", outputType=WorkItem.TYPE_BITMASK))
    
    # filter(d_year)
    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[1992, 1997]))

    # mat(c_custkey) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_4", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(c_nation) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_5", outputType=WorkItem.TYPE_INTEGER))

    # mat(s_suppkey) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_6", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(s_nation) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_9", outputType=WorkItem.TYPE_INTEGER))

    # join mat(c_custkey) with lo_custkey
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[4], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_3_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_custkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_10", outputType=WorkItem.TYPE_INTEGER))

    # mat(lo_orderdate, lo_suppkey, lo_revenue) using join(lo_custkey)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_12", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=50, dependsOn=[10, 5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_10_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_5", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_50", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=14, dependsOn=[6, 11], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_3_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_3_it_11", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[14, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_15", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[14, 13], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_13", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_16", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[14, 7], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_14_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_17", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[14, 50], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_50", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_18", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=19, dependsOn=[8, 15], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_3_it_8", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_3_it_15", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_19", outputType=WorkItem.TYPE_INTEGER)) 
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[19, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_16", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_20", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=21, dependsOn=[19, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=22, dependsOn=[19, 18], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_18", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_22", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[19, 17], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_17", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_23", outputType=WorkItem.TYPE_INTEGER))
        
    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=24, dependsOn=[19, 20, 21, 22, 23], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate","intermediate"], 
        columns=["q_3_3_it_22", "q_3_3_it_23","q_3_3_it_21"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_3_3_it_19_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_3_3_it_19_cluster",
        aggregationTable="intermediate", aggregationColumn="q_3_3_it_20", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_3_3_it_24", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=26, dependsOn=[19, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_21", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_26", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[19, 22], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_22", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_27", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=28, dependsOn=[19, 23], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_3_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_3_it_23", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_3_it_28", outputType=WorkItem.TYPE_INTEGER))
      
    workItems.append(util.create_work_item(itemType="sort",
        planId=planId, itemId=29, dependsOn=[24, 26], operatorId=WorkItem.OP_SORT,
        inputTables=["intermediate","intermediate"], 
        inputColumns=["q_3_3_it_26","q_3_3_it_24_agg"],
        indexOutputTable="intermediate", indexOutputColumn="q_3_3_it_29",
        sortOrder=[True,False]))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=30, dependsOn=[24, 26, 27, 28, 29], 
        resultTables=["intermediate","intermediate","intermediate","intermediate"], 
        resultColumns=["q_3_3_it_27","q_3_3_it_28","q_3_3_it_26","q_3_3_it_24_agg"], 
        resultHeader=["c_nation","s_nation","d_year","revenue"],
        resultIndexTable="intermediate", resultIndexColumn="q_3_3_it_29",
        resultName="result_q_3_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_3_4(planId: int = 10) -> msg.TCPMessage:
    workItems = []
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=100, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_4_it_100", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI1"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=101, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_4_it_101", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI5"]))
    
    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=1, dependsOn=[100, 101], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_UNION,
        innerTable="intermediate", innerColumn="q_3_4_it_100", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_3_4_it_101", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_3_4_it_1", outputType=WorkItem.TYPE_BITMASK))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=200, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_4_it_200", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI1"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=201, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_city", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_3_4_it_201", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED KI5"]))
    
    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=2, dependsOn=[200, 201], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_UNION,
        innerTable="intermediate", innerColumn="q_3_4_it_200", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_3_4_it_201", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_3_4_it_2", outputType=WorkItem.TYPE_BITMASK))
    
    # filter(d_year)
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_yearmonth", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["Dec1997"]))

    # mat(c_custkey) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=4, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_4", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(c_nation) using filter(c_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=5, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_5", outputType=WorkItem.TYPE_INTEGER))

    # mat(s_suppkey) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_6", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(s_nation) using filter(s_region)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    # mat(d_datekey) using filter(d_year)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_9", outputType=WorkItem.TYPE_INTEGER))

    # join mat(c_custkey) with lo_custkey
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[4], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_4_it_4", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_custkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_10", outputType=WorkItem.TYPE_INTEGER))

    # mat(lo_orderdate, lo_suppkey, lo_revenue) using join(lo_custkey)
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_12", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_10_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=50, dependsOn=[10, 5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_10_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_5", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_50", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=14, dependsOn=[6, 11], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_4_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_4_it_11", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[14, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_15", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[14, 13], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_13", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_16", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[14, 7], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_14_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_17", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[14, 50], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_14_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_50", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_18", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=19, dependsOn=[8, 15], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_3_4_it_8", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_3_4_it_15", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_19", outputType=WorkItem.TYPE_INTEGER)) 
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[19, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_16", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_20", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=21, dependsOn=[19, 9], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_i", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_9", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=22, dependsOn=[19, 18], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_18", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_22", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[19, 17], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_o", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_17", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_23", outputType=WorkItem.TYPE_INTEGER))
        
    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=24, dependsOn=[19, 20, 21, 22, 23], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate","intermediate"], 
        columns=["q_3_4_it_22", "q_3_4_it_23","q_3_4_it_21"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_3_4_it_19_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_3_4_it_19_cluster",
        aggregationTable="intermediate", aggregationColumn="q_3_4_it_20", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_3_4_it_24", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=26, dependsOn=[19, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_21", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_26", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[19, 22], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_22", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_27", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=28, dependsOn=[19, 23], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_3_4_it_19_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_3_4_it_23", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_3_4_it_28", outputType=WorkItem.TYPE_INTEGER))
      
    workItems.append(util.create_work_item(itemType="sort",
        planId=planId, itemId=29, dependsOn=[24, 26], operatorId=WorkItem.OP_SORT,
        inputTables=["intermediate","intermediate"], 
        inputColumns=["q_3_4_it_26","q_3_4_it_24_agg"],
        indexOutputTable="intermediate", indexOutputColumn="q_3_4_it_29",
        sortOrder=[True,False]))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=30, dependsOn=[24, 26, 27, 28, 29], 
        resultTables=["intermediate","intermediate","intermediate","intermediate"], 
        resultColumns=["q_3_4_it_27","q_3_4_it_28","q_3_4_it_26","q_3_4_it_24_agg"], 
        resultHeader=["c_nation","s_nation","d_year","revenue"],
        resultIndexTable="intermediate", resultIndexColumn="q_3_4_it_29",
        resultName="result_q_3_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_4_1(planId: int = 11) -> msg.TCPMessage:
    workItems = [] 
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_4_1_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["AMERICA"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_4_1_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["AMERICA"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_mfgr", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#1"]))

    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=4, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_mfgr", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_4", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#2"]))
    
    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=5, dependsOn=[3, 4], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_UNION,
        innerTable="intermediate", innerColumn="q_4_1_it_3", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_4_1_it_4", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_4_1_it_5", outputType=WorkItem.TYPE_BITMASK))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=6, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_6", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_nation", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=8, dependsOn=[5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_5", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_9", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=10, dependsOn=[6], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_1_it_6", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_suppkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_11", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_supplycost", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_12", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=14, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=15, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_10_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_15", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=16, dependsOn=[8, 14], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_1_it_8", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_1_it_14", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_16", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[16, 10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_16_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_1_it_10_o", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_17", outputType=WorkItem.TYPE_INTEGER))
   
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=19, dependsOn=[10], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_17", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_19", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=21, dependsOn=[9, 19], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_1_it_9", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_1_it_19", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=22, dependsOn=[21, 17], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_21_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_1_it_17", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_22", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[22], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_22", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_23", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=25, dependsOn=[7, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_21_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_1_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_25", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=26, dependsOn=[23], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="dates", innerColumn="d_datekey", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_1_it_23", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_26", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[22, 26], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_26_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_1_it_22", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_27", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=28, dependsOn=[25, 26], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_26_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_1_it_25", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_28", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=30, dependsOn=[26], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_26_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_30", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=31, dependsOn=[27], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_27", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_31", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=32, dependsOn=[27], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_27", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_supplycost", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_32", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="map",
        planId=planId, itemId=310, dependsOn=[31], operatorId=WorkItem.OP_MAP, operatorType=WorkItem.ARITH_SUB,
        inputTable="intermediate", inputColumn="q_4_1_it_31", inputType=WorkItem.TYPE_INTEGER,
        partnerTable="intermediate", partnerColumn="q_4_1_it_32", partnerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_310", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=33, dependsOn=[28, 30, 32, 310], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate"], 
        columns=["q_4_1_it_30", "q_4_1_it_28"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_4_1_it_32_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_4_1_it_32_cluster",
        aggregationTable="intermediate", aggregationColumn="q_4_1_it_310", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_4_1_it_33", agregationResultColumnType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=34, dependsOn=[33, 30], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_33_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_4_1_it_30", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_34", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=35, dependsOn=[33, 28], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_1_it_33_idx_ext", indexType=WorkItem.TYPE_POSLIST,
        filterTable="intermediate", filterColumn="q_4_1_it_28", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_1_it_35", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=36, dependsOn=[33, 34, 35], 
        resultTables=["intermediate","intermediate","intermediate"], 
        resultColumns=["q_4_1_it_34","q_4_1_it_35","q_4_1_it_33_agg",], 
        resultHeader=["d_year","c_nation","profit"],
        resultName="result_q_4_1_v2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_4_2(planId: int = 12) -> msg.TCPMessage:
    workItems = [] 
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_4_2_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["AMERICA"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=2, operatorId=WorkItem.OP_FILTER,
        inputTable="customer", inputColumn="c_region", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_4_2_it_2", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["AMERICA"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_mfgr", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#1"]))

    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=4, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_mfgr", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_4", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#2"]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=5, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_5", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[1997,1998]))
    
    workItems.append(util.create_work_item(itemType="set_operation",
        planId=planId, itemId=6, dependsOn=[3, 4], operatorId=WorkItem.OP_SETOPERATION,
        operation=WorkItem.REL_UNION,
        innerTable="intermediate", innerColumn="q_4_2_it_3", innerType=WorkItem.TYPE_BITMASK,
        outerTable="intermediate", outerColumn="q_4_2_it_4", outerType=WorkItem.TYPE_BITMASK,
        outputTable="intermediate", outputColumn="q_4_2_it_6", outputType=WorkItem.TYPE_BITMASK))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_5", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_5", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_9", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=10, dependsOn=[2], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_2", indexType=WorkItem.TYPE_BITMASK,
        filterTable="customer", filterColumn="c_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_10", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_nation", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_12", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_6", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_category", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=14, dependsOn=[6], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_6", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=15, dependsOn=[13], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_2_it_13", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_suppkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_15", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_16", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_17", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_supplycost", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_18", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=19, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_19", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_20", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=21, dependsOn=[15, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_15_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=22, dependsOn=[10, 17], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_2_it_10", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_2_it_17", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_22", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[22, 20], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_20", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_23", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=24, dependsOn=[22, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_16", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_24", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=25, dependsOn=[22, 18], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_18", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_25", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=26, dependsOn=[22, 19], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_19", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_26", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[22, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_21", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_27", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=28, dependsOn=[9, 23], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_2_it_9", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_2_it_23", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_28", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=29, dependsOn=[28, 7], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_28_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_29", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=30, dependsOn=[28, 24], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_24", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_30", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=31, dependsOn=[28, 27], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_27", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_31", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=32, dependsOn=[28, 25], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_25", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_32", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=33, dependsOn=[28, 26], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_26", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_33", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=34, dependsOn=[14, 33], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_2_it_14", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_2_it_33", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_34", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=35, dependsOn=[34, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_34_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_8", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_35", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=36, dependsOn=[34, 29], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_29", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_36", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=37, dependsOn=[34, 30], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_30", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_37", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=38, dependsOn=[34, 32], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_32", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_38", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=39, dependsOn=[34, 31], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_31", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_39", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="map",
        planId=planId, itemId=40, dependsOn=[37, 38], operatorId=WorkItem.OP_MAP, operatorType=WorkItem.ARITH_SUB,
        inputTable="intermediate", inputColumn="q_4_2_it_37", inputType=WorkItem.TYPE_INTEGER,
        partnerTable="intermediate", partnerColumn="q_4_2_it_38", partnerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_40", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=41, dependsOn=[35, 36, 39, 40], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate","intermediate"], 
        columns=["q_4_2_it_36", "q_4_2_it_39", "q_4_2_it_35"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_4_2_it_41_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_4_2_it_41_cluster",
        aggregationTable="intermediate", aggregationColumn="q_4_2_it_40", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_4_2_it_41", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=42, dependsOn=[41], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_41_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_41", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_42", outputType=WorkItem.TYPE_INTEGER))
            
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=43, dependsOn=[41, 39], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_41_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_39", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_43", outputType=WorkItem.TYPE_INTEGER))
            
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=44, dependsOn=[35, 41], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_2_it_41_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_2_it_35", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_2_it_44", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=45, dependsOn=[41, 42, 43, 44], 
        resultTables=["intermediate","intermediate","intermediate","intermediate"], 
        resultColumns=["q_4_2_it_42","q_4_2_it_43","q_4_2_it_44","q_4_2_it_41_agg"], 
        resultHeader=["d_year","s_nation","p_category","profit"],
        resultName="result_q_4_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def q_4_3(planId: int = 13) -> msg.TCPMessage:
    workItems = [] 
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=1, operatorId=WorkItem.OP_FILTER,
        inputTable="supplier", inputColumn="s_nation", inputType=WorkItem.TYPE_STRING,
        outputTable="intermediate", outputColumn="q_4_3_it_1", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["UNITED STATES"]))
    
    workItems.append(util.create_work_item(itemType="string_filter",
        planId=planId, itemId=3, operatorId=WorkItem.OP_FILTER,
        inputTable="part", inputColumn="p_category", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_3", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_EQ, filterArgVals=["MFGR#14"]))

    workItems.append(util.create_work_item(itemType="int_filter",
        planId=planId, itemId=5, operatorId=WorkItem.OP_FILTER,
        inputTable="dates", inputColumn="d_year", inputType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_5", outputType=WorkItem.TYPE_BITMASK,
        filterType=WorkItem.COMP_BETWEEN, filterArgVals=[1997,1998]))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=7, dependsOn=[5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_5", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_year", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_7", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=9, dependsOn=[5], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_5", indexType=WorkItem.TYPE_BITMASK,
        filterTable="dates", filterColumn="d_datekey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_9", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=11, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_suppkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_13", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=12, dependsOn=[1], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_1", indexType=WorkItem.TYPE_BITMASK,
        filterTable="supplier", filterColumn="s_city", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_12", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=13, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_brand", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_8", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=14, dependsOn=[3], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_3", indexType=WorkItem.TYPE_BITMASK,
        filterTable="part", filterColumn="p_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_14", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=15, dependsOn=[13], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_3_it_13", innerType=WorkItem.TYPE_INTEGER,
        outerTable="lineorder", outerColumn="lo_suppkey", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_15", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=16, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_revenue", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_16", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=17, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_custkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_17", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=18, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_supplycost", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_18", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=19, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_partkey", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_19", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=20, dependsOn=[15], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_15_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="lineorder", filterColumn="lo_orderdate", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_20", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=21, dependsOn=[15, 12], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_15_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_12", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_21", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=22, dependsOn=[10, 17], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="customer", innerColumn="c_custkey", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_3_it_17", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_22", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=23, dependsOn=[22, 20], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_20", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_23", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=24, dependsOn=[22, 16], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_16", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_24", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=25, dependsOn=[22, 18], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_18", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_25", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=26, dependsOn=[22, 19], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_19", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_26", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=27, dependsOn=[22, 21], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_22_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_21", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_27", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=28, dependsOn=[9, 23], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_3_it_9", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_3_it_23", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_28", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=29, dependsOn=[28, 7], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_28_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_7", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_29", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=30, dependsOn=[28, 24], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_24", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_30", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=31, dependsOn=[28, 27], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_27", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_31", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=32, dependsOn=[28, 25], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_25", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_32", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=33, dependsOn=[28, 26], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_28_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_26", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_33", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="join",
        planId=planId, itemId=34, dependsOn=[14, 33], operatorId=WorkItem.OP_HASHJOIN,
        innerTable="intermediate", innerColumn="q_4_3_it_14", innerType=WorkItem.TYPE_INTEGER,
        outerTable="intermediate", outerColumn="q_4_3_it_33", outerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_34", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=35, dependsOn=[34, 8], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_34_i", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_8", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_35", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=36, dependsOn=[34, 29], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_29", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_36", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=37, dependsOn=[34, 30], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_30", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_37", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=38, dependsOn=[34, 32], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_32", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_38", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=39, dependsOn=[34, 31], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_34_o", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_31", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_39", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="map",
        planId=planId, itemId=40, dependsOn=[37, 38], operatorId=WorkItem.OP_MAP, operatorType=WorkItem.ARITH_SUB,
        inputTable="intermediate", inputColumn="q_4_3_it_37", inputType=WorkItem.TYPE_INTEGER,
        partnerTable="intermediate", partnerColumn="q_4_3_it_38", partnerType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_40", outputType=WorkItem.TYPE_INTEGER))

    workItems.append(util.create_work_item(itemType="multigroup",
        planId=planId, itemId=41, dependsOn=[35, 36, 39, 40], operatorId = WorkItem.OP_GROUPBY, 
        tables=["intermediate", "intermediate","intermediate"], 
        columns=["q_4_3_it_36", "q_4_3_it_39", "q_4_3_it_35"], 
        columntypes=[WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER,WorkItem.TYPE_INTEGER],
        outputIndexTable="intermediate", outputIndexColumn="q_4_3_it_41_idx", 
        outputClustersTable="intermediate", outputClustersColumn="q_4_3_it_41_cluster",
        aggregationTable="intermediate", aggregationColumn="q_4_3_it_40", agregationColumnType=WorkItem.TYPE_INTEGER,
        aggregationResultTable="intermediate", aggregationResultColumn="q_4_3_it_41", agregationResultColumnType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=42, dependsOn=[41], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_41_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_41", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_42", outputType=WorkItem.TYPE_INTEGER))
            
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=43, dependsOn=[41, 39], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_41_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_39", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_43", outputType=WorkItem.TYPE_INTEGER))
            
    workItems.append(util.create_work_item(itemType="materialize",
        planId=planId, itemId=44, dependsOn=[35, 41], operatorId=WorkItem.OP_MATERIALIZE,
        indexTable="intermediate", indexColumn="q_4_3_it_41_idx_ext", indexType=WorkItem.TYPE_BITMASK,
        filterTable="intermediate", filterColumn="q_4_3_it_35", filterType=WorkItem.TYPE_INTEGER,
        outputTable="intermediate", outputColumn="q_4_3_it_44", outputType=WorkItem.TYPE_INTEGER))
    
    workItems.append(util.create_work_item(itemType="result",
        planId=planId, itemId=45, dependsOn=[41, 42, 43, 44], 
        resultTables=["intermediate","intermediate","intermediate","intermediate"], 
        resultColumns=["q_4_3_it_42","q_4_3_it_43","q_4_3_it_44","q_4_3_it_41_agg"], 
        resultHeader=["d_year","s_city","p_brand","profit"],
        resultName="result_q_4_2"))
    
    return util.create_query_plan(planId=planId, workItems=workItems)


def execute_ssb(client: tcp_client.TCPClient, q_parameter: list):
    global query_bodies
    # collect all query functions defined in this module whose names start with "q_"
    query_bodies = {}
    current_module = sys.modules[__name__]
    for name, func in inspect.getmembers(current_module, inspect.isfunction):
        if name.startswith("q_"):
            query_bodies[name] = func
    
    client.register_callback(msg.TCPPackageType.TaskFinished, task_finished_cb)

    """ Fetch all UUIDs for all units """
    work = util.create_uuid_request_item(type=msg.UnitType.ComputeUnit)
    client.send_message(work)
    with uuid_condition:
        if len(uuid_list) == 0 and client.connection_up:
            uuid_condition.wait(0.1)

    """ Select the first ComputeUnit that we find """
    tgt_uuid = 0
    for name, uuid in zip(name_list, uuid_list):
        if "ComputeUnit" in name:
            tgt_uuid = uuid
            break

    # if tgt_uuid == 0:
    #     print("[Error] Could not find a ComputeUnit. Exiting.")
    #     client.disconnect()
    #     exit(-1)

    deltas = []
    if q_parameter:
        for q in q_parameter:
            deltas.append(run_query(client, q))
            complete_tasks.clear()
        
        for q, d in zip(q_parameter, deltas):
            print(f"Time taken for {q}: {d} us")
    else:
        print("Running all SSB Queries.")
        for query in ["q_1_1", "q_1_2"]:
            deltas.append(run_query(client, query))
    
    client.disconnect()
    
    
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-ip', help='The IP of the message bus',
                        default="127.0.0.1", required=False)
    parser.add_argument('-port', help='The port of the message bus',
                        type=int, default=23232, required=False)
    parser.add_argument('-info', help='Some meta info about this units purpose.',
                        default="I am loading data.", required=False)
    parser.add_argument('-name', help='A pretty name to register this unit with.',
                        default="Data Loader", required=False)
    parser.add_argument('-q', help='Which quer[ies] to run. If multiple queries are given, write as CSV.',
                        default=False, required=False)
    ssb_args = parser.parse_args()

    client = tcp_client.TCPClient(
        unit_type=msg.UnitType.QueryPlaner, unit_info=ssb_args.info, name=ssb_args.name)

    """ Register custom callbacks """
    client.register_callback(
        msg.TCPPackageType.UuidForUnitResponse, update_uuids)

    """ Connect and wait until fully established """
    client.connect(ssb_args.ip, ssb_args.port)

    query_list = []
    if ssb_args.q:
        qs = ssb_args.q.split(",")
        query_list.extend([query.strip() for query in qs])
    
    execute_ssb(client, query_list)
