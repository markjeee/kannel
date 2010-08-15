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
 * wapproxy.c - an WDP, WSP, WTP layer proxy
 *
 * This module contains the main program for the WAP proxy box.
 * It's intention is to sit between a WTP initiator and WTP repsonder
 * and log all the UDP traffic that is send in a session.
 * 
 * The architecture looks like this:
 *
 *   ----------    UDP    --------    UDP    ------
 *   wap device    --->   wapproxy    --->   wap gw
 *   ----------    <---   --------    <---   ------
 *   port 51000        p 9201   p 51000     port 9201
 *                 (a)                (b)
 *
 * This means wapproxy gets the UDP/WDP packets that are actually to
 * be transmitted to the real wap gw. It changes the source addr within
 * that packet to reflect wapproxy has send it and binds to the port the
 * wap device was sending the packet. Then the packet is send to the real
 * wap gw and wapproxy listens on the client source port (i.e. 51000) for 
 * packets from the wap gw. When those are received the communication is
 * inverted, which means wapproxy changes again the source addr from the
 * value of wap gw to it's own and forwards the packet to the client source
 * addr port.
 *
 * Hence the wap device uses wapproxy transparently without knowing that
 * it is only a proxy and the packets are forwarded to other boxes.
 *
 * Stipe Tolj <stolj@wapme.de>
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#include "gwlib/gwlib.h"
#include "msg.h"
//#include "bearerbox.h"
#include "shared.h"
#include "wap/wap.h"
#include "wap/wtp.h"
#include "wap/wtp_pdu.h"

/* globals */
static volatile sig_atomic_t udp_running;
static List *udpc_list;
static Octstr *interface_name = NULL;
static Octstr *wapgw;
static int verbose = 0;
static int server_port = 0;

List *incoming_wdp;
List *outgoing_wdp;
List *flow_threads;

Counter *incoming_wdp_counter;
Counter *outgoing_wdp_counter;

enum {
    CONNECTIONLESS_PORT = 9200,
    CONNECTION_ORIENTED_PORT = 9201,
    WTLS_CONNECTIONLESS_PORT = 9202,
    WTLS_CONNECTION_ORIENTED_PORT = 9203
};

/* structure for a UDP connection */
typedef struct _udpc {
    int fd;
    Octstr *addr;
    Octstr *map_addr;
    List *outgoing_list;
    long receiver;
} Udpc;

/* forward declarations */
static void udpc_destroy(Udpc *udpc);


/*-------------------------------------------------------------
 * analyze and dump functions
 *
 */

static WAPEvent *wdp_msg2event(Msg *msg)
{
    WAPEvent *dgram = NULL;

    gw_assert(msg_type(msg) == wdp_datagram);

    if (msg->wdp_datagram.destination_port == server_port ||
        msg->wdp_datagram.source_port == server_port ||
        msg->wdp_datagram.destination_port == CONNECTION_ORIENTED_PORT ||
        msg->wdp_datagram.source_port == CONNECTION_ORIENTED_PORT) {

        dgram = wap_event_create(T_DUnitdata_Ind);
        dgram->u.T_DUnitdata_Ind.addr_tuple = wap_addr_tuple_create(
				msg->wdp_datagram.source_address,
				msg->wdp_datagram.source_port,
				msg->wdp_datagram.destination_address,
				msg->wdp_datagram.destination_port);
        dgram->u.T_DUnitdata_Ind.user_data = 
                octstr_duplicate(msg->wdp_datagram.user_data);
    }
    return dgram;
}
 

static void wdp_event_dump(Msg *msg)
{
	WAPEvent *dgram;

    if ((dgram = wdp_msg2event(msg)) != NULL)
        /* wap_dispatch_datagram(dgram); */
        wap_event_dump(dgram);

    wap_event_destroy(dgram);
}


