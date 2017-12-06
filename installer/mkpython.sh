#!/bin/bash
set -e

function fatal
{
   echo
   echo ERROR: $1
   exit 1
}

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
. $DIR/version.sh
if [ -z $VERSION ]; then
    echo "ERROR: VERSION not set in version.sh file."
    exit 1
fi

DEST=/opt/$VENDOR.deepgreendb.$VERSION
PYDEST=$DEST/ext/python

echo "--------------------------------- building python2.7"
PY=Python-2.7.11
rm -rf $PY
rm -f ./setuptools*zip
if true; then
    rm -rf $PY
    tar xfz $PY.tgz
    
    # patch for suse12
    if ( ls /usr/include/ncurses/panel.h ) >& /dev/null; then 
	ln -s /usr/include/ncurses/panel.h $PY/Include
    fi

    if ! ( cd $PY && ./configure --enable-shared --prefix=$PYDEST ) > mkpython.out;  then
        fatal "configure python failed; see mkpython.out"
    fi

    if ! ( cd $PY && make -j8 && make install ) >> mkpython.out; then
        fatal "make python failed; see mkpython.out"
    fi
    if [ ! -e $PYDEST/bin/python ]; then
        fatal "$PYDEST/bin/python missing... build must have failed."
    fi
fi
export PATH=$PYDEST/bin:$PATH
LD_LIBRARY_PATH=$PYDEST/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH


echo "--------------------------------- python2.7 ez_setup"
# CK: looks like this is optional. if build fail, try uncommenting it.
#python ez_setup.py 

echo "--------------------------------- python2.7 get-pip"
python get-pip.py

#pip install lockfile
#pip install paramiko==1.16.0
#pip install setuptools
#pip install epydoc
#pip install psi


echo "--------------------------------- python 2.7 newly installed version"
which python
which pip
python --version
