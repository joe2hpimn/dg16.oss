#!/usr/bin/env python
import re, os, signal, time, filecmp, stat, fileinput
import yaml
from gppylib.commands.gp import GpStart, chk_local_db_running
from gppylib.commands.base import Command, ExecutionError, REMOTE
from gppylib.db import dbconn
from gppylib.gparray import GpArray, MODE_SYNCHRONIZED

PARTITION_START_DATE = '2010-01-01'
PARTITION_END_DATE = '2013-01-01'

GET_APPENDONLY_DATA_TABLE_INFO_SQL ="""SELECT ALL_DATA_TABLES.oid, ALL_DATA_TABLES.schemaname, ALL_DATA_TABLES.tablename, OUTER_PG_CLASS.relname as tupletable FROM(
SELECT ALLTABLES.oid, ALLTABLES.schemaname, ALLTABLES.tablename FROM 
    (SELECT c.oid, n.nspname AS schemaname, c.relname AS tablename FROM pg_class c, pg_namespace n 
    WHERE n.oid = c.relnamespace) as ALLTABLES,
    (SELECT n.nspname AS schemaname, c.relname AS tablename
    FROM pg_class c LEFT JOIN pg_namespace n ON n.oid = c.relnamespace
    LEFT JOIN pg_tablespace t ON t.oid = c.reltablespace
    WHERE c.relkind = 'r'::"char" AND c.oid > 16384 AND (c.relnamespace > 16384 or n.nspname = 'public') 
    EXCEPT
    ((SELECT x.schemaname, x.partitiontablename FROM 
    (SELECT distinct schemaname, tablename, partitiontablename, partitionlevel FROM pg_partitions) as X,
    (SELECT schemaname, tablename maxtable, max(partitionlevel) maxlevel FROM pg_partitions group by (tablename, schemaname))
 as Y    
    WHERE x.schemaname = y.schemaname and x.tablename = Y.maxtable and x.partitionlevel != Y.maxlevel) 
    UNION (SELECT distinct schemaname, tablename FROM pg_partitions))) as DATATABLES 
WHERE ALLTABLES.schemaname = DATATABLES.schemaname and ALLTABLES.tablename = DATATABLES.tablename AND ALLTABLES.oid not in (select reloid from pg_exttable)
) as ALL_DATA_TABLES, pg_appendonly, pg_class OUTER_PG_CLASS
    WHERE ALL_DATA_TABLES.oid = pg_appendonly.relid 
    AND OUTER_PG_CLASS.oid = pg_appendonly.segrelid
"""

GET_ALL_AO_DATATABLES_SQL = """
    %s AND pg_appendonly.columnstore = 'f'
""" % GET_APPENDONLY_DATA_TABLE_INFO_SQL

GET_ALL_CO_DATATABLES_SQL = """
    %s AND pg_appendonly.columnstore = 't'
""" % GET_APPENDONLY_DATA_TABLE_INFO_SQL

master_data_dir = os.environ.get('MASTER_DATA_DIRECTORY')
if master_data_dir is None:
    raise Exception('MASTER_DATA_DIRECTORY is not set')

def execute_sql(dbname, sql):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, sql)
        conn.commit()

def execute_sql_singleton(dbname, sql):
    result = None
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        result = dbconn.execSQLForSingleton(conn, sql)

    if result is None:
        raise Exception("error running query: %s" % sql)

    return result


def has_exception(context):
    if not hasattr(context, 'exception'):
        return False
    if context.exception:
        return True
    else:
        return False

def run_command(context, command):
    context.exception = None
    cmd = Command(name='run %s' % command, cmdStr='%s' % command)
    try:
        cmd.run(validateAfter=True)
    except ExecutionError, e:
        context.exception = e

    result = cmd.get_results()
    context.ret_code = result.rc 
    context.stdout_message = result.stdout
    context.error_message = result.stderr 

def run_cmd(command):
    cmd = Command(name='run %s' % command, cmdStr='%s' % command)
    try: 
        cmd.run(validateAfter=True)
    except ExecutionError, e:
        print 'caught exception %s'%e

    result = cmd.get_results()
    return (result.rc, result.stdout, result.stderr)

def run_command_remote(context,command, host, source_file, export_mdd):
    cmd = Command(name='run command %s'%command,
                  cmdStr='gpssh -h %s -e \'source %s; %s; %s\''%(host, source_file,export_mdd, command))
    cmd.run(validateAfter=True)
    result = cmd.get_results()
    context.ret_code = result.rc
    context.stdout_message = result.stdout
    context.error_message = result.stderr

def run_gpcommand(context, command):
    context.exception = None
    cmd = Command(name='run %s' % command, cmdStr='$GPHOME/bin/%s' % command)
    try:
        cmd.run(validateAfter=True)
    except ExecutionError, e:
        context.exception = e

    result = cmd.get_results()
    context.ret_code = result.rc 
    context.stdout_message = result.stdout
    context.error_message = result.stderr 

def check_stdout_msg(context, msg):
    pat = re.compile(msg)
    if not pat.search(context.stdout_message):
        err_str = "Expected stdout string '%s' and found: '%s'" % (msg, context.stdout_message)
        raise Exception(err_str)

def check_string_not_present_stdout(context, msg):
    pat = re.compile(msg)
    if pat.search(context.stdout_message):
        err_str = "Did not expect stdout string '%s' but found: '%s'" % (msg, context.stdout_message)
        raise Exception(err_str)

def check_err_msg(context, err_msg):
    if not hasattr(context, 'exception'):
        raise Exception('An exception was not raised and it was expected')
    pat = re.compile(err_msg)
    if not pat.search(context.error_message):
        err_str = "Expected error string '%s' and found: '%s'" % (err_msg, context.error_message)
        raise Exception(err_str)

def check_return_code(context, ret_code):
    if context.ret_code != int(ret_code):
        emsg = ""
        if context.error_message:
            emsg += context.error_message
        raise Exception("expected return code '%s' does not equal acutal return code '%s' %s" % (ret_code, context.ret_code, emsg))

def check_database_is_running(context):
    if not 'PGPORT' in os.environ:
        raise Exception('PGPORT should be set')

    pgport = int(os.environ['PGPORT'])

    running_status = chk_local_db_running(master_data_dir, pgport) 
    gpdb_running = running_status[0] and running_status[1] and running_status[2] and running_status[3]
    
    return gpdb_running

def start_database_if_not_started(context):
    if not check_database_is_running(context):
        start_database(context)

def start_database(context):
    run_gpcommand(context, 'gpstart -a')
    if context.exception:
        raise context.exception

def stop_database_if_started(context):
    if check_database_is_running(context):
        stop_database(context)

def stop_database(context):
    run_gpcommand(context, 'gpstop -M fast -a')
    if context.exception:
        raise context.exception

def getRows(dbname, exec_sql):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        curs = dbconn.execSQL(conn, exec_sql)
        results = curs.fetchall()
    return results

def getRow(dbname, exec_sql):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        curs = dbconn.execSQL(conn, exec_sql)
        result = curs.fetchone()
    return result


