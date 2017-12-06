#!/bin/bash
set -e

function fatal
{
   echo
   echo ERROR: $1
   exit 1
}

function usage
{
   echo "usage: $0 os vendor"
   echo
   echo $1
   exit 1
}

OS=$1
if [ -z $OS ]; then
   usage
fi

VENDOR=$2
if [ -z $VENDOR ]; then
    VENDOR=vitesse
fi

which go >& /dev/null || fatal "cannot find go in PATH"
# patch needed by madlib
which patch >& /dev/null || fatal "cannot find patch in PATH"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export GOPATH="$DIR/../dg:$DIR/../phi/go"

VERSION=$(< ../DEEPGREEN_VERSION)
echo "Building $VERSION"

REVHASH=`git rev-parse --short HEAD`
REVDATE=`git log -1 --date=short --pretty=format:%cd`
REVDATERAW=$(git log -1 --date=short --pretty=format:%cd | awk -F- '{print substr($1$2$3, 3)}')


BINFILE=$VENDOR.deepgreendb.$VERSION.$OS.x86_64.$REVDATERAW.bin

echo "export VENDOR=$VENDOR" > version.sh
echo "export VERSION=$VERSION" >> version.sh
echo "export OS=$OS" >> version.sh
echo "export BINFILE=$BINFILE" >> version.sh
echo "export REVHASH=$REVHASH" >> version.sh
echo "export REVDATE=$REVDATE" >> version.sh
echo "export REVDATERAW=$REVDATERAW" >> version.sh

echo "Bin file will be $BINFILE"

(cd ../vdbtools 2>> /dev/null) || fatal "please symlink vdbtools in deepgreen project"
(cd ../toolchain 2>> /dev/null) || fatal "please symlink toolchain in deepgreen project"

# clean the DESTDIR directory
DESTDIR=/opt/$VENDOR.deepgreendb.$VERSION
(cd $DESTDIR && rm -rf *) \
    || fatal "cannot cd and clean $DESTDIR"

echo "--------------------------------- build python"
bash mkpython.sh >& mkpython.out \
    || fatal "mkpython failed"

export PATH="$DESTDIR/ext/python/bin:$PATH"
export LD_LIBRARY_PATH="$DESTDIR/ext/python/lib:$LD_LIBRARY_PATH"

# configure 
echo "--------------------------------- configuring"
# Note: do not make clean as it would delete the python objects
# we built above. We are safe without making clean since the 
# whole DESTDIR was rm -rf when we started above.
GPDBDIR=../gpdb
( (cd .. && make config-$VENDOR prefix=$DESTDIR) 2>&1) > build.out \
    || fatal "configure failed. see build.out."


# make and install into DESTDIR
echo "--------------------------------- making gpdb"
( (cd .. && make clean && (make -j8 || make -j8) && make install) 2>&1) >> build.out \
    || fatal "make failed. see build.out."

ls $DESTDIR/bin/postgres || fatal "cannot find $DESTDIR/bin/postgres"

(cd $DESTDIR/bin && ldd postgres | awk 'FS="=>" {print $2}' | awk '{print $1}' | grep '^/usr\|^/lib') > liblist || fatal "ldd failed"
(mkdir $DESTDIR/lib2 && cp $(< liblist) $DESTDIR/lib2) || fatal "cannot copy .so"
(cd $DESTDIR/lib2 && mv libnetsnmp.* libcrypto.* libssl.* libcurl.* libcares.* libz.* libbz2.* libstdc++.* ../lib) >& /dev/null || true

# source the path because contrib module needs to use the 
# distribution headers and libs
source $DESTDIR/greenplum_path.sh

echo "--------------------------------- making orafce"
ORAFCEDIR=$GPDBDIR/gpAux/extensions/orafce
(cd $ORAFCEDIR && make install USE_PGXS=1) >> build.out \
    || fatal "make orafce failed.  See build.out."

echo "--------------------------------- making gphdfs, .so only"
GPHDFSDIR=$GPDBDIR/gpAux/extensions/gphdfs
(cd $GPHDFSDIR && make install-gphdfs.so) >> build.out \
    || fatal "make gphdfs.so failed.  See build.out."

# mkdir $DESTDIR/share/postgresql/cdb_init.d
cp $GPHDFSDIR/gphdfs.sql $DESTDIR/share/postgresql/cdb_init.d \
    || fatal "cannot copy gphdfs.sql."

mkdir -p $DESTDIR/lib/hadoop
cp $GPHDFSDIR/*.jar $GPHDFSDIR/hadoop_env.sh $DESTDIR/lib/hadoop/ \
    || fatal "cannot copy hdfs jar files."

echo "--------------------------------- making stream"
STREAMDIR=$GPDBDIR/gpMgmt/bin/src/stream


(cd $STREAMDIR && NO_M64=TRUE gcc stream.c -o stream) >> build.out \
    || fatal "cannot build stream. see build.out"

cp $STREAMDIR/stream $DESTDIR/bin/lib/ \
    || fatal "cannot copy stream."

echo "--------------------------------- making postgis"
(cd .. && bash build_postgis.sh) || fatal "cannot make postgis"

echo "--------------------------------- making madlib"
(cd .. && bash build_madlib.sh) || fatal "cannot make madlib"

echo "--------------------------------- set up license and readme"
sed -e "s/{{VERSION}}/$VERSION/g" ../LICENSE.template.$VENDOR > $DESTDIR/LICENSE \
    || fatal "cannot create LICENSE."

cp ../README $DESTDIR \
    || fatal "cannot copy README."

cp ../RELEASENOTE.txt $DESTDIR \
    || fatal "cannot copy RELEASENOTE.txt."

echo "--------------------------------- rename vitesse to $VENDOR"
bash postbuild_rename.sh $DESTDIR $VENDOR

echo "--------------------------------- finished"

echo
echo
echo "Built $DESTDIR"
echo "llvm config was:" $(readlink ../llvm-config)
echo
echo "run bash packsvr.sh to create $BINFILE"

