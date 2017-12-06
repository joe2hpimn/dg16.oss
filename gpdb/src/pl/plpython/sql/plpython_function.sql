

CREATE FUNCTION global_test_one() returns text
    AS
'if not SD.has_key("global_test"):
	SD["global_test"] = "set by global_test_one"
if not GD.has_key("global_test"):
	GD["global_test"] = "set by global_test_one"
return "SD: " + SD["global_test"] + ", GD: " + GD["global_test"]'
    LANGUAGE plpythonu;

CREATE FUNCTION global_test_two() returns text
    AS
'if not SD.has_key("global_test"):
	SD["global_test"] = "set by global_test_two"
if not GD.has_key("global_test"):
	GD["global_test"] = "set by global_test_two"
return "SD: " + SD["global_test"] + ", GD: " + GD["global_test"]'
    LANGUAGE plpythonu;


CREATE FUNCTION static_test() returns int4
    AS
'if SD.has_key("call"):
	SD["call"] = SD["call"] + 1
else:
	SD["call"] = 1
return SD["call"]
'
    LANGUAGE plpythonu;

-- import python modules

CREATE FUNCTION import_fail() returns text
    AS
'try:
	import foosocket
except Exception, ex:
	plpy.notice("import socket failed -- %s" % str(ex))
	return "failed as expected"
return "succeeded, that wasn''t supposed to happen"'
    LANGUAGE plpythonu;


CREATE FUNCTION import_succeed() returns text
	AS
'try:
  import array
  import bisect
  import calendar
  import cmath
  import errno
  import math
  import md5
  import operator
  import random
  import re
  import sha
  import string
  import time
except Exception, ex:
	plpy.notice("import failed -- %s" % str(ex))
	return "failed, that wasn''t supposed to happen"
return "succeeded, as expected"'
    LANGUAGE plpythonu;

CREATE FUNCTION import_test_one(p text) RETURNS text
	AS
'import sha
digest = sha.new(p)
return digest.hexdigest()'
	LANGUAGE plpythonu;

CREATE FUNCTION import_test_two(u users) RETURNS text
	AS
'import sha
plain = u["fname"] + u["lname"]
digest = sha.new(plain);
return "sha hash of " + plain + " is " + digest.hexdigest()'
	LANGUAGE plpythonu;

CREATE FUNCTION argument_test_one(u users, a1 text, a2 text) RETURNS text
	AS
'keys = u.keys()
keys.sort()
out = []
for key in keys:
    out.append("%s: %s" % (key, u[key]))
words = a1 + " " + a2 + " => {" + ", ".join(out) + "}"
return words'
	LANGUAGE plpythonu;


-- check module contents

CREATE FUNCTION module_contents() RETURNS text AS
$$
contents = list(filter(lambda x: not x.startswith("__"), dir(plpy)))
contents.sort()
return ", ".join(contents)
$$ LANGUAGE plpythonu;


CREATE FUNCTION elog_test() RETURNS void
AS $$
plpy.debug('debug')
plpy.log('log')
plpy.info('info')
plpy.info(37)
plpy.info()
plpy.info('info', 37, [1, 2, 3]) 
plpy.notice('notice')
plpy.warning('warning')
plpy.error('error')
$$ LANGUAGE plpythonu;

-- these triggers are dedicated to HPHC of RI who
-- decided that my kid's name was william not willem, and
-- vigorously resisted all efforts at correction.  they have
-- since gone bankrupt...

CREATE FUNCTION users_insert() returns trigger
	AS
'if TD["new"]["fname"] == None or TD["new"]["lname"] == None:
	return "SKIP"
if TD["new"]["username"] == None:
	TD["new"]["username"] = TD["new"]["fname"][:1] + "_" + TD["new"]["lname"]
	rv = "MODIFY"
else:
	rv = None
if TD["new"]["fname"] == "william":
	TD["new"]["fname"] = TD["args"][0]
	rv = "MODIFY"
return rv'
	LANGUAGE plpythonu;


CREATE FUNCTION users_update() returns trigger
	AS
'if TD["event"] == "UPDATE":
	if TD["old"]["fname"] != TD["new"]["fname"] and TD["old"]["fname"] == TD["args"][0]:
		return "SKIP"
