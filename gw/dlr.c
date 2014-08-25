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
 * gw/dlr.c
 *
 * Implementation of handling delivery reports (DLRs)
 *
 * Andreas Fink <andreas@fink.org>, 18.08.2001
 * Stipe Tolj <stolj@wapme.de>, 22.03.2002
 * Alexander Malysh <a.malysh@centrium.de> 2003
 *
 * Changes:
 * 2001-12-17: andreas@fink.org:
 *     implemented use of mutex to avoid two mysql calls to run at the same time
 * 2002-03-22: stolj@wapme.de:
 *     added more abstraction to fit for several other storage types
 * 2002-08-04: stolj@wapme.de:
 *     added simple database library (sdb) support
 * 2002-11-14: stolj@wapme.de:
 *     added re-routing info for DLRs to route via bearerbox to the same smsbox
 *     instance. This is required if you use state conditioned smsboxes or smppboxes
 *     via one bearerbox. Previously bearerbox was simple ignoring to which smsbox
 *     connection a msg is passed. Now we can route the messages inside bearerbox.
 * 2009-04-29: aguerrieri at kannel dot org:
 *     added support for ms-sql and sybase via freetds.
 */
 
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include <unistd.h>

#include "gwlib/gwlib.h"
#include "sms.h"
#include "dlr.h"
#include "dlr_p.h"

/* Our callback functions */
static struct dlr_storage *handles = NULL;

/*
 * Function to allocate a new struct dlr_entry entry
 * and initialize it to zero
 */
struct dlr_entry *dlr_entry_create(void)
{
    struct dlr_entry *dlr;

    dlr = gw_malloc(sizeof(*dlr));
    gw_assert(dlr != NULL);

    /* set all values to NULL */
    memset(dlr, 0, sizeof(*dlr));

    return dlr;
}

/* 
 * Duplicate dlr entry
 */
struct dlr_entry *dlr_entry_duplicate(const struct dlr_entry *dlr)
{
    struct dlr_entry *ret;

    if (dlr == NULL)
        return NULL;

    ret = dlr_entry_create();
    ret->smsc = octstr_duplicate(dlr->smsc);
    ret->timestamp = octstr_duplicate(dlr->timestamp);
    ret->source = octstr_duplicate(dlr->source);
    ret->destination = octstr_duplicate(dlr->destination);
    ret->service = octstr_duplicate(dlr->service);
    ret->url = octstr_duplicate(dlr->url);
    ret->boxc_id = octstr_duplicate(dlr->boxc_id);
    ret->mask = dlr->mask;

    return ret;
}

/*
 * Function to destroy the struct dlr_entry entry
 */
void dlr_entry_destroy(struct dlr_entry *dlr)
{
    /* sanity check */
    if (dlr == NULL)
        return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

    O_DELETE(dlr->smsc);
    O_DELETE(dlr->timestamp);
    O_DELETE(dlr->source);
    O_DELETE(dlr->destination);
    O_DELETE(dlr->service);
    O_DELETE(dlr->url);
    O_DELETE(dlr->boxc_id);

#undef O_DELETE

    dlr->mask = 0;
    gw_free(dlr);
}

/*
 * Load all configuration directives that are common for all database
 * types that use the 'dlr-db' group to define which attributes are 
 * used in the table
 */
struct dlr_db_fields *dlr_db_fields_create(CfgGroup *grp)
{
    struct dlr_db_fields *ret = NULL;

    ret = gw_malloc(sizeof(*ret));
    gw_assert(ret != NULL);
    memset(ret, 0, sizeof(*ret));