def check_db_exists(dbname):
    LIST_DATABASE_SQL = 'select datname from pg_database'  
  
    results = []  
    with dbconn.connect(dbconn.DbURL(dbname='template1')) as conn:
        curs = dbconn.execSQL(conn, LIST_DATABASE_SQL)
        results = curs.fetchall()

    for result in results:
        if result[0] == dbname:
            return True
 
    return False

def create_database_if_not_exists(context, dbname):
    if not check_db_exists(dbname):
        create_database(context, dbname)

def create_database(context, dbname):

    LOOPS = 10
    for i in range(LOOPS):
        context.exception = None

        run_gpcommand(context, 'createdb %s' % dbname)

        if context.exception:
            time.sleep(1)
            continue

        if check_db_exists(dbname):
            return

        time.sleep(1)

    if context.exception:
        raise context.exception

    raise Exception("createdb for '%s' failed after %d attempts" % (dbname, LOOPS))

def clear_all_saved_data_verify_files(context):
    current_dir = os.getcwd()
    data_dir = os.path.join(current_dir, './gppylib/test/data')
    cmd = 'rm %s/*' % data_dir
    run_command(context, cmd)

def get_table_data_to_file(filename, tablename, dbname):
    current_dir = os.getcwd()
    filename = os.path.join(current_dir, './gppylib/test/data', filename)
    order_sql = """
                    select string_agg(a, ',')
                        from (
                            select generate_series(1,c.relnatts+1) as a
                                from pg_class as c
                                    inner join pg_namespace as n
                                    on c.relnamespace = n.oid
                                where (n.nspname || '.' || c.relname = '%s')
                                    or c.relname = '%s'
                        ) as q;
                """ % (tablename, tablename)
    query = order_sql
    conn = dbconn.connect(dbconn.DbURL(dbname=dbname))
    try:
        res = dbconn.execSQLForSingleton(conn, query)
        data_sql = "COPY (select gp_segment_id, * from %s order by %s) TO '%s'" %(tablename.strip(), res, filename)
        query = data_sql
        dbconn.execSQL(conn, query)
        conn.commit()
    except Exception as e:
        print "Cannot execute the query '%s' on the connection %s" % (query, str(dbconn.DbURL(dbname=dbname)))
        print "Exception: %s" % str(e)
    conn.close()

def diff_backup_restore_data(context, backup_file, restore_file):
    if not filecmp.cmp(backup_file, restore_file):
        raise Exception('%s and %s do not match' % (backup_file, restore_file))
    
def validate_restore_data(context, tablename, dbname):
    filename = tablename.strip() + "_restore"
    get_table_data_to_file(filename, tablename, dbname)
    current_dir = os.getcwd()
    backup_file = os.path.join(current_dir, './gppylib/test/data', tablename.strip() + "_backup")
    restore_file = os.path.join(current_dir, './gppylib/test/data', tablename.strip() + "_restore")
    diff_backup_restore_data(context, backup_file, restore_file)


def validate_db_data(context, dbname, expected_table_count):
    tbls = get_table_names(dbname)
    if len(tbls) != expected_table_count:
        raise Exception("db %s does not have expected number of tables %d != %d" % (dbname, expected_table_count, len(tbls)))
    for t in tbls:
        name = "%s.%s" % (t[0], t[1])
        validate_restore_data(context, name, dbname)

def get_segment_hostnames(context, dbname):
    sql = "select distinct(hostname) from gp_segment_configuration where content != -1;"
    return getRows(dbname, sql)     

def backup_db_data(context, dbname):
    tbls = get_table_names(dbname)
    for t in tbls:
        nm = "%s.%s" % (t[0], t[1])
        backup_data(context, nm, dbname)

def backup_data(context, tablename, dbname):
    filename = tablename + "_backup"
    get_table_data_to_file(filename, tablename, dbname)

def check_partition_table_exists(context, dbname, schemaname, table_name, table_type=None, part_level=1, part_number=1):
    partitions = get_partition_names(schemaname, table_name, dbname, part_level, part_number)
    if not partitions:
        return False
    return check_table_exists(context, dbname, partitions[0][0].strip(), table_type) 

def check_table_exists(context, dbname, table_name, table_type=None):
    SQL = """
            select oid::regclass, relkind, relstorage, reloptions \
            from pg_class \
            where oid = '%s'::regclass; \
          """ % table_name

    table_row = None 
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        try:
            table_row = dbconn.execSQLForSingletonRow(conn, SQL) 
        except Exception as e:
            context.exception = e
            return False

    if table_type is None:
        return True

    if table_row[2] == 'a':
        original_table_type = 'ao'
    elif table_row[2] == 'c':
        original_table_type = 'co'
    elif table_row[2] == 'h':
        original_table_type = 'heap'
    elif table_row[2] == 'x':
        original_table_type = 'external'
    elif table_row[2] == 'v':
        original_table_type = 'view'
    else:
        raise Exception('Unknown table type %s' % table_row[2]) 

    if original_table_type != table_type.strip():
        return False

    return True

def check_pl_exists(context, dbname, lan_name):
    SQL = """select count(*) from pg_language where lanname='%s';""" % lan_name
    lan_count = getRows(dbname, SQL)[0][0]
    if lan_count == 0:
        return False
    return True

def check_constraint_exists(context, dbname, conname):
    SQL = """select count(*) from pg_constraint where conname='%s';""" % conname
    con_count = getRows(dbname, SQL)[0][0]
    if con_count == 0:
        return False
    return True

def drop_external_table_if_exists(context, table_name, dbname):
    if check_table_exists(context, table_name=table_name, dbname=dbname, table_type='external'):
        drop_external_table(context, table_name=table_name, dbname=dbname)

def drop_table_if_exists(context, table_name, dbname):
    if check_table_exists(context, table_name=table_name, dbname=dbname):
        drop_table(context, table_name=table_name, dbname=dbname)

def drop_external_table(context, table_name, dbname):
    SQL = 'drop external table %s' % table_name
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, SQL)
        conn.commit()

    if check_table_exists(context, table_name=table_name, dbname=dbname, table_type='external'):
        raise Exception('Unable to successfully drop the table %s' % table_name) 

def drop_table(context, table_name, dbname):
    SQL = 'drop table %s' % table_name
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, SQL)
        conn.commit()

    if check_table_exists(context, table_name=table_name, dbname=dbname):
        raise Exception('Unable to successfully drop the table %s' % table_name) 
 
def check_schema_exists(context, schema_name, dbname):
    schema_check_sql = "select * from pg_namespace where nspname='%s';" % schema_name
    if len(getRows(dbname, schema_check_sql)) < 1:
        return False
    return True

def drop_schema_if_exists(context, schema_name, dbname):
    if check_schema_exists(context, schema_name, dbname):
        drop_schema(context, schema_name, dbname)

def drop_schema(context, schema_name, dbname):
    SQL = 'drop schema %s cascade' % schema_name
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, SQL)
        conn.commit()
    if check_schema_exists(context, schema_name, dbname):
        raise Exception('Unable to successfully drop the schema %s' % schema_name) 
 