return None'
	LANGUAGE plpythonu;


CREATE FUNCTION users_delete() RETURNS trigger
	AS
'if TD["old"]["fname"] == TD["args"][0]:
	return "SKIP"
return None'
	LANGUAGE plpythonu;


CREATE TRIGGER users_insert_trig BEFORE INSERT ON users FOR EACH ROW
	EXECUTE PROCEDURE users_insert ('willem');

CREATE TRIGGER users_update_trig BEFORE UPDATE ON users FOR EACH ROW
	EXECUTE PROCEDURE users_update ('willem');

CREATE TRIGGER users_delete_trig BEFORE DELETE ON users FOR EACH ROW
	EXECUTE PROCEDURE users_delete ('willem');



-- dump trigger data
DROP TABLE IF EXISTS trigger_test; 

CREATE TABLE trigger_test
	(i int, v text );

CREATE FUNCTION trigger_data() returns trigger language plpythonu as $$

if TD.has_key('relid'):
	TD['relid'] = "bogus:12345"

skeys = TD.keys()
skeys.sort()
for key in skeys:
	val = TD[key]
	plpy.notice("TD[" + key + "] => " + str(val))

return None  

$$;

CREATE TRIGGER show_trigger_data_trig 
BEFORE INSERT OR UPDATE OR DELETE ON trigger_test
FOR EACH ROW EXECUTE PROCEDURE trigger_data(23,'skidoo');

insert into trigger_test values(1,'insert');
update trigger_test set v = 'update' where i = 1;
delete from trigger_test;
      
DROP TRIGGER show_trigger_data_trig on trigger_test;
      
DROP FUNCTION trigger_data();

-- nested calls
--

CREATE FUNCTION nested_call_one(a text) RETURNS text
	AS
'q = "SELECT nested_call_two(''%s'')" % a
r = plpy.execute(q)
return r[0]'
	LANGUAGE plpythonu ;

CREATE FUNCTION nested_call_two(a text) RETURNS text
	AS
'q = "SELECT nested_call_three(''%s'')" % a
r = plpy.execute(q)
return r[0]'
	LANGUAGE plpythonu ;

CREATE FUNCTION nested_call_three(a text) RETURNS text
	AS
'return a'
	LANGUAGE plpythonu ;

-- some spi stuff

CREATE FUNCTION spi_prepared_plan_test_one(a text) RETURNS text
	AS
'if not SD.has_key("myplan"):
	q = "SELECT count(*) FROM users WHERE lname = $1"
	SD["myplan"] = plpy.prepare(q, [ "text" ])
try:
	rv = plpy.execute(SD["myplan"], [a])
	return "there are " + str(rv[0]["count"]) + " " + str(a) + "s"
except Exception, ex:
	plpy.error(str(ex))
return None
'
	LANGUAGE plpythonu;

CREATE FUNCTION spi_prepared_plan_test_nested(a text) RETURNS text
	AS
'if not SD.has_key("myplan"):
	q = "SELECT spi_prepared_plan_test_one(''%s'') as count" % a
	SD["myplan"] = plpy.prepare(q)
try:
	rv = plpy.execute(SD["myplan"])
	if len(rv):
		return rv[0]["count"]
except Exception, ex:
	plpy.error(str(ex))
return None
'
	LANGUAGE plpythonu;


/* really stupid function just to get the module loaded
*/
CREATE FUNCTION stupid() RETURNS text AS 'return "zarkon"' LANGUAGE plpythonu;

/* a typo
*/
CREATE FUNCTION invalid_type_uncaught(a text) RETURNS text
	AS
'if not SD.has_key("plan"):
	q = "SELECT fname FROM users WHERE lname = $1"
	SD["plan"] = plpy.prepare(q, [ "test" ])
rv = plpy.execute(SD["plan"], [ a ])
if len(rv):
	return rv[0]["fname"]
return None
'
	LANGUAGE plpythonu;

/* for what it's worth catch the exception generated by
 * the typo, and return None
 */
CREATE FUNCTION invalid_type_caught(a text) RETURNS text
	AS
