import getpass
import glob
import os
import re
import thread
import time
import filecmp
import shutil
import signal
import socket
import subprocess
import commands
import sys
import tarfile
import platform
from datetime import datetime
import yaml
from collections import defaultdict
from gppylib.commands.base import Command, ExecutionError, REMOTE
from gppylib.commands.unix import findCmdInPath, RemoteCopy
from gppylib.commands.gp import SegmentStart, GpStandbyStart
from gppylib.db import dbconn
from gppylib.gparray import GpArray
from gppylib.operations.unix import ListRemoteFilesByPattern, CheckRemoteFile
from gppylib.operations.startSegments import MIRROR_MODE_MIRRORLESS
from gppylib.test.behave_utils.utils import bring_nic_down, bring_nic_up, run_cmd, get_table_names, get_segment_hostnames,check_schema_exists,\
                                            create_database, create_database_if_not_exists, run_command_remote, wait_till_change_tracking_transition, \
                                            wait_till_resync_transition, wait_till_insync_transition, create_gpfilespace_config,modify_sql_file, \
                                            match_table_select, check_empty_table, check_err_msg, check_stdout_msg, \
                                            check_return_code, create_int_table, create_partition, check_table_exists, create_large_num_partitions,\
                                            create_fake_pg_aoseg_table, drop_database, drop_database_if_exists, drop_table_if_exists, getRows,\
                                            get_hosts_and_datadirs, get_master_hostname, insert_row, start_database_if_not_started, stop_database_if_started,\
                                            run_gpcommand, check_db_exists, check_database_is_running, are_segments_synchronized, has_exception, modify_data, modify_partition_data, \
                                            validate_part_table_data_on_segments, validate_table_data_on_segments, validate_db_data, \
                                            get_partition_tablenames, check_partition_table_exists, create_indexes, get_partition_names, \
                                            validate_restore_data, backup_data, backup_db_data, cleanup_report_files, run_command, \
                                            get_distribution_policy, validate_distribution_policy, cleanup_backup_files, create_schema, drop_schema_if_exists, \
                                            create_mixed_storage_partition, create_external_partition, validate_mixed_partition_storage_types, validate_storage_type, truncate_table, \
                                            get_table_oid, verify_truncate_in_pg_stat_last_operation, verify_truncate_not_in_pg_stat_last_operation, insert_numbers, \
                                            populate_partition_diff_data_same_eof, populate_partition_same_data, execute_sql, verify_integer_tuple_counts,  validate_no_aoco_stats, \
                                            check_string_not_present_stdout, clear_all_saved_data_verify_files, copy_file_to_all_db_hosts, validate_num_restored_tables, \
                                            get_partition_list, verify_stats, drop_external_table_if_exists, get_all_hostnames_as_list, get_pid_for_segment, kill_process, get_num_segments, \
                                            check_user_permissions, get_change_tracking_segment_info, add_partition, drop_partition, check_pl_exists, check_constraint_exists, \
                                            are_segments_running, execute_sql_singleton, check_row_count, diff_backup_restore_data, check_dump_dir_exists, verify_restored_table_is_analyzed, \
                                            analyze_database, delete_rows_from_table, check_count_for_specific_query

from gppylib.operations.dump import get_partition_state
from gppylib.test.behave_utils.gpfdist_utils.gpfdist_mgmt import Gpfdist

master_data_dir = os.environ.get('MASTER_DATA_DIRECTORY')
if master_data_dir is None:
    raise Exception('Please set MASTER_DATA_DIRECTORY in environment')

@given('the database is running')
def impl(context): 
    start_database_if_not_started(context)
    if has_exception(context):
        raise context.exception

@given('the database is not running')
@when('the database is not running')
def impl(context): 
    stop_database_if_started(context)
    if has_exception(context):
        raise context.exception

@given('the database is "{version}" with dburl "{dbconn}"')
def impl(context, dbconn, version):
    command = '%s -t -q -c \'select version();\''%(dbconn)
    (rc, out, err) = run_cmd(command)
    if not ('Greenplum Database '+version) in out:
        print 'version %s does not match current gpdb version %s'%(version, out)  

@given('database "{dbname}" exists')
@then('database "{dbname}" exists')
def impl(context, dbname):
    create_database(context, dbname)

@given('database "{dbname}" is created if not exists')
@then('database "{dbname}" is created if not exists')
def impl(context, dbname):
    create_database_if_not_exists(context, dbname)

@when('the database "{dbname}" does not exist')
@given('the database "{dbname}" does not exist')
@then('the database "{dbname}" does not exist')
def impl(context, dbname):
    drop_database_if_exists(context, dbname)

@given('the database "{dbname}" does not exist with connection "{dbconn}"')
@when('the database "{dbname}" does not exist with connection "{dbconn}"')
@then('the database "{dbname}" does not exist with connection "{dbconn}"')
def impl(context, dbname, dbconn): 
    command = '%s -c \'drop database if exists %s;\''%(dbconn, dbname)
    run_command(context, command)

def get_segment_hostlist():
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    segment_hostlist = sorted(gparray.get_hostlist(includeMaster=False)) 
    if not segment_hostlist:
        raise Exception('segment_hostlist was empty')
    return segment_hostlist

@given('we have determined the first segment hostname')
def impl(context):
    segment_hostlist = get_segment_hostlist()    
    context.first_segment_hostname = segment_hostlist[0] 

@given('{nic} on the first segment host is {nic_status}')
@then('{nic} on the first segment host is {nic_status}')
def impl(context, nic, nic_status):
    if nic_status.strip() == 'down': 
        bring_nic_down(context.first_segment_hostname, nic)
    elif nic_status.strip() == 'up':
        bring_nic_up(context.first_segment_hostname, nic)
    else:
        raise Exception('Invalid nic status in feature file %s' % nic_status)

@when('an insert on "{table}" in "{dbname}" is rolled back')
def impl(context, table, dbname):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        insert_sql = """INSERT INTO %s values (1)""" % table
        dbconn.execSQL(conn, insert_sql)
        conn.rollback()

@when('a truncate on "{table}" in "{dbname}" is rolled back')
def impl(context, table, dbname):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        insert_sql = """TRUNCATE table %s""" % table
        dbconn.execSQL(conn, insert_sql)
        conn.rollback()

@when('an alter on "{table}" in "{dbname}" is rolled back')
def impl(context, table, dbname):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        insert_sql = """ALTER TABLE %s add column cnew int default 0""" % table
        dbconn.execSQL(conn, insert_sql)
        conn.rollback()
 
    
@when('table "{table_name}" is deleted in "{dbname}"')
def impl(context, table_name, dbname):
    drop_table_if_exists(context, table_name=table_name, dbname=dbname)

@given('the user truncates "{table_list}" tables in "{dbname}"')
@when('the user truncates "{table_list}" tables in "{dbname}"')
@then('the user truncates "{table_list}" tables in "{dbname}"')
def impl(context, table_list, dbname):
    if not table_list:
        raise Exception('Table list is empty')
    tables = table_list.split(',') 
    for t in tables:
        truncate_table(dbname, t.strip()) 

def populate_regular_table_data(context, tabletype, table_name, compression_type, dbname, rowcount=1094):
    create_database_if_not_exists(context, dbname)
    drop_table_if_exists(context, table_name=table_name, dbname=dbname)
    if compression_type == "None":
        create_partition(context, table_name, tabletype, dbname, compression_type=None, partition=False, rowcount=rowcount)
    else:
        create_partition(context, table_name, tabletype, dbname, compression_type, partition=False, rowcount=rowcount)

@given('there is a "{tabletype}" table "{table_name}" with compression "{compression_type}" in "{dbname}" with data')
@when('there is a "{tabletype}" table "{table_name}" with compression "{compression_type}" in "{dbname}" with data')
@then('there is a "{tabletype}" table "{table_name}" with compression "{compression_type}" in "{dbname}" with data')
def impl(context, tabletype, table_name, compression_type, dbname):
    populate_regular_table_data(context, tabletype, table_name, compression_type, dbname)

@when('the partition table "{table_name}" in "{dbname}" is populated with similar data')
def impl(context, table_name, dbname):
    populate_partition_diff_data_same_eof(table_name, dbname)

@given('the partition table "{table_name}" in "{dbname}" is populated with same data')
def impl(context, table_name, dbname):
    populate_partition_same_data(table_name, dbname)

@given('there is a "{tabletype}" table "{table_name}" with index "{indexname}" compression "{compression_type}" in "{dbname}" with data')
def impl(context, tabletype, table_name, compression_type, indexname, dbname):
    create_database_if_not_exists(context, dbname)
    drop_table_if_exists(context, table_name=table_name, dbname=dbname)
    if compression_type == "None":
        create_partition(context, table_name, tabletype, dbname, compression_type=None, partition=False)
    else:
        create_partition(context, table_name, tabletype, dbname, compression_type, partition=False)
    create_indexes(context, table_name, indexname, dbname)

@given('there is a "{tabletype}" partition table "{table_name}" with compression "{compression_type}" in "{dbname}" with data')
def impl(context, tabletype, table_name, compression_type, dbname):
    create_database_if_not_exists(context, dbname)
    drop_table_if_exists(context, table_name=table_name, dbname=dbname)
    if compression_type == "None":
        create_partition(context, table_name, tabletype, dbname)
    else:
        create_partition(context, table_name, tabletype, dbname, compression_type)

@given('there is a mixed storage partition table "{tablename}" in "{dbname}" with data')
def impl(context, tablename, dbname):
    create_database_if_not_exists(context, dbname)
    drop_table_if_exists(context, table_name=tablename, dbname=dbname)
    create_mixed_storage_partition(context, tablename, dbname)

@given('there is a partition table "{tablename}" has external partitions of gpfdist with file "{filename}" on port "{port}" in "{dbname}" with data')
def impl(context, tablename, dbname, filename, port):
    create_database_if_not_exists(context, dbname)
    drop_table_if_exists(context, table_name=tablename, dbname=dbname)
    create_external_partition(context, tablename, dbname, port, filename)
 
@given('there is {table_type} table {table_name} in "{dbname}" with data')
def impl(context, table_type, table_name, dbname):
    create_database_if_not_exists(context, dbname)
    drop_table_if_exists(context, table_name=table_name, dbname=dbname)
    create_int_table(context, table_type=table_type, table_name=table_name, dbname=dbname)

@given('"{dbname}" does not exist')
def impl(context, dbname):
    drop_database(context, dbname)

@given('{env_var} environment variable is not set')
def impl(context, env_var):
    if not hasattr(context, 'orig_env'):
        context.orig_env = dict()
    context.orig_env[env_var] = os.environ.get(env_var)

    if env_var in os.environ:
        del os.environ[env_var]

@given('there are no "{tmp_file_prefix}" tempfiles')
def impl(context, tmp_file_prefix):
    if tmp_file_prefix is not None and tmp_file_prefix:
        run_command(context, 'rm -f /tmp/%s*' % tmp_file_prefix)
    else:
        raise Exception('Invalid call to temp file removal %s' % tmp_file_prefix)
    
@then('{env_var} environment variable should be restored')
def impl(context, env_var):
    if not hasattr(context, 'orig_env'):
        raise Exception('%s can not be reset' % env_var)

    if env_var not in context.orig_env:
        raise Exception('%s can not be reset.' % env_var)

    os.environ[env_var] = context.orig_env[env_var]

    del context.orig_env[env_var]

@when('the table names in "{dbname}" is stored')
def impl(context, dbname):
    context.table_names = get_table_names(dbname)
   
@given('the user runs "{command}"') 
@when('the user runs "{command}"')
@then('the user runs "{command}"')
def impl(context, command):
    run_gpcommand(context, command)

@given('the user runs command "{command}"') 
@when('the user runs command "{command}"') 
@then('the user runs command "{command}"')
def impl(context, command):                 
    run_command(context, command) 

@given('the user puts cluster on "{HOST}" "{PORT}" "{USER}" in "{transition}"') 
@when('the user puts cluster on "{HOST}" "{PORT}" "{USER}" in "{transition}"') 
@then('the user puts cluster on "{HOST}" "{PORT}" "{USER}" in "{transition}"')
def impl(context, HOST, PORT, USER, transition):
    host = os.environ.get(HOST)
    user = os.environ.get(USER)
    port = os.environ.get(PORT)
    source_file = os.path.join(os.environ.get('GPHOME'),'greenplum_path.sh') 
    master_dd = os.environ.get('MASTER_DATA_DIRECTORY')
    export_mdd = 'export MASTER_DATA_DIRECTORY=%s;export PGPORT=%s'%(master_dd, port)
    # reset all fault inject entry if exists
    command = 'gpfaultinjector -f all -m async -y reset -r primary -H ALL'
    run_command_remote(context, command, host, source_file, export_mdd)
    command = 'gpfaultinjector -f all -m async -y resume -r primary -H ALL'
    run_command_remote(context, command, host, source_file, export_mdd)
    trigger_transition = "psql -d template1 -h %s -U %s -p %s -c 'drop table if exists trigger;'"%(host,user,port)
    if transition == 'ct':
        command = 'gpfaultinjector -f filerep_consumer -m async -y fault -r primary -H ALL'
        run_command_remote(context, command, host, source_file, export_mdd)
        run_command(context, trigger_transition)
        wait_till_change_tracking_transition(host,port,user)
    if transition == 'resync':
        command = 'gpfaultinjector -f filerep_consumer -m async -y fault -r primary -H ALL'
        run_command_remote(context,command, host, source_file, export_mdd)
        run_command(context, trigger_transition)
        wait_till_change_tracking_transition(host,port,user)
        command = 'gpfaultinjector -f filerep_resync -m async -y suspend -r primary -H ALL'
        run_command_remote(context, command, host, source_file, export_mdd)
        run_command_remote(context, 'gprecoverseg -a', host, source_file, export_mdd)
        wait_till_resync_transition(host,port,user)
    if transition == 'sync':
        run_command_remote(context, 'gpstop -air', host, source_file, export_mdd)
        run_command_remote(context, 'gprecoverseg -a', host, source_file, export_mdd)
        wait_till_insync_transition(host,port,user)
        run_command_remote(context, 'gprecoverseg -ar', host, source_file, export_mdd)  


@given('the user runs workload under "{dir}" with connection "{dbconn}"')
@when('the user runs workload under "{dir}" with connection "{dbconn}"')
def impl(context, dir, dbconn): 
    for file in os.listdir(dir):
        if file.endswith('.sql'):
            command = '%s -f %s'%(dbconn, os.path.join(dir,file))
            run_command(context, command)

@given('the user "{USER}" creates filespace_config file for "{fs_name}" on host "{HOST}" with gpdb port "{PORT}" and config "{config_file}" in "{dir}" directory')
@then('the user "{USER}" creates filespace_config file for "{fs_name}" on host "{HOST}" with gpdb port "{PORT}" and config "{config_file}" in "{dir}" directory')
def impl(context, USER, HOST, PORT,fs_name,config_file,dir):
    user = os.environ.get(USER)
    host = os.environ.get(HOST)
    port = os.environ.get(PORT)
    if not dir.startswith("/"):
        dir = os.environ.get(dir)
    config_file_path = dir + "/" + config_file
    create_gpfilespace_config(host,port, user, fs_name, config_file_path, dir)

@given('the user "{USER}" creates filespace on host "{HOST}" with gpdb port "{PORT}" and config "{config_file}" in "{dir}" directory')
@when('the user "{USER}" creates filespace on host "{HOST}" with gpdb port "{PORT}" and config "{config_file}" in "{dir}" directory')
def impl(context, USER, HOST, PORT, config_file, dir):
    user = os.environ.get(USER)
    host = os.environ.get(HOST)
    port = os.environ.get(PORT)
    if not dir.startswith("/"):
        dir = os.environ.get(dir)
    config_file_path = dir + "/" + config_file
    cmdStr = 'gpfilespace -h %s -p %s -U %s -c "%s"'%(host, port, user, config_file_path)
    run_command(context, cmdStr)


@given('the user modifies the external_table.sql file "{filepath}" with host "{HOST}" and port "{port}"')
@when('the user modifies the external_table.sql file "{filepath}" with host "{HOST}" and port "{port}"')
def impl(context, filepath, HOST, port): 
    host=os.environ.get(HOST)
    substr = host+':'+port
    modify_sql_file(filepath, substr)

@given('the user starts the gpfdist on host "{HOST}" and port "{port}" in work directory "{dir}" from remote "{ctxt}"')
@then('the user starts the gpfdist on host "{HOST}" and port "{port}" in work directory "{dir}" from remote "{ctxt}"')
def impl(context, HOST, port, dir, ctxt):
    host = os.environ.get(HOST)
    remote_gphome = os.environ.get('GPHOME')
    if not dir.startswith("/"):
        dir = os.environ.get(dir)
    gp_source_file = os.path.join(remote_gphome, 'greenplum_path.sh')
    gpfdist = Gpfdist('gpfdist on host %s'%host, dir, port, os.path.join(dir,'gpfdist.pid'), int(ctxt), host, gp_source_file)
    gpfdist.startGpfdist()


@given('the user stops the gpfdist on host "{HOST}" and port "{port}" in work directory "{dir}" from remote "{ctxt}"')
@then('the user stops the gpfdist on host "{HOST}" and port "{port}" in work directory "{dir}" from remote "{ctxt}"')
def impl(context, HOST, port, dir, ctxt):
    host = os.environ.get(HOST)
    remote_gphome = os.environ.get('GPHOME')
    if not dir.startswith("/"):
        dir = os.environ.get(dir)
    gp_source_file = os.path.join(remote_gphome, 'greenplum_path.sh')
    gpfdist = Gpfdist('gpfdist on host %s'%host, dir, port, os.path.join(dir,'gpfdist.pid'), int(ctxt), host, gp_source_file)
    gpfdist.cleanupGpfdist()

def parse_netbackup_params():
    current_path = os.path.realpath(__file__)
    current_dir = os.path.dirname(current_path)
    netbackup_yaml_file_path = os.path.join(current_dir, 'data/netbackup_behave_config.yaml')
    try:
        nbufile = open(netbackup_yaml_file_path, 'r')
    except IOError,e:
        raise Exception("Unable to open file %s: %s" % (netbackup_yaml_file_path, e))
    try:
        nbudata = yaml.load(nbufile.read())
    except yaml.YAMLError, exc:
        raise Exception("Error reading file %s: %s" % (netbackup_yaml_file_path, exc))
    finally:
        nbufile.close()

    if len(nbudata) == 0:
        raise Exception("The load of the config file %s failed.\
         No configuration information to continue testing operation." % netbackup_yaml_file_path)
    else:
        return nbudata

@given('the netbackup params have been parsed')
def impl(context):
    NETBACKUPDICT = defaultdict(dict)
    NETBACKUPDICT['NETBACKUPINFO'] = parse_netbackup_params()
    context.netbackup_service_host = NETBACKUPDICT['NETBACKUPINFO']['NETBACKUP_PARAMS']['NETBACKUP_SERVICE_HOST']
    context.netbackup_policy = NETBACKUPDICT['NETBACKUPINFO']['NETBACKUP_PARAMS']['NETBACKUP_POLICY']
    context.netbackup_schedule = NETBACKUPDICT['NETBACKUPINFO']['NETBACKUP_PARAMS']['NETBACKUP_SCHEDULE']

def run_valgrind_command(context, command, suppressions_file):
    current_path = os.path.realpath(__file__)
    current_dir = os.path.dirname(current_path)
    cmd_text = "valgrind --suppressions=%s/%s %s" %(current_dir, suppressions_file, command)
    run_command(context, cmd_text)
    for line in context.error_message.splitlines():
        if 'ERROR SUMMARY' in line:
            if '0 errors from 0 contexts' not in line: 
                raise Exception('Output: %s' % context.error_message)
            else:
                return
    raise Exception('Could not find "ERROR SUMMARY" in %s' % context.error_message)

