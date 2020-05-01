create table A(
    a int,
    b int
);

create table B(
    b int,
    c int
);

insert into A values(1,1);
insert into A values(2,1);
insert into A values(3,1);
insert into A values(4,2);
insert into B values(1,1);
insert into B values(1,2);
insert into B values(2,3);
insert into B values(2,4);

select A.a, A.b, B.c 
from A,B
where A.b=B.b;