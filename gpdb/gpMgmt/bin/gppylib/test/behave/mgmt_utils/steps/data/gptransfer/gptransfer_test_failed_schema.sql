DROP DATABASE gptransfer_test_db_failed_schema;

CREATE DATABASE gptransfer_test_db_failed_schema;

\c gptransfer_test_db_failed_schema

CREATE TABLE t1 (id int, description text);

CREATE SCHEMA gptransfer;
