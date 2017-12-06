#!/bin/bash

echo "Replacing all vitesse in $1 with $2 ..."

cd $1

if [ $2 = "vitesse" ]
then 
    #mv ./share/postgresql/contrib/deepgreen.sql ./share/postgresql/contrib/vitesse.sql
    true
else
    mv ./share/postgresql/contrib/vitesse.sql ./share/postgresql/contrib/deepgreen.sql
    sed -i -e "s/vitesse/$2/g" ./share/postgresql/contrib/deepgreen.sql
    # sed -i -e "s/Vitesse Data, Inc/$2/g" ./bin/pg2fdw
    # sed -i -e "s/Vitesse Data, Inc/$2/g" ./bin/lib/pg2fdw.py
    # sed -i -e "s/Vitesse Data, Inc/$2/g" ./bin/pg2disk
    sed -i -e "s/vitesse/$2/g" ./bin/pg2disk
    # sed -i -e "s/Vitesse Data, Inc/$2/g" ./bin/pg2ext
    sed -i -e "s/vitesse/$2/g" ./bin/pg2ext
    # sed -i -e "s/Vitesse Data, Inc/$2/g" ./bin/loftpush
    # sed -i -e "s/Vitesse Data, Inc/$2/g" ./bin/pg2spq
fi