'if not SD.has_key("plan"):
	q = "SELECT fname FROM users WHERE lname = $1"
	try:
		SD["plan"] = plpy.prepare(q, [ "test" ])
	except plpy.SPIError, ex:
		plpy.notice(str(ex))
		return None
rv = plpy.execute(SD["plan"], [ a ])
if len(rv):
	return rv[0]["fname"]
return None
'
	LANGUAGE plpythonu;

/* for what it's worth catch the exception generated by
 * the typo, and reraise it as a plain error
 */
CREATE FUNCTION invalid_type_reraised(a text) RETURNS text
	AS
'if not SD.has_key("plan"):
	q = "SELECT fname FROM users WHERE lname = $1"
	try:
		SD["plan"] = plpy.prepare(q, [ "test" ])
	except plpy.SPIError, ex:
		plpy.error(str(ex))
rv = plpy.execute(SD["plan"], [ a ])
if len(rv):
	return rv[0]["fname"]
return None
'
	LANGUAGE plpythonu;


/* no typo no messing about
*/
CREATE FUNCTION valid_type(a text) RETURNS text
	AS
'if not SD.has_key("plan"):
	SD["plan"] = plpy.prepare("SELECT fname FROM users WHERE lname = $1", [ "text" ])
rv = plpy.execute(SD["plan"], [ a ])
if len(rv):
	return rv[0]["fname"]
return None
'
	LANGUAGE plpythonu;
/* Flat out syntax error
*/
CREATE FUNCTION sql_syntax_error() RETURNS text
        AS
'plpy.execute("syntax error")'
        LANGUAGE plpythonu;

/* check the handling of uncaught python exceptions
 */
CREATE FUNCTION exception_index_invalid(text) RETURNS text
	AS
'return args[1]'
	LANGUAGE plpythonu;

/* check handling of nested exceptions
 */
CREATE FUNCTION exception_index_invalid_nested() RETURNS text
	AS
'rv = plpy.execute("SELECT test5(''foo'')")
return rv[0]'
	LANGUAGE plpythonu;


CREATE FUNCTION join_sequences(s sequences) RETURNS text
	AS
'if not s["multipart"]:
	return s["sequence"]
q = "SELECT sequence FROM xsequences WHERE pid = ''%s''" % s["pid"]
rv = plpy.execute(q)
seq = s["sequence"]
for r in rv:
	seq = seq + r["sequence"]
return seq
'
	LANGUAGE plpythonu;


--
-- plan and result objects
--

CREATE FUNCTION result_nrows_test() RETURNS int 
AS $$
plan = plpy.prepare("SELECT 1 UNION SELECT 2")
plpy.info(plan.status()) # not really documented or useful
result = plpy.execute(plan)
if result.status() > 0:
   return result.nrows()
else:
   return None
$$ LANGUAGE plpythonu;

--
-- Universal Newline Support
-- 

CREATE OR REPLACE FUNCTION newline_lf() RETURNS integer AS
E'x = 100\ny = 23\nreturn x + y\n'
LANGUAGE plpythonu;

CREATE OR REPLACE FUNCTION newline_cr() RETURNS integer AS
E'x = 100\ry = 23\rreturn x + y\r'
LANGUAGE plpythonu;

CREATE OR REPLACE FUNCTION newline_crlf() RETURNS integer AS
E'x = 100\r\ny = 23\r\nreturn x + y\r\n'
LANGUAGE plpythonu;

--
-- Unicode error handling
--

CREATE FUNCTION unicode_return_error() RETURNS text AS E'
return u"\\x80"
' LANGUAGE plpythonu;

CREATE FUNCTION unicode_trigger_error() RETURNS trigger AS E'
TD["new"]["testvalue"] = u"\\x80"
return "MODIFY"
' LANGUAGE plpythonu;

CREATE TRIGGER unicode_test_bi BEFORE INSERT ON unicode_test
  FOR EACH ROW EXECUTE PROCEDURE unicode_trigger_error();

CREATE FUNCTION unicode_plan_error1() RETURNS text AS E'
plan = plpy.prepare("SELECT $1 AS testvalue", ["text"])
rv = plpy.execute(plan, [u"\\x80"], 1)
return rv[0]["testvalue"]
' LANGUAGE plpythonu;

