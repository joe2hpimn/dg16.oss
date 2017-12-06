#!/bin/bash
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
. $DIR/version.sh
if [ -z $VERSION ]; then
    echo "ERROR: VERSION not set in version.sh file."
    exit 1
fi

if [ -z $OS ]; then
    echo "ERROR: OS not set in version.sh file"
    exit 1
fi

if [ -z $BINFILE ]; then
    echo "ERROR: BINFILE not set in versin.sh file"
    exit 1
fi

echo "Bin file will be $BINFILE"


SRC=/opt/$VENDOR.deepgreendb.$VERSION

if [ ! -e $SRC ]; then
    echo "ERROR: $SRC missing."
    exit 1
fi
if [ ! -e $SRC/bin/postgres ]; then
    echo "ERROR: $SRC/bin/postgres missing... build must have failed."
    exit 1
fi

echo "------------------------- populate payload dir"
rm -rf payload
mkdir payload

if ! (cd $SRC && tar cf $DIR/payload/files.tar *); then
    exit 1
fi
if ! (cd payload && cp $SRC/LICENSE $SRC/README $DIR/installer.sh $DIR/version.sh .); then
    exit 1
fi

echo "------------------------- create payload.tgz from inside payload dir"
# create payload.tgz from inside payload dir
(cd payload && tar cfz ../payload.tgz ./*)

# create
echo "------------------------- add decompress.sh"
cat decompress.sh payload.tgz > $BINFILE

echo "------------------------- finished"
echo
echo
echo "Created $BINFILE"
echo REV $REVHASH on $REVDATE
echo



