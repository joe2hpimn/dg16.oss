-- Test: Subqueries 2
SELECT '' AS two, f1 AS "Constant Select" FROM SUBSELECT_TBL1
 					 WHERE f1 IN (SELECT 1) ORDER BY 2;
                        