    if (!(ret->table = cfg_get(grp, octstr_imm("table"))))
   	    panic(0, "DLR: DB: directive 'table' is not specified!");
    ret->ttl = 0;
    if (cfg_get_integer(&ret->ttl, grp, octstr_imm("ttl")) == -1 || ret->ttl < 0)
        ret->ttl = 0;
    if (!(ret->field_smsc = cfg_get(grp, octstr_imm("field-smsc"))))
   	    panic(0, "DLR: DB: directive 'field-smsc' is not specified!");
    if (!(ret->field_ts = cfg_get(grp, octstr_imm("field-timestamp"))))
        panic(0, "DLR: DB: directive 'field-timestamp' is not specified!");
    if (!(ret->field_src = cfg_get(grp, octstr_imm("field-source"))))
   	    panic(0, "DLR: DB: directive 'field-source' is not specified!");
    if (!(ret->field_dst = cfg_get(grp, octstr_imm("field-destination"))))
   	    panic(0, "DLR: DB: directive 'field-destination' is not specified!");
    if (!(ret->field_serv = cfg_get(grp, octstr_imm("field-service"))))
   	    panic(0, "DLR: DB: directive 'field-service' is not specified!");
    if (!(ret->field_url = cfg_get(grp, octstr_imm("field-url"))))
   	    panic(0, "DLR: DB: directive 'field-url' is not specified!");
    if (!(ret->field_mask = cfg_get(grp, octstr_imm("field-mask"))))
        panic(0, "DLR: DB: directive 'field-mask' is not specified!");
    if (!(ret->field_status = cfg_get(grp, octstr_imm("field-status"))))
   	    panic(0, "DLR: DB: directive 'field-status' is not specified!");
    if (!(ret->field_boxc = cfg_get(grp, octstr_imm("field-boxc-id"))))
   	    panic(0, "DLR: DB: directive 'field-boxc-id' is not specified!");

    return ret;
}

void dlr_db_fields_destroy(struct dlr_db_fields *fields)
{
    /* sanity check */
    if (fields == NULL)
        return;

#define O_DELETE(a)	 { if (a) octstr_destroy(a); a = NULL; }

    O_DELETE(fields->table);
    O_DELETE(fields->field_smsc);
    O_DELETE(fields->field_ts);
    O_DELETE(fields->field_src);
    O_DELETE(fields->field_dst);
    O_DELETE(fields->field_serv);
    O_DELETE(fields->field_url);
    O_DELETE(fields->field_mask);
    O_DELETE(fields->field_status);
    O_DELETE(fields->field_boxc);

#undef O_DELETE

    gw_free(fields);
}


/*
 * Initialize specifically dlr storage. If defined storage is unknown
 * then panic.
 */
void dlr_init(Cfg* cfg)
{
    CfgGroup *grp;
    Octstr *dlr_type;

    /* check which DLR storage type we are using */
    grp = cfg_get_single_group(cfg, octstr_imm("core"));
    if(grp == NULL)
	panic(0, "DLR: can't find group core");

    dlr_type = cfg_get(grp, octstr_imm("dlr-storage"));

    /* 
     * assume we are using internal memory in case no directive
     * has been specified, warn the user anyway
     */
    if (dlr_type == NULL) {
        dlr_type = octstr_imm("internal");
        warning(0, "DLR: using default 'internal' for storage type.");
    }

    /* call the sub-init routine */
    if (octstr_compare(dlr_type, octstr_imm("spool")) == 0) {
        handles = dlr_init_spool(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("mysql")) == 0) {
        handles = dlr_init_mysql(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("sdb")) == 0) {
        handles = dlr_init_sdb(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("oracle")) == 0) {
        handles = dlr_init_oracle(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("internal")) == 0) {
        handles = dlr_init_mem(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("pgsql")) == 0) {
        handles = dlr_init_pgsql(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("mssql")) == 0) {
        handles = dlr_init_mssql(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("sqlite3")) == 0) {
        handles = dlr_init_sqlite3(cfg);
    } else if (octstr_compare(dlr_type, octstr_imm("redis")) == 0) {
        handles = dlr_init_redis(cfg);
    }

    /*
     * add aditional types here
     */

     if (handles == NULL) {
   	    panic(0, "DLR: storage type '%s' is not supported!", octstr_get_cstr(dlr_type));
    }

    /* check needed function pointers */
    if (handles->dlr_add == NULL || handles->dlr_get == NULL || handles->dlr_remove == NULL)
        panic(0, "DLR: storage type '%s' don't implement needed functions", octstr_get_cstr(dlr_type));

    /* get info from storage */
    info(0, "DLR using storage type: %s", handles->type);

    /* cleanup */
    octstr_destroy(dlr_type);
}

/*
 * Shutdown dlr storage
 */
void dlr_shutdown()
{
    if (handles != NULL && handles->dlr_shutdown != NULL)
        handles->dlr_shutdown();
}

/* 
 * Return count waiting delivery entries or -1 if error occurs
 */
long dlr_messages(void)
{
    if (handles != NULL && handles->dlr_messages != NULL)
        return handles->dlr_messages();

    return -1;
}

/*
 * Return type of used dlr storage
 */
const char* dlr_type(void)
{
    if (handles != NULL && handles->type != NULL)
        return handles->type;

    return "unknown";
}
 