@then('the user runs valgrind with "{command}" and options "{options}"')
@when('the user runs valgrind with "{command}" and options "{options}"')
def impl(context, command, options):
    port = os.environ.get('PGPORT')
    user = getpass.getuser()
    if hasattr(context, 'backup_timestamp'):
        ts = context.backup_timestamp 
    bnr_tool = command.split()[0].strip()
    if bnr_tool == 'gp_dump':
        command_str = command
    elif bnr_tool == 'gp_dump_agent':
        command_str = command + ' -p %s' % port
    elif bnr_tool == 'gp_restore':
        command_str = "%s %s --gp-k %s --gp-d db_dumps/%s --gp-r db_dumps/%s" % (command, options, context.backup_timestamp, context.backup_timestamp[0:8], context.backup_timestamp[0:8])
    elif bnr_tool == 'gp_restore_agent':
        command_str = "%s %s --gp-k %s --gp-d db_dumps/%s -p %s -U %s --target-host localhost --target-port %s db_dumps/%s/gp_dump_1_1_%s_post_data.gz" % (command, options, ts, ts[0:8], port, user, port, ts[0:8], ts) 
        
    run_valgrind_command(context, command_str, "valgrind_suppression.txt")

@then('the user runs valgrind with "{command}" and options "{options}" and suppressions file "{suppressions_file}" using netbackup')
@when('the user runs valgrind with "{command}" and options "{options}" and suppressions file "{suppressions_file}" using netbackup')
def impl(context, command, options, suppressions_file):
    port = os.environ.get('PGPORT')
    user = getpass.getuser()
    if hasattr(context, 'backup_timestamp'):
        ts = context.backup_timestamp
    if hasattr(context, 'netbackup_service_host'):
        netbackup_service_host = context.netbackup_service_host
    if hasattr(context, 'netbackup_policy'):
        netbackup_policy = context.netbackup_policy
    if hasattr(context, 'netbackup_schedule'):
        netbackup_schedule = context.netbackup_schedule
    bnr_tool = command.split()[0].strip()
    if bnr_tool == 'gp_dump':
        command_str = command + " --netbackup-service-host " + netbackup_service_host + " --netbackup-policy " + netbackup_policy + " --netbackup-schedule " + netbackup_schedule
    elif bnr_tool == 'gp_dump_agent':
        command_str = command + ' -p %s' % port + " --netbackup-service-host " + netbackup_service_host + " --netbackup-policy " + netbackup_policy + " --netbackup-schedule " + netbackup_schedule
    elif bnr_tool == 'gp_restore':
        command_str = "%s %s --gp-k %s --gp-d db_dumps/%s --gp-r db_dumps/%s --netbackup-service-host %s" % (command, options, context.backup_timestamp, context.backup_timestamp[0:8], context.backup_timestamp[0:8], netbackup_service_host)
    elif bnr_tool == 'gp_restore_agent':
        command_str = "%s %s --gp-k %s --gp-d db_dumps/%s -p %s -U %s --target-host localhost --target-port %s db_dumps/%s/gp_dump_1_1_%s_post_data.gz --netbackup-service-host %s" % (command, options, ts, ts[0:8], port, user, port, ts[0:8], ts, netbackup_service_host)
    else:
        command_str = "%s %s" % (command, options)

    run_valgrind_command(context, command_str, "netbackup_suppressions.txt")
 
@when('the timestamp key is stored')
def impl(context):
    stdout = context.stdout_message
    for line in stdout.splitlines():
        if '--gp-k' in line:
            pat = re.compile('.* --gp-k=([0-9]{14}).*')
            m = pat.search(line) 
            if not m:
                raise Exception('Timestamp key not found')
            context.timestamp_key = m.group(1)
            return

@then('{command} should print {err_msg} error message')
def impl(context, command, err_msg):
    check_err_msg(context, err_msg)

@then('{command} should print {out_msg} to stdout')
def impl(context, command, out_msg):
    check_stdout_msg(context, out_msg)

@given('{command} should not print {out_msg} to stdout')
@when('{command} should not print {out_msg} to stdout')
@then('{command} should not print {out_msg} to stdout')
def impl(context, command, out_msg):
    check_string_not_present_stdout(context, out_msg)

@given('{command} should return a return code of {ret_code}')
@when('{command} should return a return code of {ret_code}')
@then('{command} should return a return code of {ret_code}')
def impl(context, command, ret_code):
    check_return_code(context, ret_code)

@then('an "{ex_type}" should be raised')
def impl(context, ex_type):
    if not context.exception:
        raise Exception('An exception was expected but was not thrown')
    typ = context.exception.__class__.__name__
    if typ != ex_type:
        raise Exception('got exception of type "%s" but expected type "%s"' % (typ, ex_type))

@given('database "{dbname}" health check should pass on table "{tablename}"')
@when('database "{dbname}" health check should pass on table "{tablename}"')
@then('database "{dbname}" health check should pass on table "{tablename}"')
def impl(context, dbname, tablename):
    
    drop_database_if_exists(context, dbname)
    create_database(context, dbname)
    create_int_table(context, tablename, dbname=dbname)
    drop_database(context, dbname)

    
@given('the segments are synchronized')
@when('the segments are synchronized')
@then('the segments are synchronized')
def impl(context):

    times = 30
    sleeptime = 10

    for i in range(times):
        if are_segments_synchronized():
            return
        time.sleep(sleeptime)

    raise Exception('segments are not in sync after %d seconds' % (times * sleeptime))

@when('table "{table_list}" is assumed to be in dirty state in "{dbname}"')
@then('table "{table_list}" is assumed to be in dirty state in "{dbname}"')
@given('table "{table_list}" is assumed to be in dirty state in "{dbname}"')
def impl(context, table_list, dbname):
    tables = table_list.split(',')
    for t in tables:
        modify_data(context, t.strip(), dbname)
        backup_data(context, t.strip(), dbname)

    get_distribution_policy(dbname)

@given('all the data from "{dbname}" is saved for verification')
@when('all the data from "{dbname}" is saved for verification')
@then('all the data from "{dbname}" is saved for verification')
def impl(context, dbname):
    backup_db_data(context, dbname)
    
@then('partition "{partition}" of partition table "{table_list}" is assumed to be in dirty state in "{dbname}" in schema "{schema}"')
@when('partition "{partition}" of partition table "{table_list}" is assumed to be in dirty state in "{dbname}" in schema "{schema}"')
@given('partition "{partition}" of partition table "{table_list}" is assumed to be in dirty state in "{dbname}" in schema "{schema}"')
@then('partition "{partition}" in partition level "{partitionlevel}" of partition table "{table_list}" is assumed to be in dirty state in "{dbname}" in schema "{schema}"')
@when('partition "{partition}" in partition level "{partitionlevel}" of partition table "{table_list}" is assumed to be in dirty state in "{dbname}" in schema "{schema}"')
@given('partition "{partition}" in partition level "{partitionlevel}" of partition table "{table_list}" is assumed to be in dirty state in "{dbname}" in schema "{schema}"')
def impl(context, partition, table_list, dbname, schema, partitionlevel=1):
    tables = table_list.split(',')
    for t in tables:
        part_t = get_partition_names(schema, t.strip(), dbname, partitionlevel, partition)
        if len(part_t) < 1 or len(part_t[0]) < 1:
            print part_t 
        dirty_table_name = part_t[0][0].strip()
        modify_partition_data(context, dirty_table_name, dbname, int(partition))
        backup_data(context, dirty_table_name, dbname)

def validate_timestamp(ts):
    try:
        int_ts = int(ts)
    except Exception as e:
        raise Exception('Timestamp is not valid %s' % ts)

    if len(ts) != 14:
        raise Exception('Timestamp is invalid %s' % ts)

@when('the subdir from gpcrondump is stored')    
@then('the subdir from gpcrondump is stored')
def impl(context):
    stdout = context.stdout_message
    for line in stdout.splitlines():
        if 'Dump subdirectory' in line:
            log_msg, delim, subdir = line.partition('=') 
            context.backup_subdir = subdir.strip()
            return 

    raise Exception('Dump subdirectory not found %s' % stdout)

def get_timestamp_from_output(context):
    ts = None
    stdout = context.stdout_message
    for line in stdout.splitlines():
        if 'Timestamp key = ' in line:
            log_msg, delim, timestamp = line.partition('=') 
            ts = timestamp.strip()
            validate_timestamp(ts)
            return ts

    raise Exception('Timestamp not found %s' % stdout)
 

@given('the full backup timestamp from gpcrondump is stored')
@when('the full backup timestamp from gpcrondump is stored')
@then('the full backup timestamp from gpcrondump is stored')
def impl(context):
    context.full_backup_timestamp = get_timestamp_from_output(context)

@when('the timestamp from gpcrondump is stored')    
@then('the timestamp from gpcrondump is stored')
def impl(context):
    context.backup_timestamp = get_timestamp_from_output(context)

@when('the timestamp is labeled "{lbl}"')
def impl(context, lbl):
    if not hasattr(context, 'timestamp_labels'):
        context.timestamp_labels = {}

    context.timestamp_labels[lbl] = get_timestamp_from_output(context)

@given('there is a list to store the incremental backup timestamps')
def impl(context):
    context.inc_backup_timestamps = []

@given('there is a list to store the backup timestamps')
def impl(context):
    context.backup_timestamp_list = []

@then('the timestamp from gpcrondump is stored in a list')
@when('the timestamp from gpcrondump is stored in a list')
def impl(context):
    context.backup_timestamp = get_timestamp_from_output(context)
    context.inc_backup_timestamps.append(context.backup_timestamp)


@then('Verify data integrity of database "{dbname}" between source and destination system, work-dir "{dir}"')
def impl(context, dbname, dir):
    dbconn_src = 'psql -p $GPTRANSFER_SOURCE_PORT -h $GPTRANSFER_SOURCE_HOST -U $GPTRANSFER_SOURCE_USER -d %s'%dbname
    dbconn_dest = 'psql -p $GPTRANSFER_DEST_PORT -h $GPTRANSFER_DEST_HOST -U $GPTRANSFER_DEST_USER -d %s'%dbname
    for file in os.listdir(dir):
        if file.endswith('.sql'):
            filename_prefix = os.path.splitext(file)[0]
            ans_file_path = os.path.join(dir,filename_prefix+'.ans')
            out_file_path = os.path.join(dir,filename_prefix+'.out')
            diff_file_path = os.path.join(dir,filename_prefix+'.diff')
            # run the command to get the exact data from the source system
            command = '%s -f %s > %s'%(dbconn_src, os.path.join(dir,file), ans_file_path)
            run_command(context, command)

            # run the command to get the data from the destination system, locally
            command = '%s -f %s > %s'%(dbconn_dest, os.path.join(dir,file), out_file_path)
            run_command(context, command)
            
            gpdiff_cmd = 'gpdiff.pl -w  -I NOTICE: -I HINT: -I CONTEXT: -I GP_IGNORE: --gp_init_file=gppylib/test/behave/mgmt_utils/steps/data/global_init_file %s %s > %s'%(ans_file_path, out_file_path, diff_file_path)             
            run_command(context, gpdiff_cmd)
    for file in os.listdir(dir):
        if file.endswith('.diff') and os.path.getsize(os.path.join(dir,file)) > 0: 
            # if there is some difference generated into the diff file, raise expception
                raise Exception ("Found difference between source and destination system, see %s"%file)


@then('run post verifying workload under "{dir}"')
def impl(context, dir):
    for file in os.listdir(dir):
        if file.endswith('.sql'):
            filename_prefix = os.path.splitext(file)[0]
            ans_file_path = os.path.join(dir,filename_prefix+'.ans')
            out_file_path = os.path.join(dir,filename_prefix+'.out')
            diff_file_path = os.path.join(dir,filename_prefix+'.diff')

            # run the command to get the data from the destination system, locally
            dbconn = 'psql -d template1 -p $GPTRANSFER_DEST_PORT -U $GPTRANSFER_DEST_USER -h $GPTRANSFER_DEST_HOST'
            command = '%s -f %s > %s'%(dbconn, os.path.join(dir,file), out_file_path)
            run_command(context, command)
     
            gpdiff_cmd = 'gpdiff.pl -w  -I NOTICE: -I HINT: -I CONTEXT: -I GP_IGNORE: --gp_init_file=gppylib/test/behave/mgmt_utils/steps/data/global_init_file %s %s > %s'%(ans_file_path, out_file_path, diff_file_path)          
            run_command(context, gpdiff_cmd)
    for file in os.listdir(dir):
        if file.endswith('.diff') and os.path.getsize(os.path.join(dir,file)) > 0:
            # if there is some difference generated into the diff file, raise expception
                raise Exception ("Found difference between source and destination system, see %s"%file) 

@then('the timestamp from gpcrondump is stored in the backup timestamp list')
def impl(context):
    context.backup_timestamp = get_timestamp_from_output(context)
    context.backup_timestamp_list.append(context.backup_timestamp)

@then('verify that the incremental file has the stored timestamp')
def impl(context):

    inc_file_name = 'gp_dump_%s_increments' % context.full_backup_timestamp
    subdirectory = context.full_backup_timestamp[0:8]
    full_path = os.path.join(master_data_dir, 'db_dumps', subdirectory, inc_file_name) 

    if not os.path.isfile(full_path):
        raise Exception ("Can not find increments file: %s" % full_path)

    contents = ""
    with open(full_path) as fd: 
        contents = fd.read().strip()

    if context.backup_timestamp != contents:
        raise Exception("The increments file '%s' does not contain the timestamp %s" % (full_path, context.backup_timestamp))

def check_increments_file_for_list(context, location):
    inc_file_name = 'gp_dump_%s_increments' % context.full_backup_timestamp
    subdirectory = context.full_backup_timestamp[0:8]
    full_path = os.path.join(location, 'db_dumps', subdirectory, inc_file_name) 

    if not os.path.isfile(full_path):
        raise Exception ("Can not find increments file: %s" % full_path)

    found_timestamps = []
    contents = ""
    with open(full_path) as fd: 
        contents = fd.read()
        for line in contents.splitlines():
            line = line.strip()
            if not line:
                continue
            found_timestamps.append(line)

    found_timestamps = sorted(found_timestamps)
    context.inc_backup_timestamps = sorted(context.inc_backup_timestamps)

    if found_timestamps != context.inc_backup_timestamps:
        print "Found timestamps: "
        print found_timestamps
        print "Expected timestamps: "
        print context.inc_backup_timestamps
        raise Exception("Expected timestamps not found")

@then('verify that the incremental file in "{location}" has all the stored timestamps')
def impl(context, location):
    check_increments_file_for_list(context, location)

@then('verify that the incremental file has all the stored timestamps')
def impl(context):
    check_increments_file_for_list(context, master_data_dir)

@then('verify that the plan file is created for the latest timestamp')
def impl(context):
    context.inc_backup_timestamps = sorted(context.inc_backup_timestamps)
    latest_ts = context.inc_backup_timestamps[-1]
    plan_file_dir = os.path.join(master_data_dir, 'db_dumps', latest_ts[0:8])
    plan_file_count =  len(glob.glob('/%s/*_plan' % plan_file_dir)) 
    if plan_file_count != 1: 
        raise Exception('Expected only one plan file, found %s' % plan_file_count)
    filename = '%s/gp_restore_%s_plan' % (plan_file_dir, latest_ts)
    if not os.path.exists(filename):
        raise Exception('Plan file %s not created for the latest timestamp' % filename)

@then('the timestamp from gp_dump is stored and subdir is "{subdir}"')
def impl(context, subdir):
    stdout = context.stdout_message
    context.backup_subdir = subdir
    for line in stdout.splitlines():
        if 'Timestamp Key: ' in line:
            context.backup_timestamp = line.split()[-1] 
            validate_timestamp(context.backup_timestamp)
            return 

    raise Exception('Timestamp not found %s' % stdout)
   
@when('the state files are generated under "{dir}" for stored "{backup_type}" timestamp') 
@then('the state files are generated under "{dir}" for stored "{backup_type}" timestamp') 
def impl(context, dir, backup_type):
    dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    if backup_type == 'full':
        timestamp = context.full_backup_timestamp
    else:
        timestamp = context.backup_timestamp

    ao_state_filename = "%s/db_dumps/%s/gp_dump_%s_ao_state_file" % (dump_dir, timestamp[0:8], timestamp)
    co_state_filename = "%s/db_dumps/%s/gp_dump_%s_co_state_file" % (dump_dir, timestamp[0:8], timestamp)

    if not os.path.exists(ao_state_filename):
        raise Exception('AO state file %s not generated' % ao_state_filename)
    if not os.path.exists(co_state_filename):
        raise Exception('CO state file %s not generated' % co_state_filename)

    verify_integer_tuple_counts(context, ao_state_filename)
    verify_integer_tuple_counts(context, co_state_filename)

@then('the "{file_type}" files are generated under "{dirname}" for stored "{backup_type}" timestamp')
def impl(context, file_type, dirname, backup_type):
    dump_dir = dirname if len(dirname.strip()) != 0 else master_data_dir
    if backup_type == 'full':
        timestamp = context.full_backup_timestamp
    else:
        timestamp = context.backup_timestamp
    last_operation_filename = "%s/db_dumps/%s/gp_dump_%s_last_operation" % (dump_dir, timestamp[0:8], timestamp) 
    if not os.path.exists(last_operation_filename):
        raise Exception('Last operation file %s not generated' % last_operation_filename)

@given('the user runs gp_restore with the the stored timestamp subdir and stored filename in "{dbname}"')
@when('the user runs gp_restore with the the stored timestamp subdir and stored filename in "{dbname}"')
def impl(context, dbname):
    command = 'gp_restore -i --gp-k %s --gp-d db_dumps/%s --gp-i --gp-r db_dumps/%s --gp-l=p -d %s --gp-c --gp-f %s' % (context.backup_timestamp, context.backup_subdir, context.backup_subdir, dbname, context.filename)
    run_gpcommand(context, command)

@then('the user runs gp_restore with the the stored timestamp and subdir in "{dbname}"')
def impl(context, dbname):
    command = 'gp_restore -i --gp-k %s --gp-d db_dumps/%s --gp-i --gp-r db_dumps/%s --gp-l=p -d %s --gp-c' % (context.backup_timestamp, context.backup_subdir, context.backup_subdir, dbname)
    run_gpcommand(context, command)

@then('the user runs gp_restore with the the stored timestamp and subdir in "{dbname}" and bypasses ao stats')
def impl(context, dbname):
    command = 'gp_restore -i --gp-k %s --gp-d db_dumps/%s --gp-i --gp-r db_dumps/%s --gp-l=p -d %s --gp-c --gp-nostats' % (context.backup_timestamp, context.backup_subdir, context.backup_subdir, dbname)
    run_gpcommand(context, command)

@then('the user runs gp_restore with the stored timestamp and subdir in "{dbname}" and backup_dir "{backup_dir}"')
def impl(context, dbname, backup_dir):
    command = 'gp_restore -i --gp-k %s --gp-d %s/db_dumps/%s --gp-i --gp-r %s/db_dumps/%s --gp-l=p -d %s --gp-c' % (context.backup_timestamp, backup_dir, context.backup_subdir, backup_dir, context.backup_subdir, dbname)
    run_gpcommand(context, command)

@when('the user runs gp_restore with the the stored timestamp and subdir for metadata only in "{dbname}"')
@then('the user runs gp_restore with the the stored timestamp and subdir for metadata only in "{dbname}"')
def impl(context, dbname):
    command = 'gp_restore -i --gp-k %s --gp-d db_dumps/%s --gp-i --gp-r db_dumps/%s --gp-l=p -d %s --gp-c -s db_dumps/%s/gp_dump_1_1_%s.gz' % \
                            (context.backup_timestamp, context.backup_subdir, context.backup_subdir, dbname, context.backup_subdir, context.backup_timestamp)
    run_gpcommand(context, command)

@when('the user runs gpdbrestore with the stored timestamp')
@then('the user runs gpdbrestore with the stored timestamp')
def impl(context):
    command = 'gpdbrestore -e -t %s -a' % context.backup_timestamp
    run_gpcommand(context, command)

@when('the user runs gpdbrestore with the stored timestamp to print the backup set with options "{options}"')
def impl(context, options):
    command = 'gpdbrestore -t %s %s --list-backup' % (context.backup_timestamp, options)
    run_gpcommand(context, command)