def validate_table_data_on_segments(context, tablename, dbname):

    seg_data_sql = "select gp_segment_id, count(*) from gp_dist_random('%s') group by gp_segment_id;" % tablename
    
    rows = getRows(dbname, seg_data_sql)     
    for row in rows:
        if row[1] == '0' :
           raise Exception('Data not present in segment %s' % row[0]) 
   
def get_table_names(dbname):
    sql = """
            SELECT n.nspname AS schemaname, c.relname AS tablename\
            FROM pg_class c\
            LEFT JOIN pg_namespace n ON n.oid = c.relnamespace\
            LEFT JOIN pg_tablespace t ON t.oid = c.reltablespace\
            WHERE c.relkind = 'r'::"char" AND c.oid > 16384 AND (c.relnamespace > 16384 or n.nspname = 'public')
                  AND n.nspname NOT LIKE 'pg_temp_%'
          """

    return getRows(dbname, sql)

def get_partition_tablenames(tablename, dbname, part_level = 1):
 
    child_part_sql = "select partitiontablename from pg_partitions where tablename='%s' and partitionlevel=%s;" % (tablename, part_level)
    rows = getRows(dbname, child_part_sql)
    return rows
     
def get_partition_names(schemaname, tablename, dbname, part_level, part_number):
    part_num_sql = """select partitionschemaname || '.' || partitiontablename from pg_partitions 
                             where schemaname='%s' and tablename='%s'
                             and partitionlevel=%s and partitionposition=%s;""" % (schemaname, tablename, part_level, part_number)
    rows = getRows(dbname, part_num_sql)
    return rows
     
def validate_part_table_data_on_segments(context, tablename, part_level, dbname):
    
    rows = get_partition_tablenames(tablename, dbname, part_level)
    for part_tablename in rows :
        seg_data_sql = "select gp_segment_id, count(*) from gp_dist_random('%s') group by gp_segment_id;" % part_tablename[0]
        rows = getRows(dbname, seg_data_sql)        
        for row in rows:
            if row[1] == '0' :
               raise Exception('Data not present in segment %s' % row[0]) 

def validate_mixed_partition_storage_types(context, tablename, dbname):
    partition_names = get_partition_tablenames(tablename, dbname, part_level = 1)
    for position, partname in enumerate(partition_names):
        if position in(0, 2, 5, 7): 
            storage_type = 'c' 
        elif position in(1, 3, 6, 8): 
            storage_type = 'a' 
        else:
            storage_type = 'h' 
        for part in partname:
            validate_storage_type(context, part, storage_type, dbname)
    
def validate_storage_type(context, partname, storage_type, dbname):
    
    storage_type_sql = "select oid::regclass, relstorage from pg_class where oid = '%s'::regclass;" % (partname)
    rows = getRows(dbname, storage_type_sql)
    for row in rows:
        if row[1].strip() != storage_type.strip():
            raise Exception("The storage type of the partition %s is not as expected %s "% (row[1], storage_type))

def create_mixed_storage_partition(context, tablename, dbname):
    
    table_definition = 'Column1 int, Column2 varchar(20), Column3 date'
    create_table_str = "Create table %s (%s) Distributed randomly \
                        Partition by list(Column2)  \
                        Subpartition by range(Column3) Subpartition Template ( \
                        subpartition s_1  start(date '2010-01-01') end(date '2011-01-01') with (appendonly=true, orientation=column, compresstype=quicklz, compresslevel=1), \
                        subpartition s_2  start(date '2011-01-01') end(date '2012-01-01') with (appendonly=true, orientation=row, compresstype=zlib, compresslevel=1), \
                        subpartition s_3  start(date '2012-01-01') end(date '2013-01-01') with (appendonly=true, orientation=column), \
                        subpartition s_4  start(date '2013-01-01') end(date '2014-01-01') with (appendonly=true, orientation=row), \
                        subpartition s_5  start(date '2014-01-01') end(date '2015-01-01') ) \
                        (partition p1 values('backup') , partition p2 values('restore')) \
                        ;" % (tablename, table_definition)
    
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, create_table_str)
        conn.commit()

    populate_partition(tablename, '2010-01-01', dbname, 0)

def create_external_partition(context, tablename, dbname, port, filename):
    
    table_definition = 'Column1 int, Column2 varchar(20), Column3 date'
    create_table_str = "Create table %s (%s) Distributed randomly \
                        Partition by range(Column3) ( \
                        partition p_1  start(date '2010-01-01') end(date '2011-01-01') with (appendonly=true, orientation=column, compresstype=quicklz, compresslevel=1), \
                        partition p_2  start(date '2011-01-01') end(date '2012-01-01') with (appendonly=true, orientation=row, compresstype=zlib, compresslevel=1), \
                        partition s_3  start(date '2012-01-01') end(date '2013-01-01') with (appendonly=true, orientation=column), \
                        partition s_4  start(date '2013-01-01') end(date '2014-01-01') with (appendonly=true, orientation=row), \
                        partition s_5  start(date '2014-01-01') end(date '2015-01-01') ) \
                        ;" % (tablename, table_definition)
    
    master_hostname = get_master_hostname();
    create_ext_table_str = "Create readable external table %s_ret (%s) \
                            location ('gpfdist://%s:%s/%s') \
                            format 'csv' encoding 'utf-8' \
                            log errors segment reject limit 1000 \
                            ;" % (tablename, table_definition, master_hostname[0][0].strip(), port, filename)

    alter_table_str = "Alter table %s exchange partition p_2 \
                       with table %s_ret without validation \
                       ;" % (tablename, tablename)

    drop_table_str = "Drop table %s_ret;" % (tablename)

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, create_table_str)
        dbconn.execSQL(conn, create_ext_table_str)
        dbconn.execSQL(conn, alter_table_str)
        dbconn.execSQL(conn, drop_table_str)
        conn.commit()

    populate_partition(tablename, '2010-01-01', dbname, 0, 100)


def modify_partition_data(context, tablename, dbname, partitionnum):

    # ONLY works for partition 1 to 3
    if partitionnum == 1:
        year = '2010'
    elif partitionnum == 2:
        year = '2011'
    elif partitionnum == 3:
        year = '2012'
    else:
        raise Exception("BAD PARAM to modify_partition_data %s" % partitionnum)

    cmdStr = """ echo "90,backup,%s-12-30" | psql -d %s -c "copy %s from stdin delimiter ',';" """ % (year, dbname, tablename)    
    for i in range(10):
        cmd = Command(name='insert data into %s' % tablename, cmdStr=cmdStr)
        cmd.run(validateAfter=True) 

def modify_data(context, tablename, dbname):
    cmdStr = 'psql -d %s -c "copy %s to stdout;" | psql -d %s -c "copy %s from stdin;"' % (dbname, tablename, dbname, tablename)
    cmd = Command(name='insert data into %s' % tablename, cmdStr=cmdStr)
    cmd.run(validateAfter=True) 

