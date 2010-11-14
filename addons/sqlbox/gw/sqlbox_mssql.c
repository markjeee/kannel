#include "gwlib/gwlib.h"
#ifdef HAVE_MSSQL
#include "gwlib/dbpool.h"
#include <ctpublic.h>
#define sqlbox_mssql_c
#include "sqlbox_mssql.h"

#define sql_update dbpool_conn_update
#define sql_select dbpool_conn_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;

/*
 * Our connection pool to mssql.
 */

static DBPool *pool = NULL;

void sqlbox_configure_mssql(Cfg* cfg)
{
    DBPoolConn *pc;
    CfgGroup *grp;
    Octstr *sql;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: MSSql: group 'sqlbox' is not specified!");

    sqlbox_logtable = cfg_get(grp, octstr_imm("sql-log-table"));
    if (sqlbox_logtable == NULL) {
        panic(0, "Parameter 'sql-log-table' not configured.");
    }
    sqlbox_insert_table = cfg_get(grp, octstr_imm("sql-insert-table"));
    if (sqlbox_insert_table == NULL) {
        panic(0, "Parameter 'sql-insert-table' not configured.");
    }

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MSSql: DBPool Error!");
        return;
    }

    /* create send_sms && sent_sms tables if they do not exist */
    sql = octstr_format(SQLBOX_MSSQL_CREATE_LOG_TABLE, sqlbox_logtable, sqlbox_logtable);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);

    sql = octstr_format(SQLBOX_MSSQL_CREATE_INSERT_TABLE, sqlbox_insert_table, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);

    dbpool_conn_produce(pc);

}

Octstr *get_column(Octstr *str)
{
    Octstr *ret = octstr_create(octstr_get_cstr(str));
    if (ret != NULL)
        octstr_strip_blanks(ret);
    return ret;
}

static Octstr *get_numeric_value_or_return_null(long int num)
{
    if (num == -1) {
        return octstr_create("NULL");
    }
    return octstr_format("%ld", num);
}

static Octstr *get_string_value_or_return_null(Octstr *str)
{
    if (str == NULL) {
        return octstr_create("NULL");
    } else if (octstr_compare(str, octstr_imm("")) == 0) {
        return octstr_create("NULL");
    }
    octstr_replace(str, octstr_imm("\\"), octstr_imm("\\\\"));
    octstr_replace(str, octstr_imm("\'"), octstr_imm("\\\'"));
    return octstr_format("\'%S\'", str);
}

#define st_num(x) (stuffer[stuffcount++] = get_numeric_value_or_return_null(x))
#define st_str(x) (stuffer[stuffcount++] = get_string_value_or_return_null(x))
#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
#define atol_null(x) ((x != NULL) ? atol(x) : -1)
#define get_mssql_octstr_col(x) (get_column(gwlist_get(row,x)))
#define get_mssql_long_col(x) (atol(octstr_get_cstr(gwlist_get(row,x))))

Msg *mssql_fetch_msg()
{
    Msg *msg = NULL;
    Octstr *sql, *delet, *id;
    List *res, *row;
    int ret;
    DBPoolConn *pc;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MSSql: DBPool error!");
        return;
    }

    sql = octstr_format(SQLBOX_MSSQL_SELECT_QUERY, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (sql_select(pc, sql, NULL, &res) != 0) {
        debug("sqlbox", 0, "SQL statement failed: %s", octstr_get_cstr(sql));
    } else {
        if (gwlist_len(res) > 0) {
            row = gwlist_extract_first(res);
            id = get_mssql_octstr_col(0);
            /* save fields in this row as msg struct */
            msg = msg_create(sms);
            msg->sms.sender     = get_mssql_octstr_col(2);
            msg->sms.receiver   = get_mssql_octstr_col(3);
            msg->sms.udhdata    = get_mssql_octstr_col(4);
            msg->sms.msgdata    = get_mssql_octstr_col(5);
            msg->sms.time       = get_mssql_long_col(6);
            msg->sms.smsc_id    = get_mssql_octstr_col(7);
            msg->sms.service    = get_mssql_octstr_col(8);
            msg->sms.account    = get_mssql_octstr_col(9);
            /* msg->sms.id      = get_mssql_long_col(10); */
            msg->sms.sms_type   = get_mssql_long_col(11);
            msg->sms.mclass     = get_mssql_long_col(12);
            msg->sms.mwi        = get_mssql_long_col(13);
            msg->sms.coding     = get_mssql_long_col(14);
            msg->sms.compress   = get_mssql_long_col(15);
            msg->sms.validity   = get_mssql_long_col(16);
            msg->sms.deferred   = get_mssql_long_col(17);
            msg->sms.dlr_mask   = get_mssql_long_col(18);
            msg->sms.dlr_url    = get_mssql_octstr_col(19);
            msg->sms.pid        = get_mssql_long_col(20);
            msg->sms.alt_dcs    = get_mssql_long_col(21);
            msg->sms.rpi        = get_mssql_long_col(22);
            msg->sms.charset    = get_mssql_octstr_col(23);
            msg->sms.binfo      = get_mssql_octstr_col(25);
            msg->sms.meta_data  = get_mssql_octstr_col(26);
            if (gwlist_get(row,24) == NULL) {
                msg->sms.boxc_id = octstr_duplicate(sqlbox_id);
            }
            else {
                msg->sms.boxc_id = get_mssql_octstr_col(24);
            }
            /* delete current row */
            delet = octstr_format(SQLBOX_MSSQL_DELETE_QUERY, sqlbox_insert_table, id);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(delet));
#endif
            sql_update(pc, delet, NULL);
            octstr_destroy(id);
            octstr_destroy(delet);
            gwlist_destroy(row, octstr_destroy_item);
        }
        gwlist_destroy(res, NULL);
    }
    dbpool_conn_produce(pc);
    octstr_destroy(sql);
    return msg;
}

