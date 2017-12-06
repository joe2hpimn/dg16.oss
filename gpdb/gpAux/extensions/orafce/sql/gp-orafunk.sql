-- --------------------------------------
-- bring in da noise, bring in da orafunk
-- --------------------------------------

-- *********************************************************************
-- *********************************************************************
-- This script will produce diffs if you add or change definitions in
-- the contrib/orafce module. If you want to change the results, you
-- must make the changes in regress/output/orafunk.source, not
-- regress/expected, and use gpsourcify.pl to generate a ".source" file.
--
-- From the regress directory invoke the command:
--
--    gpsourcify.pl results/orafunk.out > output/orafunk.source
--
-- *********************************************************************
-- *********************************************************************
-- *********************************************************************
-- *********************************************************************

-- start_ignore
\i orafunc.sql
-- end_ignore

set search_path = oracompat;
show search_path;

SET DATESTYLE TO ISO;

select nvl('A'::text, 'B');
select nvl(NULL::text, 'B');
select nvl(NULL::text, NULL);
select nvl(1, 2);
select nvl(NULL, 2);

SELECT add_months ('2003-08-01', 3);
SELECT add_months ('2003-08-01', -3);
SELECT add_months ('2003-08-21', -3);
SELECT add_months ('2003-01-31', 1);
SELECT add_months ('2008-02-28', 1);
SELECT add_months ('2008-02-29', 1);
SELECT add_months ('2008-01-31', 12);
SELECT add_months ('2008-01-31', -12);
SELECT add_months ('2008-01-31', 95903);
SELECT add_months ('2008-01-31', -80640);

SELECT last_day(to_date('2003/03/15', 'yyyy/mm/dd'));
SELECT last_day(to_date('2003/02/03', 'yyyy/mm/dd'));
SELECT last_day(to_date('2004/02/03', 'yyyy/mm/dd'));
SELECT last_day('1900-02-01');
SELECT last_day('2000-02-01');
SELECT last_day('2007-02-01');
SELECT last_day('2008-02-01');

SELECT next_day ('2003-08-01', 'TUESDAY');
SELECT next_day ('2003-08-06', 'WEDNESDAY');
SELECT next_day ('2003-08-06', 'SUNDAY');
SELECT next_day ('2008-01-01', 'sun');
SELECT next_day ('2008-01-01', 'sunAAA');
SELECT next_day ('2008-01-01', 1);
SELECT next_day ('2008-01-01', 7);

