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
 *
 * wsopt.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Optimizations for the WMLScript symbolic assembler.
 *
 */

#include "wsint.h"
#include "wsasm.h"

/* TODO: liveness analyzation */
/* TODO: jumps to return or return_es */
/* TODO: remove empty labels (helps peephole opt) */
/* TODO: i++; becomes "load, incr, pop", optimize to just incr. */
/* TODO: { const; tjump } -> { jump or nothing } */

/********************* Optimization functions ***************************/

static WsBool opt_jumps_to_jumps(WsCompilerPtr compiler)
{
    WsAsmIns *i;
    WsBool change = WS_TRUE;
    unsigned int count = 0;

    ws_info(compiler, "optimize: jumps to jumps");

    while (change) {
        count++;
        change = WS_FALSE;

        for (i = compiler->asm_head; i; i = i->next) {
            WsAsmIns * j;

            if (!WS_ASM_P_BRANCH(i))
                continue;

            /* Find the next instruction following the label. */
            for (j = i->ws_label; j && j->type == WS_ASM_P_LABEL; j = j->next)
                ;

            if (j == NULL || j->type != WS_ASM_P_JUMP)
                /* Can't optimize this case. */
                continue;

            /* We can optimize the jump `i' directly to the label of
                      `j'.  We must remember to update the reference counts
                      too. */

            i->ws_label->ws_label_refcount--;
            j->ws_label->ws_label_refcount++;

            i->ws_label = j->ws_label;
            change = WS_TRUE;
        }
    }

    return count > 1;
}


static WsBool opt_jumps_to_next_instruction(WsCompilerPtr compiler)
{
    WsAsmIns *i;
    WsBool change = WS_FALSE;

    ws_info(compiler, "optimize: jumps to next instruction");

    for (i = compiler->asm_head; i; i = i->next) {
        WsAsmIns * j;

        if (i->type != WS_ASM_P_JUMP)
            continue;

        for (j = i->next;
             j && j->type == WS_ASM_P_LABEL && i->ws_label != j;
             j = j->next)
            ;

        if (i->ws_label != j)
            /* Nop. */
            continue;

        /* Remove the jump instruction `i'. */

        change = WS_TRUE;
        i->ws_label->ws_label_refcount--;

        if (i->next)
            i->next->prev = i->prev;
        else
            compiler->asm_tail = i->prev;

        if (i->prev)
            i->prev->next = i->next;
        else
            compiler->asm_head = i->next;

        /* Continue from the label `j'. */
        i = j;
    }

    return change;
}


static WsBool opt_dead_code(WsCompilerPtr compiler)
{
    WsBool change = WS_FALSE;
    WsAsmIns *i;

    ws_info(compiler, "optimize: dead code");

    for (i = compiler->asm_head; i; i = i->next) {
        WsAsmIns * j;

        if (!(i->type == WS_ASM_P_JUMP ||
              i->type == WS_ASM_RETURN ||
              i->type == WS_ASM_RETURN_ES))
            continue;

        /* Skip until the next referenced label is found. */
        for (j = i->next;
             j && (j->type != WS_ASM_P_LABEL || j->ws_label_refcount == 0);
             j = j->next) {
            /* Update label reference counts in the deleted block. */
            if (WS_ASM_P_BRANCH(j))
                j->ws_label->ws_label_refcount--;
        }

        if (j == i->next)
            /* Nothing to delete. */
            continue;

        /* Delete everything between `i' and `j'. */
        i->next = j;
        if (j)
            j->prev = i;
        else
            compiler->asm_tail = i;

        change = WS_TRUE;
    }

    return change;
}