static void wtp_event_dump(Msg *msg)
{
    WAPEvent *dgram;
    List *events;
    long i, n;

    dgram = wdp_msg2event(msg);
    if (dgram == NULL)
        error(0, "dgram is null");

    /*
    pdu = wtp_pdu_unpack(dgram->u.T_DUnitdata_Ind.user_data);
    if (pdu == NULL) {
        error(0, "WTP PDU unpacking failed, WAP event is:");
        wap_event_dump(dgram);
    } else {
        wtp_pdu_dump(pdu, 0);
        wtp_pdu_destroy(pdu);
    }
    */

    events = wtp_unpack_wdp_datagram(dgram);
    n = gwlist_len(events);
    debug("wap.proxy",0,"datagram contains %ld events", n);

    i = 1;
    while (gwlist_len(events) > 0) {
        WAPEvent *event;

	    event = gwlist_extract_first(events);
        
        info(0, "WTP: %ld/%ld event %s.", i, n, wap_event_name(event->type));

        if (wtp_event_is_for_responder(event))
            /* wtp_resp_dispatch_event(event); */
            debug("",0,"datagram is for WTP responder");
        else
            /* wtp_initiator_dispatch_event(event); */
            debug("",0,"datagram is for WTP initiator");
 
        wap_event_dump(event);
        /*
        switch (event->type) {
            RcvInvoke:
                debug("",0,"XXX invoke");
                break;
            RcvResult:
                debug("",0,"XXX result");
                break;
            default:
                error(0,"unkown WTP event type while unpacking");
                break;
        }
        */
        i++;
    }   		

    wap_event_destroy(dgram);
    gwlist_destroy(events, NULL);
}


static void dump(Msg *msg)
{
    switch (verbose) {
        case 0: 
            break;
        case 1:
            msg_dump(msg, 0);
            break;
        case 2:
            wdp_event_dump(msg);
            break;
        case 3:
            msg_dump(msg, 0);
            wdp_event_dump(msg);
            break;
        case 4:
            wtp_event_dump(msg);
            break;
        case 5: 
            msg_dump(msg, 0);
            wtp_event_dump(msg);
            break;
       case 6: 
            wdp_event_dump(msg);
            wtp_event_dump(msg);
            break;
       case 7: 
            msg_dump(msg, 0);
            wdp_event_dump(msg);
            wtp_event_dump(msg);
            break;
    }
}

  
/*-------------------------------------------------
 *  receiver thread
 */

static void udp_receiver(void *arg)
{
    Octstr *datagram, *cliaddr;
    int ret;
    Msg *msg;
    Udpc *conn = arg;
    Octstr *ip;

    gwlist_add_producer(incoming_wdp);
    gwlist_add_producer(flow_threads);
    gwthread_wakeup(MAIN_THREAD_ID);
    
    /* remove messages from socket until it is closed */
    while (1) {

        if (read_available(conn->fd, 100000) < 1)
            continue;

        ret = udp_recvfrom(conn->fd, &datagram, &cliaddr);
        if (ret == -1) {
            if (errno == EAGAIN)
                /* No datagram available, don't block. */
                continue;

            error(errno, "Failed to receive an UDP");
            continue;
        }

    	ip = udp_get_ip(cliaddr);
        msg = msg_create(wdp_datagram);
    
        msg->wdp_datagram.source_address = udp_get_ip(cliaddr);
        msg->wdp_datagram.source_port = udp_get_port(cliaddr);
        msg->wdp_datagram.destination_address = udp_get_ip(conn->addr);
        msg->wdp_datagram.destination_port = udp_get_port(conn->addr);
        msg->wdp_datagram.user_data = datagram;

        info(0, "datagram received <%s:%d> -> <%s:%d>",
             octstr_get_cstr(udp_get_ip(cliaddr)), udp_get_port(cliaddr),
             octstr_get_cstr(udp_get_ip(conn->addr)), udp_get_port(conn->addr));

        dump(msg);

        /* 
         * Descide if this is (a) or (b) UDP packet and add them to the
         * corresponding queues
         */
        if (octstr_compare(conn->addr, conn->map_addr) == 0) {
            gwlist_produce(incoming_wdp, msg);
            counter_increase(incoming_wdp_counter);
        } else {
            gwlist_produce(outgoing_wdp, msg);
	        counter_increase(outgoing_wdp_counter);
        }        

        octstr_destroy(cliaddr);
        octstr_destroy(ip);
    }    
    gwlist_remove_producer(incoming_wdp);
    gwlist_remove_producer(flow_threads);
}


/*---------------------------------------------
 * sender thread
 */

static int send_udp(int fd, Msg *msg)
{
    Octstr *cliaddr;
    int ret;

    cliaddr = udp_create_address(msg->wdp_datagram.destination_address,
                                 msg->wdp_datagram.destination_port);
    ret = udp_sendto(fd, msg->wdp_datagram.user_data, cliaddr);
    if (ret == -1)
        error(0, "could not send UDP datagram");
    octstr_destroy(cliaddr);
    return ret;
}


