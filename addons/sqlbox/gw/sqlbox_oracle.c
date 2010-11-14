#include "gwlib/gwlib.h"
#ifdef HAVE_ORACLE
#include "gwlib/dbpool.h"
#include <oci.h>
#define sqlbox_oracle_c
#include "sqlbox_oracle.h"

#define sql_update dbpool_conn_update
#define sql_select dbpool_conn_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;

/*
 * Our connection pool to oracle.
 */

static DBPool *pool = NULL;

void sqlbox_configure_oracle(Cfg* cfg)
{
    DBPoolConn *pc;
    CfgGroup *grp;
    Octstr *sql;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Oracle: group 'sqlbox' is not specified!");

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
        error(0, "Oracle: DBPool Error!");
        return;
    }

    /* create send_sms && sent_sms tables if they do not exist */
    sql = octstr_format(SQLBOX_ORACLE_CREATE_LOG_TABLE, sqlbox_logtable, sqlbox_logtable);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);

    sql = octstr_format(SQLBOX_ORACLE_CREATE_INSERT_TABLE, sqlbox_insert_table, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);
    /*
     * Oracle implementation using a sequence and a trigger for auto_increment fields.
     */
    sql = octstr_format(SQLBOX_ORACLE_CREATE_LOG_SEQUENCE, sqlbox_logtable);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);

    sql = octstr_format(SQLBOX_ORACLE_CREATE_INSERT_SEQUENCE, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);

    sql = octstr_format(SQLBOX_ORACLE_CREATE_LOG_TRIGGER, sqlbox_logtable, sqlbox_logtable, sqlbox_logtable);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);

    sql = octstr_format(SQLBOX_ORACLE_CREATE_INSERT_TRIGGER, sqlbox_insert_table, sqlbox_insert_table, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    sql_update(pc, sql, NULL);
    octstr_destroy(sql);
    /* end table creation */

    dbpool_conn_produce(pc);

}