def add_partition(context, partitionnum, tablename, dbname):
    alter_table_str = "alter table %s add default partition p%s; insert into %s select i+%d, 'update', i + date '%s' from generate_series(0,1094) as i" \
                      % (tablename, partitionnum, tablename, int(partitionnum), PARTITION_START_DATE)
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, alter_table_str)
        conn.commit()

def drop_partition(context, partitionnum, tablename, dbname):
    alter_table_str = "alter table %s drop partition p%s;" % (tablename, partitionnum)
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, alter_table_str)
        conn.commit()

def create_partition(context, tablename, storage_type, dbname, compression_type=None, partition=True, rowcount=1094):

    interval = '1 year'

    table_definition = 'Column1 int, Column2 varchar(20), Column3 date'
    create_table_str = "Create table " + tablename + "(" + table_definition + ")"
    storage_type_dict = {'ao':'row', 'co':'column'}

    part_table = " Distributed Randomly Partition by list(Column2) \
                    Subpartition by range(Column3) Subpartition Template \
                    (start (date '%s') end (date '%s') every (interval '%s')) \
                    (Partition p1 values('backup') , Partition p2 values('restore')) " \
                    %(PARTITION_START_DATE, PARTITION_END_DATE, interval)

    if storage_type == "heap":
        create_table_str = create_table_str
        if partition:
           create_table_str = create_table_str + part_table

    elif storage_type == "ao" or storage_type == "co":
        create_table_str = create_table_str + " WITH(appendonly = true, orientation = %s) " % storage_type_dict[storage_type]
        if compression_type is not None:
            create_table_str = create_table_str[:-2] + ", compresstype = " + compression_type + ") "
        if partition:
            create_table_str = create_table_str + part_table

    create_table_str = create_table_str + ";" 

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, create_table_str)
        conn.commit()

    populate_partition(tablename, PARTITION_START_DATE, dbname, 0, rowcount)

# same data size as populate partition, but different values
def populate_partition_diff_data_same_eof(tablename, dbname):
    populate_partition(tablename, PARTITION_START_DATE, dbname, 1)

def populate_partition_same_data(tablename, dbname):
    populate_partition(tablename, PARTITION_START_DATE, dbname, 0)

def populate_partition(tablename, start_date, dbname, data_offset, rowcount=1094):

    insert_sql_str = "insert into %s select i+%d, 'backup', i + date '%s' from generate_series(0,%d) as i" %(tablename, data_offset, start_date, rowcount)
    insert_sql_str += "; insert into %s select i+%d, 'restore', i + date '%s' from generate_series(0,%d) as i" %(tablename, data_offset, start_date, rowcount)

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, insert_sql_str)
        conn.commit()

def create_indexes(context, table_name, indexname, dbname):

    btree_index_sql = "create index btree_%s on %s using btree(column1);" % (indexname, table_name)
    bitmap_index_sql = "create index bitmap_%s on %s using bitmap(column3);" % (indexname, table_name)
    index_sql = btree_index_sql + bitmap_index_sql
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, index_sql)
        conn.commit()
    validate_index(context, table_name, dbname)

def validate_index(context, table_name, dbname):
    index_sql = "select count(indexrelid::regclass) from pg_index, pg_class where indrelid = '%s'::regclass group by indexrelid;" % table_name
    rows = getRows(dbname, index_sql)
    if len(rows) != 2:
        raise Exception('Index creation was not successful. Expected 2 rows does not match %d rows' % result)
   
def create_schema(context, schema_name, dbname):
    if not check_schema_exists(context, schema_name, dbname):
        schema_sql = "create schema %s" % schema_name
        with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
            dbconn.execSQL(conn, schema_sql)
            conn.commit()
     
def create_int_table(context, table_name, table_type='heap', dbname='testdb'):
    CREATE_TABLE_SQL = None
    NROW = 1000

    table_type = table_type.upper() 
    if table_type == 'AO':
        CREATE_TABLE_SQL = 'create table %s WITH(APPENDONLY=TRUE) as select generate_series(1,%d) as c1' % (table_name, NROW)
    elif table_type == 'CO':
        CREATE_TABLE_SQL = 'create table %s WITH(APPENDONLY=TRUE, orientation=column) as select generate_series(1, %d) as c1' % (table_name, NROW)
    elif table_type == 'HEAP':
        CREATE_TABLE_SQL = 'create table %s as select generate_series(1, %d) as c1' % (table_name, NROW)

    if CREATE_TABLE_SQL is None:
        raise Exception('Invalid table type specified')
  
    SELECT_TABLE_SQL = 'select count(*) from %s' % table_name 
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, CREATE_TABLE_SQL)
        conn.commit()
         
        result = dbconn.execSQLForSingleton(conn, SELECT_TABLE_SQL)
        if result != NROW:
            raise Exception('Integer table creation was not successful. Expected %d does not match %d' %(NROW, result))
       
def drop_database(context, dbname):

    LOOPS = 10
    for i in range(LOOPS):
        context.exception = None

        run_gpcommand(context, 'dropdb %s' % dbname)

        if context.exception:
            time.sleep(1)
            continue

        if not check_db_exists(dbname):
            return

        time.sleep(1)

    if context.exception:
        raise context.exception

    raise Exception('db exists after dropping: %s' % dbname)

def drop_database_if_exists(context, dbname):
    if check_db_exists(dbname):
        drop_database(context, dbname)

def run_on_all_segs(context, dbname, query):
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    primary_segs = [seg for seg in gparray.getDbList() if seg.isSegmentPrimary()]

    for seg in primary_segs:
        with dbconn.connect(dbconn.DbURL(dbname=dbname, hostname=seg.getSegmentHostName(), port=seg.getSegmentPort()), utility=True) as conn: 
            dbconn.execSQL(conn, query)  
            conn.commit()

def get_nic_up(hostname, nic):
    address = hostname + '-cm'
    cmd = Command(name='ifconfig nic', cmdStr='sudo /sbin/ifconfig %s' % nic, remoteHost=address, ctxt=REMOTE)
    cmd.run(validateAfter=True)

    return 'UP' in cmd.get_results().stdout
   
def bring_nic_down(hostname, nic):
    address = hostname + '-cm'
    cmd = Command(name='bring down nic', cmdStr='sudo /sbin/ifdown %s' % nic, remoteHost=address, ctxt=REMOTE)
    cmd.run(validateAfter=True)
    
    if get_nic_up(hostname, nic):
        raise Exception('Unable to bring down nic %s on host %s' % (nic, hostname))

def bring_nic_up(hostname, nic):
    address = hostname + '-cm'
    cmd = Command(name='bring up nic', cmdStr='sudo /sbin/ifup %s' % nic, remoteHost=address, ctxt=REMOTE)
    cmd.run(validateAfter=True)
    
    if not get_nic_up(hostname, nic):
        raise Exception('Unable to bring up nic %s on host %s' % (nic, hostname))