/*
 * Add new dlr entry into dlr storage.
 */
void dlr_add(const Octstr *smsc, const Octstr *ts, Msg *msg, int use_dst)
{
    struct dlr_entry *dlr = NULL;

    /* Add the foreign_id so all SMSC modules can use it.
     * Obey also the original message in the split_parts list. */
    if (msg->sms.foreign_id != NULL)
        octstr_destroy(msg->sms.foreign_id);
    msg->sms.foreign_id = octstr_duplicate(ts);
    if (msg->sms.split_parts != NULL) {
        struct split_parts *split = msg->sms.split_parts;
        if (split->orig->sms.foreign_id != NULL)
            octstr_destroy(split->orig->sms.foreign_id);
        split->orig->sms.foreign_id = octstr_duplicate(ts);
    }

    if (octstr_len(smsc) == 0) {
        warning(0, "DLR[%s]: Can't add a dlr without smsc-id", dlr_type());
        return;
    }

    /* sanity check */
    if (handles == NULL || handles->dlr_add == NULL || msg == NULL)
        return;

    /* check if delivery receipt requested */
    if (!DLR_IS_ENABLED(msg->sms.dlr_mask))
        return;

     /* allocate new struct dlr_entry struct */
    dlr = dlr_entry_create();
    gw_assert(dlr != NULL);

    /* now copy all values, we are interested in */
    dlr->smsc = (smsc ? octstr_duplicate(smsc) : octstr_create(""));
    dlr->timestamp = (ts ? octstr_duplicate(ts) : octstr_create(""));
    dlr->source = (msg->sms.sender ? octstr_duplicate(msg->sms.sender) : octstr_create(""));
    dlr->destination = (msg->sms.receiver ? octstr_duplicate(msg->sms.receiver) : octstr_create(""));
    dlr->service = (msg->sms.service ? octstr_duplicate(msg->sms.service) : octstr_create(""));
    dlr->url = (msg->sms.dlr_url ? octstr_duplicate(msg->sms.dlr_url) : octstr_create(""));
    dlr->boxc_id = (msg->sms.boxc_id ? octstr_duplicate(msg->sms.boxc_id) : octstr_create(""));
    dlr->mask = msg->sms.dlr_mask;
    dlr->use_dst = use_dst;

    debug("dlr.dlr", 0, "DLR[%s]: Adding DLR smsc=%s, ts=%s, src=%s, dst=%s, mask=%d, boxc=%s",
          dlr_type(), octstr_get_cstr(dlr->smsc), octstr_get_cstr(dlr->timestamp),
          octstr_get_cstr(dlr->source), octstr_get_cstr(dlr->destination), dlr->mask, octstr_get_cstr(dlr->boxc_id));
	
    /* call registered function */
    handles->dlr_add(dlr);
}

/*
 * Return Msg* if dlr entry found in DB, otherwise NULL.
 * NOTE: If typ is end status (e.g. DELIVERED) then dlr entry
 *       will be removed from DB.
 */
