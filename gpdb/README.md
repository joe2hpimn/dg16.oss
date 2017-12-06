![Greenplum](/gpAux/releng/images/logo-greenplum.png)

The Greenplum Database (GPDB) is an advanced, fully featured, open
source data warehouse. It provides powerful and rapid analytics on
petabyte scale data volumes. Uniquely geared toward big data
analytics, Greenplum Database is powered by the world’s most advanced
cost-based query optimizer delivering high analytical query
performance on large data volumes.

The Greenplum project is released under the [Apache 2
license](http://www.apache.org/licenses/LICENSE-2.0). We want to thank
all our current community contributors and are really interested in
all new potential contributions. For the Greenplum Database community
no contribution is too small, we encourage all types of contributions.

## Overview

A Greenplum cluster consists of a __master__ server, and multiple
__segment__ servers. All user data resides in the segments, the master
contains only metadata. The master server, and all the segments, share
the same schema.

Users always connect to the master server, which divides up the query
into fragments that are executed in the segments, sends the fragments
to the segments, and collects the results.

## Requirements

* From the GPDB doc set, review [Configuring Your Systems and
  Installing
  Greenplum](http://gpdb.docs.pivotal.io/4360/prep_os-overview.html#topic1)
  and perform appropriate updates to your system for GPDB use.

* **gpMgmt** utilities - command line tools for managing the cluster.

  You will need to add the following Python modules (2.7 & 2.6 are
  supported) into your installation

  * psi
  * lockfile
  * paramiko
  * setuptools
  * epydoc

   Ensure the ed text-editor is installed for gpinitsystem. For RHEL/Centos:
   ```
   sudo yum install ed
   ```

## Code layout

The directory layout of the repository follows the same general layout
as upstream PostgreSQL. There are changes compared to PostgreSQL
throughout the codebase, but a few larger additions worth noting:

* __gpMgmt/__

  Contains Greenplum-specific command-line tools for managing the
  cluster. Scripts like gpinit, gpstart, gpstop live here. They are
  mostly written in Python.

* __gpAux/__

  Contains Greenplum-specific extensions such as gpfdist and
  gpmapreduce.  Some additional directories are submodules and will be
  made available over time.

* __doc/__

  In PostgreSQL, the user manual lives here. In Greenplum, the user
  manual is distributed separately (see http://gpdb.docs.pivotal.io),
  and only the reference pages used to build man pages are here.

* __src/backend/cdb/__

  Contains larger Greenplum-specific backend modules. For example,
  communication between segments, turning plans into parallelizable
  plans, mirroring, distributed transaction and snapshot management,
  etc. __cdb__ stands for __Cluster Database__ - it was a workname used in
  the early days. That name is no longer used, but the __cdb__ prefix
  remains.

* __src/backend/gpopt/__

  Contains the so-called __translator__ library, for using the ORCA
  optimizer with Greenplum. The translator library is written in C++
  code, and contains glue code for translating plans and queries
  between the DXL format used by ORCA, and the PostgreSQL internal
  representation. This goes unused, unless building with
  _--enable-orca_.

* __src/backend/gp_libpq_fe/__

  A slightly modified copy of libpq. The master node uses this to
  connect to segments, and to send fragments of a query plan to
  segments for execution. It is linked directly into the backend, it
  is not a shared library like libpq.

* __src/backend/fts/__

  FTS is a process that runs in the master node, and periodically
  polls the segments to maintain the status of each segment.

## Regression tests

* The default regression tests

```
make installcheck-good
```

* optional extra/heavier regression tests

```
make installcheck-bugbuster
```

* The PostgreSQL __check__ target does not work. Setting up a
  Greenplum cluster is more complicated than a single-node PostgreSQL
  installation, and no-one's done the work to have __make check__
  create a cluster. Create a cluster manually or use gpAux/gpdemo/
  (example below) and run __make installcheck-good__ against
  that. Patches are welcome!

* The PostgreSQL __installcheck__ target does not work either, because
  some tests are known to fail with Greenplum. The
  __installcheck-good__ schedule excludes those tests.

## Basic GPDB source configuration, compilation, gpdemo cluster creation and test execution example

* Configure build environment

```
configure --prefix=<install location>
```

* Compilation and install

```
make
make install
```

* Bring in greenplum environment into your running shell

```
source <install location>/greenplum_path.sh
```

* Start demo cluster (gpdemo-env.sh is created which contain
  __PGPORT__ and __MASTER_DATA_DIRECTORY__ values)


```
cd gpAux/gpdemo
make cluster
source gpdemo-env.sh
```

* Run tests

```
make installcheck-good
```

## Glossary

* __QD__

  Query Dispatcher. A synonym for the master server.

* __QE__

  Query Executor. A synonym for a segment server.

## Documentation

For Greenplum Database documentation, please check online docs:
http://gpdb.docs.pivotal.io
