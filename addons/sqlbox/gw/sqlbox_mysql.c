#include "gwlib/gwlib.h"
#ifdef HAVE_MYSQL
#include "gwlib/dbpool.h"
#include <mysql/mysql.h>
#define sqlbox_mysql_c
#include "sqlbox_mysql.h"

#define sql_update mysql_update
#define sql_select mysql_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;

/*
 * Our connection pool to mysql.
 */

static DBPool *pool = NULL;

static void mysql_update(const Octstr *sql)
{
    int state;
    DBPoolConn *pc;

#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MYSQL: Database pool got no connection! DB update failed!");
        return;
    }

    state = mysql_query(pc->conn, octstr_get_cstr(sql));
    if (state != 0)
        error(0, "MYSQL: %s", mysql_error(pc->conn));

    dbpool_conn_produce(pc);
}

static MYSQL_RES* mysql_select(const Octstr *sql)
{
    int state;
    MYSQL_RES *result = NULL;
    DBPoolConn *pc;

#if defined(SQLBOX_TRACE)
    debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "MYSQL: Database pool got no connection! DB update failed!");
        return NULL;
    }

    state = mysql_query(pc->conn, octstr_get_cstr(sql));
    if (state != 0) {
        error(0, "MYSQL: %s", mysql_error(pc->conn));
    } else {
        result = mysql_store_result(pc->conn);
    }

    dbpool_conn_produce(pc);

    return result;
}

void sqlbox_configure_mysql(Cfg* cfg)
{
    CfgGroup *grp;
    Octstr *sql;

    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: MySQL: group 'sqlbox' is not specified!");

    sqlbox_logtable = cfg_get(grp, octstr_imm("sql-log-table"));
    if (sqlbox_logtable == NULL) {
        panic(0, "No 'sql-log-table' not configured.");
    }
    sqlbox_insert_table = cfg_get(grp, octstr_imm("sql-insert-table"));
    if (sqlbox_insert_table == NULL) {
        panic(0, "No 'sql-insert-table' not configured.");
    }

    /* create send_sms && sent_sms tables if they do not exist */
    sql = octstr_format(SQLBOX_MYSQL_CREATE_LOG_TABLE, sqlbox_logtable);
    sql_update(sql);
    octstr_destroy(sql);
    sql = octstr_format(SQLBOX_MYSQL_CREATE_INSERT_TABLE, sqlbox_insert_table);
    sql_update(sql);
    octstr_destroy(sql);
    /* end table creation */
}

#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
#define atol_null(x) ((x != NULL) ? atol(x) : -1)
Msg *mysql_fetch_msg()
{
    Msg *msg = NULL;
    Octstr *sql, *delet, *id;
    MYSQL_RES *res;
    MYSQL_ROW row;

    sql = octstr_format(SQLBOX_MYSQL_SELECT_QUERY, sqlbox_insert_table);
#if defined(SQLBOX_TRACE)
     debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));
#endif
    res = mysql_select(sql);
    if (res == NULL) {
        debug("sqlbox", 0, "SQL statement failed: %s", octstr_get_cstr(sql));
    }
    else {
        if (mysql_num_rows(res) >= 1) {
            row = mysql_fetch_row(res);
            id = octstr_null_create(row[0]);
            /* save fields in this row as msg struct */
            msg = msg_create(sms);
            msg->sms.sender     = octstr_null_create(row[2]);
            msg->sms.receiver   = octstr_null_create(row[3]);
            msg->sms.udhdata    = octstr_null_create(row[4]);
            msg->sms.msgdata    = octstr_null_create(row[5]);
            msg->sms.time       = atol_null(row[6]);
            msg->sms.smsc_id    = octstr_null_create(row[7]);
            msg->sms.service    = octstr_null_create(row[8]);
            msg->sms.account    = octstr_null_create(row[9]);
            /* msg->sms.id      = atol_null(row[10]); */
            msg->sms.sms_type   = atol_null(row[11]);
            msg->sms.mclass     = atol_null(row[12]);
            msg->sms.mwi        = atol_null(row[13]);
            msg->sms.coding     = atol_null(row[14]);
            msg->sms.compress   = atol_null(row[15]);
            msg->sms.validity   = atol_null(row[16]);
            msg->sms.deferred   = atol_null(row[17]);
            msg->sms.dlr_mask   = atol_null(row[18]);
            msg->sms.dlr_url    = octstr_null_create(row[19]);
            msg->sms.pid        = atol_null(row[20]);
            msg->sms.alt_dcs    = atol_null(row[21]);
            msg->sms.rpi        = atol_null(row[22]);
            msg->sms.charset    = octstr_null_create(row[23]);
            msg->sms.binfo      = octstr_null_create(row[25]);
            msg->sms.meta_data  = octstr_null_create(row[26]);
            if (row[24] == NULL) {
                msg->sms.boxc_id= octstr_duplicate(sqlbox_id);
            }
            else {
                msg->sms.boxc_id= octstr_null_create(row[24]);
            }
            /* delete current row */
            delet = octstr_format(SQLBOX_MYSQL_DELETE_QUERY, sqlbox_insert_table, id);
#if defined(SQLBOX_TRACE)
            debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(delet));
#endif
            mysql_update(delet);
            octstr_destroy(id);
            octstr_destroy(delet);
        }
        mysql_free_result(res);
    }
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

