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
 * dlr_redis.c
 *
 * Implementation of handling delivery reports (DLRs)
 * for the Redis keystore
 *
 * Toby Phipps <toby.phipps at nexmedia.com.sg>, 2011-08-23
 * Stipe Tolj <stolj at kannel.org>, 2013-12-12
 */

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "dlr_p.h"

#ifdef HAVE_REDIS

/*
 * Some SMSCs (such as the Logica SMPP simulator when bound multiple times
 * under high load) erroneously return duplicate message IDs. Before writing
 * the DLR, check to ensure that an existing DLR with the same message ID
 * doesn't already exist (HMSET of an existing key overwrites it silently
 * and we want the first message to win, not the erroneous one).
 * We issue a HSETNX first to ensure that the key doesn't already exist.
 * Only if it succeeds do we proceed to update it with full DLR info.
 * Define the following macro in case you want this extra handling.
 */
/* #define REDIS_PRECHECK 1 */

/*
 * Our connection pool to redis.
 */
static DBPool *pool = NULL;

/*
 * Database-centric DLR definition (common across all engines)
 */
static struct dlr_db_fields *fields = NULL;

static void dlr_redis_shutdown()
{
    dbpool_destroy(pool);
    dlr_db_fields_destroy(fields);
}

static void dlr_redis_add(struct dlr_entry *entry)
{
    Octstr *key, *sql, *os_mask;
    Octstr *dstclean, *srcclean, *tsclean;
    DBPoolConn *pconn;
    List *binds;
    int res, len;

    debug("dlr.redis", 0, "Adding DLR into keystore");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        error(0, "DLR: REDIS: No connection available - dropping DLR");
        dlr_entry_destroy(entry);
        return;
    }

    /*
     * This code is needed as we are mis-using the Hiredis API and
     * passing a fully-formed Redis command rather than tokenized
     * command components. Redis treats a space are a command delimiter
     * so any component that can include a space needs to be quoted.
     * Should be re-written to use the RedisCommandArgv() API.
     */

    dstclean = octstr_duplicate(entry->destination);
    octstr_replace(dstclean, octstr_imm(" "), octstr_imm("__space__"));

    srcclean = octstr_duplicate(entry->source);
    octstr_replace(srcclean, octstr_imm(" "), octstr_imm("__space__"));

    tsclean = octstr_duplicate(entry->timestamp);
    octstr_replace(tsclean, octstr_imm(" "), octstr_imm("__space__"));

    if (entry->use_dst && entry->destination) {
        Octstr *dst_min;

        /* keep a shorten version for the key part */
        dst_min = octstr_duplicate(dstclean);
        len = octstr_len(dst_min);
        if (len > MIN_DST_LEN)
            octstr_delete(dst_min, 0, len - MIN_DST_LEN);

        key = octstr_format("%S:%S:%S:%S", fields->table,
                entry->smsc,
                tsclean,
                dst_min);

        octstr_destroy(dst_min);
    } else {
        key = octstr_format("%S:%S:%S", fields->table,
                entry->smsc,
                tsclean);
    }

#ifdef REDIS_PRECHECK
    binds = gwlist_create();
    sql = octstr_format("HSETNX %S %S ?", key, fields->field_smsc);
    if (dbpool_conn_update(pconn, sql, binds) != 1) {
        error(0, "DLR: REDIS: DLR for %s already exists! Duplicate Message ID?",
              octstr_get_cstr(key));

        octstr_destroy(sql);
        octstr_destroy(key);
        octstr_destroy(tsclean);
        octstr_destroy(dstclean);
        octstr_destroy(srcclean);
        gwlist_destroy(binds, NULL);
        dbpool_conn_produce(pconn);
        return;
    }
    octstr_destroy(sql);
    gwlist_destroy(binds, NULL);
