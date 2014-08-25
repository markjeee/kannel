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
 * dbpool.h - database pool functions
 *
 * Stipe Tolj <stolj@wapme-group.de>
 * Alexander Malysh <a.malysh@centrium.de>
 */

#ifndef GWDBPOOL_H
#define GWDBPOOL_H

#if defined(HAVE_MYSQL) || defined(HAVE_SDB) || \
    defined(HAVE_ORACLE) || defined(HAVE_SQLITE) || \
    defined(HAVE_PGSQL) || defined(HAVE_SQLITE3) || \
    defined(HAVE_MSSQL) || defined(HAVE_REDIS)
#define HAVE_DBPOOL 1
#endif

/* supported databases for connection pools */
enum db_type {
    DBPOOL_MYSQL, DBPOOL_SDB, DBPOOL_ORACLE, DBPOOL_SQLITE, DBPOOL_PGSQL,
    DBPOOL_SQLITE3, DBPOOL_MSSQL, DBPOOL_REDIS
};


/*
 * The DBPool type. It is opaque: do not touch it except via the functions
 * defined in this header.
 */
typedef struct DBPool DBPool;

/*
 * The DBPoolConn type. It stores the abtracted pointer to a database 
 * specific connection and the pool pointer itself to allow easy
 * re-storage into the pool (also disallowing to insert the conn into an
 * other pool).
 */
 typedef struct {
    void *conn; /* the pointer holding the database specific connection */
    DBPool *pool; /* pointer of the pool where this connection belongs to */
}  DBPoolConn;

typedef struct {
    Octstr *host;
    long port;
    Octstr *username;
    Octstr *password;
    Octstr *database;
} MySQLConf;

/*
 * TODO Think how to get rid of it and have generic Conf struct
 */
typedef struct {
    Octstr *username;
    Octstr *password;
    Octstr *server;
    Octstr *database;
} MSSQLConf;

typedef struct {
    Octstr *username;
    Octstr *password;
    Octstr *tnsname;
} OracleConf;

typedef struct {
    Octstr *url;
} SDBConf;

typedef struct {
    Octstr *file;
    int lock_timeout;
} SQLiteConf;

typedef struct {
    Octstr *file;
    int lock_timeout;
} SQLite3Conf;

typedef struct {
    Octstr *host;
    long port;
    Octstr *username;
    Octstr *password;
    Octstr *database;
    Octstr *options;    /* yet not used */
    Octstr *tty;        /* yet not used */
} PgSQLConf;

typedef struct {
    Octstr *host;
    long port;
    Octstr *password;
    long database;
    long idle_timeout;
} RedisConf;

typedef union {
    MSSQLConf *mssql;
    MySQLConf *mysql;
    SDBConf *sdb;
    OracleConf *oracle;
    SQLiteConf *sqlite;
    SQLite3Conf *sqlite3;
    PgSQLConf *pgsql;
    RedisConf *redis;
} DBConf;

/*
 * Create a database pool with #connections of connections. The pool
 * is stored within a queue list.
 * Returns a pointer to the pool object on success or NULL if the
 * creation fails.
 */
DBPool *dbpool_create(enum db_type db_type, DBConf *conf, unsigned int connections);

/*
 * Destroys the database pool. Includes also shutdowning all existing
 * connections within the pool queue.
 */
void dbpool_destroy(DBPool *p);

/*
 * Increase the connection size of the pool by #conn connections.
 * Beware that you can't increase a pool size to more then the initial
 * dbpool_create() call defined and opened the maximum pool connections.
 * Returns how many connections have been additionally created and
 * inserted to the pool.
 */
unsigned int dbpool_increase(DBPool *p, unsigned int conn);

/*
 * Decrease the connection size of the pool by #conn connections.
 * A pool size can only by reduced up to 0. So if the caller specifies
 * to close more connections then there are in the pool, all connections
 * are closed.
 * Returns how many connections have been shutdown and deleted from the
 * pool queue.
 */
unsigned int dbpool_decrease(DBPool *p, unsigned int conn);

/*
 * Return the number of connections that are currently queued in the pool.
 */
long dbpool_conn_count(DBPool *p);

/*
 * Gets and active connection from the pool and returns it.
 * The caller can use it then for queuery operations and has to put it
 * back into the pool via dbpool_conn_produce(conn).
 * If no connection is in pool and DBPool is not in destroying phase then
 * will block until connection is available otherwise returns NULL.
 */
DBPoolConn *dbpool_conn_consume(DBPool *p);

/*
 * Returns a used connection to the pool again.
 * The connection is returned to it's domestic pool for further extraction
 * using dbpool_conn_consume().
 */
void dbpool_conn_produce(DBPoolConn *conn);

int dbpool_conn_select(DBPoolConn *conn, const Octstr *sql, List *binds, List **result);
int dbpool_conn_update(DBPoolConn *conn, const Octstr *sql, List *binds);

/*
 * Perfoms a check of all connections within the pool and tries to
 * re-establish the same ammount of connections if there are broken
 * connections within the pool.
 * (This operation can only be performed if the database allows such
 * operations by its API.)
 * Returns how many connections within the pool have been checked and
 * are still considered active.
 */
unsigned int dbpool_check(DBPool *p);


#endif