@then('the user runs gpdbrestore with the stored timestamp and options "{options}"')
@when('the user runs gpdbrestore with the stored timestamp and options "{options}"')
def impl(context, options):
    if options == '-b':
        command = 'gpdbrestore -e -b %s -a' % (context.backup_timestamp[0:8])
    else:
        command = 'gpdbrestore -e -t %s %s -a' % (context.backup_timestamp, options)
    run_gpcommand(context, command)

@when('the user runs gpdbrestore with the stored timestamp and options "{options}" without -e option')
def impl(context, options):
    if options == '-b':
        command = 'gpdbrestore -b %s -a' % (context.backup_timestamp[0:8])
    else:
        command = 'gpdbrestore -t %s %s -a' % (context.backup_timestamp, options)
    run_gpcommand(context, command)

@when('the user runs "{cmd}" with the stored timestamp')
@then('the user runs "{cmd}" with the stored timestamp')
def impl(context, cmd):
    command = '%s -t %s' % (cmd, context.backup_timestamp)
    run_gpcommand(context, command)

@then('verify that there is no table "{tablename}" in "{dbname}"')
def impl(context, tablename, dbname):
    if check_table_exists(context, dbname=dbname, table_name=tablename): 
        raise Exception("Table '%s' still exists when it should not" % tablename)

@then('verify that there is no view "{viewname}" in "{dbname}"')
def impl(context, viewname, dbname):
    if check_table_exists(context, dbname=dbname, table_name=viewname, table_type='view'):
        raise Exception("View '%s' still exists when it should not" % viewname)

@then('verify that there is no procedural language "{planguage}" in "{dbname}"')
def impl(context, planguage, dbname):
    if check_pl_exists(context, dbname=dbname, lan_name=planguage):
        raise Exception("Procedural Language '%s' still exists when it should not" % planguage)

@then('verify that there is a constraint "{conname}" in "{dbname}"')
def impl(context, conname, dbname):
    if not check_constraint_exists(context, dbname=dbname, conname=conname):
        raise Exception("Constraint '%s' does not exist when it should" % conname)

@then('verify that there is a "{table_type}" table "{tablename}" in "{dbname}"')
def impl(context, table_type, tablename, dbname):
    if not check_table_exists(context, dbname=dbname, table_name=tablename,table_type=table_type): 
        raise Exception("Table '%s' of type '%s' does not exist when expected" % (tablename, table_type))

@then('verify that there is partition "{partition}" of "{table_type}" partition table "{tablename}" in "{dbname}" in "{schemaname}"')
def impl(context, partition, table_type, tablename, dbname, schemaname):
    if not check_partition_table_exists(context, dbname=dbname, schemaname=schemaname, table_name=tablename, table_type=table_type, part_level=1, part_number=partition): 
        raise Exception("Partition %s for table '%s' of type '%s' does not exist when expected" % (partition, tablename, table_type))

@then('verify that there is partition "{partition}" of mixed partition table "{tablename}" with storage_type "{storage_type}"  in "{dbname}" in "{schemaname}"')
@then('verify that there is partition "{partition}" in partition level "{partitionlevel}" of mixed partition table "{tablename}" with storage_type "{storage_type}"  in "{dbname}" in "{schemaname}"')
def impl(context, partition, tablename, storage_type, dbname, schemaname, partitionlevel=1):
    part_t = get_partition_names(schemaname, tablename, dbname, partitionlevel, partition)
    partname = part_t[0][0].strip()
    validate_storage_type(context, partname, storage_type, dbname)

@given('there is a function "{functionname}" in "{dbname}"')
def impl(context, functionname, dbname):
    SQL = """CREATE FUNCTION %s(a integer, b integer)
    RETURNS integer AS $$
        if a > b:
            return a
        return b
    $$ LANGUAGE plpythonu;""" % functionname
    execute_sql(dbname, SQL)

@then('verify that storage_types of the partition table "{tablename}" are as expected in "{dbname}"')
def impl(context, tablename, dbname):
    validate_mixed_partition_storage_types(context, tablename, dbname)

@then('data for partition table "{table_name}" with partition level "{part_level}" is distributed across all segments on "{dbname}"')
def impl(context, table_name, part_level, dbname):
    validate_part_table_data_on_segments(context, table_name, part_level, dbname) 

@then('data for table "{table_name}" is distributed across all segments on "{dbname}"')
def impl(context, table_name, dbname):
    validate_table_data_on_segments(context, table_name, dbname) 

@then('verify that the data of the {file} under "{backup_dir}" in "{dbname}" is validated after restore')
def impl(context, file, dbname, backup_dir):
    dump_dir = backup_dir if len(backup_dir.strip()) != 0 else master_data_dir

    if file == 'dirty tables': 
        dirty_list_filename = '%s/db_dumps/%s/gp_dump_%s_dirty_list' % (dump_dir, context.backup_timestamp[0:8], context.backup_timestamp)
    elif file == 'table_filter_file':
        dirty_list_filename = os.path.join(os.getcwd(), file)
        
    if not os.path.exists(dirty_list_filename):
        raise Exception('Dirty list file %s does not exist' % dirty_list_filename)
    
    with open(dirty_list_filename) as fd:
        tables = fd.readlines()
        for table in tables:
            validate_restore_data(context, table.strip(), dbname)

@then('verify that the distribution policy of all the tables in "{dbname}" are validated after restore')
def impl(context, dbname):
    validate_distribution_policy(context, dbname)

@then('verify that tables "{table_list}" in "{dbname}" has no rows')
def impl(context, table_list, dbname):
    tables = [t.strip() for t in table_list.split(',')] 
    for t in tables:
        check_empty_table(t, dbname)

@then('verify that table "{tname}" in "{dbname}" has "{nrows}" rows')
def impl(context, tname, dbname, nrows):
    check_row_count(tname, dbname, int(nrows))

@then('verify that table "{tname}" in "{dbname}" has same data on source and destination system')
def impl(context, tname, dbname):
    print 'veryfing data integraty'
    match_table_select(context, tname, dbname)

@then('verify that table "{tname}" in "{dbname}" has same data on source and destination system with order by {orderby}')
def impl(context, tname, dbname, orderby):
    print 'veryfing data integraty'
    match_table_select(context, tname, dbname, orderby)

@then('verify that partitioned tables "{table_list}" in "{dbname}" have {num_parts} partitions')
@then('verify that partitioned tables "{table_list}" in "{dbname}" have {num_parts} partitions in partition level "{partitionlevel}"')
def impl(context, table_list, dbname, num_parts, partitionlevel=1):
    num_parts = int(num_parts.strip())
    tables = [t.strip() for t in table_list.split(',')] 
    for t in tables:
        names = get_partition_tablenames(t, dbname, partitionlevel)
        if len(names) != num_parts:
            raise Exception("%s.%s should have %d partitions but has %d" % (dbname, t, num_parts, len(names)))

# raise exception if tname does not have X empty partitions
def check_x_empty_parts(dbname, tname, x):
    num_empty = 0
    parts = get_partition_tablenames(tname, dbname)
    for part in parts:
        p = part[0]
        try:
            check_empty_table(p, dbname)
            num_empty += 1
        except Exception as e:
            print e

    if num_empty != x:
        raise Exception("%s.%s has %d empty partitions and should have %d" % (dbname, tname, num_empty, x))

@then('the user runs gpdbrestore with "{opt}" option in path "{path}"')
def impl(context, opt, path):
    command = 'gpdbrestore -e -a %s localhost:%s/db_dumps/%s --verbose' % (opt, path, context.backup_subdir)
    run_gpcommand(context, command)

@then('all files for full backup have been removed in path "{path}"')
def impl(context, path):
    path = path if len(path.strip()) != 0 else master_data_dir
    file_pattern = "*_%s*" % context.full_backup_timestamp
    dir = "%s/db_dumps/%s" %(path, context.backup_subdir)
    cleanup_cmd = "rm -f %s/%s" % (dir, file_pattern)
    run_command(context, cleanup_cmd)
    if context.exception:
        raise context.exception

@when('there are no backup files')
@given('there are no backup files')
def impl(context):
    cleanup_backup_files(context, 'template1')

@given('the backup files in "{location}" are deleted')
@when('the backup files in "{location}" are deleted')
@then('the backup files in "{location}" are deleted')
def impl(context, location):
    cleanup_backup_files(context, 'template1', location)

@then('there are no report files in the master data directory')
def impl(context):
    cleanup_report_files(context, master_data_dir)

@when('verify that partitioned tables "{table_list}" in "{dbname}" has {num_parts} empty partitions')
@then('verify that partitioned tables "{table_list}" in "{dbname}" has {num_parts} empty partitions')
def impl(context, table_list, dbname, num_parts):
    expected_num_parts = int(num_parts.strip())
    tables = [t.strip() for t in table_list.split(',')] 
    for t in tables:
        check_x_empty_parts(dbname, t, expected_num_parts)

@given('there is a backupfile of tables "{table_list}" in "{dbname}" exists for validation')
@when('there is a backupfile of tables "{table_list}" in "{dbname}" exists for validation')
@then('there is a backupfile of tables "{table_list}" in "{dbname}" exists for validation')
def impl(context, table_list, dbname):
    tables = [t.strip() for t in table_list.split(',')] 
    for t in tables:
        backup_data(context, t.strip(), dbname) 

@when('verify that there is a "{table_type}" table "{tablename}" in "{dbname}" with data')
@then('verify that there is a "{table_type}" table "{tablename}" in "{dbname}" with data')
def impl(context, table_type, tablename, dbname):
    if not check_table_exists(context, dbname=dbname, table_name=tablename, table_type=table_type):
        raise Exception("Table '%s' does not exist when it should" % tablename)
    validate_restore_data(context, tablename, dbname) 

@given('there is schema "{schema_list}" exists in "{dbname}"')
@then('there is schema "{schema_list}" exists in "{dbname}"')
def impl(context, schema_list, dbname):
    schemas = [s.strip() for s in schema_list.split(',')]
    for s in schemas:
        drop_schema_if_exists(context, s.strip(), dbname)
        create_schema(context, s.strip(), dbname)

@then('the temporary file "{filename}" is removed')
def impl(context, filename):
    if os.path.exists(filename):
        os.remove(filename)

@then('the temporary table file "{filename}" is removed')
def impl(context, filename):
    table_file = 'gppylib/test/behave/mgmt_utils/steps/data/gptransfer/%s' % filename
    if os.path.exists(table_file):
        os.remove(table_file)

def create_table_file_locally(context, filename, table_list, location=os.getcwd()):
    tables = table_list.split(',')
    file_path = os.path.join(location, filename)
    with open(file_path, 'w') as fp:
        for t in tables:
            fp.write(t + '\n')
    context.filename = file_path

@given('there is a file "{filename}" with tables "{table_list}"')
@then('there is a file "{filename}" with tables "{table_list}"')
def impl(context, filename, table_list):
    create_table_file_locally(context, filename, table_list)

@given('there is a fake pg_aoseg table named "{table}" in "{dbname}"')
def impl(context, table, dbname):
    create_fake_pg_aoseg_table(context, table, dbname)

def verify_file_contents(context, file_type, file_dir, text_find, should_contain=True):
    if len(file_dir.strip()) == 0:
        file_dir = master_data_dir
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''

    if file_type == 'pg_dump_log':
        fn = 'pg_dump_log'
        context.backup_timestamp = '0'
    elif file_type == 'report':
        fn = '%sgp_dump_%s.rpt' % (context.dump_prefix, context.backup_timestamp)
    elif file_type == 'status':
        fn = '%sgp_dump_status_1_1_%s' % (context.dump_prefix, context.backup_timestamp)
    elif file_type == 'filter':
        fn = '%sgp_dump_%s_filter' % (context.dump_prefix, context.backup_timestamp)

    subdirectory = context.backup_timestamp[0:8]
    
    if file_type == 'pg_dump_log':
        full_path = os.path.join(file_dir, fn)
    else:
        full_path = os.path.join(file_dir, 'db_dumps', subdirectory, fn)

    if not os.path.isfile(full_path):
        raise Exception ("Can not find %s file: %s" % (file_type, full_path))

    contents = ""
    with open(full_path) as fd:
        contents = fd.read()

    if should_contain and not text_find in contents:
        raise Exception("Did not find '%s' in file %s" % (text_find, full_path))
    elif not should_contain and text_find in contents:
        raise Exception("Found '%s' in file '%s'" % (text_find, full_path)) 
    
@then('verify that the "{file_type}" file in "{file_dir}" dir contains "{text_find}"')
def impl(context, file_type, file_dir, text_find):
    verify_file_contents(context, file_type, file_dir, text_find)

@then('verify that the "{file_type}" file in "{file_dir}" dir does not contain "{text_find}"')
def impl(context, file_type, file_dir, text_find):
    verify_file_contents(context, file_type, file_dir, text_find, should_contain=False)

@then('the timestamp in the report file should be same as timestamp key')
def impl(context):
    if not hasattr(context, 'timestamp_key'):
        raise Exception('Unable to find timestamp key in context')
       
    if hasattr(context, 'backup_dir'): 
        report_file = os.path.join(context.backup_dir, 'db_dumps', '%s' % (context.timestamp_key[0:8]),'gp_dump_%s.rpt' % context.timestamp_key)
    else:
        report_file = os.path.join(master_data_dir, 'db_dumps', '%s' % (context.timestamp_key[0:8]), 'gp_dump_%s.rpt' % context.timestamp_key)
    
    with open(report_file) as rpt:
        for line in rpt:
            if line.startswith('Timestamp Key'):
                timestamp_key = line.split(':')[-1].strip()
                if timestamp_key != context.timestamp_key:
                    raise Exception('Expected timestamp key to be %s, but found %s in report file %s' % (context.timestamp_key, timestamp_key, report_file))

@then('there should be dump files with filename having timestamp key in "{dbname}"')
def impl(context, dbname):
    if not hasattr(context, 'timestamp_key'):
        raise Exception('Unable to find timestamp key in context')
       
    master_hostname = get_master_hostname(dbname) 
    results = get_hosts_and_datadirs(dbname)

    for (host, datadir) in results:
        if host == master_hostname:
            if hasattr(context, 'backup_dir'): 
                dump_dir = os.path.join(context.backup_dir, 'db_dumps', '%s' % (context.timestamp_key[0:8]))
            else:
                dump_dir = os.path.join(master_data_dir, 'db_dumps', '%s' % (context.timestamp_key[0:8]))
    
            master_dump_files = ['%s/gp_dump_1_1_%s' % (dump_dir, context.timestamp_key),
                                 '%s/gp_dump_status_1_1_%s'  % (dump_dir, context.timestamp_key),
                                 '%s/gp_cdatabase_1_1_%s' % (dump_dir, context.timestamp_key),
                                 '%s/gp_dump_1_1_%s_post_data' % (dump_dir, context.timestamp_key)]

            for dump_file in master_dump_files:
                cmd = Command('check for dump files', 'ls -1 %s | wc -l' % (dump_file))
                cmd.run(validateAfter=True)
                results = cmd.get_results()
                if int(results.stdout.strip()) != 1:
                    raise Exception('Dump file %s not found after gp_dump on host %s' % (dump_file, host))
        else:
            if hasattr(context, 'backup_dir'): 
                dump_dir = os.path.join(context.backup_dir, 'db_dumps', '%s' % (context.timestamp_key[0:8]))
            else:
                dump_dir = os.path.join(datadir, 'db_dumps', '%s' % (context.timestamp_key[0:8]))

            segment_dump_files = ['%s/gp_dump_*_*_%s' % (dump_dir, context.timestamp_key),
                                  '%s/gp_dump_status_*_*_%s' % (dump_dir, context.timestamp_key)]

            for dump_file in segment_dump_files:
                cmd = Command('check for dump files', 'ls -1 %s | wc -l' % (dump_file), ctxt=REMOTE, remoteHost=host)
                cmd.run(validateAfter=True)
                results = cmd.get_results()
                if int(results.stdout.strip()) != 1:
                    raise Exception('Dump file %s not found after gp_dump on host %s' % (dump_file, host))

@then('"{filetype}" file should not be created under "{dir}"')
def impl(context, filetype, dir):
    if not hasattr(context, 'backup_timestamp'):
        raise Exception('Unable to find out the %s because backup timestamp has not been stored' % filename)

    filename = ''
    if filetype == "dirty_list":
        filename = 'gp_dump_%s_dirty_list' % context.backup_timestamp
    elif filetype == "plan":
        filename = 'gp_restore_%s_plan' % context.backup_timestamp
    elif filetype == 'pipes':
        filename = 'gp_dump_%s_pipes' % context.backup_timestamp
    elif filetype == 'regular_files':
        filename = 'gp_dump_%s_regular_files' % context.backup_timestamp
    else:
        raise Exception("Unknown filetype '%s' specified" % filetype)

    dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    file_path = os.path.join(dump_dir, 'db_dumps', context.backup_timestamp[0:8], filename)

    if os.path.exists(file_path):
        raise Exception("File path %s should not exist for filetype '%s'" % (file_path, filetype))

def get_plan_filename(context):
    filename = 'gp_restore_%s_plan' % context.backup_timestamp
    return os.path.join(master_data_dir, 'db_dumps', context.backup_timestamp[0:8], filename)

def get_dirty_list_filename(context, backup_dir=None):
    if not backup_dir:
        backup_dir = master_data_dir
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''
    filename = '%sgp_dump_%s_dirty_list' % (context.dump_prefix, context.backup_timestamp)
    return os.path.join(backup_dir, 'db_dumps', context.backup_timestamp[0:8], filename)

@then('plan file should match "{filename}"')
def impl(context, filename):

    current_path = os.path.realpath(__file__)
    current_dir = os.path.dirname(current_path)
    golden_filename = "%s/%s" % (current_dir, filename)
    generated_filename = get_plan_filename(context)
    if not filecmp.cmp(generated_filename, golden_filename):
        raise Exception("File contents do not match '%s' and '%s'" % (generated_filename, golden_filename))

def parse_plan_file(filename):
    plan = {} 
    with open(filename) as fd:
        for line in fd:
            parts = line.partition(":")
            ts = parts[0].strip()
            if ts not in plan:
                plan[ts] = set()
                tables = parts[2].split(",")
                for t in tables:
                    if t not in plan[ts]:
                        plan[ts].add(t.strip())
    return plan

def modify_plan_with_labels(context, expected_plan):
    newplan = {}
    for k in expected_plan:
        if k not in context.timestamp_labels:
            raise Exception("Label '%s' not specified in behave test" % k)
        ts = context.timestamp_labels[k]
        newplan[ts] = expected_plan[k]
    return newplan

def compare_plans(expected, actual):
    expected_keys = expected.keys()
    actual_keys = actual.keys()
    
    if len(expected_keys) != len(actual_keys):
        raise Exception("Expected plan has %s timestamps actual plan has %s timestamps" % (len(expected_keys), len(actual_keys)))

    for k in expected:
        if k not in actual:
            raise Exception("Expected timestamp in plan and did not find it: %s " % k)
        expected_tables = sorted(expected[k])
        actual_tables = sorted(actual[k])
        if expected_tables != actual_tables:
            print "Expected plan: %s" % expected
            print "Actual plan: %s" % actual
            raise Exception("Tables in plan for timestamp '%s' do not match expected tables" % k)

@then('the plan file is validated against "{expected_plan}"')
def impl(context, expected_plan):
    context.restore_plan = parse_plan_file(get_plan_filename(context))

    current_path = os.path.realpath(__file__)
    current_dir = os.path.dirname(current_path)
    expected_file = '%s/%s' % (current_dir, expected_plan)
    expected_plan = parse_plan_file(expected_file)
    expected_plan = modify_plan_with_labels(context, expected_plan)
    compare_plans(expected_plan, context.restore_plan)
    
@then('there should be "{numtimestamps}" timestamps in the plan file')
def impl(context, numtimestamps):
    num = int(numtimestamps)
    if len(context.restore_plan) != num:
        raise Exception("Expected %d timestamps and found %d in restore plan" % (num, len(context.restore_plan)))

@then('restore plan for timestamp "{ts}" should contain "{numtables}" tables')
def impl(context, ts, numtables):
    num = int(numtables)
    if ts not in context.restore_plan:
        raise Exception("Timestamp label '%s' not found in restore plan" % ts)

