drop schema if exists dpe_single cascade;
create schema dpe_single;
set search_path='dpe_single';
set gp_segments_for_planner=2;

drop table if exists pt;
drop table if exists pt1;
drop table if exists t;
drop table if exists t1;

create table pt(dist int, pt1 text, pt2 text, pt3 text, ptid int) 
DISTRIBUTED BY (dist)
PARTITION BY RANGE(ptid) 
          (
          START (0) END (5) EVERY (1),
          DEFAULT PARTITION junk_data
          )
;

create table pt1(dist int, pt1 text, pt2 text, pt3 text, ptid int) 
DISTRIBUTED RANDOMLY
PARTITION BY RANGE(ptid) 
          (
          START (0) END (5) EVERY (1),
          DEFAULT PARTITION junk_data
          )
;

create table t(dist int, tid int, t1 text, t2 text);

create index pt1_idx on pt using btree (pt1);
create index ptid_idx on pt using btree (ptid);

insert into pt select i, 'hello' || i, 'world', 'drop this', i % 6 from generate_series(0,53) i;

insert into t select i, i % 6, 'hello' || i, 'bar' from generate_series(0,1) i;

create table t1 as select * from t;

insert into pt1 select * from pt;

analyze pt;
analyze pt1;
analyze t;
analyze t1;

--
-- Simple positive cases
--

explain select * from t, pt where tid = ptid;

select * from t, pt where tid = ptid;

explain select * from t, pt where tid + 1 = ptid;

select * from t, pt where tid + 1 = ptid;

explain select * from t, pt where tid = ptid and t1 = 'hello' || tid;

select * from t, pt where tid = ptid and t1 = 'hello' || tid;

explain select * from t, pt where t1 = pt1 and ptid = tid;

select * from t, pt where t1 = pt1 and ptid = tid;

--
-- in and exists clauses
--

explain select * from pt where ptid in (select tid from t where t1 = 'hello' || tid);

select * from pt where ptid in (select tid from t where t1 = 'hello' || tid);

explain select * from pt where exists (select 1 from t where tid = ptid and t1 = 'hello' || tid);

select * from pt where exists (select 1 from t where tid = ptid and t1 = 'hello' || tid);

--
-- group-by on top
--

explain select count(*) from t, pt where tid = ptid;

select count(*) from t, pt where tid = ptid;

--
-- window function on top
--

explain select *, rank() over (order by ptid,pt1) from t, pt where tid = ptid;

select *, rank() over (order by ptid,pt1) from t, pt where tid = ptid;

--
-- set ops
--

explain select * from t, pt where tid = ptid
	  union all
	  select * from t, pt where tid + 2 = ptid;

select * from t, pt where tid = ptid
	  union all
	  select * from t, pt where tid + 2 = ptid;

--
-- set-ops
--

explain select count(*) from
	( select * from t, pt where tid = ptid
	  union all
	  select * from t, pt where tid + 2 = ptid
	  ) foo;

select count(*) from
	( select * from t, pt where tid = ptid
	  union all
	  select * from t, pt where tid + 2 = ptid
	  ) foo;


--
-- other join types (NL)
--
set enable_hashjoin=off;
set enable_nestloop=on;
set enable_mergejoin=off;

explain select * from t, pt where tid = ptid;

select * from t, pt where tid = ptid;

set enable_hashjoin=on;
set enable_nestloop=off;
set enable_mergejoin=off;

--
-- index scan
--

set enable_seqscan=off;
set enable_indexscan=on;
set enable_bitmapscan=off;
set enable_hashjoin=off;

explain select * from t, pt where tid = ptid and pt1 = 'hello0';

select * from t, pt where tid = ptid and pt1 = 'hello0';

--
-- NL Index Scan
--
set enable_nestloop=on;
set enable_indexscan=on;
set enable_seqscan=off;
set enable_hashjoin=off;

explain select * from t, pt where tid = ptid;

select * from t, pt where tid = ptid;

--
-- Negative test cases where transform does not apply
--

set enable_indexscan=off;
set enable_seqscan=on;
set enable_hashjoin=on;
set enable_nestloop=off;

explain select * from t, pt where t1 = pt1;

select * from t, pt where t1 = pt1;

explain select * from t, pt where tid < ptid;

select * from t, pt where tid < ptid;

--
-- cascading joins
--

explain select * from t, t1, pt where t1.t2 = t.t2 and t1.tid = ptid;

select * from t, t1, pt where t1.t2 = t.t2 and t1.tid = ptid;


--
-- explain analyze
--

explain analyze select * from t, pt where tid = ptid;

--
-- Partitioned table on both sides of the join. This will create a result node as Append node is
-- not projection capable.
--

explain select * from pt, pt1 where pt.ptid = pt1.ptid and pt.pt1 = 'hello0' order by pt1.dist;

select * from pt, pt1 where pt.ptid = pt1.ptid and pt.pt1 = 'hello0' order by pt1.dist;

explain select count(*) from pt, pt1 where pt.ptid = pt1.ptid and pt.pt1 = 'hello0';

