-- start_ignore
DROP TABLE IF EXISTS d_xpect_setup;
DROP VIEW IF EXISTS v_xpect_triangle_de;
-- end_ignore

CREATE TABLE d_xpect_setup (
    key character varying(20) NOT NULL,
    country character varying(5) NOT NULL,
    key_value character varying(50),
    key_desc character varying(200)
) DISTRIBUTED BY (country ,key);

CREATE VIEW v_xpect_triangle_de AS
    SELECT x.rep_year, y.age, ((x.rep_year - y.age) - t."offset") AS yob, t.triangle FROM (SELECT s.a AS rep_year FROM (SELECT generate_series.generate_series FROM generate_series((SELECT (substr((d_xpect_setup.key_value)::text, 1, 4))::integer AS valid_from FROM d_xpect_setup WHERE (((d_xpect_setup.key)::text = 'data_valid_from'::text) AND ((d_xpect_setup.country)::text = 'DE'::text))), (SELECT (to_char(((SELECT CASE d_xpect_setup.key_value WHEN IS NOT DISTINCT FROM 'NULL'::text THEN ('now'::text)::date ELSE to_date((d_xpect_setup.key_value)::text, 'YYYYMM'::text) END AS to_date FROM d_xpect_setup WHERE (((d_xpect_setup.key)::text = 'launch_date'::text) AND ((d_xpect_setup.country)::text = 'DE'::text))) - (((d_xpect_setup.key_value)::integer)::double precision * '1 mon'::interval)), 'yyyy'::text))::integer AS valid_to FROM d_xpect_setup WHERE (((d_xpect_setup.key)::text = 'data_valid_to'::text) AND ((d_xpect_setup.country)::text = 'DE'::text)))) generate_series(generate_series)) s(a)) x, (SELECT s.a AS age FROM (SELECT generate_series.generate_series FROM generate_series(0, 120) generate_series(generate_series)) s(a)) y, (SELECT 1 AS "offset", 'HT' AS triangle UNION SELECT 0 AS "offset", 'LT') t ORDER BY x.rep_year DESC, y.age DESC;



SELECT * FROM v_xpect_triangle_de , ( SELECT lpad(s.a ::text, 2, '0'::text) AS all_months FROM generate_series(1, 12) s(a)) b WHERE (v_xpect_triangle_de.rep_year::text || b.all_months)::text>=  ( SELECT d_xpect_setup.key_value AS valid_from FROM d_xpect_setup WHERE d_xpect_setup.key::text = 'data_valid_from'::text AND d_xpect_setup.country::text = 'NL'::text);