@then('"{filetype}" file is removed under "{dir}"')
def impl(context, filetype, dir):
    if not hasattr(context, 'backup_timestamp'):
        raise Exception('Backup timestamp has not been stored')

    if filetype == "dirty_list":
        filename = 'gp_dump_%s_dirty_list' % context.backup_timestamp
    elif filetype == "plan":
        filename = 'gp_restore_%s_plan' % context.backup_timestamp
    elif filetype == "global":
        filename = 'gp_global_1_1_%s' % context.backup_timestamp
    elif filetype == "report":
        filename = 'gp_dump_%s.rpt' % context.backup_timestamp
    else:
        raise Exception("Unknown filetype '%s' specified" % filetype)

    dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    file_path = os.path.join(dump_dir, 'db_dumps', context.backup_timestamp[0:8], filename)

    if os.path.exists(file_path):
        os.remove(file_path)
 

@when('"{filetype}" file should be created under "{dir}"')
@then('"{filetype}" file should be created under "{dir}"')
def impl(context, filetype, dir):
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''
    if not hasattr(context, 'backup_timestamp'):
        raise Exception('Backup timestamp has not been stored')

    if filetype == "dirty_list":
        filename = 'gp_dump_%s_dirty_list' % context.backup_timestamp
    elif filetype == "plan":
        filename = 'gp_restore_%s_plan' % context.backup_timestamp
    elif filetype == "global":
        filename = 'gp_global_1_1_%s' % context.backup_timestamp
    elif filetype == 'pipes':
        filename = 'gp_dump_%s_pipes' % context.backup_timestamp
    elif filetype == 'regular_files':
        filename = 'gp_dump_%s_regular_files' % context.backup_timestamp
    elif filetype == '_filter':
        filename = 'gp_dump_%s_filter' % context.backup_timestamp
    else:
        raise Exception("Unknown filetype '%s' specified" % filetype)


    dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    file_path = os.path.join(dump_dir, 'db_dumps', context.backup_timestamp[0:8], '%s%s' % (context.dump_prefix, filename))

    if not os.path.exists(file_path):
        raise Exception("File path %s does not exist for filetype '%s'" % (file_path, filetype))

@then('verify there are no "{tmp_file_prefix}" tempfiles')
def impl(context, tmp_file_prefix):
    if tmp_file_prefix is not None and tmp_file_prefix:
       if glob.glob('/tmp/%s*' % tmp_file_prefix): 
            raise Exception('Found temp %s files where they should not be present' % tmp_file_prefix)
    else:
        raise Exception('Invalid call to temp file removal %s' % tmp_file_prefix) 

@then('tables names should be identical to stored table names in "{dbname}"')
def impl(context, dbname):
    table_names = sorted(get_table_names(dbname))
    stored_table_names = sorted(context.table_names)

    if table_names != stored_table_names:
        print "Table names after backup:"
        print stored_table_names
        print "Table names after restore:"
        print table_names
        raise Exception('Schema not restored correctly. List of tables are not equal before and after restore in database %s' % dbname)
   
@then('tables in "{dbname}" should not contain any data')
def impl(context, dbname):
    for table in context.table_names:
        table_name = "%s.%s" % (table[0], table[1])
        check_empty_table(table_name, dbname)

@then('verify that the data of "{expected_count}" tables in "{dbname}" is validated after restore')
def impl(context, dbname, expected_count):
    validate_db_data(context, dbname, int(expected_count))

@then('all the data from the remote segments in "{dbname}" are stored in path "{dir}" for "{backup_type}"')
def impl(context, dbname, dir, backup_type):
    segs = get_segment_hostnames(context, dbname)
    if backup_type == 'inc':
        timestamp = context.backup_timestamp[0:8]
    elif backup_type == 'full':
        timestamp = context.full_backup_timestamp[0:8]

    from_path = '%s/db_dumps/%s' %(dir, timestamp)
    to_path = '%s/db_dumps' %(dir)
    for seg in segs:
        print type(seg[0].strip())
        cmdStr = "%s -o 'StrictHostKeyChecking no' -r %s:%s %s" % (findCmdInPath('scp'), seg[0].strip(),from_path, to_path)
        run_command(context, cmdStr)
    
@then('pg_stat_last_operation registers the truncate for tables "{table_list}" in "{dbname}" in schema "{schema}"')
def impl(context, table_list, dbname, schema):
    if not table_list:
        raise Exception('Empty table list')
    tables = table_list.split(',')
    for t in tables:
        table_oid = get_table_oid(context, dbname, schema, t.strip())  
        verify_truncate_in_pg_stat_last_operation(context, dbname, table_oid)

@then('pg_stat_last_operation does not register the truncate for tables "{table_list}" in "{dbname}" in schema "{schema}"')
def impl(context, table_list, dbname, schema):
    if not table_list:
        raise Exception('Empty table list')
    tables = table_list.split(',')
    for t in tables:
        table_oid = get_table_oid(context, dbname, schema, t.strip())  
        verify_truncate_not_in_pg_stat_last_operation(context, dbname, table_oid)

@given('the numbers "{lownum}" to "{highnum}" are inserted into "{tablename}" tables in "{dbname}"')
@when('the numbers "{lownum}" to "{highnum}" are inserted into "{tablename}" tables in "{dbname}"')
def impl(context, lownum, highnum, tablename, dbname):
    insert_numbers(dbname, tablename, lownum, highnum)


@when('the user adds column "{cname}" with type "{ctype}" and default "{defval}" to "{tname}" table in "{dbname}"')
def impl(context, cname, ctype, defval, tname, dbname):
    sql = "ALTER table %s ADD COLUMN %s %s DEFAULT %s" % (tname, cname, ctype, defval)
    execute_sql(dbname, sql)

@given('there is a fake timestamp for "{ts}"')
def impl(context, ts):
    dname = os.path.join(master_data_dir, 'db_dumps', ts[0:8])
    os.makedirs(dname)

    contents = """
Timestamp Key: %s
Backup Type: Full
gp_dump utility finished successfully.
""" % ts

    fname = os.path.join(dname, 'gp_dump_%s.rpt' % ts)
    with open(fname, 'w') as fd:
        fd.write(contents)

@then('a timestamp in increments file in "{directory}" is modified to be newer')
def impl(context, directory):
    if not hasattr(context, 'full_backup_timestamp'):
        raise Exception('Full backup timestamp needs to be specified in the test')

    if not directory.strip():
        directory = master_data_dir

    dump_dir = os.path.join(directory, 'db_dumps', context.full_backup_timestamp[0:8])
    increments_filename = os.path.join(dump_dir, 'gp_dump_%s_increments' % context.full_backup_timestamp)

    if not os.path.exists(increments_filename):
        raise Exception('Increments file %s does not exist !' % increments_filename) 

    with open(increments_filename) as fd:
        lines = fd.readlines()
        lines[0] = str(int(lines[0].strip()) + 10000)

    with open(increments_filename, 'w') as fd:
        for line in lines:
            fd.write(line + '\n')

@then('the "{table_type}" state file under "{backup_dir}" is saved for the "{backup_type}" timestamp')
def impl(context, table_type, backup_dir, backup_type):
    timestamp_key = None
    if backup_type == 'full':
        timestamp_key = context.full_backup_timestamp
    elif backup_type == 'inc':
        timestamp_key = context.backup_timestamp

    backup_dir = backup_dir if len(backup_dir.strip()) != 0 else master_data_dir
    context.state_file = os.path.join(backup_dir, 'db_dumps', timestamp_key[0:8], 'gp_dump_%s_%s_state_file' % (timestamp_key, table_type))

@then('the saved state file is deleted')
def impl(context):
    run_command(context, 'rm -f %s' % context.state_file) 
    if context.exception:
        raise context.exception

@then('the saved state file is corrupted')
def impl(context):

    write_lines = list()
    with open(context.state_file, "r") as fr:
        lines = fr.readlines()

    for line in lines:
        line = line.replace(",", "")
        write_lines.append(line)

    with open(context.state_file, "w") as fw:
        for line in write_lines:
            fw.write("%s\n" % line.rstrip())

@then('"{table_name}" is marked as dirty in dirty_list file')
def impl(context, table_name):
    dirty_list = get_dirty_list_filename(context)
    with open(dirty_list) as fp:
        for line in fp:
            if table_name.strip() in line.strip():
                return

    raise Exception('Expected table %s to be marked as dirty in %s' % (table_name, dirty_list))

@when('the "{table_name}" is recreated with same data in "{dbname}"')
def impl(context, table_name, dbname):
    select_sql = 'select * into public.temp from %s' % table_name
    execute_sql(dbname, select_sql)
    drop_sql = 'drop table %s' % table_name
    execute_sql(dbname, drop_sql)
    recreate_sql = 'select * into %s from public.temp' % table_name 
    execute_sql(dbname, recreate_sql)
    
@then('verify that plan file has latest timestamp for "{table_name}"')
def impl(context, table_name):
    plan_file = get_plan_filename(context)
    with open(plan_file) as fp:
        for line in fp:
            if table_name in line.strip():
                timestamp = line.split(':')[0].strip()
                if timestamp != context.backup_timestamp:
                    raise Exception('Expected table %s with timestamp %s in plan file %s does not match timestamp %s' \
                                        % (table_name, context.backup_timestamp, plan_file, timestamp))

@given('the row "{row_values}" is inserted into "{table}" in "{dbname}"')
def impl(context, row_values, table, dbname):
    insert_row(context, row_values, table, dbname)

@when('the method get_partition_state is executed on table "{table}" in "{dbname}" for ao table "{ao_table}"')
def impl(context, table, dbname, ao_table):
    (sch, tbl) = table.split('.')
    ao_sch, ao_tbl = ao_table.split('.') 
    part_info = [(1, ao_sch, ao_tbl, tbl)]
    try:
        context.exception = None
        context.partition_list_res = None
        context.partition_list_res = get_partition_state(master_port=os.environ.get('PGPORT'),
                        dbname=dbname, catalog_schema=sch, partition_info=part_info)
    except Exception as e:
        context.exception = e

@then('an exception should be raised with "{txt}"')
def impl(context, txt):
    if not context.exception:
        raise Exception("An exception was not raised")
    output = context.exception.__str__()
    if not txt in output:
        raise Exception("Exception output is not matching: '%s'" % output)

@then('the get_partition_state result should contain "{elem}"')
def impl(context, elem):
    if not context.partition_list_res:
        raise Exception('get_partition_state did not return any results')

    if len(context.partition_list_res) != 1:
        raise Exception('get_partition_state returned more results than expected "%s"' % context.partition_list_res)

    if elem not in context.partition_list_res:
        raise Exception('Expected text "%s" not found in partition list returned by get_partition_state "%s"' % (elem, context.partition_list_res)) 

@given('older backup directories "{dirlist}" exists')
@when('older backup directories "{dirlist}" exists')
@then('older backup directories "{dirlist}" exists')
def impl(context, dirlist):
    dirs = [d.strip() for d in dirlist.split(',')]

    for d in dirs:
        if len(d) != 8 or not d.isdigit():
            raise Exception('Invalid directory name provided %s' % d)

    for d in dirs:
        dump_dir = os.path.join(master_data_dir, 'db_dumps', d)
        if os.path.exists(dump_dir):
            continue
        os.makedirs(dump_dir)
        for i in range(10):
            with open(os.path.join(dump_dir, '%s_%s' % (d, i)), 'w'):
                pass

@then('the dump directories "{dirlist}" should not exist')
def impl(context, dirlist):
    dirs = [d.strip() for d in dirlist.split(',')]

    for d in dirs:
        if len(d) != 8 or not d.isdigit():
            raise Exception('Invalid directory name provided %s' % d)

    for d in dirs:
        dump_dir = os.path.join(master_data_dir, 'db_dumps', d)
        if os.path.exists(dump_dir):
            raise Exception('Unexpected directory exists %s' % dump_dir) 

@then('the dump directory for the stored timestamp should exist')
def impl(context):
    if not hasattr(context, 'full_backup_timestamp'):
        raise Exception('Full backup timestamp needs to be stored')
   
    dump_dir = os.path.join(master_data_dir, 'db_dumps', context.full_backup_timestamp[0:8]) 
    if not os.path.exists(dump_dir):
        raise Exception('Expected directory does not exist %s' % dump_dir)

def validate_master_config_backup_files(context):
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''
    master_dump_dir = os.path.join(master_data_dir, 'db_dumps', context.backup_timestamp[0:8]) 
    dump_files = os.listdir(master_dump_dir)
    for df in dump_files:
        if df.startswith('%sgp_master_config_files' % context.dump_prefix) and df.endswith('.tar'):
            return
    raise Exception('Config files not backed up on master "%s"' % master_config_file)

def validate_segment_config_backup_files(context):
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    primary_segs = [seg for seg in gparray.getDbList() if seg.isSegmentPrimary()]

    for ps in primary_segs:
        dump_dir = os.path.join(ps.getSegmentDataDirectory(), 'db_dumps', context.backup_timestamp[0:8])
        dump_files = ListRemoteFilesByPattern(dump_dir, 
                                              '%sgp_segment_config_files_*.tar' % context.dump_prefix,
                                              ps.getSegmentHostName()).run()
        if len(dump_files) != 1:
            raise Exception('Error in finding config files "%s" for segment %s' % (dump_files, ps.getSegmentDataDirectory())) 
        
@then('config files should be backed up on all segments')
def impl(context):
    if not hasattr(context, 'backup_timestamp'):
        raise Exception('Backup timestamp needs to be stored')
    
    validate_master_config_backup_files(context)
    validate_segment_config_backup_files(context)

@then('verify that the table "{table_name}" in "{dbname}" has dump info for the stored timestamp')
def impl(context, table_name, dbname):
    dump_history = {}
    dump_history_sql = 'select dump_key,options from public.%s' % table_name 
    dump_history =  getRows(dbname, dump_history_sql)
    for (dump_key,options) in dump_history:
        if context.backup_timestamp == dump_key.strip() and dbname in options:
            return

    raise Exception('Could not find dump info for timestamp %s in %s table' % (context.backup_timestamp, table_name))

@then('verify that database "{dbname}" does not exist')
def impl(context, dbname):
    with dbconn.connect(dbconn.DbURL(dbname='template1')) as conn:
        sql = """select datname from pg_database"""
        dbs = dbconn.execSQL(conn, sql)
        if dbname in dbs:
            raise Exception('Database exists when it shouldnt "%s"' % dbname)

@then('there are no saved data files')
def impl(context):
    clear_all_saved_data_verify_files(context)

@when('the timestamp for database dumps "{db_list}" are stored')
def impl(context, db_list):
    context.db_timestamps = get_timestamp_from_output_for_db(context)

def get_timestamp_from_output_for_db(context):
    db_timestamps = {}
    ts = None
    database = None
    stdout = context.stdout_message
    for line in stdout.splitlines():
        if 'Target database' in line:
            log_msg, delim, database = line.partition('=') 
            db = database.strip()
        elif 'Dump key ' in line:
            log_msg, delim, timestamp = line.partition('=')
            ts = timestamp.strip()
            validate_timestamp(ts)
            if database is None:
                raise Exception('Database not found for timestamp "%s"' % ts)
            db_timestamps[db] = ts
            database = None

    if not db_timestamps:
        raise Exception('No Timestamps found')

    return db_timestamps

@then('the dump timestamp for "{db_list}" are different')
def impl(context, db_list):
    if db_list is None:
        raise Exception('Expected at least 1 database in the list, found none.')

    if not hasattr(context, 'db_timestamps'):
        raise Exception('The database timestamps need to be stored')

    db_names = db_list.strip().split(',')
    for db in db_names:
        if db.strip() not in context.db_timestamps:
            raise Exception('Could not find timestamp for database: %s' % context.db_timestamps)

    timestamp_set = set([v for v in context.db_timestamps.values()]) 
    if len(timestamp_set) != len(context.db_timestamps):
        raise Exception('Some databases have same timestamp: "%s"' % context.db_timestamps)

@given('there is a "{table_type}" table "{table_name}" in "{db_name}" having large number of partitions')
def impl(context, table_type, table_name, db_name):
    create_large_num_partitions(table_type, table_name, db_name)

@given('there is a "{table_type}" table "{table_name}" in "{db_name}" having "{num_partitions}" partitions')
def impl(context, table_type, table_name, db_name, num_partitions):
    if not num_partitions.strip().isdigit():
        raise Exception('Invalid number of partitions specified "%s"' % num_partitions)

    num_partitions = int(num_partitions.strip()) + 1
    create_large_num_partitions(table_type, table_name, db_name, num_partitions)

@given('the length of partition names of table "{table_name}" in "{db_name}" exceeds the command line maximum limit')
def impl(context, table_name, db_name):
    partitions = get_partition_tablenames(table_name, db_name)
    partition_list_string = '' 
    for part in partitions:
        partition_list_string += (part[0] + ',')
    if partition_list_string[-1] == ',':
        parition_list_string = partition_list_string[:-1]
    MAX_COMMAND_LINE_LEN = 100000
    if len(partition_list_string) < MAX_COMMAND_LINE_LEN:
        raise Exception('Expected the length of the string to be greater than %s, but got %s instead' % (MAX_COMMAND_LINE_LEN, len(partition_list_string)))

@given('there is a table-file "{filename}" with tables "{table_list}"')
def impl(context, filename, table_list):
    tables = table_list.split(',')
    with open(filename, 'w') as fd:
        for table in tables:
            fd.write(table.strip() + '\n')

    if not os.path.exists(filename):
        raise Exception('Unable to create file "%s"' % filename)

def create_ext_table_file(file_location):
    with open(file_location, 'w') as fd:
        for i in range(100):
            fd.write('abc, 10, 10\n')

def get_host_and_port():
    if 'PGPORT' not in os.environ:
        raise Exception('PGPORT needs to be set in the environment')
    port = os.environ['PGPORT']
    gparray  = GpArray.initFromCatalog(dbconn.DbURL())
    master_host = None
    for seg in gparray.getDbList():
        if seg.isSegmentMaster():
            master_host = seg.getSegmentAddress()

    if master_host is None:
        raise Exception('Unable to determine the master hostname') 

    return (master_host, port)

@given('there is an external table "{tablename}" in "{dbname}" with data for file "{file_location}"')
def impl(context, tablename, dbname, file_location):
    create_ext_table_file(file_location) 
    host, port = get_host_and_port()
    ext_table_sql = """CREATE EXTERNAL WEB TABLE %s(name text, column1 int, column2 int) EXECUTE 'cat %s 2> /dev/null || true' 
                       ON MASTER FORMAT 'CSV' (DELIMITER ',')""" % (tablename, file_location)
    execute_sql(dbname, ext_table_sql)
   
    verify_ext_table_creation_sql = """SELECT count(*) FROM pg_class WHERE relname = '%s'""" % tablename 
    row_count = getRows(dbname, verify_ext_table_creation_sql)[0][0]
    if row_count != 1:
        raise Exception('Creation of external table failed for "%s:%s, row count = %s"' % (file_location, tablename, row_count))

@then('verify that there is no "{tablename}" in the "{file_type}" file in "{backup_dir}"')
def impl(context, tablename, file_type, backup_dir):
    dump_dir = backup_dir if len(backup_dir.strip()) != 0 else master_data_dir
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''

    filename = '%s/db_dumps/%s/%sgp_dump_%s_%s' % (dump_dir, context.backup_timestamp[0:8], context.dump_prefix, context.backup_timestamp, file_type)
    with open(filename) as fd:
        for line in fd:
            if tablename.strip() == line.strip():
                raise Exception('Found an unwanted table in the file: "%s" in line: "%s"' %(filename, line))  

@then('verify that exactly "{num_tables}" tables in "{dbname}" have been restored')
def impl(context, num_tables, dbname):
    validate_num_restored_tables(context, num_tables, dbname)

@then('the user runs gpdbrestore on dump date directory with options "{options}"')
def impl(context, options):
    command = 'gpdbrestore -e -b %s %s -a' % (context.backup_timestamp[0:8], options)
    run_gpcommand(context, command)

