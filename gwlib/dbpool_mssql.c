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
 * dbpool_mssql.c - implement MS SQL operations for generic database connection pool
 *
 * Alejandro Guerrieri <aguerrieri at kannel dot org>
 * 
 */

#ifdef HAVE_MSSQL

#include <ctpublic.h>

static int mssql_select(void *theconn, const Octstr *sql, List *binds, List **res);
static int mssql_update(void *theconn, const Octstr *sql, List *binds);

/*
 * Macros to set the CT-Lib column format structure
 */

#define mssql_undef_colfmt(cols) \
    for (j = 0; j < cols; j++) { gw_free(data[j].format); }

#define mssql_undef_coldata(cols) \
    mssql_undef_colfmt(cols); \
    for (j = 0; j < cols; j++) { gw_free(data[j].data); } \
    gw_free(data);

struct mssql_conn {
    CS_CONTEXT *context;
    CS_CONNECTION *connection;
    CS_COMMAND *command;
};

static void mssql_checkerr(int err)
{
    switch (err) {
        case CS_CMD_SUCCEED:
        case CS_CMD_DONE:
        case CS_END_RESULTS:
            break;
        case CS_CMD_FAIL:
            error(0, "Command Failed");
            break;
        default:
            error(0, "Unknown Command Error");
    }
}

static void* mssql_open_conn(const DBConf *db_conf)
{
    Octstr *sql;
    int ret;

    MSSQLConf *cfg = db_conf->mssql;
    struct mssql_conn *conn = gw_malloc(sizeof(struct mssql_conn));

    gw_assert(conn != NULL);
    memset(conn, 0, sizeof(struct mssql_conn));

    cs_ctx_alloc(CS_VERSION_100, &conn->context);

    ct_init(conn->context, CS_VERSION_100); 

    ct_con_alloc(conn->context, &conn->connection); 
    ct_con_props(conn->connection, CS_SET, CS_USERNAME, octstr_get_cstr(cfg->username),
            CS_NULLTERM, NULL); 
    ct_con_props(conn->connection, CS_SET, CS_PASSWORD, octstr_get_cstr(cfg->password), 
            CS_NULLTERM, NULL);

    ct_connect(conn->connection, octstr_get_cstr(cfg->server), CS_NULLTERM);
    ct_cmd_alloc(conn->connection, &conn->command);

    sql = octstr_format("USE %S", cfg->database);
    ret = mssql_update(conn, sql, NULL);
    octstr_destroy(sql);
    if (ret < 0 )
        error(0, "MSSQL: DB selection failed!");
    return conn;
}

void mssql_close_conn(void *theconn)
{
    struct mssql_conn *conn = (struct mssql_conn*) theconn;

    gw_assert(conn != NULL);

    ct_cmd_drop(conn->command);
    ct_close(conn->connection, CS_UNUSED);
    ct_con_drop(conn->connection);
    ct_exit(conn->context, CS_UNUSED);
    cs_ctx_drop(conn->context);
}

static int mssql_check_conn(void *theconn)
{
    int status;
    CS_INT *outlen;

    struct mssql_conn *conn = (struct mssql_conn*) theconn;

    if (ct_con_props(conn->connection, CS_GET, CS_CON_STATUS, 
            &status, CS_UNUSED, outlen) != CS_SUCCEED)
        return 1;

    return (status & CS_CONSTAT_CONNECTED) ? 0:1;
}

static void mssql_conf_destroy(DBConf *theconf)
{
    MSSQLConf *conf = theconf->mssql;

    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->server);

    gw_free(conf);
    gw_free(theconf);
}

struct data_s {
    CS_TEXT *data;
    CS_DATAFMT *format;
    CS_INT size;
    CS_SMALLINT ind;
};

