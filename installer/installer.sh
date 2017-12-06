#!/bin/bash
set -e

for i in "$@"
do
case $i in 
		--accept-license)
		ACCEPT_LICENSE=1
		shift
		;;
		*)
		;;
esac
done 

if [ -z $1 ]; then
    echo "ERROR: missing parameter installdir"
    exit 1
fi

if ! [ -e $1 ]; then
    echo "ERROR: installdir does not exist"
    exit 1
fi
INSTALLDIR=$1

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
. $DIR/version.sh
if [ -z $VERSION ]; then 
    echo "ERROR: internal error. VERSION not set in version.sh file."
    exit 1
fi

if [ -z $REVDATERAW ]; then
    echo "ERROR: internal error. REVDATERAW is not set in version.sh file."
    exit 1
fi

TARGETDIR=$INSTALLDIR/$VENDOR.deepgreendb.$VERSION.$REVDATERAW
if [ -e $TARGETDIR ]; then
    echo "ERROR: $TARGETDIR exists."
    echo "Please delete it or move it away."
    exit 1
fi

echo "Installing Deepgreen DB in $TARGETDIR."
echo "Press [ENTER] to proceed, or ^C to stop."
read

if ! mkdir $TARGETDIR; then 
    echo "ERROR: cannot create directory $TARGETDIR."
    exit 1
fi

if [[ -z "$ACCEPT_LICENSE" ]]; then more LICENSE; fi
echo "Press [ENTER] to proceed, or ^C to stop."
read

echo -n "Installing ."
tar -xf ./files.tar -C $TARGETDIR
echo -n .


##################################################################
# create the env.sh file
#
ENVFILE=$TARGETDIR/greenplum_path.sh
cat > $ENVFILE <<'EOF'

GPHOME="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Replace with symlink path if it is present and correct
if [ -h ${GPHOME}/../greenplum-db ]; then
    GPHOME_BY_SYMLINK=`(cd ${GPHOME}/../greenplum-db/ && pwd -P)`
    if [ x"${GPHOME_BY_SYMLINK}" = x"${GPHOME}" ]; then
        GPHOME=`(cd ${GPHOME}/../greenplum-db/ && pwd -L)`/.
    fi
    unset GPHOME_BY_SYMLINK
fi
#setup PYTHONHOME
if [ -x $GPHOME/ext/python/bin/python ]; then
    PYTHONHOME="$GPHOME/ext/python"
fi
PYTHONPATH=$GPHOME/lib/python
PATH=$GPHOME/bin:$PYTHONHOME/bin:$PATH
LD_LIBRARY_PATH=$GPHOME/lib:$PYTHONHOME/lib:$LD_LIBRARY_PATH
OPENSSL_CONF=$GPHOME/etc/openssl.cnf
export GPHOME
export PATH
export LD_LIBRARY_PATH
export PYTHONPATH
export PYTHONHOME
export OPENSSL_CONF

EOF
echo -n .

##################################################################
# create the symlink
#
if ! (cd $INSTALLDIR && rm -f deepgreendb  && ln -s $TARGETDIR deepgreendb ); then
    echo "ERROR: cannot create symlink"
fi
echo -n .

echo
echo "Successfully installed into $TARGETDIR."
echo "Created symlink $INSTALLDIR/deepgreendb"
echo 
echo "Press [ENTER] to continue"
read
echo 
echo "IMPORTANT:"
echo "  1. source $INSTALLDIR/deepgreendb/greenplum_path.sh for shell environment."
echo "  2. License term in $INSTALLDIR/deepgreendb/LICENSE."
echo "  3. For information on the release, see $INSTALLDIR/deepgreendb/RELEASENOTE.txt."
echo "  4. See $INSTALLDIR/deepgreendb/README for more information."
echo
echo "Thank you for installing Deepgreen DB."
echo