void mssql_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */)
{
    Octstr *sql;
    Octstr *stuffer[30];
    int stuffcount = 0;
    DBPoolConn *pc;
    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MSSql: DBPool Error!");
        return;
    }

    sql = octstr_format(SQLBOX_MSSQL_INSERT_QUERY, sqlbox_logtable, st_str(momt), st_str(msg->sms.sender),
        st_str(msg->sms.receiver), st_str(msg->sms.udhdata), st_str(msg->sms.msgdata), st_num(msg->sms.time),
        st_str(msg->sms.smsc_id), st_str(msg->sms.service), st_str(msg->sms.account), st_num(msg->sms.sms_type),
        st_num(msg->sms.mclass), st_num(msg->sms.mwi), st_num(msg->sms.coding), st_num(msg->sms.compress),
        st_num(msg->sms.validity), st_num(msg->sms.deferred), st_num(msg->sms.dlr_mask), st_str(msg->sms.dlr_url),
        st_num(msg->sms.pid), st_num(msg->sms.alt_dcs), st_num(msg->sms.rpi), st_str(msg->sms.charset),
        st_str(msg->sms.boxc_id), st_str(msg->sms.binfo), st_str(msg->sms.meta_data));
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    while (stuffcount > 0) {
        octstr_destroy(stuffer[--stuffcount]);
    }
    dbpool_conn_produce(pc);
    octstr_destroy(sql);
}

void mssql_leave()
{
    dbpool_destroy(pool);
}

struct server_type *sqlbox_init_mssql(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *mssql_user, *mssql_pass, *mssql_server, *mssql_database, *mssql_id;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;
    struct server_type *res = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used MSSql table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: MSSql: group 'sqlbox' is not specified!");

    if (!(mssql_id = cfg_get(grp, octstr_imm("id"))))
           panic(0, "SQLBOX: MSSql: directive 'id' is not specified!");

    /*
     * now grap the required information from the 'mssql-connection' group
     * with the mssql-id we just obtained
     *
     * we have to loop through all available MSSql connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("mssql-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, mssql_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     panic(0, "SQLBOX: MSSql: connection settings for id '%s' are not specified!",
           octstr_get_cstr(mssql_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(mssql_user = cfg_get(grp, octstr_imm("username"))))
           panic(0, "SQLBOX: MSSql: directive 'username' is not specified!");
    if (!(mssql_pass = cfg_get(grp, octstr_imm("password"))))
           panic(0, "SQLBOX: MSSql: directive 'password' is not specified!");
    if (!(mssql_server = cfg_get(grp, octstr_imm("server"))))
           panic(0, "SQLBOX: MSSql: directive 'server' is not specified!");
    if (!(mssql_database = cfg_get(grp, octstr_imm("database"))))
           panic(0, "SQLBOX: MSSql: directive 'database' is not specified!");

    /*
     * ok, ready to connect to MSSql
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->mssql = gw_malloc(sizeof(MSSQLConf));
    gw_assert(db_conf->mssql != NULL);

    db_conf->mssql->username = mssql_user;
    db_conf->mssql->password = mssql_pass;
    db_conf->mssql->server = mssql_server;
    db_conf->mssql->database = mssql_database;

    pool = dbpool_create(DBPOOL_MSSQL, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"SQLBOX: MSSql: database pool has no connections!");

    octstr_destroy(mssql_id);

    res = gw_malloc(sizeof(struct server_type));
    gw_assert(res != NULL);

    res->type = octstr_create("MSSql");
    res->sql_enter = sqlbox_configure_mssql;
    res->sql_leave = mssql_leave;
    res->sql_fetch_msg = mssql_fetch_msg;
    res->sql_save_msg = mssql_save_msg;
    return res;
}
#endif