static WsBool opt_peephole(WsCompilerPtr compiler)
{
    WsBool change = WS_FALSE;
    WsAsmIns *i, *i2, *prev;
    WsAsmIns *new;

    ws_info(compiler, "optimize: peephole");

    prev = NULL;
    i = compiler->asm_head;
    while (i) {
        /* Two instructions wide peephole. */
        if (i->next) {
            i2 = i->next;

            /*
             * {load*,const*}	=>	-
             * pop
             */
            if (i2->type == WS_ASM_POP
                && (i->type == WS_ASM_P_LOAD_VAR
                    || i->type == WS_ASM_P_LOAD_CONST
                    || i->type == WS_ASM_CONST_0
                    || i->type == WS_ASM_CONST_1
                    || i->type == WS_ASM_CONST_M1
                    || i->type == WS_ASM_CONST_ES
                    || i->type == WS_ASM_CONST_INVALID
                    || i->type == WS_ASM_CONST_TRUE
                    || i->type == WS_ASM_CONST_FALSE)) {
                /* Remove both instructions. */
                change = WS_TRUE;

                if (prev)
                    prev->next = i2->next;
                else
                    compiler->asm_head = i2->next;

                if (i2->next)
                    i2->next->prev = prev;
                else
                    compiler->asm_tail = prev;

                i = i2->next;
                continue;
            }

            /*
             * const_es           =>      return_es
             * return
             */
            if (i2->type == WS_ASM_RETURN && i->type == WS_ASM_CONST_ES) {
                /* Replace with WS_ASM_RETURN_ES */
                new = ws_asm_ins(compiler, i->line, WS_ASM_RETURN_ES);
                if (new) {
                    change = WS_TRUE;

                    if (prev)
                        prev->next = new;
                    else
                        compiler->asm_head = new;

                    new->prev = prev;
                    new->next = i2->next;

                    if (new->next)
                        new->next->prev = new;
                    else
                        compiler->asm_tail = new;

                    i = new;
                    continue;
                }
            }
        }

        /* Move forward. */
        prev = i;
        i = i->next;
    }

    /* The interpreter will by default return the empty string if a
     * function ends without a return statement, so returning the
     * empty string at the end of a function is never useful. */
    if (compiler->asm_tail && compiler->asm_tail->type == WS_ASM_RETURN_ES) {
        compiler->asm_tail = compiler->asm_tail->prev;
        if (compiler->asm_tail == NULL)
            compiler->asm_head = NULL;
        else
            compiler->asm_tail->next = NULL;
    }

    return change;
}

/*
 * Remove conversions that are followed by an opcode that does
 * that conversion automatically anyway.
 */
static WsBool opt_conv(WsCompilerPtr compiler)
{
    WsBool change = WS_FALSE;
    WsAsmIns *i, *next, *prev;

    ws_info(compiler, "optimize: peephole");

    prev = NULL;
    i = compiler->asm_head;
    while (i) {
        if (i->type == WS_ASM_TOBOOL) {
	    next = i->next;

            /* Skip labels.  They're not going to affect which instruction
             * gets executed after this TOBOOL. */
  	    while (next != NULL && next->type == WS_ASM_P_LABEL)
	        next = next->next;

   	    if (next != NULL &&
                (next->type == WS_ASM_P_TJUMP ||
                 next->type == WS_ASM_NOT ||
	         next->type == WS_ASM_SCAND ||
	         next->type == WS_ASM_SCOR ||
	         next->type == WS_ASM_TOBOOL ||
	         next->type == WS_ASM_POP)) {
	        /* The next instruction will automatically convert its
	         * operand to boolean, or does not care about its operand
	         * (POP), so the TOBOOL is not necessary.  Delete it.  */
	        change = WS_TRUE;

	        /* Use i->next here because next might have been incremented
	         * past a label, which we do not want to delete. */
	        if (prev)
	    	    prev->next = i->next;
	        else
	    	    compiler->asm_head = i->next;

	        if (i->next)
                    i->next->prev = prev;
                else
                    compiler->asm_tail = prev;
	    }
        }

	prev = i;
	i = i->next;
    }

    return change;
}


/********************* Global entry point *******************************/

void ws_asm_optimize(WsCompilerPtr compiler)
{
    WsBool change = WS_TRUE;

    /* While we manage to change the assembler, perform the requested
       optimizations. */
    while (change) {
        change = WS_FALSE;

	/* Useless conversions */
	if (!compiler->params.no_opt_conv && opt_conv(compiler))
	    change = WS_TRUE;

        /* Peephole. */
        if (!compiler->params.no_opt_peephole && opt_peephole(compiler))
            change = WS_TRUE;

        /* Jumps to jump instructions. */
        if (!compiler->params.no_opt_jumps_to_jumps
            && opt_jumps_to_jumps(compiler))
            change = WS_TRUE;

        /* Jumps to the next instruction. */
        if (!compiler->params.no_opt_jumps_to_next_instruction
            && opt_jumps_to_next_instruction(compiler))
            change = WS_TRUE;

        /* Unreachable code. */
        if (!compiler->params.no_opt_dead_code && opt_dead_code(compiler))
            change = WS_TRUE;
    }
}
