/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2010 Kannel Group  
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
 * shared.c - some utility routines shared by all Kannel boxes
 *
 * Lars Wirzenius
 */

#include <libxml/xmlversion.h>

#include "gwlib/gwlib.h"
#include "shared.h"


volatile enum program_status program_status = starting_up;


/***********************************************************************
 * Communication with the bearerbox.
 */

/* this is a static connection if only *one* boxc connection is 
 * established from a foobarbox to bearerbox. */
static Connection *bb_conn;


Connection *connect_to_bearerbox_real(Octstr *host, int port, int ssl, Octstr *our_host)
{
    Connection *conn;

#ifdef HAVE_LIBSSL
	if (ssl) 
	    conn = conn_open_ssl(host, port, NULL, our_host);
        /* XXX add certkeyfile to be given to conn_open_ssl */
	else
#endif /* HAVE_LIBSSL */
    conn = conn_open_tcp(host, port, our_host);
    if (conn == NULL)
        return NULL;

    if (ssl)
        info(0, "Connected to bearerbox at %s port %d using SSL.",
	         octstr_get_cstr(host), port);
    else
        info(0, "Connected to bearerbox at %s port %d.",
	         octstr_get_cstr(host), port);

    return conn;
}


void connect_to_bearerbox(Octstr *host, int port, int ssl, Octstr *our_host)
{
    bb_conn = connect_to_bearerbox_real(host, port, ssl, our_host);
    if (bb_conn == NULL)
        panic(0, "Couldn't connect to the bearerbox.");
}


void close_connection_to_bearerbox_real(Connection *conn)
{
    conn_destroy(conn);
}


void close_connection_to_bearerbox(void)
{
    close_connection_to_bearerbox_real(bb_conn);
    bb_conn = NULL;
}


void write_to_bearerbox_real(Connection *conn, Msg *pmsg)
{
    Octstr *pack;

    pack = msg_pack(pmsg);
    if (conn_write_withlen(conn, pack) == -1)
    	error(0, "Couldn't write Msg to bearerbox.");

    msg_destroy(pmsg);
    octstr_destroy(pack);
}


void write_to_bearerbox(Msg *pmsg)
{
    write_to_bearerbox_real(bb_conn, pmsg);
}


int deliver_to_bearerbox_real(Connection *conn, Msg *msg) 
{
     
    Octstr *pack;
    
    pack = msg_pack(msg);
    if (conn_write_withlen(conn, pack) == -1) {
    	error(0, "Couldn't deliver Msg to bearerbox.");
        octstr_destroy(pack);
        return -1;
    }
                                   
    octstr_destroy(pack);
    msg_destroy(msg);
    return 0;
}


int deliver_to_bearerbox(Msg *msg)
{
    return deliver_to_bearerbox_real(bb_conn, msg);
}
                                           

int read_from_bearerbox_real(Connection *conn, Msg **msg, double seconds)
{
    int ret;
    Octstr *pack;

    pack = NULL;
    *msg = NULL;
    while (program_status != shutting_down) {
        pack = conn_read_withlen(conn);
        gw_claim_area(pack);
        if (pack != NULL)
            break;

        if (conn_error(conn)) {
            error(0, "Error reading from bearerbox, disconnecting.");
            return -1;
        }
        if (conn_eof(conn)) {
            error(0, "Connection closed by the bearerbox.");
            return -1;
        }

        ret = conn_wait(conn, seconds);
        if (ret < 0) {
            error(0, "Connection to bearerbox broke.");
            return -1;
        }
        else if (ret == 1) {
            /* debug("gwlib.gwlib", 0, "Connection to bearerbox timed out after %.2f seconds.", seconds); */
            return 1;
        }
    }

    if (pack == NULL)
        return -1;

    *msg = msg_unpack(pack);
    octstr_destroy(pack);

    if (*msg == NULL) {
        error(0, "Failed to unpack data!");
        return -1;
    }

    return 0;
}


int read_from_bearerbox(Msg **msg, double seconds)
{
    return read_from_bearerbox_real(bb_conn, msg, seconds);
}


/*****************************************************************************
 *
 * Function validates an OSI date. Return unmodified octet string date when it
 * is valid, NULL otherwise.
 */

Octstr *parse_date(Octstr *date)
{
    long date_value;

    if (octstr_get_char(date, 4) != '-')
        goto error;
    if (octstr_get_char(date, 7) != '-')
        goto error;
    if (octstr_get_char(date, 10) != 'T')
        goto error;
    if (octstr_get_char(date, 13) != ':')
        goto error;
    if (octstr_get_char(date, 16) != ':')
        goto error;
    if (octstr_get_char(date, 19) != 'Z')
        goto error;

    if (octstr_parse_long(&date_value, date, 0, 10) < 0)
        goto error;
    if (octstr_parse_long(&date_value, date, 5, 10) < 0)
        goto error;
    if (date_value < 1 || date_value > 12)
        goto error;
    if (octstr_parse_long(&date_value, date, 8, 10) < 0)
        goto error;
    if (date_value < 1 || date_value > 31)
        goto error;
    if (octstr_parse_long(&date_value, date, 11, 10) < 0)
        goto error;
    if (date_value < 0 || date_value > 23)
        goto error;
    if (octstr_parse_long(&date_value, date, 14, 10) < 0)
        goto error;
    if (date_value < 0 || date_value > 59)
        goto error;
    if (date_value < 0 || date_value > 59)
        goto error;
    if (octstr_parse_long(&date_value, date, 17, 10) < 0)
        goto error;

    return date;

error:
    warning(0, "parse_date: not an ISO date");
    return NULL;
}