void mysql_save_msg(Msg *msg, Octstr *momt /*, Octstr smsbox_id */)
{
    Octstr *sql;
    Octstr *stuffer[30];
    int stuffcount = 0;

    sql = octstr_format(SQLBOX_MYSQL_INSERT_QUERY, sqlbox_logtable, st_str(momt), st_str(msg->sms.sender),
        st_str(msg->sms.receiver), st_str(msg->sms.udhdata), st_str(msg->sms.msgdata), st_num(msg->sms.time),
        st_str(msg->sms.smsc_id), st_str(msg->sms.service), st_str(msg->sms.account), st_num(msg->sms.sms_type),
        st_num(msg->sms.mclass), st_num(msg->sms.mwi), st_num(msg->sms.coding), st_num(msg->sms.compress),
        st_num(msg->sms.validity), st_num(msg->sms.deferred), st_num(msg->sms.dlr_mask), st_str(msg->sms.dlr_url),
        st_num(msg->sms.pid), st_num(msg->sms.alt_dcs), st_num(msg->sms.rpi), st_str(msg->sms.charset),
        st_str(msg->sms.boxc_id), st_str(msg->sms.binfo), st_str(msg->sms.meta_data));
    sql_update(sql);
    while (stuffcount > 0) {
        octstr_destroy(stuffer[--stuffcount]);
    }
    octstr_destroy(sql);
}

void mysql_leave()
{
    dbpool_destroy(pool);
}

struct server_type *sqlbox_init_mysql(Cfg* cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db, *mysql_id;
    Octstr *p = NULL;
    long pool_size, mysql_port;
    int have_port;
    DBConf *db_conf = NULL;
    struct server_type *res = NULL;

    /*
     * check for all mandatory directives that specify the field names
     * of the used MySQL table
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
        panic(0, "SQLBOX: MySQL: group 'sqlbox' is not specified!");

    if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
        panic(0, "SQLBOX: MySQL: directive 'id' is not specified!");

    /*
     * now grap the required information from the 'mysql-connection' group
     * with the mysql-id we just obtained
     *
     * we have to loop through all available MySQL connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("mysql-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
         p = cfg_get(grp, octstr_imm("id"));
         if (p != NULL && octstr_compare(p, mysql_id) == 0) {
             goto found;
         }
         if (p != NULL) octstr_destroy(p);
     }
     panic(0, "SQLBOX: MySQL: connection settings for id '%s' are not specified!",
         octstr_get_cstr(mysql_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(mysql_host = cfg_get(grp, octstr_imm("host"))))
        panic(0, "SQLBOX: MySQL: directive 'host' is not specified!");
    if (!(mysql_user = cfg_get(grp, octstr_imm("username"))))
        panic(0, "SQLBOX: MySQL: directive 'username' is not specified!");
    if (!(mysql_pass = cfg_get(grp, octstr_imm("password"))))
        panic(0, "SQLBOX: MySQL: directive 'password' is not specified!");
    if (!(mysql_db = cfg_get(grp, octstr_imm("database"))))
        panic(0, "SQLBOX: MySQL: directive 'database' is not specified!");
    have_port = (cfg_get_integer(&mysql_port, grp, octstr_imm("port")) != -1);

    /*
     * ok, ready to connect to MySQL
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->mysql = gw_malloc(sizeof(MySQLConf));
    gw_assert(db_conf->mysql != NULL);

    db_conf->mysql->host = mysql_host;
    db_conf->mysql->username = mysql_user;
    db_conf->mysql->password = mysql_pass;
    db_conf->mysql->database = mysql_db;
    if (have_port) {
        db_conf->mysql->port = mysql_port;
    }

    pool = dbpool_create(DBPOOL_MYSQL, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"SQLBOX: MySQL: database pool has no connections!");

    octstr_destroy(mysql_id);

    res = gw_malloc(sizeof(struct server_type));
    gw_assert(res != NULL);

    res->type = octstr_create("MySQL");
    res->sql_enter = sqlbox_configure_mysql;
    res->sql_leave = mysql_leave;
    res->sql_fetch_msg = mysql_fetch_msg;
    res->sql_save_msg = mysql_save_msg;
    return res;
}
#endif