Msg *dlr_find(const Octstr *smsc, const Octstr *ts, const Octstr *dst, int typ, int use_dst)
{
    Msg	*msg = NULL;
    struct dlr_entry *dlr = NULL;
    Octstr *dst_min = NULL;
    
    if(octstr_len(smsc) == 0) {
	warning(0, "DLR[%s]: Can't find a dlr without smsc-id", dlr_type());
        return NULL;
    }

    /* check if we have handler registered */
    if (handles == NULL || handles->dlr_get == NULL)
        return NULL;

    if (use_dst && dst) {
        dst_min = octstr_duplicate(dst);
        int len = octstr_len(dst);

        if (len > MIN_DST_LEN)
            octstr_delete(dst_min, 0, len - MIN_DST_LEN);
    }
    debug("dlr.dlr", 0, "DLR[%s]: Looking for DLR smsc=%s, ts=%s, dst=%s, type=%d",
                                 dlr_type(), octstr_get_cstr(smsc), octstr_get_cstr(ts), octstr_get_cstr(dst), typ);

    dlr = handles->dlr_get(smsc, ts, dst_min);
    if (dlr == NULL)  {
        warning(0, "DLR[%s]: DLR from SMSC<%s> for DST<%s> not found.",
                dlr_type(), octstr_get_cstr(smsc), octstr_get_cstr(dst));         
        return NULL;
    }

#define O_SET(x, val) if (octstr_len(val) > 0) { x = val; val = NULL; }

    if ((typ & dlr->mask) > 0) {
        /* its an entry we are interested in */
        msg = msg_create(sms);
        msg->sms.sms_type = report_mo;
        msg->sms.dlr_mask = typ;
        O_SET(msg->sms.service, dlr->service);
        O_SET(msg->sms.smsc_id, dlr->smsc);
        O_SET(msg->sms.receiver, dlr->destination);
        O_SET(msg->sms.sender, dlr->source);
        /* if dlr_url was present, recode it here again */
        O_SET(msg->sms.dlr_url, dlr->url);
        /* add the foreign_id */
        msg->sms.foreign_id = octstr_duplicate(ts);
        /* 
         * insert original message to the data segment 
         * later in the smsc module 
         */
        msg->sms.msgdata = NULL;
        /* 
         * If a boxc_id is available, then instruct bearerbox to 
         * route this msg back to originating smsbox
         */
        O_SET(msg->sms.boxc_id, dlr->boxc_id);

        time(&msg->sms.time);
        debug("dlr.dlr", 0, "DLR[%s]: created DLR message for URL <%s>",
                      dlr_type(), (msg->sms.dlr_url?octstr_get_cstr(msg->sms.dlr_url):""));
    } else {
        debug("dlr.dlr", 0, "DLR[%s]: Ignoring DLR message because of mask type=%d dlr->mask=%d", dlr_type(), typ, dlr->mask);
        /* ok that was a status report but we where not interested in having it */
        msg = NULL;
    }

#undef O_SET
 
    /* check for end status and if so remove from storage */
    if ((typ & DLR_BUFFERED) && ((dlr->mask & DLR_SUCCESS) || (dlr->mask & DLR_FAIL))) {
        debug("dlr.dlr", 0, "DLR[%s]: DLR not destroyed, still waiting for other delivery report", dlr_type());
        /* update dlr entry status if function defined */
        if (handles != NULL && handles->dlr_update != NULL){
            handles->dlr_update(smsc, ts, dst_min, typ);
        }
    } else {
        if (handles != NULL && handles->dlr_remove != NULL){
            /* it's not good for internal storage, but better for all others */
            handles->dlr_remove(smsc, ts, dst_min);
        } else {
            warning(0, "DLR[%s]: Storage don't have remove operation defined", dlr_type());
        }
    }

    /* destroy struct dlr_entry */
    dlr_entry_destroy(dlr);
    octstr_destroy(dst_min);

    return msg;
}
    
void dlr_flush(void)
{
    info(0, "Flushing all %ld queued DLR messages in %s storage", dlr_messages(), 
            dlr_type());
 
    if (handles != NULL && handles->dlr_flush != NULL)
        handles->dlr_flush();
}


Msg* create_dlr_from_msg(const Octstr *smsc, const Msg *msg, const Octstr *reply, long stat)
{
    Msg *dlrmsg;

    if (msg == NULL)
        return NULL;

    /* generate DLR */
    debug("dlr.dlr", 0,"SMSC[%s]: creating DLR message",
                (smsc ? octstr_get_cstr(smsc) : "UNKNOWN"));

    dlrmsg = msg_create(sms);
    gw_assert(dlrmsg != NULL);

    dlrmsg->sms.service = octstr_duplicate(msg->sms.service);
    dlrmsg->sms.dlr_mask = stat;
    dlrmsg->sms.sms_type = report_mo;
    dlrmsg->sms.smsc_id = octstr_duplicate(smsc ? smsc : msg->sms.smsc_id);
    dlrmsg->sms.sender = octstr_duplicate(msg->sms.sender);
    dlrmsg->sms.receiver = octstr_duplicate(msg->sms.receiver);
    dlrmsg->sms.dlr_url = octstr_duplicate(msg->sms.dlr_url);
    dlrmsg->sms.msgdata = octstr_duplicate(reply);
    dlrmsg->sms.boxc_id = octstr_duplicate(msg->sms.boxc_id);
    dlrmsg->sms.foreign_id = octstr_duplicate(msg->sms.foreign_id);
    time(&dlrmsg->sms.time);
    dlrmsg->sms.meta_data = octstr_duplicate(msg->sms.meta_data);

    debug("dlr.dlr", 0,"SMSC[%s]: DLR = %s",
                (smsc ? octstr_get_cstr(smsc) : "UNKNOWN"),
                (dlrmsg->sms.dlr_url ? octstr_get_cstr(dlrmsg->sms.dlr_url) : ""));

    return dlrmsg;
}
