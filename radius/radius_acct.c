/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2009 Kannel Group  
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
 * radius_acct.c - RADIUS accounting proxy thread
 *
 * Stipe Tolj <stolj@kannel.org>
 */

#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "gwlib/gwlib.h"
#include "radius/radius_acct.h"
#include "radius/radius_pdu.h"

static Dict *radius_table = NULL;      /* maps client ip -> msisdn */
static Dict *session_table = NULL;     /* maps session id -> client ip */
static Dict *client_table = NULL;      /* maps client ip -> session id */

/* we will initialize hash tables in the size of our NAS ports */
#define RADIUS_NAS_PORTS    30

static Mutex *radius_mutex = NULL;
static int run_thread = 0;

/*
 * Beware that the official UDP port for RADIUS accounting packets 
 * is 1813 (according to RFC2866). The previously used port 1646 has
 * been conflicting with an other protocol and "should" not be used.
 */
static Octstr *our_host = NULL;
static long our_port = 1813;
static Octstr *remote_host = NULL;
static long remote_port = 1813;

/* the shared secrets for NAS and remote RADIUS communication */
static Octstr *secret_nas = NULL;
static Octstr *secret_radius = NULL;

/* the global unified-prefix list */
static Octstr *unified_prefix = NULL;

/* timeout in msec for the remote RADIUS responses */
static long remote_timeout = 40000;

/*************************************************************************
 *
 */

/*
 * Updates the internal RADIUS mapping tables. Returns 1 if the 
 * mapping has been processes and the PDU should be proxied to the
 * remote RADIUS server, otherwise if it is a duplicate returns 0.
 */
static int update_tables(RADIUS_PDU *pdu)
{
    Octstr *client_ip, *msisdn;
    Octstr *type, *session_id;
    int ret = 0;
    Octstr *rm_item;

    client_ip = msisdn = type = session_id = NULL;

    /* only add if we have a Accounting-Request PDU */
    if (pdu->type == 0x04) {

        /* check if we have a START or STOP event */
        type = dict_get(pdu->attr, octstr_imm("Acct-Status-Type"));

        /* get the sesion id */
        session_id = dict_get(pdu->attr, octstr_imm("Acct-Session-Id"));

        /* grep the needed data */
        client_ip = dict_get(pdu->attr, octstr_imm("Framed-IP-Address"));
        msisdn = dict_get(pdu->attr, octstr_imm("Calling-Station-Id"));

        /* we can't add mapping without both components */
        if (client_ip == NULL || msisdn == NULL) {
            warning(0, "RADIUS: NAS did either not send 'Framed-IP-Address' or/and "
                    "'Calling-Station-Id', dropping mapping but will forward.");
            /* anyway forward the packet to remote RADIUS server */
            return 1;
        }

        if (octstr_compare(type, octstr_imm("1")) == 0 && session_id && msisdn) {
            /* session START */
            if (dict_get(radius_table, client_ip) == NULL &&
                dict_get(session_table, session_id) == NULL) {
                Octstr *put_msisdn = octstr_duplicate(msisdn);
                Octstr *put_client_ip = octstr_duplicate(client_ip);
                Octstr *put_session_id = octstr_duplicate(session_id);
                Octstr *old_session_id, *old_client_ip;

                /* ok, this is a new session. If it contains an IP that is still
                 * in the session/client tables then remove the old session from the
                 * two tables session/client */
                if ((old_session_id = dict_get(client_table, client_ip)) != NULL &&
                    (old_client_ip = dict_get(session_table, old_session_id)) != NULL &&
                    octstr_compare(old_session_id, session_id) != 0) {
                    rm_item = dict_remove(client_table, client_ip);
                    octstr_destroy(rm_item);
                    rm_item = dict_remove(session_table, old_session_id);
                    octstr_destroy(rm_item);
                    octstr_destroy(old_session_id);
                    octstr_destroy(old_client_ip);
                }

                /* insert both, new client IP and session to mapping tables */
                dict_put(radius_table, client_ip, put_msisdn);
                dict_put(session_table, session_id, put_client_ip);
                dict_put(client_table, client_ip, put_session_id);

                info(0, "RADIUS: Mapping `%s <-> %s' for session id <%s> added.",
                     octstr_get_cstr(client_ip), octstr_get_cstr(msisdn),
                     octstr_get_cstr(session_id));
                ret = 1;
            } else {
                warning(0, "RADIUS: Duplicate mapping `%s <-> %s' for session "
                        "id <%s> received, ignoring.",
                        octstr_get_cstr(client_ip), octstr_get_cstr(msisdn),
                        octstr_get_cstr(session_id));
            }
        } else if (octstr_compare(type, octstr_imm("2")) == 0) {
            /* session STOP */
            Octstr *comp_client_ip;
            if ((msisdn = dict_get(radius_table, client_ip)) != NULL &&
                (comp_client_ip = dict_get(session_table, session_id)) != NULL &&
                octstr_compare(client_ip, comp_client_ip) == 0) {
                dict_remove(radius_table, client_ip);
                rm_item = dict_remove(client_table, client_ip);
                octstr_destroy(rm_item);
                dict_remove(session_table, session_id);
                info(0, "RADIUS: Mapping `%s <-> %s' for session id <%s> removed.",
                     octstr_get_cstr(client_ip), octstr_get_cstr(msisdn),
                     octstr_get_cstr(session_id));
                octstr_destroy(msisdn);
                octstr_destroy(comp_client_ip);

                ret = 1;
            } else {
                warning(0, "RADIUS: Could not find mapping for `%s' session "
                        "id <%s>, ignoring.",
                        octstr_get_cstr(client_ip), octstr_get_cstr(session_id));
            }

        } else {
            error(0, "RADIUS: unknown Acct-Status-Type `%s' received, ignoring.",
                  octstr_get_cstr(type));
        }
    }

    return ret;
}