CREATE FUNCTION unicode_plan_error2() RETURNS text AS E'
plan = plpy.prepare("SELECT $1 AS testvalue1, $2 AS testvalue2", ["text", "text"])
rv = plpy.execute(plan, u"\\x80", 1)
return rv[0]["testvalue1"]
' LANGUAGE plpythonu;

-- Tests for functions that return void

CREATE FUNCTION test_void_func1() RETURNS void AS $$
x = 10
$$ LANGUAGE plpythonu;

-- illegal: can't return non-None value in void-returning func
CREATE FUNCTION test_void_func2() RETURNS void AS $$
return 10
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_return_none() RETURNS int AS $$
None
$$ LANGUAGE plpythonu;


--
-- Test named and nameless parameters
--
CREATE FUNCTION test_param_names0(integer, integer) RETURNS int AS $$
return args[0] + args[1]
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_param_names1(a0 integer, a1 text) RETURNS boolean AS $$
assert a0 == args[0]
assert a1 == args[1]
return True
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_param_names2(u users) RETURNS text AS $$
assert u == args[0]
return str(u)
$$ LANGUAGE plpythonu;

-- use deliberately wrong parameter names
CREATE FUNCTION test_param_names3(a0 integer) RETURNS boolean AS $$
try:
	assert a1 == args[0]
	return False
except NameError, e:
	assert e.args[0].find("a1") > -1
	return True
$$ LANGUAGE plpythonu;


--
-- Test returning SETOF
--
CREATE FUNCTION test_setof_as_list(count integer, content text) RETURNS SETOF text AS $$
return [ content ]*count
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_setof_as_tuple(count integer, content text) RETURNS SETOF text AS $$
t = ()
for i in xrange(count):
	t += ( content, )
return t
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_setof_as_iterator(count integer, content text) RETURNS SETOF text AS $$
class producer:
	def __init__ (self, icount, icontent):
		self.icontent = icontent
		self.icount = icount
	def __iter__ (self):
		return self
	def next (self):
		if self.icount == 0:
			raise StopIteration
		self.icount -= 1
		return self.icontent
return producer(count, content)
$$ LANGUAGE plpythonu;


CREATE FUNCTION test_setof_spi_in_iterator() RETURNS SETOF text AS
$$
    for s in ('Hello', 'Brave', 'New', 'World'):
        plpy.execute('select 1')
        yield s
        plpy.execute('select 2')
$$
LANGUAGE plpythonu;


-- test of exception handling 

CREATE OR REPLACE FUNCTION queryexec( query text )
        returns boolean
AS $$
try:
  plan = plpy.prepare( query )
  rv = plpy.execute( plan )
except:
  plpy.notice( 'Error Trapped' )
  return False

for r in rv:
  plpy.notice( str( r ) )

return True
$$ LANGUAGE plpythonu;

--
-- Test returning tuples
--
CREATE FUNCTION test_table_record_as(typ text, first text, second integer, retnull boolean) RETURNS table_record AS $$
if retnull:
	return None
if typ == 'dict':
	return { 'first': first, 'second': second, 'additionalfield': 'must not cause trouble' }
elif typ == 'tuple':
	return ( first, second )
elif typ == 'list':
	return [ first, second ]
elif typ == 'obj':
	class type_record: pass
	type_record.first = first
	type_record.second = second
	return type_record
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_type_record_as(typ text, first text, second integer, retnull boolean) RETURNS type_record AS $$
if retnull:
	return None
if typ == 'dict':
	return { 'first': first, 'second': second, 'additionalfield': 'must not cause trouble' }
elif typ == 'tuple':
	return ( first, second )
elif typ == 'list':
	return [ first, second ]
elif typ == 'obj':
	class type_record: pass
	type_record.first = first
	type_record.second = second
	return type_record
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_in_out_params(first in text, second out text) AS $$
return first + '_in_to_out';
$$ LANGUAGE plpythonu;

-- this doesn't work yet :-(
CREATE FUNCTION test_in_out_params_multi(first in text,
                                         second out text, third out text) AS $$
