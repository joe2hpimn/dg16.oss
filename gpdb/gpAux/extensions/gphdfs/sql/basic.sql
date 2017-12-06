--
-- Create demoprot (for testing)
--
set optimizer_disable_missing_stats_collection = on;
CREATE OR REPLACE FUNCTION write_to_file() RETURNS integer as '$libdir/gpextprotocol.so', 'demoprot_export' LANGUAGE C STABLE NO SQL;
CREATE OR REPLACE FUNCTION read_from_file() RETURNS integer as '$libdir/gpextprotocol.so', 'demoprot_import' LANGUAGE C STABLE NO SQL;
DROP PROTOCOL IF EXISTS demoprot;
CREATE PROTOCOL demoprot (readfunc = 'read_from_file', writefunc = 'write_to_file'); -- should succeed

--
-- Cleanup script
--
create external web table cleanup(a int)
  execute 'rm -rf $GP_SEG_DATADIR/hdfsfile.*;' on all
  format 'text';

create external web table touchfile(a int)
  execute 'touch $GP_SEG_DATADIR/hdfsfile.out;' on all
  format 'text';

--
-- Test 0: String serialization in/out test
--
select * from cleanup;

drop table test1;
CREATE TABLE test1(
  v0 text,
  v1 time,
  v2 timestamp,
  v3 numeric
) 
WITH (appendonly = true) DISTRIBUTED BY (v0);

insert into test1 values(
  'aaaaddddcccc',
  '00:00:00',
  '2004-10-19 10:23:54',
  123.1231231241231242
);
insert into test1 values(
  lpad('aaaaddddcccc', 2000, 's'),
  null,
  '2004-10-19 10:23:54',
  null
);

insert into test1(v0) select relname from pg_class;

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 FORMAT 'CUSTOM' (formatter='gphdfs_export') distributed by (v0);

insert into example_out select * from test1;

drop external web table example_in;
CREATE EXTERNAL WEB TABLE example_in(like test1)
execute 'cat $GP_SEG_DATADIR/hdfsfile.out'
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

select * from example_in except select * from test1;
select * from test1 except select * from example_in;


--
-- Test 1: Complete type in/out test
--
select * from cleanup;

drop table test1;
CREATE TABLE test1(
  v0 bigint,
  v1 boolean,
  v2 bytea,
  v3 varchar,
  v4 char(5),
  v5 date,
  v6 float8,
  v7 integer,
  v8 numeric,
  v9 real,
  va smallint,
  vb text,
  vc time,
  vd timestamp
) 
WITH (appendonly = true) DISTRIBUTED BY (v0);

insert into test1 values(
  12345,
  true,
  E'\\001\\002\\003\\004\\005\\006\\007'::bytea,
  'abcdef',
  'xxxxx',
  '2001-10-05',
  123.1231231241231242,
  556677,
  9.99999999,
  1123.123,
  444,
  'aaaaddddcccc',
  '00:00:00',
  '2004-10-19 10:23:54'
);

insert into test1(v0) select oid from pg_class;

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 format 'custom' (formatter='gphdfs_export') distributed by (v0);

insert into example_out select * from test1;

drop external web table example_in;
CREATE  EXTERNAL WEB TABLE example_in(like test1)
execute 'cat $GP_SEG_DATADIR/hdfsfile.out'
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

select * from example_in except select * from test1;
select * from test1 except select * from example_in;

--
-- Test 2: pg_class dump/restore test
--
select * from cleanup;

drop external table hdfsformatter_out;
CREATE WRITABLE EXTERNAL TABLE hdfsformatter_out(like pg_class)
location('demoprot://hdfsfile.out')
 FORMAT 'CUSTOM' (formatter='gphdfs_export');

drop external table hdfsformatter_in;
CREATE EXTERNAL TABLE hdfsformatter_in(like pg_class)
location('demoprot://hdfsfile.out')
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

drop table tmp_in;
create table tmp_in(like pg_class);

drop table tmp_pg_class;
create table tmp_pg_class(like pg_class);
insert into tmp_pg_class select * from pg_class;

insert into hdfsformatter_out select * from tmp_pg_class;
insert into tmp_in select * from hdfsformatter_in;

select relname, relnamespace, reltype, relowner, relam, relfilenode, reltablespace, reltoastrelid, reltoastidxid, relaosegrelid, relaosegidxid,
 relhasindex, relisshared, relkind, relstorage, relnatts, relchecks, reltriggers,
 relukeys, relfkeys, relrefs, relhasoids, relhaspkey, relhasrules, relhassubclass, reloptions
from tmp_in
except
select relname, relnamespace, reltype, relowner, relam, relfilenode, reltablespace, reltoastrelid, reltoastidxid, relaosegrelid, relaosegidxid,
 relhasindex, relisshared, relkind, relstorage, relnatts, relchecks, reltriggers,
 relukeys, relfkeys, relrefs, relhasoids, relhaspkey, relhasrules, relhassubclass, reloptions
from tmp_pg_class;

select relname, relnamespace, reltype, relowner, relam, relfilenode, reltablespace,
 relpages, reltoastrelid, reltoastidxid, relaosegrelid, relaosegidxid,
 relhasindex, relisshared, relkind, relstorage, relnatts, relchecks, reltriggers,
 relukeys, relfkeys, relrefs, relhasoids, relhaspkey, relhasrules, relhassubclass, reloptions
