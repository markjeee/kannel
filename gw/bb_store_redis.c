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

/**
 * bb_store_redis.c - bearerbox box SMS storage/retrieval module using a redis database
 *
 * Author: Alejandro Guerrieri, 2015
 * Adds: Stipe Tolj, 2015
 */

#include "gw-config.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include "gwlib/gwlib.h"
#include "msg.h"
#include "sms.h"
#include "bearerbox.h"
#include "bb_store.h"

#ifdef HAVE_REDIS
#include "gwlib/dbpool.h"

/*
 * Define REDIS_TRACE to get DEBUG level output of the
 * Redis commands send to the server.
 */
/* #define REDIS_TRACE 1 */

static Counter *counter;
static List *loaded;

static DBPool *pool = NULL;

struct store_db_fields {
    Octstr *table;
    Octstr *field_uuid;
    Octstr *field_message;
};

static struct store_db_fields *fields = NULL;

static int hash = 0;


/*
 * Convert a Msg structure to a Dict hash.
 * This will assume we handle the msg->sms type only.
 */
/*
static Dict *hash_msg_pack(Msg *msg)
{
    Dict *h;

    gw_assert(msg->type == sms);

    h = dict_create(32, octstr_destroy_item);

#define INTEGER(name) dict_put(h, octstr_imm(#name), octstr_format("%ld", p->name));
#define OCTSTR(name) dict_put(h, octstr_imm(#name), octstr_duplicate(p->name));
#define UUID(name) { \
        char id[UUID_STR_LEN + 1]; \
        uuid_unparse(p->name, id); \
        dict_put(h, octstr_imm(#name), octstr_create(id)); \
    }
#define VOID(name)
#define MSG(type, stmt) \
    case type: { struct type *p = &msg->type; stmt } break;

    switch (msg->type) {
#include "msg-decl.h"
    default:
        panic(0, "Internal error: unknown message type: %d",
              msg->type);
    }

    return h;
}
*/