def are_segments_synchronized():
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    segments = gparray.getDbList()
    for seg in segments:
        if seg.mode != MODE_SYNCHRONIZED:
            return False 
    return True

def get_distribution_policy(dbname):
    filename = dbname.strip() + "_dist_policy_backup"
    get_dist_policy_to_file(filename, dbname)

def get_dist_policy_to_file(filename, dbname):
    dist_policy_sql = " \
            SELECT \
                c.relname as tablename, p.attrnums as distribution_policy \
            FROM \
                pg_class c \
                INNER JOIN \
                gp_distribution_policy p \
                ON (c.relfilenode = p.localoid) \
                AND \
                c.relstorage != 'x' \
            ORDER BY c.relname"

    current_dir = os.getcwd()
    filename = os.path.join(current_dir, './gppylib/test/data', filename)
    data_sql = "COPY (%s) TO '%s'" %(dist_policy_sql, filename)

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, data_sql)
        conn.commit()

def validate_distribution_policy(context, dbname):
    filename = dbname.strip() + "_dist_policy_restore"
    get_dist_policy_to_file(filename, dbname)
    current_dir = os.getcwd()
    backup_file = os.path.join(current_dir, './gppylib/test/data', dbname.strip() + "_dist_policy_backup")
    restore_file = os.path.join(current_dir, './gppylib/test/data', dbname.strip() + "_dist_policy_restore")
    diff_backup_restore_data(context, backup_file, restore_file)

def check_row_count(tablename, dbname, nrows):
    NUM_ROWS_QUERY = 'select count(*) from %s' % tablename
    # We want to bubble up the exception so that if table does not exist, the test fails
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        result = dbconn.execSQLForSingleton(conn, NUM_ROWS_QUERY)
    if result != nrows:
        raise Exception('%d rows in table %s.%s, expected row count = %d' % (result, dbname, tablename, nrows))
 
def check_empty_table(tablename, dbname):
    check_row_count(tablename, dbname, 0)
     
def match_table_select(context, tablename, dbname, orderby=None):
    if orderby != None :
        check_query = 'psql -d %s -c \'select * from %s order by %s\'' % (dbname, tablename, orderby)
        command = 'psql -p $GPTRANSFER_SOURCE_PORT -h $GPTRANSFER_SOURCE_HOST -U $GPTRANSFER_SOURCE_USER -d %s -c \'select * from %s order by %s\''%(dbname, tablename, orderby)
    else:
        check_query = 'psql -d %s -c \'select * from %s\'' % (dbname, tablename)
        command = 'psql -p $GPTRANSFER_SOURCE_PORT -h $GPTRANSFER_SOURCE_HOST -U $GPTRANSFER_SOURCE_USER -d %s -c \'select * from %s\''%(dbname, tablename)
    (rc, out1, err) = run_cmd(check_query)
    (rc, out2, err) = run_cmd(command)
    if out2 != out1:
        raise Exception('table %s in database %s between source and destination system does not match'%(tablename,dbname))

def get_master_hostname(dbname='template1'):
    master_hostname_sql = "select distinct hostname from gp_segment_configuration where content=-1 and role='p'"
    return getRows(dbname, master_hostname_sql)

def get_hosts_and_datadirs(dbname='template1'):
    get_hosts_and_datadirs_sql = "select hostname, fselocation from gp_segment_configuration, pg_filespace_entry where fsedbid = dbid and role='p';"
    return getRows(dbname, get_hosts_and_datadirs_sql)

def get_hosts(dbname='template1'):
    get_hosts_sql = "select distinct hostname from gp_segment_configuration where role='p';"
    return getRows(dbname, get_hosts_sql)

def get_backup_dir_for_host(hostname, dbname='template1'):
    get_backup_dir_sql = "select f.fselocation from pg_filespace_entry f inner join gp_segment_configuration g on \
                            f.fsedbid=g.dbid and g.role='p' and g.hostname = '%s'" % (hostname)
    return getRows(dbname, get_backup_dir_sql)
   
def cleanup_dir(context, host, location):
    cleanup_cmd = "gpssh -h %s -e 'rm -rf %s/db_dumps %s/gpcrondump.pid'" % (host, location, location)
    run_command(context, cleanup_cmd)
    if context.exception:
        raise context.exception
    
def cleanup_backup_files(context, dbname, location=None):
    hosts = get_hosts(dbname)
    for host in hosts:
        if location:
            cleanup_dir(context, host[0], location)
        else:
            dirs = get_backup_dir_for_host(host[0], dbname)
            for dir in dirs:
                cleanup_dir(context, host[0], dir[0])

def cleanup_report_files(context, master_data_dir):
    if not master_data_dir:
        raise Exception("master_data_dir not specified in cleanup_report_files")
    if master_data_dir.strip() == '/':
        raise Exception("Can't call cleanup_report_files on root directory")

    file_pattern = "gp_*.rpt"
    cleanup_cmd = "rm -f %s/%s" % (master_data_dir, file_pattern)
    run_command(context, cleanup_cmd)
    if context.exception:
        raise context.exception

def truncate_table(dbname, tablename):
    TRUNCATE_SQL = 'TRUNCATE %s' % tablename
    execute_sql(dbname, TRUNCATE_SQL)

def verify_truncate_in_pg_stat_last_operation(context, dbname, oid):
    VERIFY_TRUNCATE_SQL = """SELECT *
                             FROM pg_stat_last_operation
                             WHERE objid = %d and staactionname = 'TRUNCATE' """ % oid
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        row = dbconn.execSQLForSingletonRow(conn, VERIFY_TRUNCATE_SQL)
    if len(row) != 7:
        raise Exception('Invalid number of colums %d' % len(row))
    if row[2] != 'TRUNCATE':
        raise Exception('Actiontype not expected TRUNCATE "%s"' % row[2])
    if row[5]:
        raise Exception('Subtype for TRUNCATE operation is not empty %s' % row[5])

def verify_truncate_not_in_pg_stat_last_operation(context, dbname, oid):
    VERIFY_TRUNCATE_SQL = """SELECT count(*)
                             FROM pg_stat_last_operation
                             WHERE objid = %d and staactionname = 'TRUNCATE' """ % oid
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        thecount = dbconn.execSQLForSingleton(conn, VERIFY_TRUNCATE_SQL)
        if thecount != 0:
            raise Exception("Found %s rows from query '%s' should be 0" % (thecount, VERIFY_TRUNCATE_SQL))

def get_table_oid(context, dbname, schema, tablename):
    OID_SQL = """SELECT c.oid 
                 FROM pg_class c, pg_namespace n
                 WHERE c.relnamespace = n.oid AND c.relname = '%s' AND n.nspname = '%s'""" % (tablename, schema)

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        oid = dbconn.execSQLForSingleton(conn, OID_SQL)

    return oid

def insert_numbers(dbname, tablename, lownum, highnum):
    sql = "insert into %s select generate_series(%s, %s)" % (tablename, lownum, highnum)
    execute_sql(dbname, sql)