SELECT months_between (to_date ('2003/01/01', 'yyyy/mm/dd'),
					   to_date ('2003/03/14 ', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/07/01', 'yyyy/mm/dd'),
					   to_date ('2003/03/14', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/07/02', 'yyyy/mm/dd'),
					   to_date ('2003/07/02', 'yyyy/mm/dd'));
SELECT months_between (to_date ('2003/08/02', 'yyyy/mm/dd'),
					   to_date ('2003/06/02', 'yyyy/mm/dd'));
SELECT months_between ('2007-02-28', '2007-04-30');
SELECT months_between ('2008-01-31', '2008-02-29');
SELECT months_between ('2008-02-29', '2008-03-31');
SELECT months_between ('2008-02-29', '2008-04-30');

SELECT trunc(months_between('21-feb-2008', '2008-02-29'));


select round(to_date ('22-AUG-03', 'DD-MON-YY'),'YEAR')  =  to_date ('01-JAN-04', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'Q')  =  to_date ('01-OCT-03', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'MONTH') =  to_date ('01-SEP-03', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'DDD')  =  to_date ('22-AUG-03', 'DD-MON-YY');
select round(to_date ('22-AUG-03', 'DD-MON-YY'),'DAY')  =  to_date ('24-AUG-03', 'DD-MON-YY');
select round(timestamp with time zone '2012-03-14 17:36:33.366057-07', 'HH') = (timestamp with time zone '2012-03-14 18:00:00-07');
select round(timestamp with time zone '2012-03-14 17:36:33.366057-07', 'Mi') = (timestamp with time zone '2012-03-14 17:37:00-07');
select round(timestamp with time zone '2012-03-14 17:36:33.366057-07', 'Q') = (timestamp with time zone '2012-04-01 00:00:00-07');
select round(timestamp with time zone '2012-03-14 17:36:33.366057-07')::date = (date '2012-03-15');
select round(timestamp with time zone '2012-03-14 17:36:33.366057-07'::date) = (date '2012-03-14');
select round(timestamp with time zone '2012-03-14 17:36:33.366057-07'::date, 'Q') = (date '2012-04-01');

select trunc(to_date ('22-AUG-03', 'DD-MON-YY'), 'YEAR')  =  to_date ('01-JAN-03', 'DD-MON-YY');
select trunc(to_date ('22-AUG-03', 'DD-MON-YY'), 'Q')  =  to_date ('01-JUL-03', 'DD-MON-YY');
select trunc(to_date ('22-AUG-03', 'DD-MON-YY'), 'MONTH') =  to_date ('01-AUG-03', 'DD-MON-YY');
select trunc(to_date ('22-AUG-03', 'DD-MON-YY'), 'DDD')  =  to_date ('22-AUG-03', 'DD-MON-YY');
select trunc(to_date ('22-AUG-03', 'DD-MON-YY'), 'DAY')  =  to_date ('17-AUG-03', 'DD-MON-YY');
select trunc(timestamp with time zone '2012-03-14 17:36:33.366057-07', 'HH') = (timestamp with time zone '2012-03-14 17:00:00-07');
select trunc(timestamp with time zone '2012-03-14 17:36:33.366057-07', 'Mi') = (timestamp with time zone '2012-03-14 17:36:00-07');
select trunc(timestamp with time zone '2012-03-14 17:36:33.366057-07', 'Q') = (timestamp with time zone '2012-01-01 00:00:00-08');
select trunc(timestamp with time zone '2012-03-14 17:36:33.366057-07') = (timestamp with time zone '2012-03-14 00:00:00-07');
select trunc(timestamp with time zone '2012-03-14 17:36:33.366057-07'::date, 'Q') = (date '2012-01-01');
select trunc(timestamp with time zone '2012-03-14 17:36:33.366057-07'::date) = (date '2012-03-14');

select next_day(to_date('01-Aug-03', 'DD-MON-YY'), 'TUESDAY')  =  to_date ('05-Aug-03', 'DD-MON-YY');
select next_day(to_date('06-Aug-03', 'DD-MON-YY'), 'WEDNESDAY') =  to_date ('13-Aug-03', 'DD-MON-YY');
select next_day(to_date('06-Aug-03', 'DD-MON-YY'), 'SUNDAY')  =  to_date ('10-Aug-03', 'DD-MON-YY');
select instr('Tech on the net', 'e') =2;
select instr('Tech on the net', 'e', 1, 1) = 2;
select instr('Tech on the net', 'e', 1, 2) = 11;
select instr('Tech on the net', 'e', 1, 3) = 14;
select instr('Tech on the net', 'e', -3, 2) = 2;
select instr('abc', NULL) IS NULL;
select 1 = instr('abc', '');
select 1 = instr('abc', 'a');
select 3 = instr('abc', 'c');
select 0 = instr('abc', 'z');
select 1 = instr('abcabcabc', 'abca', 1);
select 4 = instr('abcabcabc', 'abca', 2);
select 0 = instr('abcabcabc', 'abca', 7);
select 0 = instr('abcabcabc', 'abca', 9);
select 4 = instr('abcabcabc', 'abca', -1);
select 1 = instr('abcabcabc', 'abca', -8);
select 1 = instr('abcabcabc', 'abca', -9);
select 0 = instr('abcabcabc', 'abca', -10);
select 1 = instr('abcabcabc', 'abca', 1, 1);
select 4 = instr('abcabcabc', 'abca', 1, 2);
select 0 = instr('abcabcabc', 'abca', 1, 3);

select reverse('abcdef') = 'fedcba';
select reverse('中国') = '国中';
select concat('Tech on', ' the Net') =  'Tech on the Net';
select concat('a', 'b') =  'ab';
select concat('a', NULL) = 'a';
select concat(NULL, 'b') = 'b';
select concat('a', 2) = 'a2';
select concat(1, 'b') = '1b';
select concat(1, 2) = '12';
select concat(1, NULL) = '1';
select concat(NULL, 2) = '2';
SELECT nanvl(12345, 1), nanvl('NaN', 1);
SELECT nanvl(12345::float4, 1), nanvl('NaN'::float4, 1);
SELECT nanvl(12345::float8, 1), nanvl('NaN'::float8, 1);
SELECT nanvl(12345::numeric, 1), nanvl('NaN'::numeric, 1);
SELECT bitand(5,1), bitand(5,2), bitand(5,4);

select listagg(i::text) from generate_series(1,3) g(i);
select listagg(i::text, ',') from generate_series(1,3) g(i);
select coalesce(listagg(i::text), '<NULL>') from (SELECT ''::text) g(i);
select coalesce(listagg(i::text), '<NULL>') from generate_series(1,0) g(i);

select nvl2('A'::text, 'B', 'C');
select nvl2(NULL::text, 'B', 'C');
select nvl2('A'::text, NULL, 'C');
select nvl2(NULL::text, 'B', NULL);
select nvl2(1, 2, 3);
select nvl2(NULL, 2, 3);

SELECT dump('Yellow dog'::text) ~ E'^Typ=25 Len=(\\d+): \\d+(,\\d+)*$' AS t;
SELECT dump('Yellow dog'::text, 10) ~ E'^Typ=25 Len=(\\d+): \\d+(,\\d+)*$' AS t;
SELECT dump('Yellow dog'::text, 17) ~ E'^Typ=25 Len=(\\d+): .(,.)*$' AS t;
SELECT dump(10::int2) ~ E'^Typ=21 Len=2: \\d+(,\\d+){1}$' AS t;
SELECT dump(10::int4) ~ E'^Typ=23 Len=4: \\d+(,\\d+){3}$' AS t;
SELECT dump(10::int8) ~ E'^Typ=20 Len=8: \\d+(,\\d+){7}$' AS t;
SELECT dump(10.23::float4) ~ E'^Typ=700 Len=4: \\d+(,\\d+){3}$' AS t;
SELECT dump(10.23::float8) ~ E'^Typ=701 Len=8: \\d+(,\\d+){7}$' AS t;
SELECT dump(10.23::numeric) ~ E'^Typ=1700 Len=(\\d+): \\d+(,\\d+)*$' AS t;
SELECT dump('2008-10-10'::date) ~ E'^Typ=1082 Len=4: \\d+(,\\d+){3}$' AS t;
SELECT dump('2008-10-10'::timestamp) ~ E'^Typ=1114 Len=8: \\d+(,\\d+){7}$' AS t;


RESET DATESTYLE;

-- start_ignore
\i @abs_srcdir@/../../../contrib/orafce/uninstall_orafunc.sql
-- end_ignore