@then('the timestamps should be printed in sorted order')
def impl(context):
    stdout_lines = context.stdout_message.split('\n')
    process_ts = False
    timestamps = []
    for line in stdout_lines:
        if '--------------------------' in line:
            process_ts = True
        elif process_ts:
            if 'Enter timestamp number to restore' not in line:
                timestamps.append(line.strip().split('......')[-1].strip()) 
            else:
                process_ts = False
                break
            
    timestamps = [ts.split()[0]+ts.split()[1] for ts in timestamps] 

    sorted_timestamps = sorted(timestamps, key=lambda x: int(x))    
    
    if sorted_timestamps != timestamps:
        raise Exception('Timestamps are not in sorted order "%s"' % timestamps)

@given('there are "{table_count}" "{tabletype}" tables "{table_name}" with data in "{dbname}"')
def impl(context, table_count, tabletype, table_name, dbname):
    table_count = int(table_count)
    for i in range(1, table_count+1):
        tablename = "%s_%s" % (table_name, i)
        create_database_if_not_exists(context, dbname)
        drop_table_if_exists(context, table_name=tablename, dbname=dbname)
        create_partition(context, tablename, tabletype, dbname, compression_type=None, partition=False)

@given('the tables "{table_list}" are in dirty hack file "{dirty_hack_file}"')
def impl(context, table_list, dirty_hack_file):
    tables = [t.strip() for t in table_list.split(',')]
    
    with open(dirty_hack_file, 'w') as fd:
        for t in tables:
            fd.write(t + '\n')

    if not os.path.exists(dirty_hack_file):
        raise Exception('Failed to create dirty hack file "%s"' % dirty_hack_file)

@given('partition "{part_num}" of partition tables "{table_list}" in "{dbname}" in schema "{schema}" are in dirty hack file "{dirty_hack_file}"')
def impl(context, part_num, table_list, dbname, schema, dirty_hack_file):
    tables = table_list.split(',')
    with open(dirty_hack_file, 'w') as fd:
        part_num = int(part_num.strip())
        for table in tables:
            part_t = get_partition_names(schema, table.strip(), dbname, 1, part_num)
            if len(part_t) < 1 or len(part_t[0]) < 1:
                print part_t
            partition_name = part_t[0][0].strip()
            fd.write(partition_name + '\n')

    if not os.path.exists(dirty_hack_file):
        raise Exception('Failed to write to dirty hack file "%s"' % dirty_hack_file)

@then('verify that the config files are backed up with the stored timestamp')
def impl(context):
    if not hasattr(context, 'backup_timestamp'):
        raise Exception('Timestamp needs to be stored')
    
    config_file = os.path.join(master_data_dir, 'db_dumps', context.backup_timestamp[0:8],
                               'gp_master_config_files_%s.tar' % context.backup_timestamp)
    if not os.path.exists(config_file):
        raise Exception('Failed to locate config file on master "%s"' % config_file)

    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    
    primary_segs = [seg for seg in gparray.getDbList() if seg.isSegmentPrimary(current_role=True)]

    for seg in primary_segs:
        segment_config_file = os.path.join(seg.getSegmentDataDirectory(), 'db_dumps', context.backup_timestamp[0:8],
                                           'gp_segment_config_files_0_%s_%s.tar' % (seg.getSegmentDbId(), context.backup_timestamp)) 
        if not CheckRemoteFile(segment_config_file, seg.getSegmentAddress()):
            raise Exception('Failed to locate "%s" on "%s"' % segment_config_file, seg.getSegmentDataDirectory())

@then('verify that the list of stored timestamps is printed to stdout')
def impl(context):
    found_ts = 0
    stdout = context.stdout_message
    for ts in context.inc_backup_timestamps:
        for line in stdout.splitlines():
            ts_str = 'Backup Timestamp: %s' % ts
            if ts_str in line:
                found_ts += 1
                          
    print 'context.inc_backup_timestamps = ', context.inc_backup_timestamps
    if found_ts != len(context.inc_backup_timestamps):
        raise Exception('Expected "%d" timestamps but found "%d" timestamps' % (len(context.inc_backup_timestamps), found_ts))

@given('there is a "{table_type}" table "{tablename}" with funny characters in "{dbname}"')
def impl(context, table_type, tablename, dbname):
    funny_chars_table_name_sql = """create table "%s
        new line in it" (a int)""" % tablename.strip()

    table_type = table_type.strip()
    if table_type == 'ao':
        funny_chars_table_name_sql += ' with(appendonly=true)'
    elif table_type == 'co':
        funny_chars_table_name_sql += ' with(appendonly=true, orientation=column)'
    elif table_type == 'heap':
        pass
    else:
        raise Exception('Unknown table type specified "%s"' % table_type) 

    execute_sql(dbname.strip(), funny_chars_table_name_sql)

@then('verify that the tuple count of all appendonly tables are consistent in "{dbname}"')
def impl(context, dbname):
    ao_partition_list = get_partition_list('ao', dbname)
    verify_stats(dbname, ao_partition_list)
    co_partition_list = get_partition_list('co', dbname)
    verify_stats(dbname, co_partition_list)

@then('verify that there are no aoco stats in "{dbname}" for table "{tables}"')
def impl(context, dbname, tables):
    tables = tables.split(',')
    for t in tables:
        validate_no_aoco_stats(context, dbname, t.strip())

@when('the performance timer is started')
def impl(context):
    context.performance_timer = time.time()

@then('the performance timer should be less then "{num_seconds}" seconds')
def impl(context, num_seconds):
    max_seconds = float(num_seconds)
    current_time = time.time()
    elapsed = current_time - context.performance_timer
    if elapsed > max_seconds:
        raise Exception("Performance timer ran for %.1f seconds but had a max limit of %.1f seconds" % (elapsed, max_seconds))
    print "Elapsed time was %.1f seconds" % elapsed


@given('the file "{filename}" is removed from the system')
@when('the file "{filename}" is removed from the system')
@then('the file "{filename}" is removed from the system')
def impl(context, filename): 
    os.remove(filename)

@given('the client program "{program_name}" is present under {parent_dir} in "{sub_dir}"')
def impl(context, program_name, parent_dir, sub_dir):

    if parent_dir == 'CWD':
        parent_dir = os.getcwd()
    program_path = '%s/%s/%s' % (parent_dir, sub_dir, program_name)

    print program_path
    if not os.path.isfile(program_path):
        raise Exception('gpfdist client progream does not exist: %s' % (program_path))

@when('the user runs client program "{program_name}" from "{subdir}" under {parent_dir}')
def impl(context, program_name, subdir, parent_dir):
    if parent_dir == 'CWD':
        parent_dir = os.getcwd()

    command_line = "python %s/%s/%s" % (parent_dir, subdir, program_name)
    run_command(context, command_line)

@then('the gpfdist should print {msg} to "{filename}" under {parent_dir}')
def impl(context, msg, filename, parent_dir):
    if parent_dir == 'CWD':
        parent_dir = os.getcwd()
    filepath = '%s/%s' % (parent_dir, filename)

    with open(filepath, 'r') as fp:
        for line in fp:
            if msg in line:
                return

        raise Exception('Log file %s did not contain the message %s' % (filepath, msg))

@given('the "{process_name}" process is killed')
@then('the "{process_name}" process is killed')
@when('the "{process_name}" process is killed')
def impl(context, process_name):
    run_command(context, 'pkill %s' % process_name)

@then('the client program should print {msg} to stdout with value in range {min_val} to {max_val}')
def impl(context, msg, min_val, max_val):
    stdout = context.stdout_message

    for line in stdout:
        if msg in line:
            val = re.finadall(r'\d+', line)
            if not val:
                raise Exception('Expected a numeric digit after message: %s' % msg) 
            if len(val) > 1:
                raise Exception('Expected one numeric digit after message: %s' % msg) 

            if val[0] < min_val or val[0] > max_val:
                raise Exception('Value not in acceptable range %s' % val[0]) 

@given('the directory "{dirname}" exists in current working directory')
def impl(context, dirname):
    dirname = os.path.join(os.getcwd(), dirname)
    if os.path.isdir(dirname):
        shutil.rmtree(dirname, ignore_errors=True)
        if os.path.isdir(dirname):
            raise Exception("directory '%s' not removed" % dirname)
    os.mkdir(dirname)
    if not os.path.isdir(dirname):
        raise Exception("directory '%s' not created" % dirname)

@given('the file "{filename}" exists under "{directory}" in current working directory')
def impl(context, filename, directory):
    directory = os.path.join(os.getcwd(), directory)
    if not os.path.isdir(directory):
        raise Exception("directory '%s' not exists" % directory)
    filepath = os.path.join(directory, filename)
    open(filepath, 'a').close()
    if not os.path.exists(filepath):
        raise Exception("file '%s' not created" % filepath)

@given('the directory "{dirname}" does not exist in current working directory')
def impl(context, dirname):
    dirname = os.path.join(os.getcwd(), dirname)
    if os.path.isdir(dirname):
        shutil.rmtree(dirname, ignore_errors=True)
    if os.path.isdir(dirname):
        raise Exception("directory '%s' not removed" % dirname)

@when('the data line "{dataline}" is appened to "{fname}" in cwd')
@then('the data line "{dataline}" is appened to "{fname}" in cwd')
def impl(context, dataline, fname):
    fname = os.path.join(os.getcwd(), fname)
    with open(fname, 'a') as fd:
        fd.write("%s\n" % dataline)


@when('a "{readwrite}" external table "{tname}" is created on file "{fname}" in "{dbname}"')
def impl(context, readwrite, tname, fname, dbname):

    hostname = socket.gethostname()

    sql = """CREATE %s EXTERNAL TABLE  
            %s (name varchar(255), id int) 
            LOCATION ('gpfdist://%s:8088/%s') 
            FORMAT 'text'
            (DELIMITER '|');
            """ % (readwrite, tname, hostname, fname)

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, sql)
        conn.commit()

@given('the external table "{tname}" does not exist in "{dbname}"')
def impl(context, tname, dbname):
    drop_external_table_if_exists(context, table_name=tname, dbname=dbname)


@when('all rows from table "{tname}" db "{dbname}" are stored in the context')
def impl(context, tname, dbname):

    context.stored_rows = []

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        sql = "SELECT * FROM %s" % tname 
        curs = dbconn.execSQL(conn, sql)
        context.stored_rows = curs.fetchall()

@then('validate that "{dataline}" "{formatter}" seperated by "{delim}" is in the stored rows')
def impl(context, dataline, formatter, delim):
    lookfor = dataline.split(delim)
    formatter = formatter.split(delim)

    for i in range(len(formatter)):
        if formatter[i] == 'int':
            lookfor[i] = int(lookfor[i])

    if lookfor not in context.stored_rows:
        print context.stored_rows
        print lookfor
        raise Exception("'%s' not found in stored rows" % dataline)

@then('validate that following rows are in the stored rows')
def impl(context):
    for row in context.table:
        found_match = False

        for stored_row in context.stored_rows:
            match_this_row = True
            for i in range(len(stored_row)):
                if row[i] != str(stored_row[i]):
                    match_this_row = False
                    break

            if match_this_row:
                found_match = True
                break

        if not found_match:
            print context.stored_rows
            raise Exception("'%s' not found in stored rows" % row)


@then('validate that stored rows has "{numlines}" lines of output')
def impl(context, numlines):
    num_found = len(context.stored_rows)
    numlines = int(numlines)
    if num_found != numlines:
        raise Exception("Found %d of stored query result but expected %d records" % (num_found, numlines))

def get_standby_host():
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    segments = gparray.getDbList() 
    standby_master = [seg.getSegmentHostName() for seg in segments if seg.isSegmentStandby()]
    if len(standby_master) > 0:
        return standby_master[0]
    else:
        return []

@given('user does not have ssh permissions') 
def impl(context):
    user_home = os.environ.get('HOME')
    authorized_keys_file = '%s/.ssh/authorized_keys' % user_home
    if os.path.exists(os.path.abspath(authorized_keys_file)):
       shutil.move(authorized_keys_file, '%s.bk' % authorized_keys_file) 

@then('user has ssh permissions') 
def impl(context):
    user_home = os.environ.get('HOME')
    authorized_keys_backup_file = '%s/.ssh/authorized_keys.bk' % user_home
    if os.path.exists(authorized_keys_backup_file):
        shutil.move(authorized_keys_backup_file, authorized_keys_backup_file[:-3])

def delete_data_dir(host):
    cmd = Command(name='remove data directories',
           cmdStr='rm -rf %s' % master_data_dir,
           ctxt=REMOTE,
           remoteHost=host)
    cmd.run(validateAfter=True)

@when('the user initializes standby master on "{hostname}"') 
def impl(context, hostname):
    create_standby(context, hostname)

def create_standby(context, hostname):
    port = os.environ.get('PGPORT')
    if hostname.strip() == 'mdw':
        if not hasattr(context, 'master') or not hasattr(context, 'standby_host'):
            raise Exception('Expected master to be saved but was not found')
        delete_data_dir(context.master)
        cmd = Command(name='gpinitstandby',
                      cmdStr='PGPORT=%s MASTER_DATA_DIRECTORY=%s gpinitstandby -a -s %s' % (port, master_data_dir, context.master),
                      ctxt=REMOTE,
                      remoteHost=context.standby_host)
        cmd.run(validateAfter=True)
        return
    elif hostname.strip() == 'smdw':
        context.master = get_master_hostname('template1')[0][0]
        context.ret_code = 0
        context.stdout_message = ''
        context.error_message = ''
        segment_hostlist = get_segment_hostlist()    
        segment_hosts = [seg for seg in segment_hostlist if seg != context.master] 
        context.standby_host = segment_hosts[0]
        gparray = GpArray.initFromCatalog(dbconn.DbURL())
        segdbs = gparray.getDbList()
        for seg in segdbs:
            if seg.getSegmentHostName() == context.standby_host:
                context.standby_host_data_dir = seg.getSegmentDataDirectory()

        context.standby_was_initialized = False
        standby = get_standby_host()

        if standby:
            context.standby_was_initialized = True
            context.standby_host = standby
            return
    
        delete_data_dir(context.standby_host)
        cmd = "gpinitstandby -a -s %s" % context.standby_host
        run_gpcommand(context, cmd)
    else:
        raise Exception('Invalid host type specified "%s"' % hostname)

@when('the user runs the query "{query}" on "{dbname}"')
def impl(context, query, dbname):
    if query.lower().startswith('create') or query.lower().startswith('insert'):
        thread.start_new_thread(execute_sql, (dbname, query))
    else:
        thread.start_new_thread(getRows, (dbname, query))
    time.sleep(30)

@given('we have exchanged keys with the cluster')
def impl(context):
    hostlist = get_all_hostnames_as_list(context, 'template1')
    host_str = ' -h '.join(hostlist)
    cmd_str = 'gpssh-exkeys %s' % host_str
    run_gpcommand(context, cmd_str) 

@then('the temp files "{filename_prefix}" are not created in the system')
def impl(context, filename_prefix):
    hostlist = get_all_hostnames_as_list(context, 'template1')
    print hostlist
    file_pattern = 'ls /tmp/%s* | wc -l' % filename_prefix
    print file_pattern
    for host in hostlist:
        cmd = Command(name='check for temp files', 
                      cmdStr=file_pattern, 
                      ctxt=REMOTE, 
                      remoteHost=host)
        cmd.run(validateAfter=True)
        results = cmd.get_results()
        if int(results.stdout.strip()) > 0:
            raise Exception('Temp files with prefix %s are not cleaned up on host %s after gpcrondump' % (filename_prefix, host))

@when('the user activates standby on the standbyhost')
@then('the user activates standby on the standbyhost')
def impl(context):
    port = os.environ.get('PGPORT')
    cmd = 'PGPORT=%s MASTER_DATA_DIRECTORY=%s gpactivatestandby -d %s -fa' % (port, master_data_dir, master_data_dir) 
    cmd = Command('run remote command', cmd, ctxt=REMOTE, remoteHost=context.standby_host)
    cmd.run(validateAfter=True)

@then('the user runs the command "{cmd}" from standby')
@when('the user runs the command "{cmd}" from standby')
def impl(context, cmd):
    port = os.environ.get('PGPORT')
    cmd = 'PGPORT=%s MASTER_DATA_DIRECTORY=%s %s' % (port, master_data_dir, cmd) 
    cmd = Command('run remote command', cmd, ctxt=REMOTE, remoteHost=context.standby_host)
    cmd.run(validateAfter=True)

@given('user kills a primary postmaster process')
@when('user kills a primary postmaster process')
@then('user kills a primary postmaster process')
def impl(context):
    if hasattr(context, 'pseg'):
        seg_data_dir = context.pseg_data_dir
        seg_host = context.pseg_hostname
        seg_port = context.pseg.getSegmentPort() 
    else:
        gparray=GpArray.initFromCatalog(dbconn.DbURL())
        for seg in gparray.getDbList():
            if seg.isSegmentPrimary():
                seg_data_dir = seg.getSegmentDataDirectory()
                seg_host = seg.getSegmentHostName()
                seg_port = seg.getSegmentPort()
                break
    
    pid = get_pid_for_segment(seg_data_dir, seg_host) 
    if pid is None:
        raise Exception('Unable to locate segment "%s" on host "%s"' % (seg_data_dir, seg_host))

    kill_process(int(pid), seg_host)

    time.sleep(10)

    pid = get_pid_for_segment(seg_data_dir, seg_host)
    if pid is not None:
        raise Exception('Unable to kill postmaster with pid "%d" datadir "%s"' % (pid, seg_data_dir)) 

    context.killed_seg_host = seg_host
    context.killed_seg_port = seg_port

@when('the temp files "{filename_prefix}" are removed from the system')
@given('the temp files "{filename_prefix}" are removed from the system')
def impl(context, filename_prefix):
    hostlist = get_all_hostnames_as_list(context, 'template1')
    print hostlist
    for host in hostlist:
        cmd = Command(name='remove data directories',
                cmdStr='rm -rf /tmp/%s*' % filename_prefix,
                ctxt=REMOTE,
                remoteHost=host)
        cmd.run(validateAfter=True)

@then('the standby is initialized if required')
def impl(context):
    if context.standby_was_initialized or hasattr(context, 'cluster_had_standby'):
        if get_standby_host():
            return
        delete_data_dir(context.standby_host)
        cmd = Command('create the standby', cmdStr='gpinitstandby -s %s -a' % context.standby_host)
        cmd.run(validateAfter=True)
    else:
        standby = get_standby_host()
        if standby:
            run_gpcommand(context, 'gpinitstandby -ra')

@given('user can start transactions')
@when('user can start transactions')
@then('user can start transactions')
def impl(context):
    num_retries = 50
    attempt = 0
    while attempt < num_retries:
        try:
            with dbconn.connect(dbconn.DbURL()) as conn:
                break
        except Exception as e:
            attempt +=1 
            pass
        time.sleep(1)
   
    if attempt == num_retries:
        raise Exception('Unable to establish a connection to database !!!') 
    
@when('user runs "{cmd}" with sudo access')
def impl(context, cmd):
    gphome = os.environ.get('GPHOME')
    python_path = os.environ.get('PYTHONPATH')
    python_home = os.environ.get('PYTHONHOME')
    ld_library_path = os.environ.get('LD_LIBRARY_PATH')
    path = os.environ.get('PATH')
    cmd_str = """sudo GPHOME=%s 
                    PATH=%s 
                    PYTHONHOME=%s 
                    PYTHONPATH=%s 
                    LD_LIBRARY_PATH=%s %s/bin/%s""" % (gphome, path, python_home, python_path, ld_library_path, gphome, cmd) 
    run_command(context, cmd_str)

def verify_num_files(results, expected_num_files, timestamp):
    num_files = results.stdout.strip()
    if num_files != expected_num_files:
        raise Exception('Expected "%s" files with timestamp key "%s" but found "%s"' % (expected_num_files, timestamp,num_files))
    
    
def verify_timestamps_on_master(timestamp, dump_type):
    list_cmd = 'ls -l %s/db_dumps/%s/*%s* | wc -l' % (master_data_dir, timestamp[:8], timestamp) 
    cmd = Command('verify timestamps on master', list_cmd)
    cmd.run(validateAfter=True)
    expected_num_files = '10' if dump_type == 'incremental' else '8'
    verify_num_files(cmd.get_results(), expected_num_files, timestamp)