#endif

    binds = gwlist_create();
    sql = octstr_format("HMSET %S %S ? %S ? %S ? %S ? %S ? %S ? %S ? %S ? %S 0",
            key,
            fields->field_smsc,
            fields->field_ts,
            fields->field_src,
            fields->field_dst,
            fields->field_serv,
            fields->field_url,
            fields->field_mask,
            fields->field_boxc,
            fields->field_status);

    /* prepare values */
    if (entry->url) {
        octstr_url_encode(entry->url);
        octstr_replace(entry->url, octstr_imm("%"), octstr_imm("%%"));
    }
    os_mask = octstr_format("%d", entry->mask);

    gwlist_append(binds, entry->smsc);
    gwlist_append(binds, tsclean);
    gwlist_append(binds, srcclean);
    gwlist_append(binds, dstclean);
    gwlist_append(binds, entry->service);
    gwlist_append(binds, entry->url);
    gwlist_append(binds, os_mask);
    gwlist_append(binds, entry->boxc_id);

    res = dbpool_conn_update(pconn, sql, binds);

    if (res == -1) {
        error(0, "DLR: REDIS: Error while adding dlr entry %s:%s:%s:%s",
              octstr_get_cstr(fields->table),
              octstr_get_cstr(entry->smsc),
              octstr_get_cstr(tsclean),
              octstr_get_cstr(dstclean));
    }
    else  {
        /* HMSET returned OK. Set EXPIRE if applicable and then
         * increment the DLR counter */
        if (fields->ttl) {
            octstr_destroy(sql);
            sql = octstr_format("EXPIRE %S %ld", key, fields->ttl);
            res = dbpool_conn_update(pconn, sql, NULL);
        }
        /* We are not performing an 'INCR <table>:Count'
         * operation here, since we can't be accurate due
         * to TTL'ed expiration. Rather use 'DBSIZE' based
         * on seperated databases in redis. */
    }

    dbpool_conn_produce(pconn);
    octstr_destroy(os_mask);
    octstr_destroy(sql);
    octstr_destroy(key);
    octstr_destroy(tsclean);
    octstr_destroy(dstclean);
    octstr_destroy(srcclean);
    gwlist_destroy(binds, NULL);
    dlr_entry_destroy(entry);
}

static inline void get_octstr_value(Octstr **os, const List *r, const int i)
{
    *os = octstr_duplicate(gwlist_get((List*)r, i));
    if (octstr_str_compare(*os, "_NULL_") == 0) {
        octstr_destroy(*os);
        *os = NULL;
    }
}

static struct dlr_entry *dlr_redis_get(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *key, *sql;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    List *result = NULL, *row;
    struct dlr_entry *res = NULL;

    pconn = dbpool_conn_consume(pool);
    if (pconn == NULL) {
        error(0, "DLR: REDIS: No connection available");
        gwlist_destroy(binds, NULL);
        dbpool_conn_produce(pconn);
        return NULL;
    }

    /* If the destination address is not NULL, then
     * it has been shortened by the abstractive layer. */
    key = octstr_format((dst ? "%S:?:?:?" : "%S:?:?"), fields->table);

    sql = octstr_format("HMGET %S ? ? ? ? ? ?", key);
    gwlist_append(binds, (Octstr *)smsc); /* key */
    gwlist_append(binds, (Octstr *)ts); /* key */
    if (dst)
        gwlist_append(binds, (Octstr *)dst); /* key */
    gwlist_append(binds, fields->field_mask);
    gwlist_append(binds, fields->field_serv);
    gwlist_append(binds, fields->field_url);
    gwlist_append(binds, fields->field_src);
    gwlist_append(binds, fields->field_dst);
    gwlist_append(binds, fields->field_boxc);

    if (dbpool_conn_select(pconn, sql, binds, &result) != 0) {
        error(0, "DLR: REDIS: Failed to fetch DLR for %s", octstr_get_cstr(key));
        octstr_destroy(sql);
        octstr_destroy(key);
        gwlist_destroy(binds, NULL);
        dbpool_conn_produce(pconn);
        return NULL;
    }

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    octstr_destroy(key);
    gwlist_destroy(binds, NULL);

#define LO2CSTR(r, i) octstr_get_cstr(gwlist_get(r, i))

    if (gwlist_len(result) > 0) {
        row = gwlist_extract_first(result);

        /*
         * If we get an empty set back from redis, this is
         * still an array with "" values, representing (nil).
         * If the mask is empty then this can't be a valid
         * set, therefore bail out.
         */
        if (octstr_len(gwlist_get(row, 0)) > 0) {
            res = dlr_entry_create();
            gw_assert(res != NULL);
            res->mask = atoi(octstr_get_cstr(gwlist_get(row, 0)));
            get_octstr_value(&res->service, row, 1);
            get_octstr_value(&res->url, row, 2);
            octstr_url_decode(res->url);
            get_octstr_value(&res->source, row, 3);
            get_octstr_value(&res->destination, row, 4);
            get_octstr_value(&res->boxc_id, row, 5);
            res->smsc = octstr_duplicate(smsc);

            octstr_replace(res->source, octstr_imm("__space__"), octstr_imm(" "));
            octstr_replace(res->destination, octstr_imm("__space__"), octstr_imm(" "));
        }
        gwlist_destroy(row, octstr_destroy_item);
    }
    gwlist_destroy(result, NULL);

#undef LO2CSTR

    return res;
}

