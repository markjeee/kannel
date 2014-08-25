/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
 * Copyright (c) 1998-2001 WapIT Ltd.   
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the 
 *    distribution. 
 * 
 * 3. The end-user documentation included with the redistribution, 
 *    if any, must include the following acknowledgment: 
 *       "This product includes software developed by the 
 *        Kannel Group (http://www.kannel.org/)." 
 *    Alternately, this acknowledgment may appear in the software itself, 
 *    if and wherever such third-party acknowledgments normally appear. 
 * 
 * 4. The names "Kannel" and "Kannel Group" must not be used to 
 *    endorse or promote products derived from this software without 
 *    prior written permission. For written permission, please  
 *    contact org@kannel.org. 
 * 
 * 5. Products derived from this software may not be called "Kannel", 
 *    nor may "Kannel" appear in their name, without prior written 
 *    permission of the Kannel Group. 
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS 
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,  
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT  
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR  
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,  
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE  
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 * ==================================================================== 
 * 
 * This software consists of voluntary contributions made by many 
 * individuals on behalf of the Kannel Group.  For more information on  
 * the Kannel Group, please see <http://www.kannel.org/>. 
 * 
 * Portions of this software are based upon software originally written at  
 * WapIT Ltd., Helsinki, Finland for the Kannel project.  
 */ 

/*
 * dbpool_mysql.c - implement MySQL operations for generic database connection pool
 *
 * Stipe Tolj <stolj@wapme.de>
 *      2003 Initial version.
 * Alexander Malysh <a.malysh@centrium.de>
 *      2003 Made dbpool more generic.
 */

#ifdef HAVE_MYSQL
#include <mysql.h>


static void *mysql_open_conn(const DBConf *db_conf)
{
    MYSQL *mysql = NULL;
    MySQLConf *conf = db_conf->mysql; /* make compiler happy */

    /* sanity check */
    if (conf == NULL)
        return NULL;

    /* pre-allocate */
    mysql = gw_malloc(sizeof(MYSQL));
    gw_assert(mysql != NULL);

    /* initialize mysql structures */
    if (!mysql_init(mysql)) {
        error(0, "MYSQL: init failed!");
        error(0, "MYSQL: %s", mysql_error(mysql));
        goto failed;
    }

    if (!mysql_real_connect(mysql, octstr_get_cstr(conf->host),
                            octstr_get_cstr(conf->username),
                            octstr_get_cstr(conf->password),
                            octstr_get_cstr(conf->database), 
                            conf->port, NULL, 0)) {
        error(0, "MYSQL: can not connect to database!");
        error(0, "MYSQL: %s", mysql_error(mysql));
        goto failed;
    }

    info(0, "MYSQL: Connected to server at %s.", octstr_get_cstr(conf->host));
    info(0, "MYSQL: server version %s, client version %s.",
           mysql_get_server_info(mysql), mysql_get_client_info());

    return mysql;

failed:
    if (mysql != NULL) 
        gw_free(mysql);
    return NULL;
}


static void mysql_close_conn(void *conn)
{
    if (conn == NULL)
        return;

    mysql_close((MYSQL*) conn);
    gw_free(conn);
}


static int mysql_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;

    if (mysql_ping((MYSQL*) conn)) {
        error(0, "MYSQL: database check failed!");
        error(0, "MYSQL: %s", mysql_error(conn));
        return -1;
    }

    return 0;
}


