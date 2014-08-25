#ifndef SQLBOX_SQL_H
#define SQLBOX_SQL_H
#include "gwlib/gwlib.h"
#include "gw/msg.h"
#include "sqlbox_mssql.h"
#include "sqlbox_mysql.h"
#include "sqlbox_oracle.h"
#include "sqlbox_pgsql.h"
#include "sqlbox_sdb.h"
#include "sqlbox_sqlite.h"
#include "sqlbox_sqlite3.h"

struct server_type {
    Octstr *type;
    void (*sql_enter) (Cfg *);
    void (*sql_leave) ();
    Msg *(*sql_fetch_msg) ();
    void (*sql_save_msg) (Msg *, Octstr *);
    int  (*sql_fetch_msg_list) (List *, long);
    void (*sql_save_list) (List *, Octstr *, int);
};

struct sqlbox_db_queries {
    char *create_insert_table;
    char *create_insert_sequence;
    char *create_insert_trigger;
    char *create_log_table;
    char *create_log_sequence;
    char *create_log_trigger;
    char *select_query;
    char *delete_query;
    char *insert_query;
};

struct server_type *sqlbox_init_sql(Cfg *cfg);

#ifndef sqlbox_sql_c
extern
#endif
struct server_type *sql_type;

#define gw_sql_fetch_msg sql_type->sql_fetch_msg
#define gw_sql_fetch_msg_list sql_type->sql_fetch_msg_list
#define gw_sql_save_list sql_type->sql_save_list
#define gw_sql_save_msg(message, table) \
    do { \
        octstr_url_encode(message->sms.msgdata); \
        octstr_url_encode(message->sms.udhdata); \
        sql_type->sql_save_msg(message, table); \
    } while (0)
#define gw_sql_enter sql_type->sql_enter
#define gw_sql_leave sql_type->sql_leave

/* Macro to run the queries to create tables */
#define sqlbox_run_query(query, table) \
if (query != NULL) { \
    sql = octstr_format(query, table, table, table); \
    sql_update(pc, sql); \
    octstr_destroy(sql); \
}

#undef SQLBOX_TRACE

#endif