static void dlr_redis_remove(const Octstr *smsc, const Octstr *ts, const Octstr *dst)
{
    Octstr *key, *sql;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.redis", 0, "Removing DLR from keystore");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        error(0, "DLR: REDIS: No connection available");
        gwlist_destroy(binds, NULL);
        return;
    }

    /*
    octstr_replace(dst, octstr_imm(" "), octstr_imm(""));
    octstr_replace(ts, octstr_imm(" "), octstr_imm(""));
    */

    key = octstr_format((dst ? "%S:?:?:?" : "%S:?:?"), fields->table);

    sql = octstr_format("DEL %S", key, fields->table);
    gwlist_append(binds, (Octstr *)smsc); /* key */
    gwlist_append(binds, (Octstr *)ts); /* key */
    if (dst)
        gwlist_append(binds, (Octstr *)dst); /* key */

    res = dbpool_conn_update(pconn, sql, binds);
 
    /*
     * Redis DEL returns the number of keys deleted
     */ 
    if (res != 1) {
        /* 
         * We may fail to delete a DLR that was successfully retrieved
         * just above due to race conditions when duplicate message IDs
         * are received. This happens frequently when testing via the
         * Logica SMPP emulator due to its duplicate message ID bugs.
         */
        error(0, "DLR: REDIS: Error while removing dlr entry for %s",
              octstr_get_cstr(key));
    }
    /* We don't perform 'DECR <table>:Count', since we have TTL'ed
     * expirations, which can't be handled with manual counters. */

    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
    octstr_destroy(key);
    gwlist_destroy(binds, NULL);
}

static void dlr_redis_update(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int status)
{
    Octstr *key, *sql, *os_status;
    DBPoolConn *pconn;
    List *binds = gwlist_create();
    int res;

    debug("dlr.redis", 0, "Updating DLR status in keystore");

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        error(0, "DLR: REDIS: No connection available");
        gwlist_destroy(binds, NULL);
        return;
    }

    os_status = octstr_format("%d", status);

    key = octstr_format((dst ? "%S:?:?:?" : "%S:?:?"), fields->table);

    sql = octstr_format("HSET %S %S ?", key, fields->field_status);
    gwlist_append(binds, (Octstr*)smsc);
    gwlist_append(binds, (Octstr*)ts);
    if (dst != NULL)
        gwlist_append(binds, (Octstr*)dst);
    gwlist_append(binds, os_status);

    if ((res = dbpool_conn_update(pconn, sql, binds)) == -1) {
        error(0, "DLR: REDIS: Error while updating dlr entry for %s",
              octstr_get_cstr(key));
    }
    else if (!res) {
        warning(0, "DLR: REDIS: No dlr found to update for %s",
                octstr_get_cstr(key));
    }

    dbpool_conn_produce(pconn);
    octstr_destroy(os_status);
    octstr_destroy(key);
    octstr_destroy(sql);
    gwlist_destroy(binds, NULL);
}

static long dlr_redis_messages(void)
{
    List *result, *row;
    DBPoolConn *conn;
    long msgs = -1;

    conn = dbpool_conn_consume(pool);
    if (conn == NULL)
        return -1;

    if (dbpool_conn_select(conn, octstr_imm("DBSIZE"), NULL, &result) != 0) {
        dbpool_conn_produce(conn);
        return 0;
    }

    dbpool_conn_produce(conn);

    if (gwlist_len(result) > 0) {
        row = gwlist_extract_first(result);
        msgs = atol(octstr_get_cstr(gwlist_get(row, 0)));
        gwlist_destroy(row, octstr_destroy_item);

        while ((row = gwlist_extract_first(result)) != NULL)
            gwlist_destroy(row, octstr_destroy_item);
    }
    gwlist_destroy(result, NULL);

    return msgs;
}

