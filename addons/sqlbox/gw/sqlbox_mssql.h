#include "gwlib/gwlib.h"

#if defined(HAVE_MSSQL) || defined(HAVE_SDB)

#define SQLBOX_MSSQL_CREATE_LOG_TABLE "CREATE TABLE %S ( \
sql_id NUMERIC(10,0) IDENTITY NOT NULL PRIMARY KEY, \
momt VARCHAR(3) NULL CHECK (momt IN ( 'MO', 'MT', 'DLR') OR momt IS NULL), \
sender VARCHAR(20) NULL, receiver VARCHAR(20) NULL, \
udhdata VARCHAR(4000) NULL, msgdata VARCHAR(4000) NULL, xtime INTEGER NULL, \
smsc_id VARCHAR(255) NULL, service VARCHAR(255) NULL, account VARCHAR(255) NULL, \
id INTEGER NULL, sms_type INTEGER NULL, mclass INTEGER NULL, mwi INTEGER NULL, \
coding INTEGER NULL, compress INTEGER NULL, validity INTEGER NULL, deferred INTEGER NULL, \
dlr_mask INTEGER NULL, dlr_url VARCHAR(255) NULL, pid INTEGER NULL, alt_dcs INTEGER NULL, \
rpi INTEGER NULL, charset VARCHAR(255) NULL, boxc_id VARCHAR(255) NULL, \
binfo VARCHAR(255) NULL, meta_data VARCHAR(4000) NULL)"

#define SQLBOX_MSSQL_CREATE_INSERT_TABLE "CREATE TABLE %S ( \
sql_id NUMERIC(10,0) IDENTITY NOT NULL PRIMARY KEY, \
momt VARCHAR(3) NULL CHECK (momt IN ( 'MO', 'MT', 'DLR') OR momt IS NULL), \
sender VARCHAR(20) NULL, receiver VARCHAR(20) NULL, \
udhdata VARCHAR(4000) NULL, msgdata VARCHAR(4000) NULL, xtime INTEGER NULL, \
smsc_id VARCHAR(255) NULL, service VARCHAR(255) NULL, account VARCHAR(255) NULL, \
id INTEGER NULL, sms_type INTEGER NULL, mclass INTEGER NULL, mwi INTEGER NULL, \
coding INTEGER NULL, compress INTEGER NULL, validity INTEGER NULL, deferred INTEGER NULL, \
dlr_mask INTEGER NULL, dlr_url VARCHAR(255) NULL, pid INTEGER NULL, alt_dcs INTEGER NULL, \
rpi INTEGER NULL, charset VARCHAR(255) NULL, boxc_id VARCHAR(255) NULL, \
binfo VARCHAR(255) NULL, meta_data VARCHAR(4000) NULL)"

#define SQLBOX_MSSQL_SELECT_QUERY "SELECT TOP 1 sql_id, momt, sender, receiver, udhdata, msgdata, \
xtime, smsc_id, service, account, id, sms_type, mclass, mwi, coding, compress, \
validity, deferred, dlr_mask, dlr_url, pid, alt_dcs, rpi, charset, boxc_id, binfo, meta_data \
FROM %S"

#define SQLBOX_MSSQL_INSERT_QUERY "INSERT INTO %S (momt, sender, receiver, udhdata, msgdata, \
xtime, smsc_id, service, account, sms_type, mclass, mwi, coding, compress, validity, \
deferred, dlr_mask, dlr_url, pid, alt_dcs, rpi, charset, boxc_id, binfo, meta_data) VALUES (%S, \
%S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S)"

#define SQLBOX_MSSQL_DELETE_QUERY "DELETE FROM %S WHERE sql_id = %S"

#endif /* HAVE_MSSQL || HAVE_SDB */

#ifdef HAVE_MSSQL
#include "gw/msg.h"
#include "sqlbox_sql.h"
void sql_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */);
Msg *mssql_fetch_msg();
void sql_shutdown();
struct server_type *sql_init_mssql(Cfg *cfg);
#ifndef sqlbox_mssql_c
extern
#endif
Octstr *sqlbox_id;
#endif