static int mysql_select(void *conn, const Octstr *sql, List *binds, List **res)
{
    MYSQL_STMT *stmt;
    MYSQL_RES *result;
    MYSQL_BIND *bind = NULL;
    long i, binds_len;
    int ret;

    *res = NULL;

    /* allocate statement handle */
    stmt = mysql_stmt_init((MYSQL*) conn);
    if (stmt == NULL) {
        error(0, "MYSQL: mysql_stmt_init(), out of memory.");
        return -1;
    }
    if (mysql_stmt_prepare(stmt, octstr_get_cstr(sql), octstr_len(sql))) {
        error(0, "MYSQL: Unable to prepare statement: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
    /* bind params if any */
    binds_len = gwlist_len(binds);
    if (binds_len > 0) {
        bind = gw_malloc(sizeof(MYSQL_BIND) * binds_len);
        memset(bind, 0, sizeof(MYSQL_BIND) * binds_len);
        for (i = 0; i < binds_len; i++) {
            Octstr *str = gwlist_get(binds, i);

            bind[i].buffer_type = MYSQL_TYPE_STRING;
            bind[i].buffer = octstr_get_cstr(str);
            bind[i].buffer_length = octstr_len(str);
        }
        /* Bind the buffers */
        if (mysql_stmt_bind_param(stmt, bind)) {
          error(0, "MYSQL: mysql_stmt_bind_param() failed: `%s'", mysql_stmt_error(stmt));
          gw_free(bind);
          mysql_stmt_close(stmt);
          return -1;
        }
    }

    /* execute statement */
    if (mysql_stmt_execute(stmt)) {
        error(0, "MYSQL: mysql_stmt_execute() failed: `%s'", mysql_stmt_error(stmt));
        gw_free(bind);
        mysql_stmt_close(stmt);
        return -1;
    }
    gw_free(bind);

#define DESTROY_BIND(bind, binds_len)           \
    do {                                        \
        long i;                                 \
        for (i = 0; i < binds_len; i++) {       \
            gw_free(bind[i].buffer);            \
            gw_free(bind[i].length);            \
            gw_free(bind[i].is_null);           \
        }                                       \
        gw_free(bind);                          \
    } while(0)

    /* Fetch result set meta information */
    result = mysql_stmt_result_metadata(stmt);
    if (res == NULL) {
        error(0, "MYSQL: mysql_stmt_result_metadata() failed: `%s'", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
    /* Get total columns in the query */
    binds_len = mysql_num_fields(result);
    bind = gw_malloc(sizeof(MYSQL_BIND) * binds_len);
    memset(bind, 0, sizeof(MYSQL_BIND) * binds_len);
    /* bind result bind */
    for (i = 0; i < binds_len; i++) {
        MYSQL_FIELD *field = mysql_fetch_field(result); /* retrieve field metadata */

        debug("gwlib.dbpool_mysql", 0, "column=%s buffer_type=%d max_length=%ld length=%ld", field->name, field->type, field->max_length, field->length);

        switch(field->type) {
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
            bind[i].buffer_type = field->type;
            bind[i].buffer = (char*)gw_malloc(sizeof(MYSQL_TIME));
            bind[i].is_null = gw_malloc(sizeof(my_bool));
            bind[i].length = gw_malloc(sizeof(unsigned long));
            break;
        default:
            bind[i].buffer_type = MYSQL_TYPE_STRING;
            bind[i].buffer = gw_malloc(field->length);
            bind[i].buffer_length = field->length;
            bind[i].length = gw_malloc(sizeof(unsigned long));
            bind[i].is_null = gw_malloc(sizeof(my_bool));
            break;
        }
    }
    mysql_free_result(result);

    if (mysql_stmt_bind_result(stmt, bind)) {
        error(0, "MYSQL: mysql_stmt_bind_result() failed: `%s'", mysql_stmt_error(stmt));
        DESTROY_BIND(bind, binds_len);
        mysql_stmt_close(stmt);
        return -1;
    }

    *res = gwlist_create();
    while(!(ret = mysql_stmt_fetch(stmt))) {
        List *row = gwlist_create();
        for (i = 0; i < binds_len; i++) {
            Octstr *str = NULL;
            MYSQL_TIME *ts;

            if (*bind[i].is_null) {
                gwlist_produce(row, octstr_create(""));
                continue;
            }

            switch(bind[i].buffer_type) {
            case MYSQL_TYPE_DATE:
                ts = bind[i].buffer;
                str = octstr_format("%04d-%02d-%02d", ts->year, ts->month, ts->day);
                break;
            case MYSQL_TYPE_TIME:
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_TIMESTAMP:
                ts = bind[i].buffer;
                str = octstr_format("%04d-%02d-%02d %02d:%02d:%02d", ts->year, ts->month, ts->day, ts->hour, ts->minute, ts->second);
                break;
            default:
                if (bind[i].length == 0)
                    str= octstr_create("");
                else
                    str = octstr_create_from_data(bind[i].buffer, *bind[i].length);
                break;
            }
            gwlist_produce(row, str);
        }
        gwlist_produce(*res, row);
    }
    DESTROY_BIND(bind, binds_len);
#undef DESTROY_BIND

    /* any errors by fetch? */
    if (ret != MYSQL_NO_DATA) {
        List *row;
        error(0, "MYSQL: mysql_stmt_bind_result() failed: `%s'", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        while((row = gwlist_extract_first(*res)) != NULL)
            gwlist_destroy(row, octstr_destroy_item);
        gwlist_destroy(*res, NULL);
        *res = NULL;
        return -1;
    }

    mysql_stmt_close(stmt);

    return 0;
}


static int mysql_update(void *conn, const Octstr *sql, List *binds)
{
    MYSQL_STMT *stmt;
    MYSQL_BIND *bind = NULL;
    long i, binds_len;
    int ret;

    /* allocate statement handle */
    stmt = mysql_stmt_init((MYSQL*) conn);
    if (stmt == NULL) {
        error(0, "MYSQL: mysql_stmt_init(), out of memory.");
        return -1;
    }
    if (mysql_stmt_prepare(stmt, octstr_get_cstr(sql), octstr_len(sql))) {
        error(0, "MYSQL: Unable to prepare statement: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
    /* bind params if any */
    binds_len = gwlist_len(binds);
    if (binds_len > 0) {
        bind = gw_malloc(sizeof(MYSQL_BIND) * binds_len);
        memset(bind, 0, sizeof(MYSQL_BIND) * binds_len);
        for (i = 0; i < binds_len; i++) {
            Octstr *str = gwlist_get(binds, i);

            bind[i].buffer_type = MYSQL_TYPE_STRING;
            bind[i].buffer = octstr_get_cstr(str);
            bind[i].buffer_length = octstr_len(str);
        }
        /* Bind the buffers */
        if (mysql_stmt_bind_param(stmt, bind)) {
          error(0, "MYSQL: mysql_stmt_bind_param() failed: `%s'", mysql_stmt_error(stmt));
          gw_free(bind);
          mysql_stmt_close(stmt);
          return -1;
        }
    }

    /* execute statement */
    if (mysql_stmt_execute(stmt)) {
        error(0, "MYSQL: mysql_stmt_execute() failed: `%s'", mysql_stmt_error(stmt));
        gw_free(bind);
        mysql_stmt_close(stmt);
        return -1;
    }
    gw_free(bind);

    ret = mysql_stmt_affected_rows(stmt);
    mysql_stmt_close(stmt);

    return ret;
}


static void mysql_conf_destroy(DBConf *db_conf)
{
    MySQLConf *conf = db_conf->mysql;

    octstr_destroy(conf->host);
    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->database);

    gw_free(conf);
    gw_free(db_conf);
}


static struct db_ops mysql_ops = {
    .open = mysql_open_conn,
    .close = mysql_close_conn,
    .check = mysql_check_conn,
    .select = mysql_select,
    .update = mysql_update,
    .conf_destroy = mysql_conf_destroy
};

#endif /* HAVE_MYSQL */

