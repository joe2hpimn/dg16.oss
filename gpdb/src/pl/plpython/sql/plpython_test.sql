-- first some tests of basic functionality
--
-- better succeed
--
select stupid();

-- check static and global data
--
SELECT static_test();
SELECT static_test();
SELECT global_test_one();
SELECT global_test_two();

-- import python modules
--
SELECT import_fail();
SELECT import_succeed();

-- test import and simple argument handling
--
SELECT import_test_one('sha hash of this string');

-- test import and tuple argument handling
--
select import_test_two(users) from users where fname = 'willem';

-- test multiple arguments
--
select argument_test_one(users, fname, lname) from users where lname = 'doe' order by 1;


-- spi and nested calls
--
select nested_call_one('pass this along');
select spi_prepared_plan_test_one('doe');
select spi_prepared_plan_test_one('smith');
select spi_prepared_plan_test_nested('smith');

-- quick peek at the table
--
SELECT * FROM users order by userid;

-- should fail
--
UPDATE users SET fname = 'william' WHERE fname = 'willem';

-- should modify william to willem and create username
--
INSERT INTO users (fname, lname) VALUES ('william', 'smith');
INSERT INTO users (fname, lname, username) VALUES ('charles', 'darwin', 'beagle');

SELECT * FROM users order by userid;

-- Greenplum doesn't support functions that execute SQL from segments
--
--  SELECT join_sequences(sequences) FROM sequences;
--  SELECT join_sequences(sequences) FROM sequences
--  	WHERE join_sequences(sequences) ~* '^A';
--  SELECT join_sequences(sequences) FROM sequences
--  	WHERE join_sequences(sequences) ~* '^B';

SELECT result_nrows_test();

-- test of exception handling
SELECT queryexec('SELECT 2'); 

SELECT queryexec('SELECT X'); 


select module_contents();

SELECT elog_test();


-- error in trigger
--

--
-- Check Universal Newline Support
--

SELECT newline_lf();
SELECT newline_cr();
SELECT newline_crlf();

-- Tests for functions returning void
SELECT test_void_func1(), test_void_func1() IS NULL AS "is null";
SELECT test_void_func2(); -- should fail
SELECT test_return_none(), test_return_none() IS NULL AS "is null";

-- Test for functions with named and nameless parameters
SELECT test_param_names0(2,7);
SELECT test_param_names1(1,'text');
SELECT test_param_names2(users) from users;
SELECT test_param_names3(1);

-- Test set returning functions
SELECT test_setof_as_list(0, 'list');
SELECT test_setof_as_list(1, 'list');
SELECT test_setof_as_list(2, 'list');
SELECT test_setof_as_list(2, null);

SELECT test_setof_as_tuple(0, 'tuple');
SELECT test_setof_as_tuple(1, 'tuple');
SELECT test_setof_as_tuple(2, 'tuple');
SELECT test_setof_as_tuple(2, null);

SELECT test_setof_as_iterator(0, 'list');
SELECT test_setof_as_iterator(1, 'list');
SELECT test_setof_as_iterator(2, 'list');
SELECT test_setof_as_iterator(2, null);

SELECT test_setof_spi_in_iterator();

-- Test tuple returning functions
SELECT * FROM test_table_record_as('dict', null, null, false);
SELECT * FROM test_table_record_as('dict', 'one', null, false);
SELECT * FROM test_table_record_as('dict', null, 2, false);
SELECT * FROM test_table_record_as('dict', 'three', 3, false);
SELECT * FROM test_table_record_as('dict', null, null, true);

SELECT * FROM test_table_record_as('tuple', null, null, false);
SELECT * FROM test_table_record_as('tuple', 'one', null, false);
SELECT * FROM test_table_record_as('tuple', null, 2, false);
SELECT * FROM test_table_record_as('tuple', 'three', 3, false);
SELECT * FROM test_table_record_as('tuple', null, null, true);

SELECT * FROM test_table_record_as('list', null, null, false);
SELECT * FROM test_table_record_as('list', 'one', null, false);
SELECT * FROM test_table_record_as('list', null, 2, false);
SELECT * FROM test_table_record_as('list', 'three', 3, false);
SELECT * FROM test_table_record_as('list', null, null, true);

SELECT * FROM test_table_record_as('obj', null, null, false);
SELECT * FROM test_table_record_as('obj', 'one', null, false);
SELECT * FROM test_table_record_as('obj', null, 2, false);
SELECT * FROM test_table_record_as('obj', 'three', 3, false);
SELECT * FROM test_table_record_as('obj', null, null, true);

SELECT * FROM test_type_record_as('dict', null, null, false);
SELECT * FROM test_type_record_as('dict', 'one', null, false);
SELECT * FROM test_type_record_as('dict', null, 2, false);
SELECT * FROM test_type_record_as('dict', 'three', 3, false);
SELECT * FROM test_type_record_as('dict', null, null, true);

SELECT * FROM test_type_record_as('tuple', null, null, false);
SELECT * FROM test_type_record_as('tuple', 'one', null, false);
SELECT * FROM test_type_record_as('tuple', null, 2, false);
SELECT * FROM test_type_record_as('tuple', 'three', 3, false);
SELECT * FROM test_type_record_as('tuple', null, null, true);

SELECT * FROM test_type_record_as('list', null, null, false);
SELECT * FROM test_type_record_as('list', 'one', null, false);
SELECT * FROM test_type_record_as('list', null, 2, false);
SELECT * FROM test_type_record_as('list', 'three', 3, false);
SELECT * FROM test_type_record_as('list', null, null, true);

SELECT * FROM test_type_record_as('obj', null, null, false);
SELECT * FROM test_type_record_as('obj', 'one', null, false);
SELECT * FROM test_type_record_as('obj', null, 2, false);
SELECT * FROM test_type_record_as('obj', 'three', 3, false);
SELECT * FROM test_type_record_as('obj', null, null, true);

SELECT * FROM test_in_out_params('test_in');
-- this doesn't work yet :-(
SELECT * FROM test_in_out_params_multi('test_in');
SELECT * FROM test_inout_params('test_in');

SELECT * FROM test_type_conversion_bool(true);
SELECT * FROM test_type_conversion_bool(false);
SELECT * FROM test_type_conversion_bool(null);
SELECT * FROM test_type_conversion_char('a');
SELECT * FROM test_type_conversion_char(null);
SELECT * FROM test_type_conversion_int2(100::int2);
SELECT * FROM test_type_conversion_int2(null);
SELECT * FROM test_type_conversion_int4(100);
SELECT * FROM test_type_conversion_int4(null);
SELECT * FROM test_type_conversion_int8(100);
SELECT * FROM test_type_conversion_int8(null);
SELECT * FROM test_type_conversion_float4(100);
SELECT * FROM test_type_conversion_float4(null);
SELECT * FROM test_type_conversion_float8(100);
SELECT * FROM test_type_conversion_float8(null);
SELECT * FROM test_type_conversion_numeric(100);
SELECT * FROM test_type_conversion_numeric(null);
SELECT * FROM test_type_conversion_text('hello world');
SELECT * FROM test_type_conversion_text(null);
SELECT * FROM test_type_conversion_bytea('hello world');
SELECT * FROM test_type_conversion_bytea(null);
SELECT test_type_unmarshal(x) FROM test_type_marshal() x;

SELECT (split(10)).*; 

SELECT * FROM split(100); 

select unnamed_tuple_test(); 

select named_tuple_test(); 

select oneline() 
union all 
select oneline2()
union all 
select multiline() 
union all 
select multiline2() 
union all 
select multiline3()