def verify_integer_tuple_counts(context, filename):
    with open(filename, 'r') as fp:
        for line in fp:
            tupcount = line.split(',')[-1].strip()
            if re.match("^\d+?\.\d+?$", tupcount) is not None: 
                raise Exception('Expected an integer tuplecount in file %s found float' % filename)

def create_fake_pg_aoseg_table(context, table, dbname):
    sql = """CREATE TABLE %s(segno int, 
                             eof double precision,
                             tupcount double precision,
                             modcount bigint,
                             varblockcount double precision,
                             eofuncompressed double precision)""" % table
    execute_sql(dbname, sql)

def insert_row(context, row_values, table, dbname):
    sql = """INSERT INTO %s values(%s)""" % (table, row_values)
    execute_sql(dbname, sql)

def copy_file_to_all_db_hosts(context, filename):

    hosts_set = set()
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    for seg in gparray.getDbList():
        if seg.isSegmentPrimary():
            hosts_set.add(seg.getSegmentAddress())

    hostfile = '/tmp/copy_host_file.behave' 
    with open(hostfile, 'w') as fd:
        for h in hosts_set:
            fd.write('%s\n' % h)
    
    cmd = 'gpscp -f %s %s =:%s' % (hostfile, filename, filename)
    run_command(context, cmd)
    if context.exception:
        raise Exception("FAIL: '%s' '%s'" % (cmd, context.exception.__str__()))

    os.remove(hostfile)

def create_large_num_partitions(table_type, table_name, db_name, num_partitions=None):
    if table_type == "ao":
        condition = "with(appendonly=true)"
    elif table_type == "co":
        condition = "with(appendonly=true, orientation=column)"
    else:
        condition = ""

    if num_partitions is None:
        create_large_partitions_sql = """
                                        create table %s (column1 int, column2 int) %s partition by range(column1) subpartition by range(column2) subpartition template(start(1) end(75) every(1)) (start(1) end(75) every(1))
                                      """ % (table_name, condition)
    else:
        create_large_partitions_sql = """
                                        create table %s (column1 int, column2 int) %s partition by range(column1) (start(1) end(%d) every(1)) 
                                      """ % (table_name, condition, num_partitions)
    execute_sql(db_name, create_large_partitions_sql)

    verify_table_exists_sql = """select count(*) from pg_class where relname = '%s'""" % table_name
    num_rows = getRows(db_name, verify_table_exists_sql)[0][0] 
    if num_rows != 1:
        raise Exception('Creation of table "%s:%s" failed. Num rows in pg_class = %s' % (db_name, table_name, num_rows))

def validate_num_restored_tables(context, num_tables, dbname):
    tbls = get_table_names(dbname)

    count_query = """select count(*) from %s"""
    num_validate_tables = 0
    for t in tbls:
        name = '%s.%s' % (t[0], t[1])
        count = getRows(dbname, count_query % name)[0][0]
        if count == 0:
            continue
        else:
            validate_restore_data(context, name, dbname)
            num_validate_tables += 1

    if num_validate_tables != int(num_tables.strip()):
        raise Exception('Invalid number of tables were restored. Expected "%s", Actual "%s"' % (num_tables, num_validate_tables)) 

def get_partition_list(partition_type, dbname):
    if partition_type == 'ao':
        sql = GET_ALL_AO_DATATABLES_SQL
    elif partition_type == 'co':
        sql = GET_ALL_CO_DATATABLES_SQL

    partition_list = getRows(dbname, sql) 
    for line in partition_list:
        if len(line) != 4:
            raise Exception('Invalid results from query to get all AO tables: [%s]' % (','.join(line)))
    return partition_list

def verify_stats(dbname, partition_info):

    for (oid, schemaname, partition_name, tupletable) in partition_info:
        tuple_count_sql = "select to_char(sum(tupcount::bigint), '999999999999999999999') from pg_aoseg.%s" % tupletable
        tuple_count = getRows(dbname, tuple_count_sql)[0][0]
        if tuple_count:
            tuple_count = tuple_count.strip()
        else:
            tuple_count = '0'
        validate_tuple_count(dbname, schemaname, partition_name, tuple_count)

def validate_tuple_count(dbname, schemaname, partition_name, tuple_count):    
    sql = 'select count(*) from %s.%s' % (schemaname, partition_name)
    row_count = getRows(dbname, sql)[0][0]
    if int(row_count) != int(tuple_count):
        raise Exception('Stats for the table %s.%s does not match. Stat count "%s" does not match the actual tuple count "%s"' % (schemaname, partition_name, tuple_count, row_count))
    
    
def validate_no_aoco_stats(context, dbname, table):

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:

        sql = "select relname from pg_class where oid in (select segrelid from pg_appendonly where relid in (select oid from pg_class where relname = '%s'))" % table
        tname = dbconn.execSQLForSingleton(conn, sql)
        sql = "select tupcount from pg_aoseg.%s" % tname.strip()
        rows = getRows(dbname, sql)
        if len(rows) != 0:
            raise Exception("%s has stats of %d rows in %s table and should be 0" % (table, int(rows[0][0]), tname))
 
def get_all_hostnames_as_list(context, dbname):
    hosts = []
    segs = get_segment_hostnames(context, dbname)
    for seg in segs:
        hosts.append(seg[0].strip())

    masters = get_master_hostname(dbname)
    for master in masters:
        hosts.append(master[0].strip())

    return hosts
   
def get_pid_for_segment(seg_data_dir, seg_host):
    cmd = Command(name='get list of postmaster processes',
                  cmdStr='ps -eaf | grep %s' % seg_data_dir,
                  ctxt=REMOTE,
                  remoteHost=seg_host)
    cmd.run(validateAfter=True)

    pid = None 
    results = cmd.get_results().stdout.strip().split('\n')
    for res in results:
        if 'grep' not in res: 
            pid = res.split()[1]
  
    if pid is None:
        return None

    return int(pid) 

def install_gppkg(context):
    if 'GPPKG_PATH' not in os.environ:
        raise Exception('GPPKG_PATH needs to be set in the environment to install gppkg')
    if 'GPPKG_NAME' not in os.environ:
        raise Exception('GPPKG_NAME needs to be set in the environment to install gppkg')

    gppkg_path = os.environ['GPPKG_PATH']
    gppkg_name = os.environ['GPPKG_NAME']
    command    = "gppkg --install %s/%s.gppkg" % (gppkg_path, gppkg_name)
    run_command(context, command)
    print "Install gppkg command: '%s', stdout: '%s', stderr: '%s'" % (command, context.stdout_message, context.error_message)

def enable_postgis_and_load_test_data_for_postgis_1(context):
    if 'GPHOME' not in os.environ:
        raise Exception('GPHOME needs to be set in the environment')

    install_gppkg(context)

    gphome = os.environ['GPHOME']
    path = "%s/share/postgresql/contrib" % gphome
    command = "psql -d opengeo -f %s/postgis.sql" % path
    run_command(context, command)

    command = "psql -d opengeo -f %s/spatial_ref_sys.sql" % path
    run_command(context, command)

    current_path = os.path.realpath(__file__)
    current_dir = os.path.dirname(current_path)
    postgis_data_dir = "%s/../behave/mgmt_utils/steps/data/postgis" % current_dir

    command = "psql -d opengeo -f %s/nyc_census_blocks_1.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_neighborhoods_1.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_subway_stations_1.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_census_sociodata.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_streets_1.sql" % postgis_data_dir
    run_command(context, command)

