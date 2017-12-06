-- ALTER TABLE ... SET DISTRIBUTED BY
-- This is the main interface for system expansion
\set DATA values(1, 2), (2, 3), (3, 4)
-- Basic sanity tests
set optimizer_disable_missing_stats_collection = on;
create table atsdb (i int, j text) distributed by (i);
insert into atsdb :DATA;

-- should fail
alter table atsdb set distributed by ();
alter table atsdb set distributed by (m);
alter table atsdb set distributed by (i, i);
alter table atsdb set distributed by (i, m);
alter table atsdb set distributed by (i);

-- should work
alter table atsdb set distributed randomly;
select localoid::regclass, attrnums from gp_distribution_policy where localoid = 'atsdb'::regclass;
-- not possible to correctly verify random distribution

alter table atsdb set distributed by (j);
select localoid::regclass, attrnums from gp_distribution_policy where localoid = 'atsdb'::regclass;
-- verify that the data is correctly redistributed by building a fresh 
-- table with the same policy
create table ats_test (i int, j text) distributed by (j);
insert into ats_test :DATA;
select gp_segment_id, * from ats_test except
select gp_segment_id, * from atsdb;
drop table ats_test;

alter table atsdb set distributed by (i, j);
select localoid::regclass, attrnums from gp_distribution_policy where localoid = 'atsdb'::regclass;
-- verify
create table ats_test (i int, j text) distributed by (i, j);
insert into ats_test :DATA;
select gp_segment_id, * from ats_test except
select gp_segment_id, * from atsdb;
drop table ats_test;

alter table atsdb set distributed by (j, i);
select localoid::regclass, attrnums from gp_distribution_policy where localoid = 'atsdb'::regclass;
-- verify
create table ats_test (i int, j text) distributed by (j, i);
insert into ats_test :DATA;
select gp_segment_id, * from ats_test except
select gp_segment_id, * from atsdb;
drop table ats_test;

-- Now make sure indexes work.
create index atsdb_i_idx on atsdb(i);
set enable_seqscan to off;
explain select * from atsdb where i = 1;
select * from atsdb where i = 1;
alter table atsdb set distributed by (i);
explain select * from atsdb where i = 1;
select * from atsdb where i = 1;

drop table atsdb;

-- Now try AO
create table atsdb_ao (i int, j text) distributed by (i);
insert into atsdb_ao select i, (i+1)::text from generate_series(1, 100) i;
insert into atsdb_ao select i, (i+1)::text from generate_series(1, 100) i;
-- check that we're an AO table
explain select count(*) from atsdb_ao;
select count(*) from atsdb_ao;
alter table atsdb_ao set distributed by (j);
-- Still AO?
explain select count(*) from atsdb_ao;
select count(*) from atsdb_ao;
drop table atsdb_ao;

-- Check divergent distribution policies for partitioning.
create table atsdb (i int, j int, k int) distributed by (i) partition by range(k)
(start(1) end(4) every(1));
alter table atsdb_1_prt_2 set distributed by (j);
alter table atsdb_1_prt_3 set distributed randomly;
-- test COPY
copy atsdb from stdin delimiter '|';
1|1|1
2|1|1
2|2|1
3|1|1
3|2|1
3|3|1
1|1|2
2|1|2
2|2|2
3|1|2
3|2|2
3|3|2
1|1|3
2|1|3
2|2|3
3|1|3
3|2|3
3|3|3
\.

select count(*) from atsdb;

-- compare distribution: we create a table, identical to the partitioned table,
-- and compare how the tuples have been distributed.
create table atsdb_1 (like atsdb_1_prt_1) distributed by (i);
copy atsdb_1 from stdin delimiter '|';
1|1|1
2|1|1
2|2|1
3|1|1
3|2|1
3|3|1
\.
select gp_segment_id, * from atsdb where k = 1 except 
select gp_segment_id, * from atsdb_1;

create table atsdb_2 (like atsdb_1_prt_2) distributed by (i);
copy atsdb_2 from stdin delimiter '|';
1|1|2
2|1|2
2|2|2
3|1|2
3|2|2
3|3|2
\.
select gp_segment_id, * from atsdb where k = 2 except
select gp_segment_id, * from atsdb_2;

-- Can't test randomly distributed

-- Can't test INSERT (yet)

drop table atsdb, atsdb_1, atsdb_2;

-- Can't redistribute system catalogs
alter table pg_class set distributed by (relname);
alter table pg_class set with(appendonly = true);

-- MPP-7770: allow testing of changing storage for now
set gp_setwith_alter_storage = true;

alter table pg_class set with(appendonly = true);

