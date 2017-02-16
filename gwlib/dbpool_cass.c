/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2016 Kannel Group  
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
 * dbpool_cass.c - implement Cassandra 2.x, 3.0 operations for generic database connection pool
 *
 * Stipe Tolj <stolj at kannel.org>, 2015-09-25
 */

#ifdef HAVE_CASS
#include <cassandra.h>

struct CassInstance {
    CassCluster *cluster;
    CassSession *session;
};

typedef struct CassInstance CassInstance;


static void cass_log(const CassLogMessage *message, void *data)
{
    switch (message->severity) {

#define CASS_LEVEL(e, f) \
    case e: \
        f(0, "Cassandra: (%s:%d:%s): %s", \
          message->file, message->line, message->function, \
          message->message); \
        break;

    CASS_LEVEL(CASS_LOG_INFO, info)
    CASS_LEVEL(CASS_LOG_WARN, warning)
    CASS_LEVEL(CASS_LOG_ERROR, error)
    CASS_LEVEL(CASS_LOG_CRITICAL, panic)

    default:
        break;
    }
}


static void print_error(CassFuture* future)
{
    const char* message;
    size_t message_length;

    cass_future_error_message(future, &message, &message_length);
    error(0, "Cassandra: %s", message);
}


static CassError connect_session(CassSession* session, const CassCluster* cluster)
{
    CassError rc = CASS_OK;
    CassFuture* future = cass_session_connect(session, cluster);

    cass_future_wait(future);
    rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        print_error(future);
    }
    cass_future_free(future);

    return rc;
}


static void *cass_open_conn(const DBConf *db_conf)
{
    CassInstance *i;
    CassConf *conf = db_conf->cass; /* make compiler happy */

    /* sanity check */
    if (conf == NULL)
        return NULL;

    /* set logging callback */
    cass_log_set_level(CASS_LOG_INFO);
    cass_log_set_callback(cass_log, NULL);

    /* create instance */
    i = gw_malloc(sizeof(CassInstance));
    i->cluster = cass_cluster_new();
    cass_cluster_set_contact_points(i->cluster, octstr_get_cstr(conf->host));

    /* set authentication credentials */
    cass_cluster_set_credentials(i->cluster,
            octstr_get_cstr(conf->username), octstr_get_cstr(conf->password));

    /* change idle timeout (default 60s) */
    cass_cluster_set_connection_idle_timeout(i->cluster, conf->idle_timeout);

    i->session = cass_session_new();

    if (connect_session(i->session, i->cluster) != CASS_OK) {
       error(0, "Cassandra: Can not connect to cluster!");
       goto failed;
    }

    info(0, "Cassandra: Connected to cluster <%s>",
         octstr_get_cstr(conf->host));

    return i;

failed:
    cass_cluster_free(i->cluster);
    cass_session_free(i->session);
    gw_free(i);

    return NULL;
}


static void cass_close_conn(void *conn)
{
    CassInstance *i = (CassInstance*) conn;
    CassFuture *close_future = NULL;

    if (conn == NULL)
        return;

    close_future = cass_session_close(i->session);
    cass_future_wait(close_future);
    cass_future_free(close_future);

    cass_cluster_free(i->cluster);
    cass_session_free(i->session);
    gw_free(i);
}


static int cass_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;

    return 0;
}