static Msg *hash_msg_unpack(Dict *hash)
{
    Msg *msg;
    Octstr *os;

    if (hash == NULL)
        return NULL;

    msg = msg_create(sms);
#define INTEGER(name) \
    if ((os = dict_get(hash, octstr_imm(#name))) != NULL) \
        p->name = atol(octstr_get_cstr(os));
#define OCTSTR(name) p->name = octstr_duplicate(dict_get(hash, octstr_imm(#name)));
#define UUID(name) \
    if ((os = dict_get(hash, octstr_imm(#name))) != NULL) \
        uuid_parse(octstr_get_cstr(os), p->name);
#define VOID(name)
#define MSG(type, stmt) \
    case type: { struct type *p = &msg->type; stmt } break;

    switch (msg->type) {
#include "msg-decl.h"
    default:
        panic(0, "Internal error: unknown message type: %d",
              msg->type);
    }

    return msg;
}


static int store_redis_dump()
{
    /* nothing to do */
    return 0;
}


static long store_redis_messages()
{
    return counter ? counter_value(counter) : -1;
}


static void redis_update(const Octstr *cmd, List *binds)
{
    int	res;
    DBPoolConn *pc;

#if defined(REDIS_TRACE)
     debug("store.redis", 0, "redis cmd: %s", octstr_get_cstr(cmd));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "Database pool got no connection! Redis update failed!");
        return;
    }

	res = dbpool_conn_update(pc, cmd, binds);
 
    if (res < 0) {
        error(0, "Store-Redis: Error while updating: command was `%s'",
              octstr_get_cstr(cmd));
    }

    dbpool_conn_produce(pc);
}


static void store_redis_add(Octstr *id, Octstr *os)
{
    Octstr *cmd;

    octstr_binary_to_base64(os);
    cmd = octstr_format("HSET %s %s %s",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(id), octstr_get_cstr(os));
    redis_update(cmd, NULL);

    octstr_destroy(cmd);
}


/*
static void store_redis_add_hash(Octstr *id, Dict *hash)
{
    List *l, *b;
    Octstr *cmd, *key, *val;

    cmd = octstr_create("");
    b = gwlist_create();
    gwlist_produce(b, octstr_create("HMSET"));
    gwlist_produce(b, octstr_duplicate(id));
    l = dict_keys(hash);
    while ((key = gwlist_extract_first(l)) != NULL) {
        if ((val = dict_get(hash, key)) != NULL) {
            gwlist_produce(b, key);
            gwlist_produce(b, octstr_duplicate(val));
        }
    }
    gwlist_destroy(l, NULL);

    redis_update(cmd, b);

    gwlist_destroy(b, octstr_destroy_item);
    octstr_destroy(cmd);
}
*/


/*
 * In order to a) speed-up the processing of the bind list in the dbpool_redis.c
 * module and b) safe space in the redis-server memory, we will only store
 * values that are set.
 */
static void store_redis_add_msg(Octstr *id, Msg *msg)
{
    List *b;
    Octstr *cmd;
    char uuid[UUID_STR_LEN + 1];

    cmd = octstr_create("");
    b = gwlist_create();
    gwlist_produce(b, octstr_create("HMSET"));
    gwlist_produce(b, octstr_duplicate(id));

#define INTEGER(name) \
    if (p->name != MSG_PARAM_UNDEFINED) { \
        gwlist_produce(b, octstr_imm(#name)); \
        gwlist_produce(b, octstr_format("%ld", p->name)); \
    }
#define OCTSTR(name) \
    if (p->name != NULL) { \
        gwlist_produce(b, octstr_imm(#name)); \
        gwlist_produce(b, octstr_duplicate(p->name)); \
    }
#define UUID(name) \
    gwlist_produce(b, octstr_imm(#name)); \
    uuid_unparse(p->name, uuid); \
    gwlist_produce(b, octstr_create(uuid));
#define VOID(name)
#define MSG(type, stmt) \
    case type: { struct type *p = &msg->type; stmt } break;

    switch (msg->type) {
#include "msg-decl.h"
    default:
        panic(0, "Internal error: unknown message type: %d",
              msg->type);
        break;
    }

    redis_update(cmd, b);

    gwlist_destroy(b, octstr_destroy_item);
    octstr_destroy(cmd);
}


static void store_redis_delete(Octstr *id)
{
	Octstr *cmd;

	cmd = octstr_format("HDEL %s %s",
                        octstr_get_cstr(fields->table),
                        octstr_get_cstr(id));
    redis_update(cmd, NULL);

    octstr_destroy(cmd);
}


static void store_redis_delete_hash(Octstr *id)
{
    Octstr *cmd;

    cmd = octstr_format("DEL %s", octstr_get_cstr(id));
    redis_update(cmd, NULL);

    octstr_destroy(cmd);
}


static struct store_db_fields *store_db_fields_create(CfgGroup *grp)
{
    struct store_db_fields *ret;

    ret = gw_malloc(sizeof(*ret));
    gw_assert(ret != NULL);
    memset(ret, 0, sizeof(*ret));

    if ((ret->table = cfg_get(grp, octstr_imm("table"))) == NULL) {
        grp_dump(grp);
        panic(0, "Directive 'table' is not specified in 'group = store-db' context!");
    }

    return ret;
}


static void store_db_fields_destroy(struct store_db_fields *fields)
{
    /* sanity check */
    if (fields == NULL)
        return;

    octstr_destroy(fields->table);
    octstr_destroy(fields->field_uuid);
    octstr_destroy(fields->field_message);

    gw_free(fields);
}


static int store_redis_getall(int ignore_err, void(*cb)(Octstr*, void*), void *data)
{
    DBPoolConn *pc;
    Octstr *cmd;
    Octstr *os, *key;
    List *result, *row;

    cmd = octstr_format("HGETALL %s", octstr_get_cstr(fields->table));

#if defined(REDIS_TRACE)
    debug("store.redis", 0, "redis cmd: %s", octstr_get_cstr(cmd));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "Database pool got no connection! Redis HGETALL failed!");
        dbpool_conn_produce(pc);
        return -1;
    }
    if (dbpool_conn_select(pc, cmd, NULL, &result) != 0) {
        error(0, "Failed to fetch messages from redis with cmd `%s'",
              octstr_get_cstr(cmd));
        octstr_destroy(cmd);
        dbpool_conn_produce(pc);
        return -1;
    } 
	dbpool_conn_produce(pc);
    octstr_destroy(cmd);

    if (gwlist_len(result) == 1 && (row = gwlist_extract_first(result)) != NULL) {
        while (gwlist_len(row) > 0) {
            key = gwlist_extract_first(row);
            os = gwlist_extract_first(row);
            if (key && os) {
                debug("store.redis", 0, "Found entry for message ID <%s>", octstr_get_cstr(key));
                octstr_base64_to_binary(os);
                if (os == NULL) {
                    error(0, "Could not base64 decode message ID <%s>", octstr_get_cstr(key));
                } else {
                    cb(os, data);
                }
            }
            octstr_destroy(os);
            octstr_destroy(key);
        }
        gwlist_destroy(row, octstr_destroy_item);
    } else {
        debug("store.redis", 0, "No messages loaded from redis store");
    }
    gwlist_destroy(result, NULL);

    return 0;
}


static int store_redis_getall_hash(int ignore_err, void(*cb)(Dict*, void*), void *data)
{
    DBPoolConn *pc;
    Octstr *cmd;
    Octstr *os, *key, *id;
    List *result, *row, *result_key, *row_key;
    Dict *hash;

    cmd = octstr_create("KEYS *");

#if defined(REDIS_TRACE)
    debug("store.redis", 0, "redis cmd: %s", octstr_get_cstr(cmd));
#endif

    pc = dbpool_conn_consume(pool);
    if (pc == NULL) {
        error(0, "Database pool got no connection! Redis KEYS failed!");
        dbpool_conn_produce(pc);
        return -1;
    }
    if (dbpool_conn_select(pc, cmd, NULL, &result) != 0) {
        error(0, "Failed to fetch messages from redis with cmd `%s'",
              octstr_get_cstr(cmd));
        octstr_destroy(cmd);
        dbpool_conn_produce(pc);
        return -1;
    }
    octstr_destroy(cmd);

    if (gwlist_len(result) == 1 && ((row = gwlist_extract_first(result)) != NULL)) {
        while ((id = gwlist_extract_first(row)) != NULL) {
             cmd = octstr_format("HGETALL %s", octstr_get_cstr(id));
             if (dbpool_conn_select(pc, cmd, NULL, &result_key) != 0) {
                 error(0, "Failed to fetch messages from redis with cmd `%s'",
                         octstr_get_cstr(cmd));
                 octstr_destroy(cmd);
                 dbpool_conn_produce(pc);
                 octstr_destroy(id);
                 gwlist_destroy(result, octstr_destroy_item);
                 return -1;
             }
             octstr_destroy(cmd);

             if (gwlist_len(result_key) == 1 && ((row_key = gwlist_extract_first(result_key)) != NULL)) {
                 hash = dict_create(32, octstr_destroy_item);
                 while (gwlist_len(row_key) > 0) {
                     key = gwlist_extract_first(row_key);
                     os = gwlist_extract_first(row_key);
                     if (key && os) {
                         dict_put(hash, key, os);
                     }
                     octstr_destroy(key);
                 }
                 cb(hash, data);
                 dict_destroy(hash);
                 gwlist_destroy(row_key, octstr_destroy_item);
             }
             gwlist_destroy(result_key, NULL);
         }
         gwlist_destroy(row, octstr_destroy_item);
    } else {
        debug("store.redis", 0, "No messages loaded from redis store");
    }
    dbpool_conn_produce(pc);
    gwlist_destroy(result, NULL);

    return 0;
}


struct status {
    void(*callback_fn)(Msg* msg, void *data);
    void *data;
};


static void status_cb(Octstr *msg_s, void *d)
{
    struct status *data = d;
    Msg *msg;

    msg = store_msg_unpack(msg_s);
    if (msg == NULL)
        return;

    data->callback_fn(msg, data->data);

    msg_destroy(msg);
}


static void store_redis_for_each_message(void(*callback_fn)(Msg* msg, void *data), void *data)
{
    struct status d;

    if (pool == NULL)
        return;

    d.callback_fn = callback_fn;
    d.data = data;

    /* ignore error because files may disappear */
    store_redis_getall(1, status_cb, &d);
}


static void dispatch(Octstr *msg_s, void *data)
{
    Msg *msg;
    void (*receive_msg)(Msg*) = data;

    if (msg_s == NULL)
        return;

    msg = store_msg_unpack(msg_s);
    if (msg != NULL) {
        receive_msg(msg);
        counter_increase(counter);
    } else {
        error(0, "Could not unpack message from redis store!");
    }
}


static void dispatch_hash(Dict *msg_h, void *data)
{
    Msg *msg;
    void (*receive_msg)(Msg*) = data;

    if (msg_h == NULL)
        return;

    msg = hash_msg_unpack(msg_h);
    if (msg != NULL) {
        receive_msg(msg);
        counter_increase(counter);
    } else {
        error(0, "Could not unpack message hash from redis store!");
    }
}


static int store_redis_load(void(*receive_msg)(Msg*))
{
    int rc;

    /* check if we are active */
    if (pool == NULL)
        return 0;
        
    /* sanity check */
    if (receive_msg == NULL)
        return -1;

    /*
     * We will use a Dict as an intermediate data structure to re-construct the
     * Msg struct itself. This is faster, then using pre-processor magic and
     * then strcmp() on the msg field names.
     */
    rc = hash ? store_redis_getall_hash(0, dispatch_hash, receive_msg) :
            store_redis_getall(0, dispatch, receive_msg);

    info(0, "Loaded %ld messages from store.", counter_value(counter));

    /* allow using of storage */
    gwlist_remove_producer(loaded);

    return rc;
}


static int store_redis_save(Msg *msg)
{
    char id[UUID_STR_LEN + 1];
    Octstr *id_s;

    /* always set msg id and timestamp */
    if (msg_type(msg) == sms && uuid_is_null(msg->sms.id))
        uuid_generate(msg->sms.id);

    if (msg_type(msg) == sms && msg->sms.time == MSG_PARAM_UNDEFINED)
        time(&msg->sms.time);

    if (pool == NULL)
        return 0;

    /* block here if store still not loaded */
    gwlist_consume(loaded);

    switch (msg_type(msg)) {
        case sms:
        {
            uuid_unparse(msg->sms.id, id);
            id_s = octstr_create(id);

            /* XXX we could use function pointers to avoid iteration checks */
            if (hash) {
                store_redis_add_msg(id_s, msg);
            } else {
                Octstr *os = store_msg_pack(msg);

                if (os == NULL) {
                    error(0, "Could not pack message.");
                    return -1;
                }
                store_redis_add(id_s, os);
                octstr_destroy(os);
            }
            octstr_destroy(id_s);
            counter_increase(counter);
            break;
        }
        case ack:
        {
            uuid_unparse(msg->ack.id, id);
            id_s = octstr_create(id);
            if (hash)
                store_redis_delete_hash(id_s);
            else
                store_redis_delete(id_s);
            octstr_destroy(id_s);
            counter_decrease(counter);
            break;
        }
        default:
            return -1;
    }

    return 0;
}


static int store_redis_save_ack(Msg *msg, ack_status_t status)
{
    int ret;
    Msg *nack = msg_create(ack);

    nack->ack.nack = status;
    uuid_copy(nack->ack.id, msg->sms.id);
    nack->ack.time = msg->sms.time;
    ret = store_redis_save(nack);
    msg_destroy(nack);

    return ret;
}


static void store_redis_shutdown()
{
    dbpool_destroy(pool);
    store_db_fields_destroy(fields);
        
    counter_destroy(counter);
    gwlist_destroy(loaded, NULL);
}


int store_redis_init(Cfg *cfg)
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
    if (!(grp = cfg_get_single_group(cfg, octstr_imm("store-db"))))
        panic(0, "Store-Redis: group 'store-db' is not specified!");

    if (!(redis_id = cfg_get(grp, octstr_imm("id"))))
        panic(0, "Store-Redis: directive 'id' is not specified!");

    cfg_get_bool(&hash, grp, octstr_imm("hash"));
    
    fields = store_db_fields_create(grp);
    gw_assert(fields != NULL);
    
    /* select corresponding functions */
    store_messages = store_redis_messages;
    store_save = store_redis_save;
    store_save_ack = store_redis_save_ack;
    store_load = store_redis_load;
    store_dump = store_redis_dump;
    store_shutdown = store_redis_shutdown;
    store_for_each_message = store_redis_for_each_message;

    /*
     * Now grab the required information from the 'redis-connection' group
     * with the id we just obtained.
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
    panic(0, "Connection settings for 'redis-connection' with id '%s' are not specified!",
          octstr_get_cstr(redis_id));

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(redis_host = cfg_get(grp, octstr_imm("host")))) {
        grp_dump(grp);
        panic(0, "Directive 'host' is not specified in 'group = redis-connection' context!");
    }
    if (cfg_get_integer(&redis_port, grp, octstr_imm("port")) == -1) {
        grp_dump(grp);
        panic(0, "Directive 'port' is not specified in 'group = redis-connection' context!");
    }
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
        panic(0, "Redis database pool has no connections!");

    loaded = gwlist_create();
    gwlist_add_producer(loaded);
    counter = counter_create();

    octstr_destroy(redis_id);

    return 0;
}

#endif