-- WITH clause
create table atsdb (i int, j text) distributed by (j);
insert into atsdb select i, i::text from generate_series(1, 10) i;
alter table atsdb set with(appendonly = true);
select relname, segrelid != 0, reloptions from pg_class, pg_appendonly where pg_class.oid =
'atsdb'::regclass and relid = pg_class.oid;
select * from atsdb;
drop table atsdb;

create view distcheck as select relname as rel, attname from
gp_distribution_policy g, pg_attribute p, pg_class c
where g.localoid = p.attrelid and attnum = any(g.attrnums) and
c.oid = p.attrelid;

-- dropped columns
create table atsdb (i int, j int, t text, n numeric) distributed by (j);
insert into atsdb select i, i+1, i+2, i+3 from generate_series(1, 100) i;
alter table atsdb drop column i;
select * from atsdb;
alter table atsdb set distributed by (t);
select * from distcheck where rel = 'atsdb';
alter table atsdb drop column n;

alter table atsdb set with(appendonly = true, compresslevel = 3);
select relname, segrelid != 0, reloptions from pg_class, pg_appendonly where pg_class.oid = 
'atsdb'::regclass and relid = pg_class.oid;

select * from distcheck where rel = 'atsdb';

select * from atsdb;
alter table atsdb set distributed by (j);

select * from distcheck where rel = 'atsdb';
select relname, segrelid != 0, reloptions from pg_class, pg_appendonly where pg_class.oid =
'atsdb'::regclass and relid = pg_class.oid;

select * from atsdb;
-- validate parameters
alter table atsdb set with (appendonly = ff);
alter table atsdb set with (reorganize = true);
alter table atsdb set with (fgdfgef = asds);
alter table atsdb set with(reorganize = true, reorganize = false) distributed
randomly;
drop table atsdb;

-- Check that we correctly cascade for partitioned tables
create table atsdb (i int, j int, k int) distributed by (i) partition by range(k)
(start(1) end(10) every(1));
insert into atsdb select i+2, i+1, i from generate_series(1, 9) i;
select * from distcheck where rel like 'atsdb%';
alter table atsdb set distributed by (j);
select * from distcheck where rel like 'atsdb%';
select * from atsdb order by 1, 2, 3;
alter table atsdb set with(appendonly = true);
select relname, a.blocksize, compresslevel, compresstype, checksum from pg_class c, pg_appendonly a where 
relname  like 'atsdb%' and c.oid = a.relid order by 1;
select * from atsdb order by 1, 2, 3;
insert into atsdb select i+2, i+1, i from generate_series(1, 9) i;
select * from atsdb order by 1, 2, 3;
drop table atsdb;
drop view distcheck;

-- MPP-5452
-- Should succeed
create table atsdb (i int, k int) distributed by (i) partition by range(i) (start (1) end(10)
every(1));
alter table atsdb alter partition for(rank(5)) set distributed by (i);
alter table atsdb alter partition for(rank(5)) set distributed by (i);
alter table atsdb alter partition for(rank(5)) set distributed by (i);
drop table atsdb;

--MPP-5500

CREATE TABLE test_add_drop_rename_column_change_datatype(
 text_col text,
 bigint_col bigint,
 char_vary_col character varying(30),
 numeric_col numeric,
 int_col int4,
 float_col float4,
 int_array_col int[],
 drop_col numeric,
 before_rename_col int4,
 change_datatype_col numeric,
 a_ts_without timestamp without time zone,
 b_ts_with timestamp with time zone,
 date_column date) distributed randomly;
 
insert into test_add_drop_rename_column_change_datatype values ('0_zero', 0, '0_zero', 0, 0, 0, '{0}', 0, 0, 0, '2004-10-19 10:23:54', '2004-10-19 10:23:54+02', '1-1-2000');
insert into test_add_drop_rename_column_change_datatype values ('1_zero', 1, '1_zero', 1, 1, 1, '{1}', 1, 1, 1, '2005-10-19 10:23:54', '2005-10-19 10:23:54+02', '1-1-2001');
insert into test_add_drop_rename_column_change_datatype values ('2_zero', 2, '2_zero', 2, 2, 2, '{2}', 2, 2, 2, '2006-10-19 10:23:54', '2006-10-19 10:23:54+02', '1-1-2002');
 
ALTER TABLE test_add_drop_rename_column_change_datatype ADD COLUMN added_col character varying(30);
ALTER TABLE test_add_drop_rename_column_change_datatype DROP COLUMN drop_col ;
ALTER TABLE test_add_drop_rename_column_change_datatype RENAME COLUMN before_rename_col TO after_rename_col;
ALTER TABLE test_add_drop_rename_column_change_datatype ALTER COLUMN change_datatype_col TYPE int4;
alter table test_add_drop_rename_column_change_datatype set with(reorganize =
true) distributed randomly;
select * from test_add_drop_rename_column_change_datatype ;
drop table test_add_drop_rename_column_change_datatype ;