def verify_timestamps_on_segments(timestamp):
    gparray = GpArray.initFromCatalog(dbconn.DbURL()) 
    primary_segs = [segdb for segdb in gparray.getDbList() if segdb.isSegmentPrimary()]

    for seg in primary_segs:
        db_dumps_dir = os.path.join(seg.getSegmentDataDirectory(), 
                                    'db_dumps',
                                    timestamp[:8])
        list_cmd = 'ls -l %s/*%s* | wc -l' % (db_dumps_dir, timestamp) 
        cmd = Command('get list of dump files', list_cmd, ctxt=REMOTE, remoteHost=seg.getSegmentHostName())
        cmd.run(validateAfter=True)
        verify_num_files(cmd.get_results(), '2', timestamp)
        
@then('verify that "{dump_type}" dump files have stored timestamp in their filename')
def impl(context, dump_type):
    if dump_type.strip().lower() != 'full' and dump_type.strip().lower() != 'incremental':
        raise Exception('Invalid dump type "%s"' % dump_type)

    verify_timestamps_on_master(context.backup_timestamp, dump_type.strip().lower())
    verify_timestamps_on_segments(context.backup_timestamp)

def validate_files(file_list, pattern_list, expected_file_count):
    file_count = 0 
    for pattern in pattern_list:
        pat = re.compile(pattern)
        pat_found = False
        for f in file_list:
            m = pat.search(f.strip())
            if m is not None:
                pat_found = True
                file_count += 1

        if not pat_found:
            raise Exception('Expected file not found for pattern: "%s" in file list "%s"' % (pattern, file_list))

    if file_count != expected_file_count:
        raise Exception('Expected count of %d does not match actual count %d in file list "%s"' % (expected_file_count, file_count, file_list))
     
@then('the "{file_type}" file under "{directory}" with options "{options}" is validated after dump operation')
def impl(context, file_type, directory, options):
    backup_dir = directory if directory.strip() != '' else master_data_dir  
    if len(options.split(',')) > 3:
        raise Exception('Invalid options specified "%s"' % options) 
    option_list = options.split(',')

    pipe_file_count = 1 + get_num_segments(primary=True, mirror=False, master=True, standby=False)
    reg_file_count = 6

    pipes_pattern_list = ['gp_dump_.*_%s.*(?:\.gz)?' % context.backup_timestamp]
    regular_pattern_list = ['gp_cdatabase_1_1_%s' % context.backup_timestamp, 'gp_dump_%s.*' % context.backup_timestamp, 'gp_dump_status_1_1_%s' % context.backup_timestamp]

    if '-G' in option_list: 
        pipe_file_count += 1
        pipes_pattern_list += ['gp_global_1_1_%s' % context.backup_timestamp]
    if '-g' in option_list:
        pipe_file_count += get_num_segments(primary=True, mirror=False, master=True, standby=False) 
        pipes_pattern_list += ['gp_master_config_files_%s.*' % context.backup_timestamp, 'gp_segment_config_files_.*_.*_%s.*' % context.backup_timestamp]
    if '--incremental' in option_list:
        regular_pattern_list += ['gp_dump_%s.*' % context.full_backup_timestamp]
        reg_file_count += 1

    if hasattr(context, "dump_prefix"):
        if '-t' in option_list or '-T' in option_list:
            reg_file_count += 1
        for id, p in enumerate(pipes_pattern_list):
            pipes_pattern_list[id] = '%s%s' % (context.dump_prefix, p)
        for id, p in enumerate(regular_pattern_list):
            regular_pattern_list[id] = '%s%s' % (context.dump_prefix, p)

    filename = '%s/db_dumps/%s/%sgp_dump_%s_%s' % (backup_dir, context.backup_timestamp[0:8], context.dump_prefix, context.backup_timestamp, file_type.strip())

    with open(filename) as fp:
        file_list = fp.readlines()

    if file_type == 'pipes':
        validate_files(file_list, pipes_pattern_list, pipe_file_count)
    elif file_type == 'regular_files':
        validate_files(file_list, regular_pattern_list, reg_file_count)
        
@then('the timestamp key "{timestamp_key}" for gpcrondump is stored')
def impl(context, timestamp_key):
    context.backup_timestamp = timestamp_key

@given('the prefix "{prefix}" is stored')
def impl(context, prefix):
    context.dump_prefix = prefix + '_'

def get_segment_dump_files(context, dir):
    results = [] 
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    primary_segs = [seg for seg in gparray.getDbList() if seg.isSegmentPrimary()]
    for seg in primary_segs:
        segment_dump_dir =  dir if len(dir.strip()) != 0 else seg.getSegmentDataDirectory()
        cmd = Command('check dump files', 'ls %s/db_dumps/%s' % (segment_dump_dir, context.backup_timestamp[0:8]), ctxt=REMOTE, remoteHost=seg.getSegmentHostName())
        cmd.run(validateAfter=False) #because we expect ls to fail
        results.append((seg, [r for r in cmd.get_results().stdout.strip().split()]))
    return results
    
@then('there are no dump files created under "{dir}"')
def impl(context, dir):
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''
    master_dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    segment_dump_files = get_segment_dump_files(context, dir)

    for seg, dump_files in segment_dump_files:
        segment_dump_dir =  dir if len(dir.strip()) != 0 else seg.getSegmentDataDirectory()
        if len(dump_files) != 0:
            raise Exception('Found extra dump files on the segment %s under %s/db_dumps/%s' % (seg.getSegmentDataDirectory(), segment_dump_dir, context.backup_timestamp[0:8]))
    
    cmd = Command('check dump files', 'ls %s/db_dumps/%s' % (master_dump_dir, context.backup_timestamp[0:8]))
    cmd.run(validateAfter=True)
    results = cmd.get_results().stdout.strip().split('\n')
    
    if len(results) != 2:
        raise Exception('Found extra dump files %s on the master under %s' % (results, master_dump_dir))

    pipes_file = '%sgp_dump_%s_pipes' % (context.dump_prefix, context.backup_timestamp)
    reg_file = '%sgp_dump_%s_regular_files' % (context.dump_prefix, context.backup_timestamp)
    if not pipes_file in results or not  reg_file in results:
        raise Exception('Found invalid files %s under dump dir %s' % (results, master_dump_dir))

@then('the named pipes are created for the timestamp "{timestamp_key}" under "{dir}"')
def impl(context, timestamp_key, dir):
    dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    pipes_filename = '%s/db_dumps/%s/gp_dump_%s_pipes' % (dump_dir, timestamp_key[0:8], timestamp_key)

    with open(pipes_filename, 'r') as fp:
        for f in fp:
            (host, filename) = [t.strip() for t in f.split(':')]
            cmd_str = 'mkdir -p %s' % os.path.dirname(filename)
            cmd = Command('create named pipes directory', cmd_str, ctxt=REMOTE, remoteHost=host)
            cmd.run(validateAfter=True)
            results = cmd.get_results()
            if int(results.rc) != 0:
                raise Exception('Non zero return code during makedirs command')

            cmd = Command('create named pipes', 'mkfifo %s' % filename, ctxt=REMOTE, remoteHost=host)
            cmd.run(validateAfter=True)
            results = cmd.get_results()
            if int(results.rc) != 0:
                raise Exception('Non zero return code during mkfifo command')

@then('the named pipes are validated against the timestamp "{timestamp_key}" under "{dir}"')
def impl(context, timestamp_key, dir):
    dump_dir = dir if len(dir.strip()) != 0 else master_data_dir
    pipes_filename = '%s/db_dumps/%s/gp_dump_%s_pipes' % (dump_dir, timestamp_key[0:8], timestamp_key)

    with open(pipes_filename, 'r') as fp:
        for f in fp:
            (host, filename) = [t.strip() for t in f.split(':')]
            cmd = Command('create named pipes', 'file %s' % filename, ctxt=REMOTE, remoteHost=host)
            cmd.run(validateAfter=True)
            results = cmd.get_results()
            if int(results.rc) != 0:
                raise Exception('Non zero return code during mkfifo command')
            if not 'named pipe' in results.stdout:
                raise Exception('Expected %s to be a named pipe' % filename)

@then('the named pipe script for the "{operation}" is run for the files under "{dump_directory}"')
def impl(context, operation, dump_directory):
    dump_dir = dump_directory if len(dump_directory.strip()) != 0 else master_data_dir
    if operation == 'restore' and hasattr(context, 'inc_backup_timestamps'):
        if not hasattr(context, 'backup_timestamp'):
            raise Exception('Backup timestamp not stored')
        for ts in context.inc_backup_timestamps:
            open_named_pipes(context, operation, ts, dump_dir) 
    else:
        open_named_pipes(context, operation, context.backup_timestamp, dump_dir)

@then('close all opened pipes')
def impl(context):
    hosts = set(get_all_hostnames_as_list(context, 'template1'))
    for h in hosts:
        find_cmd = Command('get list of pipe processes',
                      "ps -eaf | grep _pipe.py | grep -v grep | grep -v ssh",
                      ctxt=REMOTE,
                      remoteHost=h) 
        find_cmd.run()
        results = find_cmd.get_results().stdout.strip().split('\n')
        for process in results:
            if not process.strip():
                continue
            pid = process.split()[1].strip()
            print 'pid = %s on host %s' % (pid, h) 
            cmd = Command('Kill pipe process',
                          "kill %s" % pid,
                          ctxt=REMOTE,
                          remoteHost=h)
            cmd.run(validateAfter=True)
      
        find_cmd.run() # We expecte failure here

        results = find_cmd.get_results().stdout.strip()
        if results:
            raise Exception('Unexpected pipe processes found "%s"' % results) 
    

def open_named_pipes(context, operation, timestamp, dump_dir):
    sleeptime = 5
    pipes_filename = '%s/db_dumps/%s/gp_dump_%s_pipes' % (dump_dir, timestamp[0:8], timestamp)


    filename = os.path.join(os.getcwd(), './gppylib/test/data/%s_pipe.py' % operation)

    segs = get_all_hostnames_as_list(context, 'template1')

    for seg in segs:
        cmdStr = "%s -o 'StrictHostKeyChecking no' %s %s:%s" % (findCmdInPath('scp'), filename, seg, '/tmp')
        run_command(context, cmdStr)

    with open(pipes_filename, 'r') as fp:
        for f in fp:
            (host, filename) = [t.strip() for t in f.split(':')]
            cmd = Command('run pipe script', 'sh -c "python /tmp/%s_pipe.py %s" &>/dev/null &' % (operation, filename),
                          ctxt=REMOTE, remoteHost=host)
            cmd.run(validateAfter=True)
            time.sleep(sleeptime)
            results = cmd.get_results()

@given('the core dump directory is stored')
def impl(context):
    with open('/etc/sysctl.conf', 'r') as fp:
        for line in fp:
            if 'kernel.core_pattern' in line:
                context.core_dir = os.path.dirname(line.strip().split('=')[1])

    if not hasattr(context, 'core_dir') or not context.core_dir:
        context.core_dir = os.getcwd()

@given('the number of core files "{stage}" running "{utility}" is stored')
@then('the number of core files "{stage}" running "{utility}" is stored')
def impl(context, stage, utility):
    core_files_count = 0
    files_list = os.listdir(context.core_dir)
    for f in files_list:
        if f.startswith('core'):
            core_files_count += 1 

    if stage.strip() == 'before':
        context.before_core_count = core_files_count 
    elif stage.strip() == 'after':
        context.after_core_count = core_files_count 
    else:
        raise Exception('Invalid stage entered: %s' % stage)

@then('the number of core files is the same')
def impl(context):
    if not hasattr(context, 'before_core_count'):
        raise Exception('Core file count not stored before operation') 
    if not hasattr(context, 'after_core_count'):
        raise Exception('Core file count not stored after operation')

    if context.before_core_count != context.after_core_count:
        raise Exception('Core files count before %s does not match after %s' % (context.before_core_count, context.after_core_count))


@given('the gpAdminLogs directory has been backed up')
def impl(context):
    src = os.path.join(os.path.expanduser('~'), 'gpAdminLogs') 
    dest = os.path.join(os.path.expanduser('~'), 'gpAdminLogs.bk') 
    shutil.move(src, dest)

@given('the user does not have "{access}" permission on the home directory')
def impl(context, access):
    home_dir = os.path.expanduser('~')
    context.orig_write_permission = check_user_permissions(home_dir, 'write')
    if access == 'write':
        cmd = "sudo chmod u-w %s" % home_dir
    run_command(context, cmd)

    if check_user_permissions(home_dir, access):
        raise Exception('Unable to change "%s" permissions for the directory "%s"' % (access, home_dir))
    
@then('the "{filetype}" path "{file_path}" should "{cond}" exist')
def impl(context, filetype, file_path, cond):
    cond = cond.strip()
    if file_path[0] == '~':
        file_path = os.path.join(os.path.expanduser('~'), file_path[2:])

    if filetype == 'file':
        existence_check_fn = os.path.isfile
    elif filetype == 'directory':
        existence_check_fn  = os.path.isdir
    else:
        raise Exception('File type should be either file or directory')

    if cond == '' and not existence_check_fn(file_path):
        raise Exception('The %s "%s" does not exist' % (filetype, file_path))
    elif cond == 'not' and existence_check_fn(file_path):
        raise Exception('The %s "%s" exist' % (filetype, file_path))

@then('the directory "{file_path}" is removed')
def impl(context, file_path):

    if file_path[0] == '~':
        file_path = os.path.join(os.path.expanduser('~'), file_path[2:]) 
    backup_file_path = file_path + '.bk'

    if not os.path.exists(backup_file_path):
        raise Exception('Backup file "%s" must exist in order to delete the file "%s"' % (backup_file_path, file_path))

    if '*' in file_path:
        raise Exception('WildCard found in file path !!!!. Cannot delete')

    run_command(context, 'rm -rf %s' % file_path)

@then('there should be dump files under "{directory}" with prefix "{prefix}"')
def impl(context, directory, prefix):
    if not hasattr(context, "dump_prefix"):
        context.dump_prefix = ''
    dump_prefix = '%s_gp' % prefix.strip()
    master_dump_dir = directory if len(directory.strip()) != 0 else master_data_dir
    segment_dump_files = get_segment_dump_files(context, directory)

    for seg, dump_files in segment_dump_files:
        segment_dump_dir =  directory if len(directory.strip()) != 0 else seg.getSegmentDataDirectory()
        if len(dump_files) == 0:
            raise Exception('Failed to find dump files on the segment %s under %s/db_dumps/%s' % (seg.getSegmentDataDirectory(), segment_dump_dir, context.backup_timestamp[0:8]))
  
        for dump_file in dump_files:
            if not dump_file.startswith(dump_prefix):
                raise Exception('Dump file %s on the segment %s under %s/db_dumps/%s does not start with required prefix %s' % (dump_file, seg.getSegmentDataDirectory(), segment_dump_dir, context.backup_timestamp[0:8], prefix))
 
    cmd = Command('check dump files', 'ls %s/db_dumps/%s' % (master_dump_dir, context.backup_timestamp[0:8]))
    cmd.run(validateAfter=True)
    results = cmd.get_results().stdout.strip().split('\n')
    
    if len(results) == 0:
        raise Exception('Failed to find dump files %s on the master under %s' % (results, master_dump_dir))
    for file in results:
        if not file.startswith(prefix.strip()):
            raise Exception('Dump file %s on master under %s does not have required prefix %s' %(file, master_dump_dir, prefix))

@given('the environment variable "{var}" is not set')
def impl(context, var):
    context.env_var = os.environ.get(var)
    os.environ[var] = ''

@then('the environment variable "{var}" is reset')
def impl(context, var):
    if hasattr(context, 'env_var'):
        os.environ[var] = context.env_var
    else:
        raise Exception('Environment variable %s cannot be reset because its value was not saved.' % var)

@given('the environment variable "{var}" is set to "{val}"')
def impl(context, var, val):
    context.env_var = os.environ.get(var)
    os.environ[var] = val

@given('the path "{path}" exists')
def impl(context, path):
    if not os.path.exists(path):
        os.makedirs(path)

@then('the path "{path}" does not exist')
def impl(context, path):
    if os.path.exists(path):
        shutil.rmtree(path)

@when('the user runs the following query on "{dbname}" without fetching results')
def impl(context, dbname):
    query = context.text.strip()
    thread.start_new_thread(execute_sql, (dbname, query))
    time.sleep(30)

@when('the user runs query from the file "{filename}" on "{dbname}" without fetching results')
def impl(context, filename, dbname):
    with open(filename) as fr:
        for line in fr:
            query = line.strip() 
    thread.start_new_thread(execute_sql, (dbname, query))
    time.sleep(30)

@then('the following text should be printed to stdout')
def impl(context):
   check_stdout_msg(context, context.text.strip()) 

@then('the text in the file "{filename}" should be printed to stdout')
def impl(context, filename):
    contents = ''
    with open(filename) as fr:
        for line in fr:
            contents = line.strip()       
    print "contents: ", contents
    check_stdout_msg(context, contents) 

@when('the user runs command "{cmd}" on the "{seg_type}" segment') 
def impl(context, cmd, seg_type):
    if seg_type == 'Change Tracking':
        port, host = get_change_tracking_segment_info() 
    elif seg_type == 'Original':
        port, host = context.killed_seg_port, context.killed_seg_host
    else:
        raise Exception('Invalid segment type "%s" specified' % seg_type)

    cmd += ' -p %s -h %s' % (port, host)

    run_command(context, cmd)
    
@given('below sql is executed in "{dbname}" db')
@when('below sql is executed in "{dbname}" db')
def impl(context, dbname):
    sql = context.text
    execute_sql(dbname, sql)

@when('sql "{sql}" is executed in "{dbname}" db')
def impl(context, sql, dbname):
    execute_sql(dbname, sql)

@when('execute following sql in db "{dbname}" and store result in the context')
def impl(context, dbname):
    context.stored_rows = []

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        curs = dbconn.execSQL(conn, context.text)
        context.stored_rows = curs.fetchall()


@when('execute sql "{sql}" in db "{dbname}" and store result in the context')
def impl(context, sql, dbname):
    context.stored_rows = []

    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        curs = dbconn.execSQL(conn, sql)
        context.stored_rows = curs.fetchall()

@then('validate that "{message}" is in the stored rows')
def impl(context, message):
    for row in context.stored_rows:
        for column in row:
            if message in column:
                return

    print context.stored_rows
    print message 
    raise Exception("'%s' not found in stored rows" % message)

@then('verify that file "{filename}" exists under "{path}"')
def impl(context, filename, path):
    fullpath = "%s/%s" % (path, filename)
    fullpath = os.path.expandvars(fullpath)

    if not os.path.exists(fullpath):
        raise Exception('file "%s" is not exist' % fullpath)

@given('waiting "{second}" seconds')
def impl(context, second):
    time.sleep(float(second))

def get_opened_files(filename, pidfile):
    cmd = "if [ `uname -s` = 'SunOS' ]; then CMD=pfiles; else CMD='lsof -p'; fi && PATH=$PATH:/usr/bin:/usr/sbin $CMD `cat %s` | grep %s | wc -l" % (pidfile, filename)
    return commands.getstatusoutput(cmd)

@then('the file "{filename}" by process "{pidfile}" is not closed')
def impl(context, filename, pidfile):
    (ret, output) = get_opened_files(filename, pidfile)
    if int(output) == 0:
        raise Exception('file %s has been closed' % (filename))

@then('the file "{filename}" by process "{pidfile}" is closed')
def impl(context, filename, pidfile):
    (ret, output) = get_opened_files(filename, pidfile)
    if int(output) != 0:
        raise Exception('file %s has not been closed' % (filename))

@then('the file "{filename}" by process "{pidfile}" opened number is "{num}"')
def impl(context, filename, pidfile, num):
    (ret, output) = get_opened_files(filename, pidfile)
    if int(output) != int(num):
        raise Exception('file %s opened number %d is not %d' % (filename, int(output), int(num)))

@given('the directory {path} exists')
@then('the directory {path} exists')
def impl(context, path):
   if not os.path.isdir(path):
       raise Exception('Directory "%s" does not exist' %path)