def enable_postgis_and_load_test_data(context):
    if 'GPHOME' not in os.environ:
        raise Exception('GPHOME needs to be set in the environment')

    install_gppkg(context)

    gphome = os.environ['GPHOME']
    path = "%s/share/postgresql/contrib/postgis-2.0" % gphome

    command = "psql -d opengeo -f %s/postgis.sql" % path
    run_command(context, command)

    command = "psql -d opengeo -f %s/spatial_ref_sys.sql" % path
    run_command(context, command)

    current_path = os.path.realpath(__file__)
    current_dir = os.path.dirname(current_path)
    postgis_data_dir = "%s/../behave/mgmt_utils/steps/data/postgis" % current_dir

    command = "psql -d opengeo -f %s/nyc_census_blocks.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_neighborhoods.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_subway_stations.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_census_sociodata.sql" % postgis_data_dir
    run_command(context, command)
    command = "psql -d opengeo -f %s/nyc_streets.sql" % postgis_data_dir
    run_command(context, command)


def kill_process(pid, host=None, sig=signal.SIGTERM):
    if host is not None:
        cmd = Command('kill process on a given host',
                      cmdStr='kill -%d %d' % (sig, pid),
                      ctxt=REMOTE,
                      remoteHost=host) 
        cmd.run(validateAfter=True) 
    else:
        os.kill(pid, sig)

def get_num_segments(primary=True, mirror=True, master=True, standby=True):
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
     
    primary_segments = [seg for seg in gparray.getDbList() if seg.isSegmentPrimary()] 
    mirror_segments = [seg for seg in gparray.getDbList() if seg.isSegmentMirror()]

    num_segments = 0
    if primary:
        num_segments += len(primary_segments)
    if mirror:
        num_segments += len(mirror_segments)
    if master and gparray.master is not None:
        num_segments += 1
    if standby and gparray.standbyMaster is not None:
        num_segments += 1  

    return num_segments

def check_user_permissions(file_name, access_mode):
    st = os.stat(file_name)
    if access_mode == 'write':
        return bool(st.st_mode & stat.S_IWUSR)
    elif access_mode == 'read':
        return bool(st.st_mode & stat.S_IRUSR)
    elif access_mode == 'execute':
        return bool(st.st_mode & stat.S_IXUSR)
    else:
        raise Exception('Invalid mode specified, should be read, write or execute only')

def get_change_tracking_segment_info():
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    for seg in gparray.getDbList():
        if seg.isSegmentModeInChangeLogging():
            return seg.getSegmentPort(), seg.getSegmentHostName()

def are_segments_running():
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    segments = gparray.getDbList()
    for seg in segments:
        if seg.status != 'u':
            return False 
    return True

def modify_sql_file(file, hostport):
    if os.path.isfile(file):
        for line in fileinput.FileInput(file,inplace=1):
            if line.find("gpfdist")>=0:
                line = re.sub('(\d+)\.(\d+)\.(\d+)\.(\d+)\:(\d+)',hostport, line)
            print str(re.sub('\n','',line))

def create_gpfilespace_config(host, port, user,fs_name, config_file, working_dir='/tmp'):
    mirror_hosts = []
    primary_hosts = []
    standby_host = ''
    master_host = ''
    fspath_master = working_dir + '/fs_master'
    fspath_standby = working_dir + '/fs_standby'
    fspath_primary = working_dir + '/fs_primary'
    fspath_mirror = working_dir + '/fs_mirror'
    get_master_filespace_entry = 'psql -t -h %s -p %s -U %s -d template1 -c \" select hostname, dbid, fselocation from pg_filespace_entry, gp_segment_configuration where dbid=fsedbid and preferred_role =\'p\' and content=-1;\"'%(host, port, user)
    (rc, out, err) = run_cmd(get_master_filespace_entry)
    if rc != 0:
        raise Exception('Exception from executing psql query: %s'% get_master_filespace_entry)
    else:
        file = open(config_file,'w')
        file.write('filespace:%s\n'%fs_name)
        result = out.split('\n')
        for line in result:
            if line.strip():
                row = line.split('|')
                row = [col.strip() for col in row]
                hostname = row[0]
                master_host = hostname
                dbid = row[1]
                fs_loc = os.path.join(fspath_master,os.path.split(row[2])[1])
                file.write(hostname+':'+dbid+':'+fs_loc)
                file.write('\n')
        file.close()

    get_standby_filespace_entry= 'psql -t -h %s -p %s -U %s -d template1 -c \"select hostname, dbid, fselocation from pg_filespace_entry, gp_segment_configuration where dbid=fsedbid and preferred_role =\'m\' and content=-1;\"'%(host, port, user)
    (rc, out, err) = run_cmd(get_standby_filespace_entry)
    if rc != 0:
        raise Exception('Exception from executing psql query: %s'% get_standby_filespace_entry)
    else:
        result = out.split('\n')
        file = open(config_file,'a')
        for line in result:
            if line.strip():
                row = line.strip().split('|')
                row = [col.strip() for col in row]
                hostname = row[0]
                standby_host= hostname
                dbid = row[1]
                fs_loc = os.path.join(fspath_standby,os.path.split(row[2])[1])
                file.write(hostname+':'+dbid+':'+fs_loc)
                file.write('\n')
        file.close()

    get_primary_filespace_entry= 'psql -t -h %s -p %s -U %s -d template1 -c \"select hostname, dbid, fselocation from pg_filespace_entry, gp_segment_configuration where dbid=fsedbid and preferred_role =\'p\' and content>-1;\"'%(host, port, user)
    (rc, out, err) = run_cmd(get_primary_filespace_entry)
    if rc != 0:
        raise Exception('Exception from executing psql query: %s'% get_primary_filespace_entry)
    else:
        result = out.split('\n')
        file = open(config_file,'a')
        for line in result:
            if line.strip():
                row = line.strip().split('|')
                row = [col.strip() for col in row] 
                hostname = row[0]
                primary_hosts.append(hostname)
                dbid = row[1]
                fs_loc = os.path.join(fspath_primary,os.path.split(row[2])[1])
                file.write(hostname+':'+dbid+':'+fs_loc)
                file.write('\n')
        file.close()

    get_mirror_filespace_entry= 'psql -t -h %s -p %s -U %s -d template1 -c \"select hostname, dbid, fselocation from pg_filespace_entry, gp_segment_configuration where dbid=fsedbid and preferred_role =\'m\' and content>-1;\"'%(host, port, user)
    (rc, out, err) = run_cmd(get_mirror_filespace_entry)
    if rc != 0:
        raise Exception('Exception from executing psql query: %s'% get_mirror_filespace_entry)
    else:
        result = out.split('\n')
        file = open(config_file,'a')
        for line in result:
            if line.strip():
                row = line.strip().split('|')
                row = [col.strip() for col in row] 
                hostname = row[0]
                mirror_hosts.append(hostname)
                dbid = row[1]
                fs_loc = os.path.join(fspath_mirror,os.path.split(row[2])[1])
                file.write(hostname+':'+dbid+':'+fs_loc)
                file.write('\n')
        file.close()

    for host in primary_hosts:
        remove_dir(host,fspath_primary)
        create_dir(host,fspath_primary)
    for host in mirror_hosts:
        remove_dir(host,fspath_mirror)
        create_dir(host,fspath_mirror)
    remove_dir(master_host,fspath_master)
    remove_dir(standby_host,fspath_standby)
    create_dir(master_host,fspath_master)
    create_dir(standby_host,fspath_standby)

