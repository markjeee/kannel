#define sqlbox_sql_c
#include "sqlbox_sql.h"

struct server_type *sqlbox_init_sql(Cfg *cfg)
{
    struct server_type *res = NULL;

#ifdef HAVE_MSSQL
    res = (struct server_type *)sqlbox_init_mssql(cfg);
    if (res) {
        return res;
    }
#endif
#ifdef HAVE_MYSQL
    res = (struct server_type *)sqlbox_init_mysql(cfg);
    if (res) {
        return res;
    }
#endif
#ifdef HAVE_ORACLE
    res = (struct server_type *)sqlbox_init_oracle(cfg);
    if (res) {
        return res;
    }
#endif
#ifdef HAVE_PGSQL
    res = (struct server_type *)sqlbox_init_pgsql(cfg);
    if (res) {
        return res;
    }
#endif
#ifdef HAVE_SDB
    res = (struct server_type *)sqlbox_init_sdb(cfg);
    if (res) {
        return res;
    }
#endif
#ifdef HAVE_SQLITE
    res = (struct server_type *)sqlbox_init_sqlite(cfg);
    if (res) {
        return res;
    }
#endif
#ifdef HAVE_SQLITE3
    res = (struct server_type *)sqlbox_init_sqlite3(cfg);
    if (res) {
        return res;
    }
#endif
    return res;
}