/*************************************************************************
 * The main proxy thread.
 */

static void proxy_thread(void *arg)
{
    int ss, cs; /* server and client sockets */
    int fl; /* socket flags */
    Octstr *addr = NULL;
    int forward;
    Octstr *tmp;

    run_thread = 1;
    ss = cs = -1;

    /* create client binding, only if we have a remote server
     * and make the client socet non-blocking */
    if (remote_host != NULL) {
        cs = udp_client_socket();
        fl = fcntl(cs, F_GETFL);
        fcntl(cs, F_SETFL, fl | O_NONBLOCK);
        addr = udp_create_address(remote_host, remote_port);
    }

    /* create server binding */
    ss = udp_bind(our_port, octstr_get_cstr(our_host));

    /* make the server socket non-blocking */
    fl = fcntl(ss, F_GETFL);
    fcntl(ss, F_SETFL, fl | O_NONBLOCK);

    if (ss == -1)
        panic(0, "RADIUS: Couldn't set up server socket for port %ld.", our_port);

    while (run_thread) {
        RADIUS_PDU *pdu, *r;
        Octstr *data, *rdata;
        Octstr *from_nas, *from_radius;

        pdu = r = NULL;
        data = rdata = from_nas = from_radius = NULL;
        
        if (read_available(ss, 100000) < 1)
            continue;

        /* get request from NAS */
        if (udp_recvfrom(ss, &data, &from_nas) == -1) {
            if (errno == EAGAIN)
                /* No datagram available, don't block. */
                continue;

            error(0, "RADIUS: Couldn't receive request data from NAS");
            continue;
        }

        tmp = udp_get_ip(from_nas);
        info(0, "RADIUS: Got data from NAS <%s:%d>",
             octstr_get_cstr(tmp), udp_get_port(from_nas));
        octstr_destroy(tmp);
        octstr_dump(data, 0);

        /* unpacking the RADIUS PDU */
        if ((pdu = radius_pdu_unpack(data)) == NULL) {
            warning(0, "RADIUS: Couldn't unpack PDU from NAS, ignoring.");
            goto error;
        }
        info(0, "RADIUS: from NAS: PDU type: %s", pdu->type_name);

        /* authenticate the Accounting-Request packet */
        if (radius_authenticate_pdu(pdu, &data, secret_nas) == 0) {
            warning(0, "RADIUS: Authentication failed for PDU from NAS, ignoring.");
            goto error;
        }

        /* store to hash table if not present yet */
        mutex_lock(radius_mutex);
        forward = update_tables(pdu);
        mutex_unlock(radius_mutex);

        /* create response PDU for NAS */
        r = radius_pdu_create(0x05, pdu);

        /*
         * create response authenticator 
         * code+identifier(req)+length+authenticator(req)+(attributes)+secret 
         */
        r->u.Accounting_Response.identifier = pdu->u.Accounting_Request.identifier;
        r->u.Accounting_Response.authenticator =
            octstr_duplicate(pdu->u.Accounting_Request.authenticator);

        /* pack response for NAS */
        rdata = radius_pdu_pack(r);

        /* creates response autenticator in encoded PDU */
        radius_authenticate_pdu(r, &rdata, secret_nas);

        /* 
         * forward request to remote RADIUS server only if updated
         * and if we have a configured remote RADIUS server 
         */
        if ((remote_host != NULL) && forward) {
            if (udp_sendto(cs, data, addr) == -1) {
                error(0, "RADIUS: Couldn't send to remote RADIUS <%s:%ld>.",
                      octstr_get_cstr(remote_host), remote_port);
            } else 
            if (read_available(cs, remote_timeout) < 1) {
                error(0, "RADIUS: Timeout for response from remote RADIUS <%s:%ld>.",
                      octstr_get_cstr(remote_host), remote_port);
            } else 
            if (udp_recvfrom(cs, &data, &from_radius) == -1) {
                error(0, "RADIUS: Couldn't receive from remote RADIUS <%s:%ld>.",
                      octstr_get_cstr(remote_host), remote_port);
            } else {
                info(0, "RADIUS: Got data from remote RADIUS <%s:%d>.",
                     octstr_get_cstr(udp_get_ip(from_radius)), udp_get_port(from_radius));
                octstr_dump(data, 0);

                /* XXX unpack the response PDU and check if the response
                 * authenticator is valid */
            }
        }

        /* send response to NAS */
        if (udp_sendto(ss, rdata, from_nas) == -1)
            error(0, "RADIUS: Couldn't send response data to NAS <%s:%d>.",
                  octstr_get_cstr(udp_get_ip(from_nas)), udp_get_port(from_nas));

error:
        radius_pdu_destroy(pdu);
        radius_pdu_destroy(r);

        octstr_destroy(rdata);
        octstr_destroy(data);
        octstr_destroy(from_nas);

        debug("radius.proxy", 0, "RADIUS: Mapping table contains %ld elements",
              dict_key_count(radius_table));
        debug("radius.proxy", 0, "RADIUS: Session table contains %ld elements",
              dict_key_count(session_table));
        debug("radius.proxy", 0, "RADIUS: Client table contains %ld elements",
              dict_key_count(client_table));

    }

    octstr_destroy(addr);
}