def remove_dir(host, directory):
    cmd = 'gpssh -h %s -e \'rm -rf %s\''%(host, directory) 
    run_cmd(cmd)

def create_dir(host, directory):
    cmd = 'gpssh -h %s -e \'mkdir -p %s\''%(host, directory)
    run_cmd(cmd)

def wait_till_change_tracking_transition(host='localhost', port=os.environ.get('PGPORT'), user=os.environ.get('USER')):
    num_ct_nodes = 'psql -t -h %s -p %s -U %s -d template1 -c "select count(*) from gp_segment_configuration where mode =\'c\';"'%(host, port, user)
    (rc, out, err) = run_cmd(num_ct_nodes)
    if rc != 0:
        raise Exception('Exception from executing psql query: %s'%num_ct_nodes)
    else:
        num_cl = int(out.strip())
        count = 0
        while(num_cl == 0):
            time.sleep(30)
            (rc, out, err) = run_cmd(num_ct_nodes)
            num_cl = int(out.strip())
            count = count + 1
            if (count > 80):
                raise Exception("Timed out: cluster not in change tracking")
        return (True,num_cl)            


def wait_till_insync_transition(host='localhost', port=os.environ.get('PGPORT'), user=os.environ.get('USER')):
    num_unsync_nodes = 'psql -t -h %s -p %s -U %s -d template1 -c "select count(*) from gp_segment_configuration where mode <> \'s\' or status<> \'u\';"'%(host, port, user)
    (rc, out, err) = run_cmd(num_unsync_nodes)
    if rc != 0:
        raise Exception('Exception from executing psql query: %s'%num_unsync_nodes)
    else:
        num_unsync = int(out.strip())
        count = 0
        while(num_unsync > 0):
            time.sleep(30)
            (rc, out, err) = run_cmd(num_unsync_nodes)
            num_unsync = int(out.strip())
            count = count + 1
            if (count > 80): 
                raise Exception("Timed out: cluster not in sync transition")
        return True    

def wait_till_resync_transition(host='localhost', port=os.environ.get('PGPORT'), user=os.environ.get('USER')):
    num_resync_nodes = 'psql -t -h %s -p %s -U %s -d template1 -c "select count(*) from gp_segment_configuration where mode =\'r\';"'%(host, port, user)
    num_insync_nodes = 'psql -t -h %s -p %s -U %s -d template1 -c "select count(*) from gp_segment_configuration where mode <>\'s\';"'%(host, port, user)
    (rc1, out1, err1) = run_cmd(num_resync_nodes)
    (rc2, out2, err2) = run_cmd(num_insync_nodes)
    if rc1 !=0 or rc2 !=0:
        raise Exception('Exception from executing psql query: %s'%num_unsync_nodes)
    else:
        num_resync = int(out1.strip())
        num_insync = int(out2.strip())
        count = 0
        while(num_resync != num_insync):
            time.sleep(30)
            (rc1, out1, err1) = run_cmd(num_resync_nodes)
            (rc2, out2, err2) = run_cmd(num_insync_nodes)
            num_resync = int(out1.strip())
            num_insync = int(out2.strip())
            count = count + 1
            if (count > 80): 
                raise Exception("Timed out: cluster not in sync transition")
        return True 

def check_dump_dir_exists(context, dbname):
    hosts = get_hosts(dbname)
    for host in hosts:
	dirs = get_backup_dir_for_host(host[0], dbname)
	for dir in dirs:
	    cmd = "gpssh -h %s 'if [ -d %s/db_dumps/ ]; then echo 'EXISTS'; else echo 'NOT FOUND'; fi'" % (host[0], dir[0])
	    run_command(context, cmd)
	    if context.exception:
		raise context.exception
	    if 'EXISTS' in context.stdout_message:
		raise Exception("db_dumps directory is present in master/segments.")

def verify_restored_table_is_analyzed(context, table_name, dbname):
    ROW_COUNT_SQL = """SELECT count(*) FROM %s""" % table_name
    if table_name.find('.') != -1:
        table_name = table_name.split(".")[1]
    ROW_COUNT_PG_CLASS_SQL = """SELECT reltuples FROM pg_class WHERE relname = '%s'""" % (table_name)
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        curs = dbconn.execSQL(conn, ROW_COUNT_SQL)
        rows = curs.fetchall()
        curs = dbconn.execSQL(conn, ROW_COUNT_PG_CLASS_SQL)
        rows_from_pgclass = curs.fetchall()
    if rows == rows_from_pgclass:
        return True
    else:
        return False

def analyze_database(context, dbname):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, "analyze")

def delete_rows_from_table(context, dbname, table_name, column_name, info):
    DELETE_SQL = """DELETE FROM %s WHERE %s = %s""" % (table_name, column_name, info)
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, DELETE_SQL)
        conn.commit()

def validate_parse_email_file(context, email_file_path):
    if os.path.isfile(email_file_path) is False:
        raise Exception("\'%s\' file does not exist." % email_file_path)
    if email_file_path.split('.')[1] != "yaml":
        raise Exception("\'%s\' is not \'.yaml\' file. File containing email details should be \'.yaml\' file." % email_file_path)
    if (os.path.getsize(email_file_path) > 0) is False:
        raise Exception("\'%s\' file is empty." % email_file_path)
    email_key_list = ["DBNAME","FROM", "SUBJECT"]
    try:
        with open(email_file_path, 'r') as f:
            doc = yaml.load(f)
        context.email_details = doc['EMAIL_DETAILS']
        for email in context.email_details:
            for key in email.keys():
                if key not in email_key_list:
                    raise Exception(" %s not present" % key)
    except Exception as e:
        raise Exception("\'%s\' file is not formatted properly." % email_file_path)

def check_count_for_specific_query(dbname, query, nrows):
    NUM_ROWS_QUERY = '%s' % query
    # We want to bubble up the exception so that if table does not exist, the test fails
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        result = dbconn.execSQLForSingleton(conn, NUM_ROWS_QUERY)
    if result != nrows:
        raise Exception('%d rows in table %s.%s, expected row count = %d' % (result, dbname, tablename, nrows))