@then('{file} should be found in tarball with prefix "{prefix}" within directory {path}')
def impl(context, file, prefix, path):

    ## look for subdirectory created during collection
    collection_dirlist = os.listdir(path)

    if len(collection_dirlist) > 1:
        raise Exception('more then one data collection directory found.')
    if len(collection_dirlist) == 0:
        raise Exception('Collection directory was not found')

    ## get a listing of files in the subdirectory and make sure there is only one tarball found
    tarpath = os.path.join(path, collection_dirlist[0])
    collection_filelist = os.listdir(tarpath)

    if len(collection_filelist) > 1: 
        raise Exception('Too many files found in "%s"' %tarpath)
    if len(collection_filelist) == 0:
        raise Exception('No files found in "%s"' %tarpath)

    ## Expand tarball with prefix "GP_LOG_COLLECTION_" and search for given file within collection
    if prefix in collection_filelist[0]:

        ## extract the root tarball
        tar = tarfile.open(os.path.join(tarpath, collection_filelist[0]), "r:")
        tar.extractall(tarpath)
        tar.close()

        FOUND = False
        for tfile in os.listdir(tarpath):
            if prefix in tfile:
                continue

            ## Find any tar file that contain given file
            segtar = tarfile.open(os.path.join(tarpath, tfile), "r:gz")
            for tarinfo in segtar:
                if tarinfo.isreg() and file in tarinfo.name:
                    FOUND = True
                    segtar.close()
                    break ## break segtar loop
        if FOUND:
            return ## we found the file so pass the test case
        else:
            ## the file should have been found in the first segment tarball
            raise Exception('Unable to find "%s" in "%s" tar file' % (file, collection_filelist[0]) )
    else:
        raise Exception('tarball with prefix "%s" was not found' %prefix)

@given('the directory {path} is removed or does not exist')
@when('the directory {path} is removed or does not exist')
@then('the directory {path} is removed or does not exist')
def impl(context, path):
    if '*' in path:
        raise Exception('Wildcard not supported')

    if path[0] == '~':
        path = os.path.join(os.path.expanduser('~'), path[2:])

    run_command(context, 'rm -rf %s' % path)

@given('the user runs sbin command "{cmd}"')
@when('the user runs sbin command "{cmd}"')
@then('the user runs sbin command "{cmd}"')
def impl(context, cmd):
    gpsbin = os.path.join(os.environ.get('GPHOME'), 'sbin')

    if not os.path.isdir(gpsbin):
        raise Exception('ERROR: GPHOME not set in environment')

    cmd = gpsbin + "/" + cmd  ## don't us os.path join because command might have arguments
    run_command(context, cmd)


@given('the OS type is not "{os_type}"')
def impl(context, os_type):
    assert platform.system() != os_type

@then('{file1} and {file2} should exist and have a new mtime')
def impl(context, file1, file2):
    gphome = os.environ.get('GPHOME')
   
    if not os.path.isfile(os.path.join(gphome, file1)) or not os.path.isfile(os.path.join(gphome, file2)):
        raise Exception('installation of ' + context.utility + ' failed because one or more files do not exist in ' + os.path.join(gphome, file1))

    file1_mtime = os.path.getmtime(os.path.join(gphome, file1))
    file2_mtime = os.path.getmtime(os.path.join(gphome, file2))
    
    if file1_mtime < context.currenttime or file2_mtime < context.currenttime:
        raise Exception('one or both file mtime was not updated')

    os.chdir(context.cwd)
    run_command(context, 'rm -rf %s' % context.path)

@given('you are about to run a database query')
@when('you are about to run a database query')
@then('you are about to run a database query')
def impl(context):
    context.sessionID = []

@given('the user ran this query "{query}" against database "{dbname}" for table "{table}" and it hangs')
@when('the user ran this query "{query}" against database "{dbname}" for table "{table}" and it hangs')
def impl(context, query, dbname, table):
    get_sessionID_query = "select sess_id from pg_stat_activity where current_query ~ '" + table + "' and current_query !~ 'pg_stat_activity'"

    if not check_db_exists(dbname):
        raise Exception('The database ' + dbname + 'Does not exist')

    thread.start_new_thread(getRows, (dbname, query))
    time.sleep(15)
    context.sessionIDRow = getRows(dbname, get_sessionID_query)

    if len(context.sessionIDRow) == 0:
        raise Exception('Was not able to determine the session ID')

    context.sessionID.append( context.sessionIDRow[0][0] )


@then('user runs "{command}" against the queries session ID')
def impl(context, command):

    ## build the session_list
    session_list = None
    for session in context.sessionID:
        if not session_list:
            session_list = str(session)
        else:
            session_list = session_list + ',' + str(session)

    command += " " + session_list
    run_gpcommand(context, command)

@then('{file} file with queries sessionIDs should be found within directory {path}')
def impl(context, file, path):

    ######################################################################################
    ## This function needs to be modified.. changes are pending hung_analyzer revisions ##
    ######################################################################################


    ## look for subdirectory created during collection
    collection_dirlist = os.listdir(path)

    if len(collection_dirlist) > 1:
        raise Exception('more then one data collection directory found.  Possibly left over from a previous run of hung analyzer')
    if len(collection_dirlist) == 0:
        raise Exception('Collection directory was not found')

    ## Make sure we have a core file for each session
    sessions_found = 0
    for rootdir, dirs, files in os.walk(os.path.join(path, collection_dirlist[0])):
        for session in context.sessionID:
            for f in files:
                core_prefix = file + str(session) + '.'
                if core_prefix in f:
                    sessions_found += 1
                    break

    if sessions_found == 0:
        raise Exception('No core files were found in collection')

    if sessions_found != len(context.sessionID):
        raise Exception('Only ' + str(sessions_found) + ' core files were found out of ' + str(len(context.sessionID)))

@then('{file} file should be found within directory {path}')
def impl(context, file, path):
    ## look for subdirectory created during collection
    collection_dirlist = os.listdir(path)

    if len(collection_dirlist) > 1:
        raise Exception('more then one data collection directory found.  Possibly left over from a previous run of hung analyzer')
    if len(collection_dirlist) == 0:
        raise Exception('Collection directory was not found')

    ## get a listing of files and dirs and prune until file is found
    for rootdir, dirs, files in os.walk(os.path.join(path, collection_dirlist[0])):
        for f in files:
            if file in f:
                return

    raise Exception('File was not found in :' + path)


@then('database is restarted to kill the hung query')
def impl(context):
    try:
        stop_database_if_started(context)
    except Exception as e:
        context.exception = None
        pass   ## capture the thread dieing from our hung query

    if check_database_is_running(context):
        raise Exception('Failed to stop the database')

    start_database_if_not_started(context)
    if not check_database_is_running():
        raise Exception('Failed to start the database')
@then('partition "{partitionnum}" is added to partition table "{tablename}" in "{dbname}"')
def impl(context, partitionnum, tablename, dbname):
     add_partition(context, partitionnum, tablename, dbname)

@then('partition "{partitionnum}" is dropped from partition table "{tablename}" in "{dbname}"')
def impl(context, partitionnum, tablename, dbname):
     drop_partition(context, partitionnum, tablename, dbname)

@when('table "{tablename}" is dropped in "{dbname}"')
@then('table "{tablename}" is dropped in "{dbname}"')
def impl(context, tablename, dbname):
    drop_sql = """DROP TABLE %s""" % tablename
    execute_sql(dbname, drop_sql)

def create_trigger_function(dbname, trigger_func_name, tablename):
    trigger_func_sql = """ 
    CREATE OR REPLACE FUNCTION %s() RETURNS TRIGGER AS $$
    BEGIN
    INSERT INTO %s VALUES(2001, 'backup', '2100-08-23');
    END;
    $$ LANGUAGE plpgsql
    """ % (trigger_func_name, tablename)
    execute_sql(dbname, trigger_func_sql) 

def create_trigger(dbname, trigger_func_name, trigger_name, tablename):
    SQL = """
    CREATE TRIGGER %s AFTER INSERT OR UPDATE OR DELETE ON %s FOR EACH STATEMENT EXECUTE PROCEDURE %s();
    """ % (trigger_name, tablename, trigger_func_name)
    execute_sql(dbname, SQL)

@given('there is a trigger "{trigger_name}" on table "{tablename}" in "{dbname}" based on function "{trigger_func_name}"')
def impl(context, trigger_name, tablename, dbname, trigger_func_name):
    create_trigger_function(dbname, trigger_func_name, tablename)
    create_trigger(dbname, trigger_func_name, trigger_name, tablename)

@then('there is a trigger function "{trigger_func_name}" on table "{tablename}" in "{dbname}"')
def impl(context, trigger_func_name, tablename, dbname):
    create_trigger_function(dbname, trigger_func_name, tablename) 

@when('the index "{index_name}" in "{dbname}" is dropped')
def impl(context, index_name, dbname):
    drop_index_sql = """DROP INDEX %s""" % index_name
    execute_sql(dbname, drop_index_sql)

@when('the trigger "{trigger_name}" on table "{tablename}" in "{dbname}" is dropped')
def impl(context, trigger_name, tablename, dbname):
    drop_trigger_sql = """DROP TRIGGER %s ON %s""" % (trigger_name, tablename)
    execute_sql(dbname, drop_trigger_sql)

@given('all the segments are running')
@when('all the segments are running')
@then('all the segments are running')
def impl(context):
    if not are_segments_running():
        raise Exception("all segments are not currently running")

    return

@given('the "{seg}" segment information is saved')
@when('the "{seg}" segment information is saved')
@then('the "{seg}" segment information is saved')
def impl(context, seg):
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
   
    if seg == "primary": 
        primary_segs = [seg for seg in gparray.getDbList() if seg.isSegmentPrimary()]
        context.pseg = primary_segs[0]
        context.pseg_data_dir = context.pseg.getSegmentDataDirectory()
        context.pseg_hostname = context.pseg.getSegmentHostName()
        context.pseg_dbid = context.pseg.getSegmentDbId()
    elif seg == "mirror":
        mirror_segs = [seg for seg in gparray.getDbList() if seg.isSegmentMirror()]
        context.mseg = mirror_segs[0]
        context.mseg_hostname = context.mseg.getSegmentHostName()
        context.mseg_dbid = context.mseg.getSegmentDbId()
        context.mseg_data_dir = context.mseg.getSegmentDataDirectory()

@when('we run a sample background script to generate a pid on "{seg}" segment')
def impl(context, seg):
    if seg == "primary":
        if not hasattr(context, 'pseg_hostname'):
            raise Exception("primary seg host is not saved in the context")
        hostname = context.pseg_hostname
    elif seg == "smdw":
        if not hasattr(context, 'standby_host'):
            raise Exception("Standby host is not saved in the context")
        hostname = context.standby_host

    filename = os.path.join(os.getcwd(), './gppylib/test/behave/mgmt_utils/steps/data/pid_background_script.py')
    
    cmd = Command(name="Remove background script on remote host", cmdStr='rm -f /tmp/pid_background_script.py', remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)

    cmd = Command(name="Copy background script to remote host", cmdStr='scp %s %s:/tmp' % (filename, hostname))
    cmd.run(validateAfter=True)

    cmd = Command(name="Run Bg process to save pid", cmdStr='sh -c "python /tmp/pid_background_script.py" &>/dev/null &', remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)

    cmd = Command(name="get bg pid", cmdStr="ps ux | grep pid_background_script.py | grep -v grep | awk '{print \$2}'", remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)
    context.bg_pid = cmd.get_results().stdout.strip()
    if not context.bg_pid:
        raise Exception("Unable to obtain the pid of the background script. Seg Host: %s, get_results: %s" % (hostname, cmd.get_results().stdout.strip()))
 
@when('the background pid is killed on "{seg}" segment')
@then('the background pid is killed on "{seg}" segment')
def impl(context, seg):
    if seg == "primary":
        if not hasattr(context, 'pseg_hostname'):
            raise Exception("primary seg host is not saved in the context")
        hostname = context.pseg_hostname
    elif seg == "smdw":
        if not hasattr(context, 'standby_host'):
            raise Exception("Standby host is not saved in the context")
        hostname = context.standby_host

    cmd = Command(name="get bg pid", cmdStr="ps ux | grep pid_background_script.py | grep -v grep | awk '{print \$2}'", remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)
    pids = cmd.get_results().stdout.strip().splitlines()

    for pid in pids:
        cmd = Command(name="killbg pid", cmdStr='kill -9 %s' % pid, remoteHost=hostname, ctxt=REMOTE)
        cmd.run(validateAfter=True)

@when('we generate the postmaster.pid file with the background pid on "{seg}" segment')
def impl(context, seg):
    if seg == "primary":
        if not hasattr(context, 'pseg_hostname'):
            raise Exception("primary seg host is not saved in the context")
        hostname = context.pseg_hostname
        data_dir = context.pseg_data_dir
    elif seg == "smdw":
        if not hasattr(context, 'standby_host'):
            raise Exception("Standby host is not saved in the context")
        hostname = context.standby_host
        data_dir = context.standby_host_data_dir

    
    pid_file = os.path.join(data_dir, 'postmaster.pid')
    pid_file_orig = pid_file + '.orig'

    cmd = Command(name="Copy pid file", cmdStr='cp %s %s' % (pid_file_orig, pid_file), remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)

    cpCmd = Command(name='copy pid file to master for editing', cmdStr='scp %s:%s /tmp' % (hostname, pid_file))

    cpCmd.run(validateAfter=True)

    with open('/tmp/postmaster.pid', 'r') as fr:
        lines = fr.readlines()

    lines[0] = "%s\n" % context.bg_pid

    with open('/tmp/postmaster.pid', 'w') as fw:
        fw.writelines(lines)

    cpCmd = Command(name='copy pid file to segment after editing', cmdStr='scp /tmp/postmaster.pid %s:%s' % (hostname, pid_file))
    cpCmd.run(validateAfter=True)
 
@when('we generate the postmaster.pid file with a non running pid on the same "{seg}" segment')
def impl(context, seg):
    if seg == "primary":
        data_dir = context.pseg_data_dir
        hostname = context.pseg_hostname
    elif seg == "mirror":
        data_dir = context.mseg_data_dir
        hostname = context.mseg_hostname
    elif seg == "smdw":
        if not hasattr(context, 'standby_host'):
            raise Exception("Standby host is not saved in the context")
        hostname = context.standby_host
        data_dir = context.standby_host_data_dir

    pid_file = os.path.join(data_dir, 'postmaster.pid')
    pid_file_orig = pid_file + '.orig'
    
    cmd = Command(name="Copy pid file", cmdStr='cp %s %s' % (pid_file_orig, pid_file), remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)

    cpCmd = Command(name='copy pid file to master for editing', cmdStr='scp %s:%s /tmp' % (hostname, pid_file))

    cpCmd.run(validateAfter=True)

    with open('/tmp/postmaster.pid', 'r') as fr:
        pid = fr.readline().strip()

    while True:
        cmd = Command(name="get non-existing pid", cmdStr="ps ux | grep %s | grep -v grep | awk '{print \$2}'" % pid, remoteHost=hostname, ctxt=REMOTE)
        cmd.run(validateAfter=True)
        if cmd.get_results().stdout.strip():
            pid = pid + 1
        else:
            break

    
    with open('/tmp/postmaster.pid', 'r') as fr:
        lines = fr.readlines()

    lines[0] = "%s\n" % pid

    with open('/tmp/postmaster.pid', 'w') as fw:
        fw.writelines(lines)

    cpCmd = Command(name='copy pid file to segment after editing', cmdStr='scp /tmp/postmaster.pid %s:%s' % (hostname, pid_file))
    cpCmd.run(validateAfter=True)
        
@when('the user starts one "{seg}" segment')
def impl(context, seg):
    if seg == "primary":
        dbid = context.pseg_dbid
        hostname = context.pseg_hostname
        segment = context.pseg
    elif seg == "mirror":
        dbid = context.mseg_dbid
        hostname = context.mseg_hostname
        segment = context.mseg

    segStartCmd = SegmentStart( name = "Starting new segment dbid %s on host %s." % (str(dbid), hostname)
                              , gpdb = segment
                              , numContentsInCluster = 0  # Starting seg on it's own.
                              , era = None 
                              , mirrormode = MIRROR_MODE_MIRRORLESS
                              , utilityMode = False
                              , ctxt = REMOTE
                              , remoteHost = hostname
                              , noWait = False
                              , timeout = 300)
    segStartCmd.run(validateAfter=True)


@when('the postmaster.pid file on "{seg}" segment is saved')
def impl(context, seg):
    if seg == "primary":
        data_dir = context.pseg_data_dir
        hostname = context.pseg_hostname
    elif seg == "mirror":
        data_dir = context.mseg_data_dir
        hostname = context.mseg_hostname
    elif seg == "smdw":
        if not hasattr(context, 'standby_host'):
            raise Exception("Standby host is not saved in the context")
        hostname = context.standby_host
        data_dir = context.standby_host_data_dir

    pid_file = os.path.join(data_dir, 'postmaster.pid')
    pid_file_orig = pid_file + '.orig'
        
    cmd = Command(name="Copy pid file", cmdStr='cp %s %s' % (pid_file, pid_file_orig), remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)
 
@then('the backup pid file is deleted on "{seg}" segment')
def impl(context, seg):
    if seg == "primary":
        data_dir = context.pseg_data_dir
        hostname = context.pseg_hostname
    elif seg == "mirror":
        data_dir = context.mseg_data_dir
        hostname = context.mseg_hostname
    elif seg == "smdw":
        data_dir = context.standby_host_data_dir
        hostname = context.standby_host

    cmd = Command(name="Remove pid file", cmdStr='rm -f %s' % (os.path.join(data_dir, 'postmaster.pid.orig')), remoteHost=hostname, ctxt=REMOTE)
    cmd.run(validateAfter=True)

@given('the user creates an init config file "{to_file}" without mirrors')
@when('the user creates an init config file "{to_file}" without mirrors')
@then('the user creates an init config file "{to_file}" without mirrors')
def impl(context, to_file):
    write_lines = []
    BLDWRAP_TOP = os.environ.get('BLDWRAP_TOP')
    from_file = BLDWRAP_TOP + '/sys_mgmt_test/test/general/cluster_conf.out'
    with open(from_file) as fr:
        lines = fr.readlines()
        for line in lines:
            if not line.startswith('REPLICATION_PORT_BASE') and not line.startswith('MIRROR_REPLICATION_PORT_BASE') and not line.startswith('MIRROR_PORT_BASE') and not line.startswith('declare -a MIRROR_DATA_DIRECTORY'):
                write_lines.append(line)

    with open(to_file, 'w+') as fw:
        fw.writelines(write_lines)

@given('the user creates mirrors config file "{to_file}"')
@when('the user creates mirrors config file "{to_file}"')
@then('the user creates mirrors config file "{to_file}"')
def impl(context, to_file):
    data_dirs = []
    BLDWRAP_TOP = os.environ.get('BLDWRAP_TOP')
    from_file = BLDWRAP_TOP + '/sys_mgmt_test/test/general/cluster_conf.out'
    with open(from_file) as fr:
        lines = fr.readlines()
        for line in lines:
            if line.startswith('declare -a MIRROR_DATA_DIRECTORY'):
                data_dirs = line.split('(')[-1].strip().strip(')').split()
                break

    if not data_dirs:
        raise Exception("Could not find MIRROR_DATA_DIRECTORY in config file %s" % from_file)

    with open(to_file, 'w+') as fw:
        for dir in data_dirs:
            fw.write(dir.strip(')') + '\n')

@given('the standby hostname is saved')
@when('the standby hostname is saved')
@then('the standby hostname is saved')
def impl(context):
    gparray = GpArray.initFromCatalog(dbconn.DbURL())
    primary_segs = [seg for seg in gparray.getDbList() if (seg.isSegmentPrimary() and not seg.isSegmentMaster())]
    context.standby = primary_segs[0].getSegmentHostName()
    
@given('user runs the init command "{cmd}" with the saved standby host')
@when('user runs the init command "{cmd}" with the saved standby host')
@then('user runs the init command "{cmd}" with the saved standby host')
def impl(context, cmd):
    run_cmd = cmd + '-s %s' % context.standby
    run_cmd.run(validateAfter=True)

@given('there is a sequence "{seq_name}" in "{dbname}"')
def impl(context, seq_name, dbname):
    sequence_sql = 'CREATE SEQUENCE %s' % seq_name
    execute_sql(dbname, sequence_sql) 

