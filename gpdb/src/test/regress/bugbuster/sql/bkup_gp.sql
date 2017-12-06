\c tpch_heap
DROP INDEX IF EXISTS idx_ckey;
DROP INDEX IF EXISTS idx_ckey_cname;
DROP INDEX IF EXISTS idx_nation_bitmap;
DROP INDEX IF EXISTS idx_lineitem_keys;
DROP INDEX IF EXISTS idx_linenumber;
DROP TRIGGER IF EXISTS before_heap_ins_trig ON customer;
DROP FUNCTION IF EXISTS trigger_func();
\echo -- end_ignore

-- Create indexes on customer table
CREATE UNIQUE INDEX idx_ckey ON customer USING btree (c_custkey);

CREATE INDEX idx_ckey_cname ON customer USING btree (c_custkey, c_name);

CREATE INDEX idx_nation_bitmap ON customer USING bitmap (c_nationkey);

-- Create indexes on lineitem table
CREATE UNIQUE INDEX idx_lineitem_keys ON lineitem USING btree (l_orderkey, l_partkey, l_suppkey, l_linenumber);

CREATE INDEX idx_linenumber ON lineitem USING btree (l_linenumber);

-- Create function trigger_func() that will be used as the trigger
CREATE FUNCTION trigger_func() RETURNS trigger LANGUAGE plpgsql NO SQL AS '
BEGIN
RAISE NOTICE ''trigger_func() called: action = %, when = %, level = %'', TG_OP, TG_WHEN, TG_LEVEL;
RETURN NULL;
END;';

-- Create trigger before_heap_ins_trig
CREATE TRIGGER before_heap_ins_trig BEFORE INSERT ON customer
FOR EACH ROW EXECUTE PROCEDURE trigger_func();

\c template1

\! gp_dump --version &> /dev/null
\! gp_restore --version &> /dev/null

-- now run back up
\! rm -fr ./cdbfast_gpbkup &> /dev/null
\! mkdir ./cdbfast_gpbkup &> /dev/null
\! gp_dump tpch_heap --gp-d=`pwd`/cdbfast_gpbkup &> /dev/null

-- now restore it
drop database if exists gptest_gprestore;
create database gptest_gprestore;

\! gp_restore --gp-d=`pwd`/cdbfast_gpbkup --gp-k=`find ./cdbfast_gpbkup/gp* -name gp\*[0-9].gz -o -name gp\*[0-9] -exec basename '{}' \; | cut -f 1 -d '.' | cut -f 5 -d '_' | head -1` -d gptest_gprestore &> /dev/null
-- clean up
\! rm -fr ./cdbfast_gpbkup > /dev/null

\c gptest_gprestore
-- Check TPCH all records of all tables are correctly restored
select count(*), sum(n_nationkey), min(n_nationkey), max(n_nationkey) from nation;
select count(*), sum(r_regionkey), min(r_regionkey), max(r_regionkey) from region;
select count(*), sum(p_partkey), min(p_partkey), max(p_partkey) from part;
select count(*), sum(s_suppkey), min(s_suppkey), max(s_suppkey) from supplier;
select count(*), sum(ps_partkey + ps_suppkey), min(ps_partkey + ps_suppkey), max(ps_partkey + ps_suppkey) from partsupp;
select count(*), sum(c_custkey), min(c_custkey), max(c_custkey) from customer;
select count(*), sum(o_orderkey), min(o_orderkey), max(o_orderkey) from orders;
select count(*), sum(l_linenumber), min(l_linenumber), max(l_linenumber) from lineitem;