-- MPP-5501
-- should run without error
create table atsdb with (appendonly=true) as select * from
generate_series(1,1000);
alter table only atsdb set with(reorganize=true) distributed by (generate_series);
select count(*) from atsdb;
drop table atsdb;

-- MPP-5746
create table mpp5746 (c int[], t text);
insert into mpp5746 select array[i], i from generate_series(1, 100) i;
alter table mpp5746 set with (reorganize=true, appendonly = true);
select * from mpp5746 order by 1;
alter table mpp5746 drop column t;
select * from mpp5746 order by 1;
alter table mpp5746 set with (reorganize=true, appendonly = false);
select * from mpp5746 order by 1;
drop table mpp5746;

-- MPP-5738
create table mpp5738 (a int, b int, c int, d int)
partition by range(d) (start(1) end(10) inclusive every(1));
insert into mpp5738 select i, i+1, i+2, i from generate_series(1, 10) i;
select * from mpp5738;
alter table mpp5738 alter partition for(rank(1)) set with (appendonly=true);
select * from mpp5738;
drop table mpp5738;

drop table if exists mpp5754;
CREATE TABLE mpp5754 (
             N_NATIONKEY INTEGER,
             N_NAME CHAR(25),
             N_REGIONKEY INTEGER,
             N_COMMENT VARCHAR(152)
             ) with (appendonly = true, checksum = true)
             distributed by (N_NATIONKEY);

copy mpp5754 from stdin with delimiter '|';
0|ALGERIA|0| haggle. carefully final deposits detect slyly agai
\.
select * from mpp5754 order by n_nationkey;
alter table mpp5754 set distributed randomly;
select count(*) from mpp5754;
alter table mpp5754 set distributed by (n_nationkey);
select * from mpp5754 order by n_nationkey;
drop table mpp5754;


-- MPP-5918
create role atsdb;
create table owner_test(i int, toast text) distributed randomly;
alter table owner_test owner to atsdb;
alter table owner_test set with (reorganize = true) distributed by (i);
-- verify, atsdb should own all three
select a.relname, 
       x.rolname as relowner, 
       y.rolname as toastowner,
	   z.rolname as toastidxowner
from pg_class a, pg_class b, pg_class c, 
     pg_authid x, pg_authid y, pg_authid z
where a.reltoastrelid = b.oid 
  and b.reltoastidxid=c.oid
  and a.relname='owner_test'
  and x.oid = a.relowner
  and y.oid = b.relowner
  and z.oid = c.relowner;

-- MPP-9663 - Check that the ownership is consistent on the segments as well
select a.relname, 
       x.rolname as relowner, 
       y.rolname as toastowner,
	   z.rolname as toastidxowner
from gp_dist_random('pg_class') a, 
	 gp_dist_random('pg_class') b, 
	 gp_dist_random('pg_class') c, 
     pg_authid x, pg_authid y, pg_authid z
where a.reltoastrelid=b.oid
  and b.reltoastidxid=c.oid
  and a.relname='owner_test'
  and x.oid = a.relowner
  and y.oid = b.relowner
  and z.oid = c.relowner
  and a.gp_segment_id = 0
  and b.gp_segment_id = 0
  and c.gp_segment_id = 0;

-- MPP-9663 - The code path is different when the table has dropped columns
alter table owner_test add column d text;
alter table owner_test drop column d;
alter table owner_test set with (reorganize = true) distributed by (i);

select a.relname, 
       x.rolname as relowner, 
       y.rolname as toastowner,
	   z.rolname as toastidxowner
from gp_dist_random('pg_class') a, 
	 gp_dist_random('pg_class') b, 
	 gp_dist_random('pg_class') c, 
     pg_authid x, pg_authid y, pg_authid z
where a.reltoastrelid=b.oid
  and b.reltoastidxid=c.oid
  and a.relname='owner_test'
  and x.oid = a.relowner
  and y.oid = b.relowner
  and z.oid = c.relowner
  and a.gp_segment_id = 0
  and b.gp_segment_id = 0
  and c.gp_segment_id = 0;

drop table owner_test;
drop role atsdb;

-- MPP-5928
-- We want to test a whole bunch of different type configurations and whether
-- ATSDB can handle them as dropped types
-- Here's a script to generate all this:

--align="int2 int4 char double"
--length="variable 1 3 4 11 17 19 23 32 196"
--pbv="true false"
--storage="true false"
--
--for a in $align
--do
--	for l in $length
--	do
--		for p in $pbv
--		do
--			for s in $storage
--			do
--				if [ $p == "true" ] && [ $l != "variable" ] && [ $l -gt 8 ];
--				then
--					continue
--				fi
--				if [ $p == "true" ] && [ $l == "variable" ];
--				then
--					continue
--				fi
--				echo "
--drop table alter_distpol_g;
--create type break;
--create function breakin (cstring) returns break as 'textin' language internal;
--create function breakout (break) returns cstring as 'textout' language internal;"
--
--				echo "create type break (input = breakin, output = breakout, internallength = $l, passedbyvalue = $p, alignment = $a);"
--				echo "
--create table alter_distpol_g (i int, j break, k text) with (appendonly = $s);
--insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
--alter table alter_distpol_g drop column j;
--select * from alter_distpol_g order by 1;
--alter table alter_distpol_g set with(reorganize = true) distributed randomly;
--select * from alter_distpol_g order by 1;
--drop type break cascade;
--alter table alter_distpol_g set with(reorganize = true) distributed randomly;
--select * from alter_distpol_g order by 1;"
--			done
--		done
--	done
--done

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = int2);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = int4);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = char);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = variable, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = true, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 1, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = true, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 3, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = true, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 4, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 11, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 17, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 19, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 23, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 32, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = true);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;
create type break;
create function breakin (cstring) returns break as 'textin' language internal;
create function breakout (break) returns cstring as 'textout' language internal;
create type break (input = breakin, output = breakout, internallength = 196, passedbyvalue = false, alignment = double);

create table alter_distpol_g (i int, j break, k text) with (appendonly = false);
insert into alter_distpol_g (i, k) select i, i from generate_series(1, 10) i;
alter table alter_distpol_g drop column j;
select * from alter_distpol_g order by 1;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;
drop type break cascade;
alter table alter_distpol_g set with(reorganize = true) distributed randomly;
select * from alter_distpol_g order by 1;

drop table alter_distpol_g;

-- MPP-6332
create table abc (a int, b int, c int) distributed by (a);
Alter table abc set distributed randomly;
Alter table abc set with (reorganize=false) distributed randomly;
drop table abc;


-- MPP-7770: disable changing storage options (default, for now)
set gp_setwith_alter_storage = false;

-- disallow, so fails
create table atsdb (i int, j text) distributed by (j);
alter table atsdb set with(appendonly = true);
drop table atsdb;

-- MPP-8474: Index relfilenode mismatch: entry db to segment db.
--
-- XXX This really belongs in alter_table.sql but this is not 
--     in use in current_good_schedule.
drop table if exists mpp8474 cascade; --ignore

create table mpp8474(a int, b int, c text) 
    with (appendonly=true) 
    distributed by (b);
create index mpp8474_a 
    on mpp8474(a);
alter table mpp8474 
    add column d int default 10;

select 
    'Mismatched relfilenodes:' as oops,
    e.oid::regclass as entry_oid,
    e.relkind,
    e.relfilenode as entry_relfilenode,
    s.segid,
    s.segfilenode as segment_relfilenode
from 
    pg_class e,
    (   select gp_execution_segment(), oid, relfilenode
        from gp_dist_random('pg_class')
    ) s (segid, segoid, segfilenode)
where 
    e.oid = s.segoid 
    and e.relfilenode != s.segfilenode
    and e.relname ~ '^mpp8474.*';

drop table mpp8474;

-- MPP-18660: duplicate entry in gp_distribution_policy
set enable_indexscan=on;
set enable_seqscan=off;

drop table if exists distrib_index_test;
create table distrib_index_test (a int, b text) distributed by (a);
select count(*) from gp_distribution_policy 
	where localoid in (select oid from pg_class where relname='distrib_index_test');

begin;
drop table distrib_index_test;
rollback;

select count(*) from gp_distribution_policy 
	where localoid in (select oid from pg_class where relname='distrib_index_test');

reset enable_indexscan;
reset enable_seqscan;
drop table distrib_index_test;
-- alter partitioned table crash
-- Alter partitioned table set distributed by will crash when:
--     1. reorganize = false.
--     2. table have index.
--     3. partition table have "with" option.
drop index if exists distrib_part_test_idx;
drop table if exists distrib_part_test;
CREATE TABLE distrib_part_test 
(
  col1 int,
  col2 decimal,
  col3 text,
  col4 bool
)
distributed by (col1)
partition by list(col2)
(
    partition part1 values(1,2,3,4,5,6,7,8,9,10) WITH (appendonly=false )
);
create index distrib_part_test_idx on distrib_part_test(col1);
ALTER TABLE public.distrib_part_test SET with (reorganize=false) DISTRIBUTED RANDOMLY;
