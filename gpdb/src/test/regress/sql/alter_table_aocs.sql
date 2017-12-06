set optimizer_disable_missing_stats_collection = on;
--
-- This test case covers basic ADD COLUMN functionality for AOCS relations.
--

--
-- Switching on these gucs may be helpful in the event of failures.
--
-- set Debug_appendonly_print_storage_headers=true;
-- set Debug_appendonly_print_datumstream=true;
--

drop schema if exists aocs_addcol cascade;
create schema aocs_addcol;
set search_path=aocs_addcol,public;

create table addcol1 (a int) with (appendonly=true, orientation = column)
   distributed by (a);
-- create three varblocks
insert into addcol1 select i from generate_series(-10,5)i;
insert into addcol1 select i from generate_series(6,15)i;
insert into addcol1 select i from generate_series(21,30)i;
select count(*) from addcol1;

-- basic scenario with small content vablocks in new as well as existing column.
alter table addcol1
   add column b varchar default 'I am in a small content varblock';

-- verification on master catalog
-- TODO: How to run this on segments, through a TINC test?
-- Moreover, gp_toolkit schema is not populated in regression database
-- select segno,column_num,physical_segno,tupcount,modcount,state
--    from gp_toolkit.__gp_aocsseg(aocs_oid('addcol1')) order by segno,column_num;

-- select after alter
select b from addcol1 where a < 8 and a > 2;

-- update and delete post alter should work
update addcol1 set b = 'new value' where a < 10 and a > 0;
select * from addcol1 where a < 8 and a > 3;
delete from addcol1 where a > 25 or a < -5;
select count(*) from addcol1;
-- vacuum creates a new appendonly segment, leaving the original
-- segment active with eof=0.
vacuum addcol1;
-- alter table with one empty and one non-empty appendonly segment.
alter table addcol1 add column c float default 1.2;
select * from addcol1 where a < 8 and a > 3;

-- insert should result in two appendonly segments, each having eof > 0.
insert into addcol1
   select i, i::text, i*22/7::float
   from generate_series(31,40)i;
-- alter table with more than one non-empty appendonly segments.
alter table addcol1 add column d int default 20;
select a,c,d from addcol1 where a > 9 and a < 15 order by a;

-- try inserting after alter
insert into addcol1 select i, 'abc', 22*i/7, -i from generate_series(1,10)i;

-- add columns with compression (dense and bulk dense content varblocks)
alter table addcol1
   add column e float default 22/7::float encoding (compresstype=RLE_TYPE),
   add column f int default 20 encoding (compresstype=zlib);
select * from addcol1 where a < 2 and a > -4 order by a,c;
select a,f from addcol1 where a > 20 and a < 25 order by a,c;

-- add column with existing compressed column (dense content)
create table addcol2 (a int encoding (compresstype=zlib))
   with (appendonly=true, orientation=column)
   distributed by (a);
insert into addcol2 select i/17 from generate_series(-10000,10000)i;
insert into addcol2 select i from generate_series(10001, 50000)i;

alter table addcol2 add column b varchar
   default 'hope I end up on a magnetic disk some day'
   encoding (compresstype=RLE_TYPE, blocksize=8192);

-- select after add column
select * from addcol2 where a > 9995 and a < 10006 order by a;

-- add column with existing RLE compressed column (bulk dense content)
create table addcol3 (a int encoding (compresstype=RLE_TYPE, compresslevel=2))
   with (appendonly=true, orientation=column)
   distributed by (a);
insert into addcol3 select 10 from generate_series(1, 30000);
insert into addcol3 select -10 from generate_series(1, 20000);
insert into addcol3 select
   case when i < 100000 then 1
   	    when i >= 100000 and i < 500000 then 2
		when i >=500000 and i < 1000000 then 3
   end
   from generate_series(-1000,999999)i;

alter table addcol3 add column b float
   default 22/7::float encoding (compresstype=RLE_TYPE, compresslevel=2);

-- add column with null default
alter table addcol3 add column c varchar default null;

select count(b) from addcol3;
select count(c) from addcol3;

-- verification on master catalog
-- select segno,column_num,physical_segno,tupcount,modcount,state
--    from gp_toolkit.__gp_aocsseg(aocs_oid('addcol3')) order by segno,column_num;

-- insert after add column with null default
insert into addcol3 select i, 22*i/7, 'a non-null value'
   from generate_series(1,100)i;

select count(*) from addcol3;

-- verification on master catalog
-- select segno,column_num,physical_segno,tupcount,modcount,state
--    from gp_toolkit.__gp_aocsseg(aocs_oid('addcol3')) order by segno,column_num;

-- start with a new table, with two varblocks
create table addcol4 (a int, b float)
   with (appendonly=true, orientation=column)
   distributed by (a);
insert into addcol4 select i, 31/i from generate_series(1, 20)i;
insert into addcol4 select -i, 37/i from generate_series(1, 20)i;
select count(*) from addcol4;

-- multiple alter subcommands (add column, drop column)
alter table addcol4
   add column c varchar default null encoding (compresstype=zlib),
   drop column b,
   add column d date default date('2014-05-01')
      encoding (compresstype=RLE_TYPE, compresslevel=2);