return first + '_record_in_to_out';
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_inout_params(first inout text) AS $$
return first + '_inout';
$$ LANGUAGE plpythonu;

CREATE FUNCTION test_type_conversion_bool(x bool) returns bool AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_char(x char) returns char AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_int2(x int2) returns int2 AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_int4(x int4) returns int4 AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_int8(x int8) returns int8 AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_float4(x float4) returns float4 AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_float8(x float8) returns float8 AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_numeric(x numeric) returns numeric AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_text(x text) returns text AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_conversion_bytea(x bytea) returns bytea AS $$ return x $$ language plpythonu;
CREATE FUNCTION test_type_marshal() returns bytea AS $$ 
import marshal
return marshal.dumps('hello world')
$$ language plpythonu;
CREATE FUNCTION test_type_unmarshal(x bytea) returns text AS $$
import marshal
return marshal.loads(x)
$$ language plpythonu;

-- RETURN value testing
CREATE FUNCTION test_return_void(s text) 
RETURNS void AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_bool(s text) 
RETURNS bool AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_text(s text) 
RETURNS text AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_bytea(s text) 
RETURNS bytea AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_circle(s text) 
RETURNS circle AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_void(s text) 
RETURNS setof void AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_bool(s text) 
RETURNS setof bool AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_text(s text) 
RETURNS setof text AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_bytea(s text) 
RETURNS setof bytea AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_circle(s text) 
RETURNS setof circle AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;

-- RETURNS record types
CREATE FUNCTION test_return_table_record(s text) 
RETURNS table_record AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_type_record(s text) 
RETURNS type_record AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_table_record(s text) 
RETURNS table_record AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_setof_type_record(s text) 
RETURNS type_record AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;

-- RETURNS with OUT Parameters
CREATE FUNCTION test_return_out_text(s text, OUT text) 
AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_out_setof_text(s text, OUT text) 
RETURNS setof text AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_out_record(s text, OUT first text, OUT second int4) 
AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;
CREATE FUNCTION test_return_out_setof_record(s text, OUT first text, OUT second int4)
RETURNS setof record AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;

-- RETURNS TABLE
CREATE FUNCTION test_return_table(s text) RETURNS TABLE(first text, second int4)
AS $$
  exec('y = ' + s)
  return y
$$ language plpythonu;

CREATE OR REPLACE FUNCTION unnamed_tuple_test() RETURNS VOID LANGUAGE plpythonu AS $$
plpy.execute("SHOW client_min_messages")
$$;

CREATE FUNCTION named_tuple_test() RETURNS VARCHAR LANGUAGE plpythonu AS $$
return plpy.execute("SELECT setting FROM pg_settings WHERE name='client_min_messages'")[0]['setting']
$$;

create or replace function split(input int) returns setof ab_tuple as
$$
	plpy.log("Returning the FIRST tuple")
	yield [input, input + 1]
	plpy.log("Returning the SECOND tuple"); 
	yield [input+2, input + 3]
$$
language plpythonu; 

CREATE OR REPLACE FUNCTION oneline() returns text as $$ 
return 'No spaces' 
$$ language plpythonu;

CREATE OR REPLACE FUNCTION oneline2() returns text as $$  
x = "\""
y = ''
z = ""
w = '\'' + 'a string with # and "" inside ' + "another string with #  and '' inside "
return x + y + z + w
$$ language plpythonu;

CREATE OR REPLACE FUNCTION multiline() returns text as $$ 
return """ One space
  Two spaces
   Three spaces
No spaces""" 
$$ language plpythonu;

CREATE OR REPLACE FUNCTION multiline2() returns text as $$
# If there's something in my comment it can mess things up
return '''
The ' in the comment should not cause this line to begin with a tab
''' + 'This is a rather long string containing\n\
    several lines of text just as you would do in C.\n\
     Note that whitespace at the beginning of the line is\
significant. The string can contain both \' and ".\n' + r"This is an another long string containing\n\
two lines of text and defined with the r\"...\" syntax."
$$ language plpythonu; 

CREATE OR REPLACE FUNCTION multiline3() returns text as $$  
# This is a comment
x = """ 
  # This is not a comment so the quotes at the end of the line do end the string """ 
return x
$$ language plpythonu;