/*************************************************************************
 * Public functions: init, shutdown, mapping.
 */

Octstr *radius_acct_get_msisdn(Octstr *client_ip)
{
    Octstr *m, *r;
    char *uf;

    /* if no proxy thread is running, then pass NULL as result */
    if (radius_table == NULL || client_ip == NULL)
        return NULL;

    mutex_lock(radius_mutex);
    m = dict_get(radius_table, client_ip);
    mutex_unlock(radius_mutex);
    r = m ? octstr_duplicate(m) : NULL;

    /* apply number normalization */
    uf = unified_prefix ? octstr_get_cstr(unified_prefix) : NULL;
    normalize_number(uf, &r);

    return r;
}

void radius_acct_init(CfgGroup *grp)
{
    long nas_ports = 0;

    /* get configured parameters */
    if ((our_host = cfg_get(grp, octstr_imm("our-host"))) == NULL) {
        our_host = octstr_create("0.0.0.0");
    }
    if ((remote_host = cfg_get(grp, octstr_imm("remote-host"))) != NULL) {
        cfg_get_integer(&remote_port, grp, octstr_imm("remote-port"));
        if ((secret_radius = cfg_get(grp, octstr_imm("secret-radius"))) == NULL) {
            panic(0, "RADIUS: No shared secret `secret-radius' for remote RADIUS in `radius-acct' provided.");
        }
    }
    cfg_get_integer(&our_port, grp, octstr_imm("our-port"));
    cfg_get_integer(&remote_timeout, grp, octstr_imm("remote-timeout"));

    if ((cfg_get_integer(&nas_ports, grp, octstr_imm("nas-ports"))) == -1) {
        nas_ports = RADIUS_NAS_PORTS;
    }

    if ((secret_nas = cfg_get(grp, octstr_imm("secret-nas"))) == NULL) {
        panic(0, "RADIUS: No shared secret `secret-nas' for NAS in `radius-acct' provided.");
    }

    unified_prefix = cfg_get(grp, octstr_imm("unified-prefix"));

    info(0, "RADIUS: local RADIUS accounting proxy at <%s:%ld>",
         octstr_get_cstr(our_host), our_port);
    if (remote_host == NULL) {
        info(0, "RADIUS: remote RADIUS accounting server is absent");
    } else {
        info(0, "RADIUS: remote RADIUS accounting server at <%s:%ld>",
             octstr_get_cstr(remote_host), remote_port);
    }

    info(0, "RADIUS: initializing internal hash tables with %ld buckets.", nas_ports);

    radius_mutex = mutex_create();

    /* init hash tables */
    radius_table = dict_create(nas_ports, (void (*)(void *))octstr_destroy);
    session_table = dict_create(nas_ports, (void (*)(void *))octstr_destroy);
    client_table = dict_create(nas_ports, (void (*)(void *))octstr_destroy);

    gwthread_create(proxy_thread, NULL);
}

void radius_acct_shutdown(void)
{
    if (radius_mutex == NULL) /* haven't init'ed at all */
        return ;

    mutex_lock(radius_mutex);
    run_thread = 0;
    mutex_unlock(radius_mutex);

    gwthread_join_every(proxy_thread);

    dict_destroy(radius_table);
    dict_destroy(session_table);
    dict_destroy(client_table);

    mutex_destroy(radius_mutex);

    octstr_destroy(our_host);
    octstr_destroy(remote_host);
    octstr_destroy(secret_nas);
    octstr_destroy(secret_radius);
    octstr_destroy(unified_prefix);

    info(0, "RADIUS: accounting proxy stopped.");
}