select * from addcol4 where a > 5 and a < 10 order by a;

-- verification on master catalog
-- select segno, column_num, physical_segno, tupcount, modcount, state
--    from gp_toolkit.__gp_aocsseg(aocs_oid('addcol4')) order by segno,column_num;

-- TODO: multiple subcommands (add column, add constraint, alter type)

-- block directory
create index i4a on addcol4 (a);

alter table addcol4
   add column e varchar default 'wow' encoding (compresstype=zlib);

-- enforce index scan so that block directory is used
set enable_seqscan=off;

-- index scan after adding new column
select * from addcol4 where a > 5 and a < 10 order by a;

create table addcol5 (a int, b float)
   with (appendonly=true, orientation=column)
   distributed by (a);
create index i5a on addcol5(a);
insert into addcol5
   select i, 22*i/7 from generate_series(-10,10)i;
insert into addcol5
   select i, 22*i/7 from generate_series(11,20)i;
insert into addcol5
   select i, 22*i/7 from generate_series(21,30)i;

alter table addcol5 add column c int default 1;

-- insert after adding new column
insert into addcol5
   select i, 22*i/7, 311/i from generate_series(31,35)i;

-- index scan after adding new column
set enable_seqscan=off;
select * from addcol5 where a > 25 order by a,b;

-- firstRowNum of the first block starts with a value greater than 1
-- (first insert was aborted).

create table addcol6 (a int, b int)
   with (appendonly=true, orientation=column) distributed by (a);

begin;
insert into addcol6 select i,i from generate_series(1,10)i;
-- abort the first insert, so as to advance gp_fastsequence for this
-- relation.
abort;

insert into addcol6 select i,i/2 from generate_series(1,20)i;
alter table addcol6 add column c float default 1.2;
select a,c from addcol6 where b > 5 order by a;

-- add column with default value as sequence
alter table addcol6 add column d serial;

-- select, insert, update after 'add column'
select c,d from addcol6 where d > 15 order by d;
insert into addcol6 select i, i, 71/i from generate_series(21,30)i;
select count(*) from addcol6;
update addcol6 set b = 0, c = 0 where d > 15;
select count(*) from addcol6 where b = 0 and c = 0;

-- partitioned table tests
create table addcol7 (
   timest character varying(6),
   user_id numeric(16,0) not null,
   tag1 smallint,
   tag2 varchar(2))
   with (appendonly=true, orientation=column, compresslevel=5, oids=false)
   distributed by (user_id)
   partition by list(timest) (
      partition part201202 values('201202')
         with (appendonly=true, orientation=column, compresslevel=5),
      partition part201203 values('201203')
         with (appendonly=true, orientation=column, compresslevel=5));

insert into addcol7 select '201202', 100*i, i, 'a'
   from generate_series(1,10)i;
insert into addcol7 select '201203', 101*i, i, 'b'
   from generate_series(11,20)i;

alter table addcol7 add column new1 float default 1.2;

-- select, insert post alter
select * from addcol7 where tag1 > 7 and tag1 < 13 order by tag1;
insert into addcol7 select '201202', 100*i, i, i::text, 22*i/7
   from generate_series(21,30)i;
insert into addcol7 select '201203', 101*i, i, (i+2)::text, 22*i/7
   from generate_series(31,40)i;

-- add new partition and a new column in the same alter table command
alter table addcol7
   add partition part201204 values('201204')
      with (appendonly=true, compresslevel=5),
   add column new2 varchar default 'abc';

-- insert, select, update, delete and vacuum post alter
insert into addcol7 values
   ('201202', 101, 1, 'p1', 3/5::float, 'newcol2'),
   ('201202', 102, 2, 'p1', 1/6::float, 'newcol2'),
   ('201202', 103, 3, 'p1', 22/7::float, 'newcol2'),
   ('201203', 201, 4, 'p2', 1/3::float, 'newcol2'),
   ('201203', 202, 5, 'p2', null, null),
   ('201203', 203, 6, 'p2', null, null),
   ('201204', 301, 7, 'p3', 22/7::float, 'newcol2'),
   ('201204', 301, 8, 'p3', null, null),
   ('201204', 301, 9, 'p3', null, null);
select * from addcol7 where tag2 like 'p%' order by user_id, tag1; 
update addcol7 set new1 = 0, tag1 = -1 where tag2 like 'p%';
delete from addcol7 where new2 is null;
vacuum addcol7;
select * from addcol7 where tag2 like 'p%' order by user_id;

create table addcol8 (a int, b varchar(10), c int, d int)
   with (appendonly=true, orientation=column) distributed by (a);
insert into addcol8 select i, 'abc'||i, i, i from generate_series(1,10)i;
alter table addcol8
   alter column b type varchar(20),
   add column e float default 1,
   drop column c;
select * from addcol8 order by a;
\d addcol8

-- cleanup so as not to affect other installcheck tests
-- (e.g. column_compression).
drop schema aocs_addcol cascade;