static int mssql_select(void *theconn, const Octstr *sql, List *binds, List **res)
{
    List *row;
    int columns;
    CS_RETCODE ret, res_type;
    int num_rows, count, i, j;
    struct data_s *data;
    struct mssql_conn *conn = (struct mssql_conn*) theconn;
    int binds_len = (binds ? gwlist_len(binds) : 0);
    
    *res = NULL;

    gw_assert(conn != NULL);

    ct_command(conn->command, CS_LANG_CMD,
        octstr_get_cstr(sql), CS_NULLTERM, CS_UNUSED); 

    if ((ret = ct_send(conn->command)) != CS_SUCCEED) {
        error(0, "Error sending query");
        return -1;
    }

    *res = gwlist_create();
    while((ret = ct_results(conn->command, &res_type)) == CS_SUCCEED) { 
        switch (res_type) { 
            case CS_ROW_RESULT:
                if (ct_res_info(conn->command, CS_NUMDATA,
                    (CS_INT *)&columns, CS_UNUSED, NULL) != CS_SUCCEED) {
                    error(0, "Error fetching attributes");
                    return -1;
                }
                data = gw_malloc(sizeof(struct data_s)*columns);
                memset(data, 0, sizeof(struct data_s)*columns);

                for (i = 0; i < columns; i++) {
                    data[i].format = gw_malloc(sizeof(CS_DATAFMT));
                    memset(data[i].format, 0, sizeof(CS_DATAFMT));
                    if (ct_describe(conn->command, i+1, data[i].format) != CS_SUCCEED) {
                        error(0, "Error fetching column description");
                        mssql_undef_colfmt(i);
                        gw_free(data);
                        return -1;
                    }
                    data[i].format->maxlength++;
                    data[i].data = gw_malloc(data[i].format->maxlength);
                    data[i].format->datatype = CS_CHAR_TYPE;
                    data[i].format->format = CS_FMT_NULLTERM;
                    if (ct_bind(conn->command, i+1, data[i].format, data[i].data,
                        &data[i].size, &data[i].ind) != CS_SUCCEED) {
                        error(0, "Error binding column");
                        mssql_undef_coldata(i);
                        return -1;
                    }
                }
                while(((ret = ct_fetch(conn->command, CS_UNUSED, CS_UNUSED,
                    CS_UNUSED, &count)) == CS_SUCCEED) || (ret == CS_ROW_FAIL)) {
                    if( ret == CS_ROW_FAIL ) {
                        error(0, "Error on row %d in this fetch batch.", count+1);
                        mssql_undef_coldata(columns);
                        gw_free(data);
                        return -1;
                    }
                    row = gwlist_create();
                    for (i = 0; i < columns; i++) {                            
                        if (data[i].data == NULL || data[i].ind == -1) {
                            gwlist_insert(row, i, octstr_create(""));
                        } else {
                            gwlist_insert(row, i, octstr_create_from_data((char *)data[i].data, data[i].size));
                        }
                    }
                    gwlist_append(*res, row);                    
                }
                if( ret != CS_END_DATA ) {
                    error(0, "ct_fetch failed");
                    mssql_undef_coldata(columns);
                    while ((row = gwlist_extract_first(*res)) != NULL)
                        gwlist_destroy(row, octstr_destroy_item);
                    gwlist_destroy(*res, NULL);
                    *res = NULL;
                    return -1;
                }
                mssql_undef_coldata(columns);
                break;
            case CS_CMD_SUCCEED:
            case CS_CMD_DONE:
            case CS_STATUS_RESULT:
                break;
            case CS_CMD_FAIL:
                error(0, "select failed!");
                return -1;
                break;
            default:
                error(0, "ct_result returned unexpected result type: %d", res_type);
                return -1;
                break;
        }
    }
    return 0;
}

/* Run commands from which we expect no results returned */
static int mssql_update(void *theconn, const Octstr *sql, List *binds)
{
    CS_RETCODE status, results_ret;
    CS_INT result_type;

    struct mssql_conn *conn = (struct mssql_conn*) theconn;

    gw_assert(conn != NULL);
    if (conn->command == NULL)
        return -1;
    
    status = ct_command(conn->command, CS_LANG_CMD, octstr_get_cstr(sql), CS_NULLTERM, CS_UNUSED);
    if (status != CS_SUCCEED) {
        error(0, "ct_command() failed\n");
        return -1;
    }
    status = ct_send(conn->command);
    if (status != CS_SUCCEED) {
        error(0, "ct_send() failed\n");
        return -1;
    }
    while ((results_ret = ct_results(conn->command, &result_type)) == CS_SUCCEED) {
        switch ((int) result_type) {
            case CS_CMD_SUCCEED:
            case CS_CMD_DONE:
            case CS_STATUS_RESULT:
                break;
            default:
                mssql_checkerr(result_type);
                return -1;
        }
    }
    mssql_checkerr(results_ret);
    switch ((int) results_ret) {
        case CS_END_RESULTS:
            break;
        default:
            mssql_checkerr(result_type);
            return -1;
    }

    return 0;
}

static struct db_ops mssql_ops = {
    .open = mssql_open_conn,
    .close = mssql_close_conn,
    .check = mssql_check_conn,
    .conf_destroy = mssql_conf_destroy,
    .select = mssql_select,
    .update = mssql_update
};

#endif