@when('the user removes the "{cmd}" command on standby')
@then('the user removes the "{cmd}" command on standby')
def impl(context, cmd):
    cmdStr = 'chmod u+rw ~/.bashrc && cp ~/.bashrc ~/.bashrc.backup'
    run_cmd = Command('run remote command', cmdStr, ctxt=REMOTE, remoteHost=context.standby_host)
    run_cmd.run(validateAfter=True)
    cmdStr = """echo >>~/.bashrc && echo "shopt -s expand_aliases" >>~/.bashrc && echo "alias %s='no_command'" >>~/.bashrc""" % cmd
    run_cmd = Command('run remote command', cmdStr, ctxt=REMOTE, remoteHost=context.standby_host)
    run_cmd.run(validateAfter=True)

@when('the user restores the "{cmd}" command on the standby')
@then('the user restores the "{cmd}" command on the standby')
def impl(context, cmd):
    cmdStr = 'cp ~/.bashrc.backup ~/.bashrc'
    run_cmd = Command('run remote command', cmdStr, ctxt=REMOTE, remoteHost=context.standby_host)
    run_cmd.run(validateAfter=True)
   
@when('the user stops the syncmaster')
@then('the user stops the syncmaster')
def impl(context):
    host   = context.gparray.standbyMaster.hostname
    #Cat is added because pgrep returns all the processes of the tree, while
    #child processes are kill when the parent is kill, which produces an error
    cmdStr = 'pgrep syncmaster | xargs -i kill {} | cat'
    run_cmd = Command('kill syncmaster', cmdStr, ctxt=REMOTE, remoteHost=host)
    run_cmd.run(validateAfter=True)
    datadir=context.gparray.standbyMaster.datadir

@when('the user starts the syncmaster')
@then('the user starts the syncmaster')
def impl(context):
    host=context.gparray.standbyMaster.hostname
    datadir=context.gparray.standbyMaster.datadir
    port=context.gparray.standbyMaster.port
    dbid=context.gparray.standbyMaster.dbid
    ncontents = context.gparray.getNumSegmentContents()
    GpStandbyStart.remote('test start syncmaster',host,datadir,port,ncontents,dbid)

@when('save the cluster configuration')
@then('save the cluster configuration')
def impl(context):
    context.gparray = GpArray.initFromCatalog(dbconn.DbURL())

@given('partition "{partition}" of partition table "{schema_parent}.{table_name}" is assumed to be in schema "{schema_child}" in database "{dbname}"')
@when('partition "{partition}" of partition table "{schema_parent}.{table_name}" is assumed to be in schema "{schema_child}" in database "{dbname}"')
@then('partition "{partition}" of partition table "{schema_parent}.{table_name}" is assumed to be in schema "{schema_child}" in database "{dbname}"')
def impl(context, partition, schema_parent, table_name, schema_child, dbname):
    part_t = get_partition_names(schema_parent.strip(), table_name.strip(), dbname.strip(), 1, partition)
    if len(part_t) < 1 or len(part_t[0]) < 1: 
        print part_t 
    a_partition_name = part_t[0][0].strip()
    alter_sql = """ALTER TABLE %s SET SCHEMA %s""" % (a_partition_name, schema_child)
    execute_sql(dbname, alter_sql) 


@given('this test sleeps for "{secs}" seconds')
@when('this test sleeps for "{secs}" seconds')
@then('this test sleeps for "{secs}" seconds')
def impl(context, secs):
    secs = float(secs)
    time.sleep(secs)


@then('verify that there are no duplicates in column "{columnname}" of table "{tablename}" in "{dbname}"')
def impl(context, columnname, tablename, dbname):
    duplicate_sql = 'SELECT %s, COUNT(*) FROM %s GROUP BY %s HAVING COUNT(*) > 1' % (columnname, tablename, columnname)
    rows = getRows(dbname, duplicate_sql)
    if rows:
        raise Exception('Found duplicate rows in the column "%s" for table "%s" in database "%s"' % (columnname, tablename, dbname))

def execute_sql_for_sec(dbname, query, sec):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        dbconn.execSQL(conn, query)
        conn.commit()
        time.sleep(sec)

@given('the user runs the query "{query}" on "{dbname}" for "{sec}" seconds')
@when('the user runs the query "{query}" on "{dbname}" for "{sec}" seconds')
@then('the user runs the query "{query}" on "{dbname}" for "{sec}" seconds')
def impl(context, query, dbname, sec):
    if query.lower().startswith('create') or query.lower().startswith('insert'):
        thread.start_new_thread(execute_sql_for_sec, (dbname, query, float(sec)))
    else:
        thread.start_new_thread(getRows, (dbname, query))
    time.sleep(30)

@given('verify that the contents of the files "{filepath1}" and "{filepath2}" are identical')
@when('verify that the contents of the files "{filepath1}" and "{filepath2}" are identical')
@then('verify that the contents of the files "{filepath1}" and "{filepath2}" are identical')
def impl(context, filepath1, filepath2):
    contents1 = []
    contents2 = [] 
    with open(filepath1) as fr1:
        contents1 = fr1.readlines()

    with open(filepath2) as fr2:
        contents2 = fr2.readlines()

    if (contents1 != contents2):
        raise Exception("Contents of the files: %s and %s do not match" % (filepath1, filepath2))

def get_gp_toolkit_info(context, dbname, fname):
    cmdStr = """psql -c '\d gp_toolkit.*' -d %s > %s""" % (dbname, fname)
    cmd = Command(name='get gp_toolkit info to file', cmdStr=cmdStr)
    cmd.run(validateAfter=True)

@given('the gp_toolkit schema for "{dbname}" is saved for verification')
def impl(context, dbname):
    get_gp_toolkit_info(context, dbname, 'gp_toolkit_backup')

@then('the gp_toolkit schema for "{dbname}" is verified after restore')
def impl(context, dbname):
    get_gp_toolkit_info(context, dbname, 'gp_toolkit_restore')
    diff_backup_restore_data(context, 'gp_toolkit_backup', 'gp_toolkit_restore')

@given('the standby is not initialized')
@then('the standby is not initialized')
def impl(context):
    standby = get_standby_host()
    if standby:
        context.cluster_had_standby = True
        context.standby_host = standby
        run_gpcommand(context, 'gpinitstandby -ra')

@given('"{path}" has "{perm}" permissions')
@then('"{path}" has "{perm}" permissions')
def impl(context, path, perm):
    path = os.path.expandvars(path)
    if not os.path.exists(path):
        raise Exception('Path does not exist! "%s"' % path)
    os.chmod(path, int(perm, 8))

@when('user can "{can_ssh}" ssh locally on standby')
@then('user can "{can_ssh}" ssh locally on standby')
def impl(context, can_ssh):
    if not hasattr(context, 'standby_host'):
        raise Exception('Standby host not stored in context !')
    if can_ssh.strip() == 'not':
        cmdStr = 'mv ~/.ssh/authorized_keys ~/.ssh/authorized_keys.bk'
    else:
        cmdStr = 'mv ~/.ssh/authorized_keys.bk ~/.ssh/authorized_keys'
    cmd = Command(name='disable ssh locally', cmdStr=cmdStr,
                  ctxt=REMOTE, remoteHost=context.standby_host)
    cmd.run(validateAfter=True)

@given('all the compression data from "{dbname}" is saved for verification')
def impl(context, dbname):
    partitions = get_partition_list('ao', dbname) + get_partition_list('co', dbname)
    with open('gppylib/test/data/compression_{db}_backup'.format(db=dbname), 'w') as fp:
        with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
            for p in partitions:
                query = """SELECT get_ao_compression_ratio('{schema}.{table}')""".format(schema=p[1], table=p[2])
                compression_rate = dbconn.execSQLForSingleton(conn, query)
                fp.write('{schema}.{table}:{comp}\n'.format(schema=p[1], table=p[2], comp=compression_rate))

@then('verify that the compression ratio of "{table}" in "{dbname}" is good')
def impl(context, table, dbname):
    with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
        query = """SELECT get_ao_compression_ratio('{table}')""".format(table=table)
        compression_rate = dbconn.execSQLForSingleton(conn, query)

    found = False
    with open('gppylib/test/data/compression_{db}_backup'.format(db=dbname)) as fp:
        for line in fp:
            t, c = line.split(':')
            if t == table:
                if float(c) != compression_rate and float(c) - 0.1 * float(c) > compression_rate: #10% more than original compression rate
                    raise Exception('Expected compression ratio to be greater than or equal to %s but got %s' % (c, compression_rate))
            found = True 
    if not found:
       raise Exception('Compression ratio for table %s was not stored' % table)

@then('verify that the data of tables in "{dbname}" is validated after reload')
def impl(context, dbname):
    tbls = get_table_names(dbname)
    backed_up_data = []
    reloaded_data = []
    for t in tbls:
        with open('gppylib/test/data/%s.%s_backup' % (t[0], t[1])) as fp:
            for line in fp:
                toks = line.split()
                backed_up_data.append(' '.join(toks[1:])) #Ignore the gp_segment_id value since it changes after reload
        with dbconn.connect(dbconn.DbURL(dbname=dbname)) as conn:
            res = dbconn.execSQL(conn, 'select * from %s.%s' % (t[0], t[1]))
            for r in res:
                reloaded_data.append(' '.join([str(x) for x in r]))
        if sorted(reloaded_data) != sorted(backed_up_data):
            raise Exception('Data does not match for table %s.%s' % (t[0], t[1]))

@given('the schemas "{schema_list}" do not exist in "{dbname}"')
@then('the schemas "{schema_list}" do not exist in "{dbname}"')
def impl(context, schema_list, dbname):
    schemas = [s.strip() for s in schema_list.split(',')]
    for s in schemas:
        drop_schema_if_exists(context, s.strip(), dbname)

@then('verify that the schema "{schema_name}" exists in "{dbname}"')
def impl(context, schema_name, dbname):
    check_schema_exists(context, schema_name, dbname)

def get_gptransfer_log_name(logdir):
    today = datetime.now()
    logname = "%s/gptransfer_%s.log" % (logdir, today.strftime('%Y%m%d'))
    return logname

@then('verify that a log was created by gptransfer in the user\'s "{dirname}" directory')
def impl(context, dirname):
    absdirname = "%s/%s" % (os.path.expanduser("~"), dirname)
    if not os.path.exists(absdirname):
        raise Exception('No such directory: %s' % absdirname)
    logname = get_gptransfer_log_name(absdirname)
    if not os.path.exists(logname):
        raise Exception('Log "%s" was not created' % logname)

@then('verify that a log was created by gptransfer in the "{dirname}" directory')
def impl(context, dirname):
    if not os.path.exists(dirname):
        raise Exception('No such directory: %s' % dirname)
    logname = get_gptransfer_log_name(dirname)
    if not os.path.exists(logname):
        raise Exception('Log "%s" was not created' % logname)

@given('a table is created containing rows of length "{length}" with connection "{dbconn}"')
def impl(context, length, dbconn):
    length = int(length)
    wide_row_file = 'gppylib/test/behave/mgmt_utils/steps/data/gptransfer/wide_row_%s.sql' % length
    tablename = 'public.wide_row_%s' % length
    entry = "x" * length
    with open (wide_row_file, 'w') as sql_file:
        sql_file.write("CREATE TABLE %s (a integer, b text);\n" % tablename) 
        for i in range(10):
            sql_file.write("INSERT INTO %s VALUES (%d, \'%s\');\n" % (tablename, i, entry))
    command = '%s -f %s'%(dbconn, wide_row_file)
    run_gpcommand(context, command)


@then('drop the table "{tablename}" with connection "{dbconn}"')
def impl(context, tablename, dbconn):
    command = "%s -c \'drop table if exists %s\'"%(dbconn, tablename) 
    run_gpcommand(context, command)


# gptransfer must be run in verbose mode (-v) with default log location when using this step
@then('verify that gptransfer has a sub batch size of "{num}"')
def impl(context, num):
    num = int(num)
    logdir = "%s/gpAdminLogs" % os.path.expanduser("~")
    if not os.path.exists(logdir):
        raise Exception('No such directory: %s' % absdirname)
    logname = get_gptransfer_log_name(logdir)

    full_path = os.path.join(logdir, logname)

    if not os.path.isfile(full_path):
        raise Exception ("Can not find %s file: %s" % (file_type, full_path))

    contents = "" 
    with open(full_path) as fd:
        contents = fd.read()

    for i in range(num):
        worker = "\[DEBUG\]:-\[worker%d\]" % i
        try:
            check_stdout_msg(context, worker)
        except:
            raise Exception("gptransfer sub batch size should be %d, is %d" % (num, i))

    worker = "\[DEBUG\]:-\[worker%d\]" % num
    try:
        check_string_not_present_stdout(context, worker)
    except:
        raise Exception("gptransfer sub batch size should be %d, is at least %d" % (num, num+1))

# Read in a full map file, remove the first host, print it to a new file
@given('an incomplete map file is created')
def impl(context):
    map_file = os.environ['GPTRANSFER_MAP_FILE']
    contents = []
    with open(map_file, 'r') as fd:
        contents = fd.readlines()

    with open('/tmp/incomplete_map_file', 'w') as fd:
        for line in contents[1:]:
            fd.write(line)

@given('there is a table "{table_name}" dependent on function "{func_name}" in database "{dbname}" on the source system')
def impl(context, table_name, func_name, dbname):
    dbconn = 'psql -d %s -p $GPTRANSFER_SOURCE_PORT -U $GPTRANSFER_SOURCE_USER -h $GPTRANSFER_SOURCE_HOST' % dbname
    SQL = """CREATE TABLE %s (num integer); CREATE FUNCTION %s (integer) RETURNS integer AS 'select abs(\$1);' LANGUAGE SQL IMMUTABLE; CREATE INDEX test_index ON %s (%s(num))""" % (table_name, func_name, table_name, func_name)
    command = '%s -c "%s"'%(dbconn, SQL)
    run_command(context, command)

@then('the function-dependent table "{table_name}" and the function "{func_name}" in database "{dbname}" are dropped on the source system')
def impl(context, table_name, func_name, dbname):
    dbconn = 'psql -d %s -p $GPTRANSFER_SOURCE_PORT -U $GPTRANSFER_SOURCE_USER -h $GPTRANSFER_SOURCE_HOST' % dbname
    SQL = """DROP TABLE %s; DROP FUNCTION %s(integer);""" % (table_name, func_name)
    command = '%s -c "%s"'%(dbconn, SQL)
    run_command(context, command)

@then('verify that function "{func_name}" exists in database "{dbname}"')
def impl(context, func_name, dbname):
    SQL = """SELECT proname FROM pg_proc WHERE proname = '%s';""" % func_name
    row_count = getRows(dbname, SQL)[0][0]
    if row_count != 'test_function':
        raise Exception('Function %s does not exist in %s"' % (func_name, dbname))

@when('the user runs the query "{query}" in database "{dbname}" and sends the output to "{filename}"')
def impl(context, query, dbname, filename):
    cmd = "psql -d %s -p $GPTRANSFER_DEST_PORT -U $GPTRANSFER_DEST_USER -c '\copy (%s) to %s'" % (dbname, query, filename)
    thread.start_new_thread(run_gpcommand, (context, cmd))
    time.sleep(10)

@when('the user runs the command "{cmd}" in the background')
def impl(context, cmd):
    thread.start_new_thread(run_command, (context,cmd))
    time.sleep(10)

@then('verify that the file "{filename}" contains the string "{output}"')
def impl(context, filename, output):
    contents = ''
    with open(filename) as fr:
        for line in fr:
            contents = line.strip()       
    print contents
    check_stdout_msg(context, output) 

@then('the user waits for "{process_name}" to finish running')
def impl(context, process_name):
     run_command(context, "ps ux | grep `which %s` | grep -v grep | awk '{print $2}' | xargs" % process_name)
     pids = context.stdout_message.split()
     while len(pids) > 0:
         for pid in pids:
             try:
                 os.kill(int(pid), 0)
             except OSError, error:
                 pids.remove(pid)
         time.sleep(10)

@given('the gpfdists occupying port {port} on host "{hostfile}"')
def impl(context, port, hostfile):
    remote_gphome = os.environ.get('GPHOME')
    gp_source_file = os.path.join(remote_gphome, 'greenplum_path.sh')
    source_map_file = os.environ.get(hostfile)
    dir = '/tmp'
    ctxt = 2
    with open(source_map_file,'r') as f:
        for line in f:
            host = line.strip().split(',')[0]
            if host in ('localhost', '127.0.0.1',socket.gethostname()):
                ctxt = 1
            gpfdist = Gpfdist('gpfdist on host %s'%host, dir, port, os.path.join('/tmp','gpfdist.pid'), ctxt, host, gp_source_file)
            gpfdist.startGpfdist()


@then('the gpfdists running on port {port} get cleaned up from host "{hostfile}"')
def impl(context, port, hostfile):
    remote_gphome = os.environ.get('GPHOME')
    gp_source_file = os.path.join(remote_gphome, 'greenplum_path.sh')
    source_map_file = os.environ.get(hostfile)
    dir = '/tmp'
    ctxt = 2
    with open(source_map_file,'r') as f:
        for line in f:
            host = line.strip().split(',')[0]
            if host in ('localhost', '127.0.0.1',socket.gethostname()):
                ctxt = 1
            gpfdist = Gpfdist('gpfdist on host %s'%host, dir, port, os.path.join('/tmp','gpfdist.pid'), ctxt, host, gp_source_file)
            gpfdist.cleanupGpfdist()

@when('verify that db_dumps directory does not exist in master or segments')
@then('verify that db_dumps directory does not exist in master or segments')
def impl(context):
    check_dump_dir_exists(context, 'template1')

@when('verify that the restored table "{table_name}" in database "{dbname}" is analyzed')
@then('verify that the restored table "{table_name}" in database "{dbname}" is analyzed')
def impl(context, table_name, dbname):
    if verify_restored_table_is_analyzed(context, table_name, dbname) is not True:
        raise Exception("The restored table \'%s\' of database \'%s\' is not analyzed" % (table_name, dbname))

@when('verify that the table "{table_name}" in database "{dbname}" is not analyzed')
@then('verify that the table "{table_name}" in database "{dbname}" is not analyzed')
def impl(context, table_name, dbname):
    if (verify_restored_table_is_analyzed(context, table_name, dbname)):
        raise Exception("The restored table \'%s\' of database \'%s\' is analyzed" % (table_name, dbname))

@given('the database "{dbname}" is analyzed')
def impl(context, dbname):
    analyze_database(context, dbname)

@when('the user deletes rows from the table "{table_name}" of database "{dbname}" where "{column_name}" is "{info}"')
@then('the user deletes rows from the table "{table_name}" of database "{dbname}" where "{column_name}" is "{info}"')
def impl(context, dbname, table_name, column_name, info):
    delete_rows_from_table(context, dbname, table_name, column_name, info)

@then('verify that the query "{query}" in database "{dbname}" returns "{nrows}"')
def impl(context, dbname, query, nrows):
    check_count_for_specific_query(dbname, query, int(nrows))

@then('verify that the file "{filepath}" contains "{line}"')
def impl(context, filepath, line):
    if line not in open(filepath).read():
        raise Exception("The file '%s' does not contain '%s'" % (filepath, line))

@then('verify that gptransfer is in order of "{filepath}"')
def impl(context, filepath):
    table = []
    with open(filepath) as f:
        input_file = f.read().splitlines()
        table = [x.replace('/', "")  for x in input_file]

    split_message = re.findall("Starting transfer of.*\n", context.stdout_message)

    if len(split_message) == 0 and len(table) != 0:
        raise Exception("There were no tables transfered")

    counter_table = 0
    counter_split = 0
    found = 0

    while counter_table < len(table) and counter_split < len(split_message):
        for i in range(counter_split, len(split_message)):
            pat = table[counter_table] + " to"
            prog = re.compile(pat)
            res = prog.search(split_message[i])
            if not res:
                counter_table += 1
                break
            else:
                found += 1
                counter_split += 1

    if found != len(split_message):
        raise Exception("expected to find %s tables in order and only found %s in order" % (len(split_message), found))

