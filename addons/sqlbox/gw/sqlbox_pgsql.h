#include "gwlib/gwlib.h"

#if defined(HAVE_PGSQL) || defined(HAVE_SDB)

#define SQLBOX_PGSQL_CREATE_LOG_TABLE "CREATE TABLE %S (sql_id SERIAL PRIMARY KEY, \
momt VARCHAR(3) CHECK(momt IN ('MO', 'MT', 'DLR', NULL)) DEFAULT NULL, \
sender VARCHAR(20) NULL, receiver VARCHAR(20) NULL, udhdata VARCHAR(255) NULL, \
msgdata TEXT NULL, time BIGINT NULL, smsc_id VARCHAR(255) NULL, \
service VARCHAR(255) NULL, account VARCHAR(255) NULL, id BIGINT NULL, \
sms_type BIGINT NULL, mclass BIGINT NULL, mwi BIGINT NULL, coding BIGINT NULL, \
compress BIGINT NULL, validity BIGINT NULL, deferred BIGINT NULL, \
dlr_mask BIGINT NULL, dlr_url VARCHAR(255) NULL, pid BIGINT NULL, \
alt_dcs BIGINT NULL, rpi BIGINT NULL, charset VARCHAR(255) NULL, \
boxc_id VARCHAR(255) NULL, binfo VARCHAR(255) NULL, meta_data TEXT NULL)"

#define SQLBOX_PGSQL_CREATE_INSERT_TABLE "CREATE TABLE %S (sql_id SERIAL PRIMARY KEY, \
momt VARCHAR(3) CHECK(momt IN ('MO', 'MT', NULL)) DEFAULT NULL, \
sender VARCHAR(20) NULL, receiver VARCHAR(20) NULL, udhdata VARCHAR(255) NULL, \
msgdata TEXT NULL, time BIGINT NULL, smsc_id VARCHAR(255) NULL, \
service VARCHAR(255) NULL, account VARCHAR(255) NULL, id BIGINT NULL, \
sms_type BIGINT NULL, mclass BIGINT NULL, mwi BIGINT NULL, coding BIGINT NULL, \
compress BIGINT NULL, validity BIGINT NULL, deferred BIGINT NULL, \
dlr_mask BIGINT NULL, dlr_url VARCHAR(255) NULL, pid BIGINT NULL, \
alt_dcs BIGINT NULL, rpi BIGINT NULL, charset VARCHAR(255) NULL, \
boxc_id VARCHAR(255) NULL, binfo VARCHAR(255) NULL, meta_data TEXT NULL)"

#define SQLBOX_PGSQL_SELECT_QUERY "SELECT sql_id, momt, sender, receiver, udhdata, msgdata, \
time, smsc_id, service, account, id, sms_type, mclass, mwi, coding, compress, validity, deferred, \
dlr_mask, dlr_url, pid, alt_dcs, rpi, charset, boxc_id, binfo, meta_data FROM %S LIMIT 1 OFFSET 0"

#define SQLBOX_PGSQL_INSERT_QUERY "INSERT INTO %S (momt, sender, receiver, udhdata, msgdata, \
time, smsc_id, service, account, sms_type, mclass, mwi, coding, compress, validity, deferred, \
dlr_mask, dlr_url, pid, alt_dcs, rpi, charset, boxc_id, binfo, meta_data) VALUES (%S, %S, %S, \
%S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S)"

#define SQLBOX_PGSQL_DELETE_QUERY "DELETE FROM %S WHERE sql_id = %S"

#endif /* HAVE_PGSQL || HAVE_SDB */

#ifdef HAVE_PGSQL
#include "gw/msg.h"
#include "sqlbox_sql.h"
#define sql_fetch_msg pgsql_fetch_msg
#define sql_save_msg pgsql_save_msg
#define sql_leave pgsql_leave
void sql_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */);
Msg *pgsql_fetch_msg();
void sql_shutdown();
struct server_type *sql_init_pgsql(Cfg *cfg);
void sqlbox_configure_pgsql(Cfg *cfg);
#ifndef sqlbox_pgsql_c
extern
#endif
Octstr *sqlbox_id;
#endif
