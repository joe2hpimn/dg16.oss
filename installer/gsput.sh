# put a file in google storage under vitessedata/download

ARG0=$0

function fatal
{
   echo
   echo ERROR: $1
   exit 1
}

function usage
{
   echo usage: $ARG0 binfile
   exit 1
}


[ -z $1 ] && usage

gsutil cp $1 gs://vitessedata/download
gsutil acl ch -u AllUsers:R gs://vitessedata/download/$1