static void udp_sender(void *arg)
{
    Msg *msg;
    Udpc *conn = arg;

    gwlist_add_producer(flow_threads);    
    while (1) {

        if ((msg = gwlist_consume(conn->outgoing_list)) == NULL)
            break;

        info(0, "sending datagram <%s:%ld> -> <%s:%ld>",
              octstr_get_cstr(msg->wdp_datagram.source_address),
              msg->wdp_datagram.source_port,
              octstr_get_cstr(msg->wdp_datagram.destination_address),
              msg->wdp_datagram.destination_port);

        dump(msg);

        if (send_udp(conn->fd, msg) == -1) {
            msg_destroy(msg);
            continue;
        }
        counter_increase(outgoing_wdp_counter);
        msg_destroy(msg);
    }
    gwthread_join(conn->receiver);

    udpc_destroy(conn);
    gwlist_remove_producer(flow_threads);
}


/*---------------------------------------------------------------
 * create UDP connection
 */

static Udpc *udpc_create(int port, char *interface_name, Octstr *map_addr)
{
    Udpc *udpc;
    Octstr *os;
    int fl;
    
    udpc = gw_malloc(sizeof(Udpc));
    udpc->fd = udp_bind(port, interface_name);
  
    os = octstr_create(interface_name);
    udpc->addr = udp_create_address(os, port);
    udpc->map_addr = map_addr ? map_addr : udpc->addr;

    octstr_destroy(os);
    if (udpc->addr == NULL) {
        error(0, "updc_create: could not resolve interface <%s>", interface_name);
        close(udpc->fd);
        gw_free(udpc);
        return NULL;
    }

    fl = fcntl(udpc->fd, F_GETFL);
    fcntl(udpc->fd, F_SETFL, fl | O_NONBLOCK);

    os = udp_get_ip(udpc->addr);
    debug("wap.proxy",0, "bound to UDP <%s:%d>",
          octstr_get_cstr(os), udp_get_port(udpc->addr));

    octstr_destroy(os);
    
    udpc->outgoing_list = gwlist_create();

    return udpc;
}    


static void udpc_destroy(Udpc *udpc)
{
    if (udpc == NULL)
        return;

    if (udpc->fd >= 0)
        close(udpc->fd);
    octstr_destroy(udpc->addr);
    gw_assert(gwlist_len(udpc->outgoing_list) == 0);
    gwlist_destroy(udpc->outgoing_list, NULL);

    gw_free(udpc);
}    


static int add_service(int port, char *interface_name, Octstr *map_addr)
{
    Udpc *udpc;
    
    if ((udpc = udpc_create(port, interface_name, map_addr)) == NULL)
        goto error;
    gwlist_add_producer(udpc->outgoing_list);

    udpc->receiver = gwthread_create(udp_receiver, udpc);
    if (udpc->receiver == -1)
        goto error;

    if (gwthread_create(udp_sender, udpc) == -1)
        goto error;

    gwlist_append(udpc_list, udpc);
    return 0;
    
error:    
    error(0, "Failed to start UDP receiver/sender thread");
    udpc_destroy(udpc);
    return -1;
}
 

/*-------------------------------------------------------------
 * main calling functions
 *
 */

static int udp_start(Cfg *cfg)
{

    if (udp_running) return -1;
    
    debug("wap.proxy", 0, "starting UDP sender/receiver module");

    udpc_list = gwlist_create();	/* have a list of running systems */

    add_service(server_port, octstr_get_cstr(interface_name), NULL);  /* wsp/wtp */
    
    gwlist_add_producer(incoming_wdp);
    udp_running = 1;
    return 0;
}


static Udpc *udpc_find_mapping(Msg *msg, int inbound)
{
    int i;
    Udpc *udpc;
    Octstr *addr;
    
    /* check if there is allready a bound UDP port */
    gwlist_lock(udpc_list);
    for (i=0; i < gwlist_len(udpc_list); i++) {
        udpc = gwlist_get(udpc_list, i);

        /* decide if we compare against inbound or outbound traffic mapping */
        addr = inbound ? udpc->map_addr : udpc->addr;

        if (msg->wdp_datagram.source_port == udp_get_port(addr) &&
            octstr_compare(msg->wdp_datagram.source_address, 
                           udp_get_ip(addr)) == 0) {
            gwlist_unlock(udpc_list);
            return udpc;
        }
    }
    gwlist_unlock(udpc_list);
    return NULL;
}


