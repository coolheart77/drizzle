################################################################################
# inc/vcol_keys.inc                                                            #
#                                                                              #
# Purpose:                                                                     #
#  Testing keys, indexes defined upon virtual columns.                         #
#                                                                              #
#                                                                              #
#                                                                              #
#------------------------------------------------------------------------------#
# Original Author: Andrey Zhakov                                               #
# Original Date: 2008-09-02                                                    #
# Change Author:                                                               #
# Change Date:                                                                 #
# Change:                                                                      #
################################################################################


--echo #            - UNIQUE KEY
--echo #            - INDEX
--echo #            - FOREIGN INDEX (partially supported)
--echo #            - CHECK (allowed but not used)

--echo # UNIQUE
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
create table t1 (a int, b virtual int as (a*2) unique);
create table t1 (a int, b virtual int as (a*2) stored unique);
show create table t1;
describe t1;
drop table t1;

--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
create table t1 (a int, b virtual int as (a*2), unique key (b));
create table t1 (a int, b virtual int as (a*2) stored, unique (b));
show create table t1;
describe t1;
drop table t1;

create table t1 (a int, b virtual int as (a*2));
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
alter table t1 add unique key (b);
drop table t1;
create table t1 (a int, b virtual int as (a*2) stored);
alter table t1 add unique key (b);
drop table t1;

--echo # Testing data manipulation operations involving UNIQUE keys 
--echo # on virtual columns can be found in:
--echo #  - vcol_ins_upd.inc
--echo #  - vcol_select.inc

--echo # 
--echo # INDEX
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
create table t1 (a int, b virtual int as (a*2), index (b));
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
create table t1 (a int, b virtual int as (a*2), index (a,b));

create table t1 (a int, b virtual int as (a*2) stored, index (b));
show create table t1;
describe t1;
drop table t1;

create table t1 (a int, b virtual int as (a*2) stored, index (a,b));
show create table t1;
describe t1;
drop table t1;

create table t1 (a int, b virtual int as (a*2));
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
alter table t1 add index (b);
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
alter table t1 add index (a,b);
drop table t1;

create table t1 (a int, b virtual int as (a*2) stored);
alter table t1 add index (b);
drop table t1;

create table t1 (a int, b virtual int as (a*2) stored);
alter table t1 add index (a,b);
create table t2 like t1;
drop table t2;
drop table t1;

--echo # Testing data manipulation operations involving INDEX
--echo # on virtual columns can be found in:
--echo #  - vcol_select.inc

--echo # FOREIGN KEY

--echo # Rejected FK options.
--error ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN 
create table t1 (a int, b virtual int as (a+1) stored,
                 foreign key (b) references t2(a) on update set null);
--error ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN 
create table t1 (a int, b virtual int as (a+1) stored,
                 foreign key (b) references t2(a) on update cascade);
--error ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN 
create table t1 (a int, b virtual int as (a+1) stored,
                 foreign key (b) references t2(a) on delete set null);

create table t1 (a int, b virtual int as (a+1) stored);
--error ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN 
alter table t1 add foreign key (b) references t2(a) on update set null;
--error ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN 
alter table t1 add foreign key (b) references t2(a) on update cascade;
--error ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN 
alter table t1 add foreign key (b) references t2(a) on delete set null;
drop table t1;

--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
create table t1 (a int, b virtual int as (a+1),
                 foreign key (b) references t2(a));

create table t1 (a int, b virtual int as (a+1));
--error ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN
alter table t1 add foreign key (b) references t2(a);
drop table t1;

--echo # Allowed FK options.
create table t2 (a int primary key, b char(5));
create table t1 (a int, b virtual int as (a % 10) stored,
                 foreign key (b) references t2(a) on update restrict);
drop table t1;
create table t1 (a int, b virtual int as (a % 10) stored,
                 foreign key (b) references t2(a) on update no action);
drop table t1;
create table t1 (a int, b virtual int as (a % 10) stored,
                 foreign key (b) references t2(a) on delete restrict);
drop table t1;
create table t1 (a int, b virtual int as (a % 10) stored,
                 foreign key (b) references t2(a) on delete cascade);
drop table t1;
create table t1 (a int, b virtual int as (a % 10) stored,
                 foreign key (b) references t2(a) on delete no action);
drop table t1;

--echo 
--echo # Testing data manipulation operations involving FOREIGN KEY 
--echo # on virtual columns can be found in:
--echo #  - vcol_ins_upd.inc
--echo #  - vcol_select.inc

--echo #
--echo # TODO: CHECK