from tmp_pg_class
except
select relname, relnamespace, reltype, relowner, relam, relfilenode, reltablespace,
 relpages, reltoastrelid, reltoastidxid, relaosegrelid, relaosegidxid,
 relhasindex, relisshared, relkind, relstorage, relnatts, relchecks, reltriggers,
 relukeys, relfkeys, relrefs, relhasoids, relhaspkey, relhasrules, relhassubclass, reloptions
from tmp_in;

drop external table hdfsformatter_out;
drop external table hdfsformatter_in;
drop table tmp_in;
drop table tmp_pg_class;

--
-- Test 3: Negative test (missing byte at the end)
--
select * from cleanup;

drop table test1;
CREATE TABLE test1(
  v0 bigint,
  v1 boolean,
  v2 bytea,
  v3 varchar,
  v4 char(5),
  v5 date,
  v6 float8,
  v7 integer,
  v8 numeric,
  v9 real,
  va smallint,
  vb text,
  vc time,
  vd timestamp
) 
WITH (appendonly = true) DISTRIBUTED BY (v0);

insert into test1 values(
  12345,
  true,
  E'\\001\\002\\003\\004\\005\\006\\007'::bytea,
  'abcdef',
  'xxxxx',
  '2001-10-05',
  123.1231231241231242,
  556677,
  9.99999999,
  1123.123,
  444,
  'aaaaddddcccc',
  '00:00:00',
  '2004-10-19 10:23:54'
);

insert into test1(v0) select oid from pg_class;

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 format 'custom' (formatter='gphdfs_export') distributed by (v0);

insert into example_out select * from test1;

-- corrupt the file here
create external web table ewt1(a int)
  execute 'dd if=$GP_SEG_DATADIR/hdfsfile.out of=$GP_SEG_DATADIR/hdfsfile.corrupt bs=1 count=1' on all
  format 'text';
select * from ewt1;
drop external web table ewt1;

drop external web table example_in;
CREATE  EXTERNAL WEB TABLE example_in(like test1)
execute 'cat $GP_SEG_DATADIR/hdfsfile.corrupt'
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

select * from example_in except select * from test1;



--
-- Test 4: Negative test (type mismatch)
--
select * from cleanup;

drop table test1;
CREATE TABLE test1(
  v0 bigint
) 
WITH (appendonly = true) DISTRIBUTED BY (v0);

insert into test1 values(
  12345
);
insert into test1(v0) select oid from pg_class;

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 format 'custom' (formatter='gphdfs_export') distributed by (v0);

insert into example_out select * from test1;

drop external web table example_in;
CREATE  EXTERNAL WEB TABLE example_in(a boolean)
execute 'cat $GP_SEG_DATADIR/hdfsfile.out'
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

select * from example_in;

drop external web table example_in;
CREATE  EXTERNAL WEB TABLE example_in(a1 int, a boolean)
execute 'cat $GP_SEG_DATADIR/hdfsfile.out'
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

select * from example_in;


--
-- Test 5: gphdfs protocol (just declare an external table)
--
drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like pg_class)
location ('gphdfs://localhost:9000/gpdb_out')
 FORMAT 'CUSTOM' (formatter='gphdfs_export');

drop external table example_in;
CREATE EXTERNAL TABLE example_in(like pg_class)
location ('gphdfs://localhost:9000/gpdb_out/*')
 FORMAT 'CUSTOM' (formatter='gphdfs_import');

--
-- Test 6: formatter SREH
--
select * from cleanup;
select * from touchfile;

drop table test1;
CREATE TABLE test1(
  v0 bigint,
  v1 bigint
) 
WITH (appendonly = true) DISTRIBUTED BY (v0);
insert into test1 values(12345, 123444);

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 format 'custom' (formatter='gphdfs_export') distributed by (v0);

insert into example_out select * from test1;

drop table test1;
CREATE TABLE test1(
  v0 bigint
) 
WITH (appendonly = true) DISTRIBUTED BY (v0);
insert into test1 values(12345);

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 format 'custom' (formatter='gphdfs_export') distributed by (v0);

insert into example_out select * from test1;

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(a boolean)
 location('demoprot://hdfsfile.out')
 FORMAT 'CUSTOM' (formatter='gphdfs_export')
 distributed by (a);

insert into example_out values(true);
insert into example_out values(false);

drop external table example_out;
CREATE WRITABLE EXTERNAL TABLE example_out(like test1)
 location('demoprot://hdfsfile.out')
 format 'TEXT' distributed by (v0);

insert into example_out select * from test1;

drop external web table example_in;
CREATE  EXTERNAL WEB TABLE example_in(a boolean)
execute 'cat $GP_SEG_DATADIR/hdfsfile.out'
 FORMAT 'CUSTOM' (formatter='gphdfs_import')
 log errors into errtbl SEGMENT REJECT LIMIT 1000 rows;

select * from example_in order by 1;

select relname, filename, linenum, bytenum, errmsg, rawdata, rawbytes from errtbl order by 1,2,3,4,5,6,7;
--
-- End of Test (drop stuff)
--
select * from cleanup; 
