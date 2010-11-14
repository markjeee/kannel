#include "gwlib/gwlib.h"

#if defined(HAVE_ORACLE) || defined(HAVE_SDB)

#define SQLBOX_ORACLE_CREATE_LOG_TABLE "CREATE TABLE \"%S\" (\"sql_id\" INTEGER NOT NULL PRIMARY KEY, \
\"momt\" VARCHAR2(3) NULL, \"sender\" VARCHAR2(20) NULL, \"receiver\" VARCHAR2(20) NULL, \
\"udhdata\" VARCHAR2(4000) NULL, \"msgdata\" VARCHAR2(4000) NULL, \"time\" INTEGER NULL, \
\"smsc_id\" VARCHAR2(255) NULL, \"service\" VARCHAR2(255) NULL, \"account\" VARCHAR2(255) NULL, \
\"id\" INTEGER NULL, \"sms_type\" INTEGER NULL, \"mclass\" INTEGER NULL, \"mwi\" INTEGER NULL, \
\"coding\" INTEGER NULL, \"compress\" INTEGER NULL, \"validity\" INTEGER NULL, \"deferred\" INTEGER NULL, \
\"dlr_mask\" INTEGER NULL, \"dlr_url\" VARCHAR2(255) NULL, \"pid\" INTEGER NULL, \"alt_dcs\" INTEGER NULL, \
\"rpi\" INTEGER NULL, \"charset\" VARCHAR2(255) NULL, \"boxc_id\" VARCHAR2(255) NULL, \
\"binfo\" VARCHAR2(255) NULL, \"meta_data\" VARCHAR2(4000) NULL, \
CONSTRAINT c_%S_momt CHECK ( \"momt\" IN ( 'MO', 'MT', 'DLR', NULL)))"

#define SQLBOX_ORACLE_CREATE_INSERT_TABLE "CREATE TABLE \"%S\" (\"sql_id\" INTEGER NOT NULL PRIMARY KEY, \
\"momt\" VARCHAR2(3) NULL, \"sender\" VARCHAR2(20) NULL, \"receiver\" VARCHAR2(20) NULL, \
\"udhdata\" VARCHAR2(4000) NULL, \"msgdata\" VARCHAR2(4000) NULL, \"time\" INTEGER NULL, \
\"smsc_id\" VARCHAR2(255) NULL, \"service\" VARCHAR2(255) NULL, \"account\" VARCHAR2(255) NULL, \
\"id\" INTEGER NULL, \"sms_type\" INTEGER NULL, \"mclass\" INTEGER NULL, \"mwi\" INTEGER NULL, \
\"coding\" INTEGER NULL, \"compress\" INTEGER NULL, \"validity\" INTEGER NULL, \"deferred\" INTEGER NULL, \
\"dlr_mask\" INTEGER NULL, \"dlr_url\" VARCHAR2(255) NULL, \"pid\" INTEGER NULL, \"alt_dcs\" INTEGER NULL, \
\"rpi\" INTEGER NULL, \"charset\" VARCHAR2(255) NULL, \"boxc_id\" VARCHAR2(255) NULL, \
\"binfo\" VARCHAR2(255) NULL, \"meta_data\" VARCHAR2(4000) NULL, \
CONSTRAINT c_%S_momt CHECK ( \"momt\" IN ( 'MO', 'MT', NULL)))"

#define SQLBOX_ORACLE_CREATE_LOG_SEQUENCE "CREATE SEQUENCE \"%S_seq\" START WITH 1 INCREMENT BY 1 NOMAXVALUE"

#define SQLBOX_ORACLE_CREATE_INSERT_SEQUENCE "CREATE SEQUENCE \"%S_seq\" START WITH 1 INCREMENT BY 1 NOMAXVALUE"

#define SQLBOX_ORACLE_CREATE_LOG_TRIGGER "CREATE TRIGGER \"%S_trg\" BEFORE INSERT ON \"%S\" \
FOR EACH ROW BEGIN SELECT \"%S_seq\".nextval INTO :new.\"sql_id\" FROM DUAL; END;"

#define SQLBOX_ORACLE_CREATE_INSERT_TRIGGER "CREATE TRIGGER \"%S_trg\" BEFORE INSERT ON \"%S\" \
FOR EACH ROW BEGIN SELECT \"%S_seq\".nextval INTO :new.\"sql_id\" FROM DUAL; END;"

#define SQLBOX_ORACLE_SELECT_QUERY "SELECT \"sql_id\", \"momt\", \"sender\", \"receiver\", \"udhdata\", \"msgdata\", \
\"time\", \"smsc_id\", \"service\", \"account\", \"id\", \"sms_type\", \"mclass\", \"mwi\", \"coding\", \"compress\", \
\"validity\", \"deferred\", \"dlr_mask\", \"dlr_url\", \"pid\", \"alt_dcs\", \"rpi\", \"charset\", \"boxc_id\", \
\"binfo\", \"meta_data\" FROM \"%S\" WHERE ROWNUM = 1"

#define SQLBOX_ORACLE_INSERT_QUERY "INSERT INTO \"%S\" (\"momt\", \"sender\", \"receiver\", \"udhdata\", \"msgdata\", \
\"time\", \"smsc_id\", \"service\", \"account\", \"sms_type\", \"mclass\", \"mwi\", \"coding\", \"compress\", \"validity\", \
\"deferred\", \"dlr_mask\", \"dlr_url\", \"pid\", \"alt_dcs\", \"rpi\", \"charset\", \"boxc_id\", \"binfo\", \"meta_data\" \
) VALUES (%S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S)"

#define SQLBOX_ORACLE_DELETE_QUERY "DELETE FROM \"%S\" WHERE \"sql_id\" = %S"

#endif /* HAVE_ORACLE || HAVE_SDB */

#ifdef HAVE_ORACLE
#include "gw/msg.h"
#include "sqlbox_sql.h"
void sql_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */);
Msg *oracle_fetch_msg();
void sql_shutdown();
struct server_type *sql_init_oracle(Cfg *cfg);
#ifndef sqlbox_oracle_c
extern
#endif
Octstr *sqlbox_id;
#endif
