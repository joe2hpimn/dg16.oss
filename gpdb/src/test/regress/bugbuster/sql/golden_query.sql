-- start_ignore
-- LAST MODIFIED:
--     2009-07-17 mgilkey
--         This test had an "order" directive, but it wasn't getting passed 
--         through to the output file (and there wasn't one in the .ans file), 
--         so it wasn't taking effect.  
--         Because the "order" directive specifies columns by
--         position, not name, it requires that the columns come out in a
--         known order, so I changed the "SELECT" clauses to specify the
--         columns individually rather than use "SELECT *".
-- end_ignore
\echo -- order 1, 2, 3, 4
SELECT l_orderkey, l_partkey, l_suppkey, l_linenumber, l_quantity, 
   l_extendedprice, l_discount, l_tax, l_returnflag, l_linestatus, l_shipdate, 
   l_commitdate, l_receiptdate, l_shipinstruct, l_shipmode, l_comment 
 FROM LINEITEM 
 ORDER BY 1,2,3,4 LIMIT 1000;

SELECT COUNT(DISTINCT O_CUSTKEY) FROM ORDERS;
-- start_ignore
-- LAST MODIFIED:
--     2009-07-17 mgilkey
--         Added "order" directive.  Because this specifies columns by
--         position, not name, it requires that the columns come out in a
--         known order, so I changed the "SELECT" clauses to specify the
--         columns individually rather than use "SELECT *".
-- end_ignore
\echo -- order 1, 3, 4
SELECT O_CUSTKEY, O_ORDERSTATUS, O_ORDERPRIORITY, O_CLERK, AVG(O_TOTALPRICE)
 FROM ORDERS GROUP BY O_CUSTKEY, O_ORDERSTATUS, O_ORDERPRIORITY, O_CLERK
 ORDER BY O_CUSTKEY, O_ORDERPRIORITY, O_CLERK ASC LIMIT 1000;
\echo -- order 2, 3, 4
SELECT P.P_NAME, L.L_ORDERKEY, L.L_QUANTITY, L.L_EXTENDEDPRICE
 FROM PART P, LINEITEM L
 WHERE P.P_PARTKEY = L.L_PARTKEY
 ORDER BY L_ORDERKEY, L_QUANTITY, L_EXTENDEDPRICE LIMIT 1000;
SELECT P.P_NAME, SUM(L.L_QUANTITY), SUM(L.L_EXTENDEDPRICE) / SUM(L.L_QUANTITY) AS AVG_PRICE FROM PART P, LINEITEM L WHERE P.P_PARTKEY = L.L_PARTKEY GROUP BY P.P_NAME ORDER BY 2,3,1 LIMIT 1000;
\echo -- order 1, 2, 3
SELECT L.L_ORDERKEY, L.L_PARTKEY, L.L_SUPPKEY, SUM(L.L_QUANTITY)
 FROM LINEITEM L
 GROUP BY L.L_ORDERKEY, L.L_PARTKEY, L.L_SUPPKEY
 ORDER BY L.L_ORDERKEY, L.L_PARTKEY, L.L_SUPPKEY LIMIT 1000;
-- start_ignore
-- LAST MODIFIED:
--     2009-07-17 mgilkey
--         Added "order" directive.  Because this specifies columns by
--         position, not name, it requires that the columns come out in a
--         known order, so I changed the "SELECT" clauses to specify the
--         columns individually rather than use "SELECT *".
-- end_ignore
\echo -- order 1
SELECT O.O_ORDERKEY, SUM(O.O_TOTALPRICE) AS TOTAL, SUM(L.L_QUANTITY) AS QUANTITY
 FROM ORDERS O, LINEITEM L
 WHERE O.O_ORDERKEY = L.L_ORDERKEY
 GROUP BY O.O_ORDERKEY
 ORDER BY O_ORDERKEY ASC LIMIT 1000;

SELECT O.O_ORDERKEY, L.L_EXTENDEDPRICE, L.L_DISCOUNT FROM ORDERS O, LINEITEM L WHERE O.O_CUSTKEY = L.L_PARTKEY ORDER BY O.O_ORDERKEY, L.L_EXTENDEDPRICE,L.L_DISCOUNT ASC LIMIT 1000;
