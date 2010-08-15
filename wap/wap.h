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
 * wap.h - public interface to WAP protocol library
 *
 * The WAP protocol library consists of separate layers, which each run
 * in their own thread.  The layers are normally used together and will
 * communicate with each other, but they can be used separately by
 * specialized applications.
 *
 * Communication between layers is done by sending WAPEvent structures.
 * These events are passed to dispatch functions.  Each layer has its
 * own dispatch function which is responsible for queueing the event
 * for that layer.
 * 
 * The application using this library has to provide an application layer
 * and a datagram layer.  These layers do not have to be implemented in
 * any particular way and do not have to run in their own threads, but
 * they do have to provide dispatch functions.
 *
 * In general, if a layer receives an event that it does not know how
 * to handle, it will report this and ignore the event.
 */

#ifndef WAP_H
#define WAP_H

#include "wap_events.h"
#include "wap_addr.h"

typedef void wap_dispatch_func_t(WAPEvent *event);

/*
 * Generic dispatch function that takes T_DUnitdata_Ind events and
 * figures out to which layer they should be sent, by recognizing
 * well-known port numbers and by inspecting the datagram contents.
 * It also unpacks WTP events before dispatching, so that the WTP
 * thread is not burdened by this.
 */
void wap_dispatch_datagram(WAPEvent *event);

/*
 * Generic startup function that initializes all the layers and
 * chains their dispatch functions together.
 */
void wap_init(wap_dispatch_func_t *datagram_dispatch,
              wap_dispatch_func_t *application_dispatch);

/*
 * Undoes what wap_init did.
 */
void wap_shutdown(void);


/*
 * Datagram layer
 *
 * This layer is not provided by libwap itself.  The application is
 * expected to create one, by:
 *  - providing a dispatch function that takes T_DUnitdata_Req
 *    events (outgoing datagrams)
 *  - passing incoming datagrams to the right layer, either by
 *    calling the layer's dispatch function directly or by calling
 *    wap_dispatch_datagram().
 */


/*
 * Transaction layer, responder
 *
 * This layer implements the Responder side of WTP.
 * Its dispatch function takes events of these types:
 *
 *   RcvInvoke, RcvAck, RcvAbort, RcvErrorPDU,
 *   TR_Invoke_Res, TR_Result_Req, TR_Abort_Req,
 *   TimerTO_A, TimerTO_R, TimerTO_W
 *
 * FIXME It also takes T_DUnitdata_Ind events, which it will unpack into one
 * of the Rcv* events and then process.
 *
 * This layer will dispatch T_DUnitdata_Req events to the datagram layer,
 * and these event types to the session layer:
 *
 *   TR_Invoke_Ind, TR_Result_Cnf, TR_Abort_Ind
 *
 * Timer_freq is the timer 'tick' used. All wtp responder timers are 
 * multiplies of this value.
 */
void wtp_resp_init(wap_dispatch_func_t *datagram_dispatch,
                   wap_dispatch_func_t *session_dispatch,
                   wap_dispatch_func_t *push_dispatch, 
                   long timer_freq);
void wtp_resp_dispatch_event(WAPEvent *event);
void wtp_resp_shutdown(void);

/*
 * Transaction layer, initiator
 *
 * This layer implements the Initiator side of WTP.
 * FIXME Currently only class 0 and 1 are implemented.
 * Its dispatch function takes events of these types:
 *
 *   RcvAck, RcvAbort, RcvErrorPDU
 *   TR_Invoke_Req, TR_Abort_Req
 *   TimerTO_R
 *
 * FIXME It also takes T_DUnitdata_Ind events, which it will unpack into one
 * of the Rcv* events and then process.
 *
 * This layer will dispatch T_DUnitdata_Req events to the datagram layer,
 * and these event types to the session layer:
 *
 *   TR_Invoke_Cnf, TR_Abort_Ind
 *
 * Timer_freq is a timer 'tick'. All initiator timer values are multiplies 
 * of it.
 */
void wtp_initiator_init(wap_dispatch_func_t *datagram_dispatch,
                        wap_dispatch_func_t *session_dispatch,
                        long timer_freq);
void wtp_initiator_dispatch_event(WAPEvent *event);
void wtp_initiator_shutdown(void);

/*
 * Session layer, connectionless mode
 *
 * This layer implements Connectionless WSP.
 * FIXME Currently only the server side is implemented.
 * Its dispatch function takes events of these types:
 *
 *   T_DUnitdata_Ind
 *   S_Unit_MethodResult_Req
 *
 * This layer will dispatch T_DUnitdata_Req events to the datagram layer,
 * and S_Unit_MethodInvoke_Ind events to the application layer.
 */
void wsp_unit_init(wap_dispatch_func_t *datagram_dispatch,
                   wap_dispatch_func_t *application_dispatch);
void wsp_unit_dispatch_event(WAPEvent *event);
void wsp_unit_shutdown(void);


/*
 * Session layer, connection-oriented mode, server side
 *
 * This layer implements the server side of connection-oriented WSP.
 * FIXME Not all defined service primitives are supported yet.
 * Its dispatch function takes events of these types:
 *
 *   TR_Invoke_Ind, TR_Result_Cnf, TR_Abort_Ind
 *   S_Connect_Res, S_Resume_Res
 *   S_MethodInvoke_Res, S_MethodResult_Res
 *   Disconnect_Event, Suspend_Event  (internal)
 *
 * This layer will dispatch events of these types to the application layer:
 *
 *   S_Connect_Ind, S_Disconnect_Ind,
 *   S_Suspend_Ind, S_Resume_Ind,
 *   S_MethodInvoke_Ind, S_MethodResult_Cnf, S_MethodAbort_Ind
 *
 * and events of these types to the WTP Responder layer:
 *
 *   TR_Invoke_Res, TR_Result_Req, TR_Abort_Req
 *
 * and events of these types to the WTP Initiator layer:
 *
 *   (none yet)
 */
void wsp_session_init(wap_dispatch_func_t *responder_dispatch,
		      wap_dispatch_func_t *initiator_dispatch,
                      wap_dispatch_func_t *application_dispatch,
                      wap_dispatch_func_t *ota_dispatch);
void wsp_session_dispatch_event(WAPEvent *event);
void wsp_session_shutdown(void);


/*
 * Session layer, connection-oriented mode, client side
 *
 * FIXME Not implemented yet.
 */

void wsp_push_client_init(wap_dispatch_func_t *dispatch_self, 
                          wap_dispatch_func_t *dispatch_wtp_resp);
void wsp_push_client_shutdown(void);
void wsp_push_client_dispatch_event(WAPEvent *e);

/*
 * Application layer
 *
 * This layer is not provided by libwap itself.  The application is
 * expected to create one, by providing a dispatch function to the
 * session layer that takes events of these types:
 *
 *   S_Connect_Ind, S_Disconnect_Ind,
 *   S_Suspend_Ind, S_Resume_Ind,
 *   S_MethodInvoke_Ind, S_MethodResult_Cnf, S_MethodAbort_Ind
 *   S_Unit_MethodInvoke_Ind   (from wsp_unit)
 *
 * For most of these events the application layer is expected to send
 * a response back to the session layer.
 */

#endif
