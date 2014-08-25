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
 * dbpool.c - implement generic database connection pool
 *
 * Stipe Tolj <stolj@wapme.de>
 *      2003 Initial version.
 * Alexander Malysh <a.malysh@centrium.de>
 *      2003 Made dbpool more generic.
 * Robert Ga³ach <robert.galach@my.tenbit.pl>
 *      2004 Added support for binding variables.
 * Alejandro Guerrieri <aguerrieri at kannel dot org>
 *      2009 Added support for MS-SQL using FreeTDS
 */

#include "gwlib.h"
#include "dbpool.h"
#include "dbpool_p.h"

#ifdef HAVE_DBPOOL

#include "dbpool_mysql.c"
#include "dbpool_oracle.c"
#include "dbpool_sqlite.c"
#include "dbpool_sqlite3.c"
#include "dbpool_sdb.c"
#include "dbpool_pgsql.c"
#include "dbpool_mssql.c"
#include "dbpool_redis.c"


static void dbpool_conn_destroy(DBPoolConn *conn)
{
    gw_assert(conn != NULL);

    if (conn->conn != NULL)
        conn->pool->db_ops->close(conn->conn);

    gw_free(conn);
}


/*************************************************************************
 * public functions
 */

DBPool *dbpool_create(enum db_type db_type, DBConf *conf, unsigned int connections)
{
    DBPool *p;

    if (conf == NULL)
        return NULL;

    p = gw_malloc(sizeof(DBPool));
    gw_assert(p != NULL);
    p->pool = gwlist_create();
    gwlist_add_producer(p->pool);
    p->max_size = connections;
    p->curr_size = 0;
    p->conf = conf;
    p->db_type = db_type;

    switch(db_type) {
#ifdef HAVE_MSSQL
        case DBPOOL_MSSQL:
            p->db_ops = &mssql_ops;
            break;
#endif
#ifdef HAVE_MYSQL
        case DBPOOL_MYSQL:
            p->db_ops = &mysql_ops;
            break;
#endif
#ifdef HAVE_ORACLE
        case DBPOOL_ORACLE:
            p->db_ops = &oracle_ops;
            break;
#endif
#ifdef HAVE_SQLITE
        case DBPOOL_SQLITE:
            p->db_ops = &sqlite_ops;
            break;
#endif
#ifdef HAVE_SQLITE3
        case DBPOOL_SQLITE3:
            p->db_ops = &sqlite3_ops;
            break;
#endif
#ifdef HAVE_SDB
        case DBPOOL_SDB:
            p->db_ops = &sdb_ops;
            break;
#endif
#ifdef HAVE_PGSQL
       case DBPOOL_PGSQL:
           p->db_ops = &pgsql_ops;
           break;
#endif
#ifdef HAVE_REDIS
       case DBPOOL_REDIS:
           p->db_ops = &redis_ops;
           break;
#endif
        default:
            panic(0, "Unknown dbpool type defined.");
    }

    /*
     * XXX what is todo here if not all connections
     * where established ???
     */
    dbpool_increase(p, connections);

    return p;
}


void dbpool_destroy(DBPool *p)
{

    if (p == NULL)
        return; /* nothing todo here */

    gw_assert(p->pool != NULL && p->db_ops != NULL);

    gwlist_remove_producer(p->pool);
    gwlist_destroy(p->pool, (void*) dbpool_conn_destroy);

    p->db_ops->conf_destroy(p->conf);
    gw_free(p);
}


unsigned int dbpool_increase(DBPool *p, unsigned int count)
{
    unsigned int i, opened = 0;

    gw_assert(p != NULL && p->conf != NULL && p->db_ops != NULL && p->db_ops->open != NULL);


    /* lock dbpool for updates */
    gwlist_lock(p->pool);

    /* ensure we don't increase more items than the max_size border */
    for (i=0; i < count && p->curr_size < p->max_size; i++) {
        void *conn = p->db_ops->open(p->conf);
        if (conn != NULL) {
            DBPoolConn *pc = gw_malloc(sizeof(DBPoolConn));
            gw_assert(pc != NULL);

            pc->conn = conn;
            pc->pool = p;

            p->curr_size++;
            opened++;
            gwlist_produce(p->pool, pc);
        }
    }

    /* unlock dbpool for updates */
    gwlist_unlock(p->pool);

    return opened;
}