/*
 * this function receives an WDP message and adds it to
 * corresponding outgoing_list.
 */
static int udp_addwdp_from_server(Msg *msg)
{
    Udpc *udpc;
    Octstr *os;
    Octstr *source;

    if (!udp_running) return -1;
    assert(msg != NULL);
    assert(msg_type(msg) == wdp_datagram);

    octstr_destroy(msg->wdp_datagram.source_address);
    msg->wdp_datagram.source_address = 
        octstr_create(octstr_get_cstr(msg->wdp_datagram.destination_address));
    msg->wdp_datagram.source_port = msg->wdp_datagram.destination_port;

    if ((udpc = udpc_find_mapping(msg, 0)) == NULL) 
        /* there should have been one */
        panic(0,"Could not find UDP mapping, internal error");

    /* insert the found mapped destination */
    octstr_destroy(msg->wdp_datagram.source_address);
    octstr_destroy(msg->wdp_datagram.destination_address);
    
    msg->wdp_datagram.destination_address = udp_get_ip(udpc->map_addr);
    msg->wdp_datagram.destination_port = udp_get_port(udpc->map_addr);

    /* now search for our inbound UDP socket */
    os = octstr_duplicate(interface_name);
    source = udp_create_address(os, server_port);

    msg->wdp_datagram.source_address = udp_get_ip(source);
    msg->wdp_datagram.source_port = udp_get_port(source);
    if ((udpc = udpc_find_mapping(msg, 0)) == NULL) 
        panic(0,"Could not find main inbound UDP socket, internal error");

    /* 
     * ok, got the destination, got the socket, 
     * now put it on the outbound queue
     */
    gwlist_produce(udpc->outgoing_list, msg);

    octstr_destroy(os);

    return 0;
}


/*
 * this function receives an WDP message and checks if a UDP
 * service for this client has to be created
 */
static int udp_addwdp_from_client(Msg *msg)
{
    Udpc *udpc;
    Octstr *map_addr;
    Octstr *os;
    Octstr *source;
    
    if (!udp_running) return -1;
    assert(msg != NULL);
    assert(msg_type(msg) == wdp_datagram);
    
    /* 
     * Check if there is allready a bound UDP port for this mapping.
     * If not create a mapping and bind the mapped UDP port
     * The mapped port is simply 2x of the client port. 
     */
    if ((udpc = udpc_find_mapping(msg, 1)) == NULL) {
        info(0, "Creating UDP mapping <%s:%ld> <-> <%s:%ld>",
              octstr_get_cstr(msg->wdp_datagram.source_address),
              msg->wdp_datagram.source_port,
              octstr_get_cstr(msg->wdp_datagram.destination_address),
              msg->wdp_datagram.source_port*2);

        map_addr = udp_create_address(msg->wdp_datagram.source_address, 
                                      msg->wdp_datagram.source_port);
        add_service(msg->wdp_datagram.source_port * 2, 
                    octstr_get_cstr(interface_name), map_addr);
        /* now we should find it in the udpc_list */
        if ((udpc = udpc_find_mapping(msg, 1)) == NULL)
            panic(0,"Could not find UDP mapping, internal error");
    }

    /* now swap the message addressing */
    octstr_destroy(msg->wdp_datagram.source_address);
    octstr_destroy(msg->wdp_datagram.destination_address);

    os = octstr_duplicate(interface_name);
    source = udp_create_address(os, msg->wdp_datagram.source_port * 2);
    msg->wdp_datagram.source_address = udp_get_ip(source);
    msg->wdp_datagram.source_port = udp_get_port(source);
    msg->wdp_datagram.destination_address = octstr_duplicate(wapgw);
    msg->wdp_datagram.destination_port = CONNECTION_ORIENTED_PORT;

    octstr_destroy(os);

    gwlist_produce(udpc->outgoing_list, msg);
    
    return -1;
}


static int udp_shutdown(void)
{
    if (!udp_running) return -1;

    debug("bb.thread", 0, "udp_shutdown: Starting avalanche");
    gwlist_remove_producer(incoming_wdp);
    return 0;
}


