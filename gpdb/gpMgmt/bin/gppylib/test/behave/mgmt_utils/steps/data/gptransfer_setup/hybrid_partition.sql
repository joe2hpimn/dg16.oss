CREATE TABLE hybrid_part1 (
                PS_PARTKEY INTEGER,
                PS_SUPPKEY INTEGER,
                PS_AVAILQTY integer,
                PS_SUPPLYCOST decimal,
                PS_COMMENT VARCHAR(199)
                ) 
 partition by range (ps_supplycost)
 subpartition by range (ps_suppkey)
,subpartition by range (ps_partkey) subpartition template (start('1') end('200001') every(66666)

)
(
partition p1 start('1') end('20') inclusive
(
 subpartition sp1 start('1') end('3030')  WITH (checksum=false,   appendonly=true,blocksize=1171456,compresslevel=3),
 subpartition sp2 end('6096') inclusive,
 subpartition sp3   start('6096') exclusive end('7201') with (orientation='column',appendonly=true),
 subpartition sp4 end('10001')  WITH (checksum=false,appendonly=true,     blocksize=1171456,compresslevel=3)
), 
partition p2 end('1001') inclusive
(
subpartition sp1 start('1') ,        
subpartition sp2 start('4139') with (orientation='column',appendonly=true) ,
subpartition sp3 start('4685')  WITH (checksum=false,appendonly=true, blocksize=1171456,compresslevel=3) ,
subpartition sp4 start('5675') )
);

COPY hybrid_part1 FROM 'gppylib/test/behave/mgmt_utils/steps/data/hybrid_part.data' delimiter as '|';


CREATE TABLE hybrid_part2 (
                PS_PARTKEY INTEGER,
                PS_SUPPKEY INTEGER,
                PS_AVAILQTY integer,
                PS_SUPPLYCOST decimal,
                PS_COMMENT VARCHAR(199)
                ) 
 partition by range (ps_supplycost)
 subpartition by range (ps_suppkey)
,subpartition by range (ps_partkey) subpartition template (start('1') end('200001') every(66666)

)
(
partition p1 start('1') end('20') inclusive
(
 subpartition sp1 start('1') end('3030')  WITH (checksum=false,   appendonly=true,blocksize=1171456,compresslevel=3),
 subpartition sp2 end('6096') inclusive,
 subpartition sp3   start('6096') exclusive end('7201') with (orientation='column',appendonly=true),
 subpartition sp4 end('10001')  WITH (checksum=false,appendonly=true,     blocksize=1171456,compresslevel=3)
), 
partition p2 end('1001') inclusive
(
subpartition sp1 start('1') ,        
subpartition sp2 start('4139') with (orientation='column',appendonly=true) ,
subpartition sp3 start('4685')  WITH (checksum=false,appendonly=true, blocksize=1171456,compresslevel=3) ,
subpartition sp4 start('5675') )
);

COPY hybrid_part2 FROM 'gppylib/test/behave/mgmt_utils/steps/data/hybrid_part.data' delimiter as '|';