static int cass_select(void *conn, const Octstr *sql, List *binds, List **res)
{
    CassInstance *instance = conn;
    CassError rc = CASS_OK;
    CassStatement *statement = NULL;
    CassFuture *future = NULL;
    long i, binds_len;
    List *res_row;

    binds_len = gwlist_len(binds);

    /* allocate statement handle */
    statement = cass_statement_new(octstr_get_cstr(sql), binds_len);

    /* bind parameters if any */
    if (binds_len > 0) {
        for (i = 0; i < binds_len; i++) {
            cass_statement_bind_string(statement, i, octstr_get_cstr(gwlist_get(binds, i)));
        }
    }

    /* execute statement */
    future = cass_session_execute(instance->session, statement);
    cass_future_wait(future);

    /* evaluate result */
    rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        print_error(future);
        cass_future_free(future);
        cass_statement_free(statement);
        return -1;
    } else {
        const CassResult *result = cass_future_get_result(future);
        CassIterator *iterator_row = cass_iterator_from_result(result);

        *res = gwlist_create();

        /* iterate through all rows */
        while (cass_iterator_next(iterator_row)) {
            const CassRow *row = cass_iterator_get_row(iterator_row);
            CassIterator *iterator_column = cass_iterator_from_row(row);

            res_row = gwlist_create();

            /* iterate through all columns (of one row) */
            while (cass_iterator_next(iterator_column)) {
                const CassValue *value;
                CassValueType type;
                cass_int32_t i;
                cass_int64_t bi;
                cass_bool_t b;
                cass_double_t d;
                cass_float_t f;
                const char* s;
                size_t s_length;
                CassUuid u;
                char us[CASS_UUID_STRING_LENGTH];

                value = cass_iterator_get_column(iterator_column);
                type  = cass_value_type(value);

                /* determine value type and convert */
                switch (type) {
                    case CASS_VALUE_TYPE_INT:
                        cass_value_get_int32(value, &i);
                        gwlist_append(res_row, octstr_format("%d", i));
                        break;

                    case CASS_VALUE_TYPE_BIGINT:
                        cass_value_get_int64(value, &bi);
                        gwlist_append(res_row, octstr_format("%ld", bi));
                        break;

                    case CASS_VALUE_TYPE_BOOLEAN:
                        cass_value_get_bool(value, &b);
                        gwlist_append(res_row, (b ? octstr_imm("true") : octstr_imm("false")));
                        break;

                    case CASS_VALUE_TYPE_DOUBLE:
                        cass_value_get_double(value, &d);
                        gwlist_append(res_row, octstr_format("%g", d));
                        break;

                    case CASS_VALUE_TYPE_FLOAT:
                         cass_value_get_float(value, &f);
                         gwlist_append(res_row, octstr_format("%f", f));
                         break;

                    case CASS_VALUE_TYPE_TEXT:
                    case CASS_VALUE_TYPE_ASCII:
                    case CASS_VALUE_TYPE_VARCHAR:
                        cass_value_get_string(value, &s, &s_length);
                        gwlist_append(res_row, octstr_create_from_data(s, s_length));
                        break;

                    case CASS_VALUE_TYPE_UUID:
                        cass_value_get_uuid(value, &u);
                        cass_uuid_string(u, us);
                        gwlist_append(res_row, octstr_create(us));
                        break;

                    default:
                        if (cass_value_is_null(value)) {
                            gwlist_append(res_row, octstr_imm("_NULL_"));
                        } else {
                            error(0, "Cassandra: %s: unhandled type %d", __func__, type);
                        }
                        break;
                }

            }
            gwlist_append(*res, res_row);
            cass_iterator_free(iterator_column);

        }

        cass_result_free(result);
        cass_iterator_free(iterator_row);
    }

    cass_future_free(future);
    cass_statement_free(statement);

    return 0;
}


static int cass_update(void *conn, const Octstr *sql, List *binds)
{
    CassInstance *instance = conn;
    CassError rc = CASS_OK;
    CassStatement *statement = NULL;
    CassFuture *future = NULL;
    long i, binds_len;

    binds_len = gwlist_len(binds);

    /* allocate statement handle */
    statement = cass_statement_new(octstr_get_cstr(sql), binds_len);

    /* bind parameters if any */
    if (binds_len > 0) {
        for (i = 0; i < binds_len; i++) {
            cass_statement_bind_string(statement, i, octstr_get_cstr(gwlist_get(binds, i)));
        }
    }

    /* execute statement */
    future = cass_session_execute(instance->session, statement);
    cass_future_wait(future);

    /* evaluate result */
    rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        print_error(future);
        cass_future_free(future);
        cass_statement_free(statement);
        return -1;
    }

    cass_future_free(future);
    cass_statement_free(statement);

    return 0;
}


static void cass_conf_destroy(DBConf *db_conf)
{
    CassConf *conf = db_conf->cass;

    octstr_destroy(conf->host);
    octstr_destroy(conf->username);
    octstr_destroy(conf->password);
    octstr_destroy(conf->database);

    gw_free(conf);
    gw_free(db_conf);
}


static struct db_ops cass_ops = {
    .open = cass_open_conn,
    .close = cass_close_conn,
    .check = cass_check_conn,
    .select = cass_select,
    .update = cass_update,
    .conf_destroy = cass_conf_destroy
};

#endif /* HAVE_CASS */