static int udp_die(void)
{
    Udpc *udpc;

    if (!udp_running) return -1;
    
    /*
     * remove producers from all outgoing lists.
     */
    debug("bb.udp", 0, "udp_die: removing producers from udp-lists");

    while ((udpc = gwlist_consume(udpc_list)) != NULL) {
        gwlist_remove_producer(udpc->outgoing_list);
    }
    gwlist_destroy(udpc_list, NULL);
    udp_running = 0;
    
    return 0;
}


/*-------------------------------------------------------------
 * main consumer threads
 *
 */

static void wdp_router(void *arg)
{
    Msg *msg;

    gwlist_add_producer(flow_threads);
    
    while (1) {

        if ((msg = gwlist_consume(outgoing_wdp)) == NULL)
            break;

        gw_assert(msg_type(msg) == wdp_datagram);
	
        udp_addwdp_from_server(msg);
    }
    udp_die();

    gwlist_remove_producer(flow_threads);
}


static void service_router(void *arg)
{
    Msg *msg;

    gwlist_add_producer(flow_threads);
    
    while (1) {

        if ((msg = gwlist_consume(incoming_wdp)) == NULL)
            break;

        gw_assert(msg_type(msg) == wdp_datagram);
	
        udp_addwdp_from_client(msg);
    }
    udp_die();

    gwlist_remove_producer(flow_threads);
}


/*-------------------------------------------------------------
 * main functions
 *
 */

static void help(void) 
{
    info(0, "Usage: wapproxy [options] host ...");
    info(0, "where host is the real wap gw to forward to and options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-i interface");
    info(0, "    bind to the given interface for UDP server port (default: 0.0.0.0)");
    info(0, "-p port");
    info(0, "    bind to the given port for UDP server port (default: 9201)");
    info(0, "-m");
    info(0, "    dump WDP/UDP packets, msg_dump()");
    info(0, "-e");
    info(0, "    dump WAP event packets, wap_event_dump()");
    info(0, "-t");
    info(0, "    dump WTP PDUs, wtp_pdu_dump()");
}


int main(int argc, char **argv) 
{
    int opt;
    Cfg *cfg = NULL;
	
	gwlib_init();
    
    server_port = CONNECTION_ORIENTED_PORT;
    
    while ((opt = getopt(argc, argv, "v:meti:p:")) != EOF) {

        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;

            case 'm':
                verbose += 1;                                           
                break;

            case 'e':
                verbose += 2;
                break;

            case 't':
                verbose += 4;
                break;
                
            case 'h':
                help();
                exit(0);

            case 'i':
                interface_name = octstr_create(optarg);
                break;
                
            case 'p':
                server_port = atoi(optarg);
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }
    
    if (optind == argc) {
        help();
        exit(0);
    }

    /* get the host or IP of the real wap gw to forward the WDP packets */
    wapgw = octstr_create(argv[optind]);

    /* if no interface was given use 0.0.0.0 */
    if (!interface_name)
        interface_name = octstr_create("*");

    report_versions("wapproxy");

    /* initialize main inbound and outbound queues */
    outgoing_wdp = gwlist_create();
    incoming_wdp = gwlist_create();
    flow_threads = gwlist_create();

    outgoing_wdp_counter = counter_create();
    incoming_wdp_counter = counter_create();

    /* start the main UDP listening threads */
    udp_start(cfg);

    gwlist_add_producer(outgoing_wdp);    
    
    debug("bb", 0, "starting WDP routers");
    if (gwthread_create(service_router, NULL) == -1)
        panic(0, "Failed to start a new thread for inbound WDP routing");
    if (gwthread_create(wdp_router, NULL) == -1)
        panic(0, "Failed to start a new thread for outbound WDP routing");

    gwthread_sleep(5.0); /* give time to threads to register themselves */

    while (gwlist_consume(flow_threads) != NULL)
	;

    udp_shutdown();

    gwlist_remove_producer(outgoing_wdp);

    gwlist_destroy(flow_threads, NULL);
    gwlist_destroy(incoming_wdp, NULL);
    gwlist_destroy(outgoing_wdp, NULL);

    counter_destroy(incoming_wdp_counter);
    counter_destroy(outgoing_wdp_counter);
    octstr_destroy(interface_name);
    octstr_destroy(wapgw);

    gwlib_shutdown();

	return 0;
}