static void dlr_redis_flush(void)
{
    Octstr *sql;
    DBPoolConn *pconn;
    int rows;

    pconn = dbpool_conn_consume(pool);
    /* just for sure */
    if (pconn == NULL) {
        error(0, "DLR: REDIS: No connection available");
        return;
    }

    sql = octstr_imm("FLUSHDB");
    rows = dbpool_conn_update(pconn, sql, NULL);
    if (rows == -1)
        error(0, "DLR: REDIS: Error while flushing dlr entries from database");
    else
        debug("dlr.redis", 0, "Flushed %d DLR entries from database", rows);
    dbpool_conn_produce(pconn);
    octstr_destroy(sql);
}

static struct dlr_storage handles = {
    .type = "redis",
    .dlr_add = dlr_redis_add,
    .dlr_get = dlr_redis_get,
    .dlr_update = dlr_redis_update,
    .dlr_remove = dlr_redis_remove,
    .dlr_shutdown = dlr_redis_shutdown,
    .dlr_messages = dlr_redis_messages,
    .dlr_flush = dlr_redis_flush
};

struct dlr_storage *dlr_init_redis(Cfg *cfg)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *redis_host, *redis_pass, *redis_id;
    long redis_port = 0, redis_database = -1, redis_idle_timeout = -1;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;

    /*
     * Check for all mandatory directives that specify the field names
     * of the used Redis key
     */
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("dlr-db"))))
        panic(0, "DLR: Redis: group 'dlr-db' is not specified!");

    if (!(redis_id = cfg_get(grp, octstr_imm("id"))))
        panic(0, "DLR: Redis: directive 'id' is not specified!");

    fields = dlr_db_fields_create(grp);
    gw_assert(fields != NULL);

    /*
     * Escaping special quotes for field/table names
     */
    octstr_replace(fields->table, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_smsc, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_ts, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_src, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_dst, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_serv, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_url, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_mask, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_status, octstr_imm("`"), octstr_imm("``"));
    octstr_replace(fields->field_boxc, octstr_imm("`"), octstr_imm("``"));

    /*
     * Now grab the required information from the 'redis-connection' group
     * with the redis-id we just obtained.
     *
     * We have to loop through all available Redis connection definitions
     * and search for the one we are looking for.
     */
    grplist = cfg_get_multi_group(cfg, octstr_imm("redis-connection"));
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, redis_id) == 0) {
            goto found;
        }
        if (p != NULL)
            octstr_destroy(p);
    }
    panic(0, "DLR: Redis: connection settings for id '%s' are not specified!",
          octstr_get_cstr(redis_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(redis_host = cfg_get(grp, octstr_imm("host"))))
   	    panic(0, "DLR: Redis: directive 'host' is not specified!");
    if (cfg_get_integer(&redis_port, grp, octstr_imm("port")) == -1)
   	    panic(0, "DLR: Redis: directive 'port' is not specified!");
    redis_pass = cfg_get(grp, octstr_imm("password"));
    cfg_get_integer(&redis_database, grp, octstr_imm("database"));
    cfg_get_integer(&redis_idle_timeout, grp, octstr_imm("idle-timeout"));

    /*
     * Ok, ready to connect to Redis
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->redis = gw_malloc(sizeof(RedisConf));
    gw_assert(db_conf->redis != NULL);

    db_conf->redis->host = redis_host;
    db_conf->redis->port = redis_port;
    db_conf->redis->password = redis_pass;
    db_conf->redis->database = redis_database;
    db_conf->redis->idle_timeout = redis_idle_timeout;

    pool = dbpool_create(DBPOOL_REDIS, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * Panic on failure to connect. Should we just try to reconnect?
     */
    if (dbpool_conn_count(pool) == 0)
        panic(0,"DLR: Redis: database pool has no connections!");

    octstr_destroy(redis_id);

    return &handles;
}
#else
/*
 * Return NULL, so we point dlr-core that we were
 * not compiled in.
 */
struct dlr_storage *dlr_init_redis(Cfg* cfg)
{
    return NULL;
}
#endif /* HAVE_REDIS */