#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
#define atol_null(x) ((x != NULL) ? atol(x) : -1)
#define get_oracle_octstr_col(x) (octstr_create(octstr_get_cstr(gwlist_get(row,x))))
#define get_oracle_long_col(x) (atol(octstr_get_cstr(gwlist_get(row,x))))
Msg *oracle_fetch_msg()
{
    Msg *msg = NULL;
    Octstr *sql, *delet, *id;
    List *res, *row;
    int ret;
    DBPoolConn *pc;

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "Oracle: DBPool error!");
        return;
    }

    sql = octstr_format(SQLBOX_ORACLE_SELECT_QUERY, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    if (sql_select(pc, sql, NULL, &res) != 0) {
        debug("sqlbox", 0, "SQL statement failed: %s", octstr_get_cstr(sql));
    } else {
        if (gwlist_len(res) > 0) {
            row = gwlist_extract_first(res);
            id = get_oracle_octstr_col(0);
            /* save fields in this row as msg struct */
            msg = msg_create(sms);
            msg->sms.sender     = get_oracle_octstr_col(2);
            msg->sms.receiver   = get_oracle_octstr_col(3);
            msg->sms.udhdata    = get_oracle_octstr_col(4);
            msg->sms.msgdata    = get_oracle_octstr_col(5);
            msg->sms.time       = get_oracle_long_col(6);
            msg->sms.smsc_id    = get_oracle_octstr_col(7);
            msg->sms.service    = get_oracle_octstr_col(8);
            msg->sms.account    = get_oracle_octstr_col(9);
            /* msg->sms.id      = get_oracle_long_col(10); */
            msg->sms.sms_type   = get_oracle_long_col(11);
            msg->sms.mclass     = get_oracle_long_col(12);
            msg->sms.mwi        = get_oracle_long_col(13);
            msg->sms.coding     = get_oracle_long_col(14);
            msg->sms.compress   = get_oracle_long_col(15);
            msg->sms.validity   = get_oracle_long_col(16);
            msg->sms.deferred   = get_oracle_long_col(17);
            msg->sms.dlr_mask   = get_oracle_long_col(18);
            msg->sms.dlr_url    = get_oracle_octstr_col(19);
            msg->sms.pid        = get_oracle_long_col(20);
            msg->sms.alt_dcs    = get_oracle_long_col(21);
            msg->sms.rpi        = get_oracle_long_col(22);
            msg->sms.charset    = get_oracle_octstr_col(23);
            msg->sms.binfo      = get_oracle_octstr_col(25);
            msg->sms.binfo      = get_oracle_octstr_col(26);
            if (gwlist_get(row,24) == NULL) {
                msg->sms.boxc_id= octstr_duplicate(sqlbox_id);
            }
            else {
                msg->sms.boxc_id= get_oracle_octstr_col(24);
            }
            /* delete current row */
            delet = octstr_format(SQLBOX_ORACLE_DELETE_QUERY, sqlbox_insert_table, id);
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
    }
    if (octstr_compare(str, octstr_imm("")) == 0) {
        return octstr_create("NULL");
    }
    octstr_replace(str, octstr_imm("\\"), octstr_imm("\\\\"));
    octstr_replace(str, octstr_imm("\'"), octstr_imm("\\\'"));
    return octstr_format("\'%S\'", str);
}

#define st_num(x) (stuffer[stuffcount++] = get_numeric_value_or_return_null(x))
#define st_str(x) (stuffer[stuffcount++] = get_string_value_or_return_null(x))

void oracle_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */)
{
    Octstr *sql;
    Octstr *stuffer[30];
    int stuffcount = 0;
    DBPoolConn *pc;
    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "Oracle: DBPool Error!");
        return;
    }

    sql = octstr_format(SQLBOX_ORACLE_INSERT_QUERY, sqlbox_logtable, st_str(momt), st_str(msg->sms.sender),
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

void oracle_leave()
{
    dbpool_destroy(pool);
}

struct server_type *sqlbox_init_oracle(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *oracle_user, *oracle_pass, *oracle_tnsname, *oracle_id;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;
    struct server_type *res = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used Oracle table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: Oracle: group 'sqlbox' is not specified!");

    if (!(oracle_id = cfg_get(grp, octstr_imm("id"))))
           panic(0, "SQLBOX: Oracle: directive 'id' is not specified!");

    /*
     * now grap the required information from the 'oracle-connection' group
     * with the oracle-id we just obtained
     *
     * we have to loop through all available Oracle connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("oracle-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, oracle_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     panic(0, "SQLBOX: Oracle: connection settings for id '%s' are not specified!",
           octstr_get_cstr(oracle_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(oracle_user = cfg_get(grp, octstr_imm("username"))))
           panic(0, "SQLBOX: Oracle: directive 'username' is not specified!");
    if (!(oracle_pass = cfg_get(grp, octstr_imm("password"))))
           panic(0, "SQLBOX: Oracle: directive 'password' is not specified!");
    if (!(oracle_tnsname = cfg_get(grp, octstr_imm("tnsname"))))
           panic(0, "SQLBOX: Oracle: directive 'tnsname' is not specified!");

    /*
     * ok, ready to connect to Oracle
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->oracle = gw_malloc(sizeof(OracleConf));
    gw_assert(db_conf->oracle != NULL);

    db_conf->oracle->username = oracle_user;
    db_conf->oracle->password = oracle_pass;
    db_conf->oracle->tnsname = oracle_tnsname;

    pool = dbpool_create(DBPOOL_ORACLE, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"SQLBOX: Oracle: database pool has no connections!");

    octstr_destroy(oracle_id);

    res = gw_malloc(sizeof(struct server_type));
    gw_assert(res != NULL);

    res->type = octstr_create("Oracle");
    res->sql_enter = sqlbox_configure_oracle;
    res->sql_leave = oracle_leave;
    res->sql_fetch_msg = oracle_fetch_msg;
    res->sql_save_msg = oracle_save_msg;
    return res;
}
#endif
