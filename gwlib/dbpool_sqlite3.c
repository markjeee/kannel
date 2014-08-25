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
 * dbpool_sqlite3.c - implement SQLite3 operations for generic database connection pool
 *
 * Stipe Tolj <st@tolj.org>
 * David Butler <gdb@dbSystems.com> - modeled select and update from dbpool_oracle.c 
 */

#ifdef HAVE_SQLITE3
#include <sqlite3.h>

static void *sqlite3_open_conn(const DBConf *db_conf)
{
    sqlite3 *db = NULL;
    SQLite3Conf *conf = db_conf->sqlite3; /* make compiler happy */

    /* sanity check */
    if (conf == NULL)
        return NULL;

    if (sqlite3_open(octstr_get_cstr(conf->file), &db) != SQLITE_OK) {
        error(0, "SQLite3: can not open or create database file `%s'!", 
              octstr_get_cstr(conf->file));
        error(0, "SQLite3: %s", sqlite3_errmsg(db));
        sqlite3_close(db);
        goto failed;
    }
    if (conf->lock_timeout > 0) {
    	info(0, "SQLite3: Setting lock timeout to %d", conf->lock_timeout);
    	sqlite3_busy_timeout(db, conf->lock_timeout);
    }

    info(0, "SQLite3: Opened or created database file `%s'.", octstr_get_cstr(conf->file));
    info(0, "SQLite3: library version %s.", sqlite3_version);

    return db;

failed:
    return NULL;
}


static void sqlite3_close_conn(void *conn)
{
    int rc;
    if (conn == NULL)
        return;

    /* in case we are busy, loop until we can close */
    do {
        rc = sqlite3_close((sqlite3*) conn);
    } while (rc == SQLITE_BUSY);
    
    if (rc == SQLITE_ERROR) {
        error(0, "SQLite3: error while closing database file.");
    }
}


static int sqlite3_check_conn(void *conn)
{
    if (conn == NULL)
        return -1;

    /* There is no such construct in SQLite3,
     * so return a valid connection indication */
    return 0;
}


static void sqlite3_conf_destroy(DBConf *db_conf)
{
    SQLite3Conf *conf = db_conf->sqlite3;

    octstr_destroy(conf->file);

    gw_free(conf);
    gw_free(db_conf);
}

static int sqlite3_select(void *theconn, const Octstr *sql, List *binds, List **res)
{
    sqlite3 *db = theconn;
    sqlite3_stmt *stmt;
    const char *rem;
    List *row;
    int status;
    int columns;
    int i;
    int binds_len = (binds ? gwlist_len(binds) : 0);

    *res = NULL;

    /* prepare statement */
#if SQLITE_VERSION_NUMBER >= 3003009    
    status = sqlite3_prepare_v2(db, octstr_get_cstr(sql), octstr_len(sql) + 1, &stmt, &rem);
#else    
    status = sqlite3_prepare(db, octstr_get_cstr(sql), octstr_len(sql) + 1, &stmt, &rem);
#endif
    if (SQLITE_OK != status) {
        error(0, "SQLite3: %s", sqlite3_errmsg(db));
        return -1;
    }

    /* bind variables */
    for (i = 0; i < binds_len; i++) {
        Octstr *bind = gwlist_get(binds, i);
        status = sqlite3_bind_text(stmt, i + 1, octstr_get_cstr(bind), octstr_len(bind), SQLITE_STATIC);
        if (SQLITE_OK != status) {
            error(0, "SQLite3: %s", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return -1;
        }
    }

    /* execute our statement */
    *res = gwlist_create();
    while ((status = sqlite3_step(stmt)) == SQLITE_ROW) {
        columns = sqlite3_data_count(stmt);
        debug("dbpool.sqlite3",0,"SQL has %d columns", columns);
        row = gwlist_create();
        for (i = 0; i < columns; i++) {
            if (sqlite3_column_type(stmt, i) == SQLITE_NULL) {
                gwlist_insert(row, i, octstr_create(""));
            } else {
                gwlist_insert(row, i, octstr_create(sqlite3_column_text(stmt, i)));
            }
            /* debug("dbpool.sqlite3",0,"inserted value = '%s'", 
                     octstr_get_cstr(gwlist_get(row,i))); */
        }
        gwlist_append(*res, row);
    }

    if (SQLITE_DONE != status) {
        error(0, "SQLite3: %s", sqlite3_errmsg(db));
        while ((row = gwlist_extract_first(*res)) != NULL)
            gwlist_destroy(row, octstr_destroy_item);
        gwlist_destroy(*res, NULL);
        *res = NULL;
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);

    return 0;
}


static int sqlite3_update(void *theconn, const Octstr *sql, List *binds)
{
    sqlite3 *db = theconn;
    sqlite3_stmt *stmt;
    const char *rem;
    int status;
    int rows;
    int i;
    int binds_len = (binds ? gwlist_len(binds) : 0);

    /* prepare statement */
#if SQLITE_VERSION_NUMBER >= 3003009    
    status = sqlite3_prepare_v2(db, octstr_get_cstr(sql), octstr_len(sql) + 1, &stmt, &rem);
#else
    status = sqlite3_prepare(db, octstr_get_cstr(sql), octstr_len(sql) + 1, &stmt, &rem);
#endif    
    if (SQLITE_OK != status) {
        error(0, "SQLite3: %s", sqlite3_errmsg(db));
        return -1;
    }
    debug("dbpool.sqlite3",0,"sqlite3_prepare done");

    /* bind variables */
    for (i = 0; i < binds_len; i++) {
        Octstr *bind = gwlist_get(binds, i);
        status = sqlite3_bind_text(stmt, i + 1, octstr_get_cstr(bind), octstr_len(bind), SQLITE_STATIC);
        if (SQLITE_OK != status) {
            error(0, "SQLite3: %s", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            return -1;
        }
    }

    /* execute our statement */
    if ((status = sqlite3_step(stmt)) != SQLITE_DONE) {
        error(0, "SQLite3: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    debug("dbpool.sqlite3",0,"sqlite3_step done");

    rows = sqlite3_changes(db);
    debug("dbpool.sqlite3",0,"rows processed = %d", rows);

    sqlite3_finalize(stmt);

    return rows;
}

static struct db_ops sqlite3_ops = {
    .open = sqlite3_open_conn,
    .close = sqlite3_close_conn,
    .check = sqlite3_check_conn,
    .conf_destroy = sqlite3_conf_destroy,
    .select = sqlite3_select,
    .update = sqlite3_update
};

#endif /* HAVE_SQLITE3 */