unsigned int dbpool_decrease(DBPool *p, unsigned int c)
{
    unsigned int i;

    gw_assert(p != NULL && p->pool != NULL && p->db_ops != NULL && p->db_ops->close != NULL);

    /* lock dbpool for updates */
    gwlist_lock(p->pool);

    /*
     * Ensure we don't try to decrease more then available in pool.
     */
    for (i = 0; i < c; i++) {
        DBPoolConn *pc;

        /* gwlist_extract_first doesn't block even if no conn here */
        pc = gwlist_extract_first(p->pool);

        /* no conn availible anymore */
        if (pc == NULL)
            break;

        /* close connections and destroy pool connection */
        dbpool_conn_destroy(pc);
        p->curr_size--;
    }

    /* unlock dbpool for updates */
    gwlist_unlock(p->pool);

    return i;
}


long dbpool_conn_count(DBPool *p)
{
    gw_assert(p != NULL && p->pool != NULL);

    return gwlist_len(p->pool);
}


DBPoolConn *dbpool_conn_consume(DBPool *p)
{
    DBPoolConn *pc;

    gw_assert(p != NULL && p->pool != NULL);
    
    /* check for max connections and if 0 return NULL */
    if (p->max_size < 1)
        return NULL;

    /* check if we have any connection */
    while (p->curr_size < 1) {
        debug("dbpool", 0, "DBPool has no connections, reconnecting up to maximum...");
        /* dbpool_increase ensure max_size is not exceeded so don't lock */
        dbpool_increase(p, p->max_size - p->curr_size);
        if (p->curr_size < 1)
            gwthread_sleep(0.1);
    }

    /* garantee that you deliver a valid connection to the caller */
    while ((pc = gwlist_consume(p->pool)) != NULL) {

        /* 
         * XXX check that the connection is still existing.
         * Is this a performance bottle-neck?!
         */
        if (!pc->conn || (p->db_ops->check && p->db_ops->check(pc->conn) != 0)) {
            /* something was wrong, reinitialize the connection */
            /* lock dbpool for update */
            gwlist_lock(p->pool);
            dbpool_conn_destroy(pc);
            p->curr_size--;
            /* unlock dbpool for update */
            gwlist_unlock(p->pool);
            /*
             * maybe not needed, just try to get next connection, but it
             * can be dangeros if all connections where broken, then we will
             * block here for ever.
             */
            while (p->curr_size < 1) {
                debug("dbpool", 0, "DBPool has too few connections, reconnecting up to maximum...");
                /* dbpool_increase ensure max_size is not exceeded so don't lock */
                dbpool_increase(p, p->max_size - p->curr_size);
                if (p->curr_size < 1)
                    gwthread_sleep(0.1);
            }

        } else {
            break;
        }
    }

    return (pc->conn != NULL ? pc : NULL);
}


void dbpool_conn_produce(DBPoolConn *pc)
{
    gw_assert(pc != NULL && pc->conn != NULL && pc->pool != NULL && pc->pool->pool != NULL);

    gwlist_produce(pc->pool->pool, pc);
}


unsigned int dbpool_check(DBPool *p)
{
    long i, len, n = 0, reinit = 0;

    gw_assert(p != NULL && p->pool != NULL && p->db_ops != NULL);

    /*
     * First check if db_ops->check function pointer is here.
     * NOTE: db_ops->check is optional, so if it is not there, then
     * we have nothing todo and we simple return list length.
     */
    if (p->db_ops->check == NULL)
        return gwlist_len(p->pool);

    gwlist_lock(p->pool);
    len = gwlist_len(p->pool);
    for (i = 0; i < len; i++) {
        DBPoolConn *pconn;

        pconn = gwlist_get(p->pool, i);
        if (p->db_ops->check(pconn->conn) != 0) {
            /* something was wrong, reinitialize the connection */
            gwlist_delete(p->pool, i, 1);
            dbpool_conn_destroy(pconn);
            p->curr_size--;
            reinit++;
            len--;
            i--;
        } else {
            n++;
        }
    }
    gwlist_unlock(p->pool);

    /* reinitialize brocken connections */
    if (reinit > 0)
        n += dbpool_increase(p, reinit);


    return n;
}


int dbpool_conn_select(DBPoolConn *conn, const Octstr *sql, List *binds, List **result)
{
    if (sql == NULL || conn == NULL)
        return -1;

    if (conn->pool->db_ops->select == NULL)
        return -1; /* may be panic here ??? */

    return conn->pool->db_ops->select(conn->conn, sql, binds, result);
}


int dbpool_conn_update(DBPoolConn *conn, const Octstr *sql, List *binds)
{
    if (sql == NULL || conn == NULL)
        return -1;

    if (conn->pool->db_ops->update == NULL)
        return -1; /* may be panic here ??? */

    return conn->pool->db_ops->update(conn->conn, sql, binds);
}

#endif /* HAVE_DBPOOL */
