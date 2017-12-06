#!/bin/bash
# see http://www.linuxjournal.com/node/1005818
set -e

TMPDIR=$(mktemp -d /tmp/selfextract.XXXXXX)

ARCHIVE=$(awk '/^__ARCHIVE_BELOW__/ {print NR + 1; exit 0; }' $0)

tail -n+$ARCHIVE $0 | tar xz -C $TMPDIR

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
(cd $TMPDIR && bash installer.sh $@ "$DIR")

rm -rf $TMPDIR
exit 0


__ARCHIVE_BELOW__