select count(*) from pt, pt1 where pt.ptid = pt1.ptid and pt.pt1 = 'hello0';

--
-- Multi-level partitions
--

drop schema if exists dpe_multi cascade;
create schema dpe_multi;
set search_path='dpe_multi';
set gp_segments_for_planner=2;

create table dim1(dist int, pid int, code text, t1 text);

insert into dim1 values (1, 0, 'OH', 'world1');
insert into dim1 values (1, 1, 'OH', 'world2');
insert into dim1 values (1, 100, 'GA', 'world2'); -- should not have a match at all

create table fact1(dist int, pid int, code text, u int)
partition by range(pid)
subpartition by list(code)
subpartition template 
(
 subpartition ca values('CA'),
 subpartition oh values('OH'),
 subpartition wa values('WA')
)
(
 start (0)
 end (4) 
 every (1)
);

insert into fact1 select 1, i % 4 , 'OH', i from generate_series (1,100) i;
insert into fact1 select 1, i % 4 , 'CA', i + 10000 from generate_series (1,100) i;

--
-- Join on all partitioning columns
--

set gp_dynamic_partition_pruning=off;
explain select * from dim1 inner join fact1 on (dim1.pid=fact1.pid and dim1.code=fact1.code) order by fact1.u;
select * from dim1 inner join fact1 on (dim1.pid=fact1.pid and dim1.code=fact1.code) order by fact1.u;

set gp_dynamic_partition_pruning=on;
explain select * from dim1 inner join fact1 on (dim1.pid=fact1.pid and dim1.code=fact1.code) order by fact1.u;
select * from dim1 inner join fact1 on (dim1.pid=fact1.pid and dim1.code=fact1.code) order by fact1.u;

--
-- Join on one of the partitioning columns
--

set gp_dynamic_partition_pruning=off;
explain select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) order by fact1.u;
select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) order by fact1.u;

set gp_dynamic_partition_pruning=on;
explain select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) order by fact1.u;
select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) order by fact1.u;

--
-- Join on one of the partitioning columns and static elimination on other
--

set gp_dynamic_partition_pruning=off;
explain select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) and fact1.code = 'OH' order by fact1.u;
select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) and fact1.code = 'OH' order by fact1.u;

set gp_dynamic_partition_pruning=on;
explain select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) and fact1.code = 'OH' order by fact1.u;
select * from dim1 inner join fact1 on (dim1.pid=fact1.pid) and fact1.code = 'OH' order by fact1.u;

--
-- add aggregates
--

set gp_dynamic_partition_pruning=off;
explain select fact1.code, count(*) from dim1 inner join fact1 on (dim1.pid=fact1.pid) group by 1 order by 1;
select fact1.code, count(*) from dim1 inner join fact1 on (dim1.pid=fact1.pid) group by 1 order by 1;

set gp_dynamic_partition_pruning=on;
explain select fact1.code, count(*) from dim1 inner join fact1 on (dim1.pid=fact1.pid) group by 1 order by 1;
select fact1.code, count(*) from dim1 inner join fact1 on (dim1.pid=fact1.pid) group by 1 order by 1;


--
-- multi-attribute list partitioning
--
drop schema if exists dpe_malp cascade;
create schema dpe_malp;
set search_path='dpe_malp';
set gp_segments_for_planner=2;

create table malp (i int, j int, t text) 
distributed by (i) 
partition by list (i, j) 
( 
partition p1 values((1,10)) ,
partition p2 values((2,20)),
partition p3 values((3,30)) 
);

insert into malp select 1, 10, 'hello1';
insert into malp select 1, 10, 'hello2';
insert into malp select 1, 10, 'hello3';
insert into malp select 2, 20, 'hello4';
insert into malp select 2, 20, 'hello5';
insert into malp select 3, 30, 'hello6';

create table dim(i int, j int)
distributed randomly;

insert into dim values(1, 10);

analyze malp;
analyze dim;

explain select * from dim inner join malp on (dim.i = malp.i);

set gp_dynamic_partition_pruning = off;
select * from dim inner join malp on (dim.i = malp.i);

set gp_dynamic_partition_pruning = on;
select * from dim inner join malp on (dim.i = malp.i);

set gp_dynamic_partition_pruning = on;
select * from dim inner join malp on (dim.i = malp.i and dim.j = malp.j); -- only one partition should be chosen

--
-- bugs
--

drop schema if exists dpe_bugs cascade;
create schema dpe_bugs;
set search_path='dpe_bugs';
set gp_segments_for_planner=2;

create table pat(a int, b date) partition by range (b) (start ('2010-01-01') end ('2010-01-05') every (1), default partition other);
insert into pat select i,date '2010-01-01' + i from generate_series(1, 10)i;  
create table jpat(a int, b date);
insert into jpat values(1, '2010-01-02');
explain select * from (select count(*) over (order by a rows between 1 preceding and 1 following), a, b from jpat)jpat inner join pat using(b);

select * from (select count(*) over (order by a rows between 1 preceding and 1 following), a, b from jpat)jpat inner join pat using(b);

drop table if exists pt;
drop table if exists pt1;
drop table if exists t;
drop table if exists t1;

