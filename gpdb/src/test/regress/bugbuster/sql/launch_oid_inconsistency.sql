\! psql -d regression -f bugbuster/sql/oid_inconsistency.sql &> bugbuster/data/oidinconsis.txt
\! grep -i "ERROR" bugbuster/data/oidinconsis.txt | wc -l
