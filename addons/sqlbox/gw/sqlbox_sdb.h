#include "gwlib/gwlib.h"

#ifdef HAVE_SDB

#define SQLBOX_OTHER_SELECT_QUERY "SELECT sql_id, momt, sender, receiver, udhdata, \
msgdata, time, smsc_id, service, account, id, sms_type, mclass, mwi, coding, \
compress, validity, deferred, dlr_mask, dlr_url, pid, alt_dcs, rpi, \
charset, boxc_id, binfo, meta_data FROM %S"

#define SQLBOX_OTHER_INSERT_QUERY "INSERT INTO %S (sql_id, momt, sender, \
receiver, udhdata, msgdata, time, smsc_id, service, account, sms_type, \
mclass, mwi, coding, compress, validity, deferred, dlr_mask, dlr_url, \
pid, alt_dcs, rpi, charset, boxc_id, binfo ) VALUES ( \
NULL, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, %S, \
%S, %S, %S, %S, %S, %S, %S, %S, %S)"

#define SQLBOX_OTHER_DELETE_QUERY "DELETE FROM %S WHERE sql_id = %S"

#include "gw/msg.h"
#include "sqlbox_sql.h"
void sql_save_msg(Msg *msg, Octstr *momt );
Msg *sdb_fetch_msg();
void sql_shutdown();
struct server_type *sql_init_sdb(Cfg *cfg);
#ifndef sqlbox_sdb_c
extern
#endif
Octstr *sqlbox_id;
#endif
