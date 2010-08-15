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
 * wsasm.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Byte-code assembler.
 *
 */

#include "wsint.h"
#include "wsasm.h"
#include "wsstdlib.h"

/********************* Macros to fetch items from BC operands ***********/

#define WS_OPNAME(op) (operands[(op)].name)
#define WS_OPSIZE(op) (operands[(op)].size)

/********************* Byte-code operands *******************************/

static struct
{
    char *name;
    int size;
} operands[256] = {
#include "wsopcodes.h"
};

/********************* Symbolic assembler instructions ******************/

/* General helpers. */

void ws_asm_link(WsCompiler *compiler, WsAsmIns *ins)
{
    if (compiler->asm_tail) {
        compiler->asm_tail->next = ins;
        ins->prev = compiler->asm_tail;

        compiler->asm_tail = ins;
    } else
        compiler->asm_tail = compiler->asm_head = ins;
}


void ws_asm_print(WsCompiler *compiler)
{
    WsAsmIns *ins;

    for (ins = compiler->asm_head; ins; ins = ins->next) {
        if (ins->type > 0xff) {
            /* A symbolic operand. */
            switch (ins->type) {
            case WS_ASM_P_LABEL:
                ws_fprintf(WS_STDOUT, ".L%d:\t\t\t\t/* refcount=%d */\n",
                           ins->ws_label_idx, ins->ws_label_refcount);
                break;

            case WS_ASM_P_JUMP:
                ws_fprintf(WS_STDOUT, "\tjump*\t\tL%d\n",
                           ins->ws_label->ws_label_idx);
                break;

            case WS_ASM_P_TJUMP:
                ws_fprintf(WS_STDOUT, "\ttjump*\t\tL%d\n",
                           ins->ws_label->ws_label_idx);
                break;

            case WS_ASM_P_CALL:
                ws_fprintf(WS_STDOUT, "\tcall*\t\t%s\n",
                           compiler->functions[ins->ws_findex].name);
                break;

            case WS_ASM_P_CALL_LIB:
                {
                    const char *lib;
                    const char *func;

                    ws_stdlib_function_name(ins->ws_lindex,
                                            ins->ws_findex,
                                            &lib, &func);
                    ws_fprintf(WS_STDOUT, "\tcall_lib*\t%s.%s\n",
                               lib ? lib : "???",
                               func ? func : "???");
                }
                break;

            case WS_ASM_P_CALL_URL:
                ws_fprintf(WS_STDOUT, "\tcall_url*\t%u %u %u\n",
                           ins->ws_lindex, ins->ws_findex, ins->ws_args);
                break;

            case WS_ASM_P_LOAD_VAR:
                ws_fprintf(WS_STDOUT, "\tload_var*\t%u\n", ins->ws_vindex);
                break;

            case WS_ASM_P_STORE_VAR:
                ws_fprintf(WS_STDOUT, "\tstore_var*\t%u\n", ins->ws_vindex);
                break;

            case WS_ASM_P_INCR_VAR:
                ws_fprintf(WS_STDOUT, "\tincr_var*\t%u\n", ins->ws_vindex);
                break;

            case WS_ASM_P_LOAD_CONST:
                ws_fprintf(WS_STDOUT, "\tload_const*\t%u\n", ins->ws_cindex);
                break;
            }
        } else {
            WsUInt8 op = WS_ASM_OP(ins->type);

            if (operands[op].name) {
                /* Operands add_asg and sub_asg are special. */
                if (op == WS_ASM_ADD_ASG || op == WS_ASM_SUB_ASG)
                    ws_fprintf(WS_STDOUT, "\t%s\t\t%u\n", operands[ins->type].name,
                               ins->ws_vindex);
                else
                    ws_fprintf(WS_STDOUT, "\t%s\n", operands[ins->type].name);
            } else
                ws_fatal("ws_asm_print(): unknown operand 0x%x", op);
        }
    }
}


void ws_asm_dasm(WsCompilerPtr compiler, const unsigned char *code, size_t len)
{
    size_t i = 0;

    while (i < len) {
        WsUInt8 byt = code[i];
        WsUInt8 op;
        WsUInt8 arg;
        WsUInt8 i8, j8, k8;
        WsUInt16 i16, j16;

        op = WS_ASM_OP(byt);
        arg = WS_ASM_ARG(byt);

        ws_fprintf(WS_STDOUT, "%4x:\t%-16s", i, WS_OPNAME(op));

        switch (op) {
            /* The `short jumps'. */
        case WS_ASM_JUMP_FW_S:
        case WS_ASM_TJUMP_FW_S:
            ws_fprintf(WS_STDOUT, "%x\n", i + WS_OPSIZE(op) + arg);
            break;

        case WS_ASM_JUMP_BW_S:
            ws_fprintf(WS_STDOUT, "%x\n", i - arg);
            break;

            /* Jumps with WsUInt8 argument. */
        case WS_ASM_JUMP_FW:
        case WS_ASM_TJUMP_FW:
            WS_GET_UINT8(code + i + 1, i8);
            ws_fprintf(WS_STDOUT, "%x\n", i + WS_OPSIZE(op) + i8);
            break;

        case WS_ASM_JUMP_BW:
        case WS_ASM_TJUMP_BW:
            WS_GET_UINT8(code + i + 1, i8);
            ws_fprintf(WS_STDOUT, "%x\n", i - i8);
            break;

            /* Jumps with wide argument. */
        case WS_ASM_JUMP_FW_W:
        case WS_ASM_TJUMP_FW_W:
            WS_GET_UINT16(code + i + 1, i16);
            ws_fprintf(WS_STDOUT, "%x\n", i + WS_OPSIZE(op) + i16);
            break;

        case WS_ASM_JUMP_BW_W:
        case WS_ASM_TJUMP_BW_W:
            WS_GET_UINT16(code + i + 1, i16);
            ws_fprintf(WS_STDOUT, "%x\n", i - i16);
            break;

            /* The `short' opcodes. */
        case WS_ASM_LOAD_VAR_S:
        case WS_ASM_STORE_VAR_S:
        case WS_ASM_INCR_VAR_S:
            ws_fprintf(WS_STDOUT, "%d\n", arg);
            break;

            /* Local script function calls. */
        case WS_ASM_CALL_S:
            ws_fprintf(WS_STDOUT, "%d\n", arg);
            break;

        case WS_ASM_CALL:
            WS_GET_UINT8(code + i + 1, i8);
            ws_fprintf(WS_STDOUT, "%d\n", i8);
            break;

            /* Library calls. */
        case WS_ASM_CALL_LIB_S:
        case WS_ASM_CALL_LIB:
        case WS_ASM_CALL_LIB_W:
            {
                WsUInt8 findex;
                WsUInt16 lindex;
                char lnamebuf[64];
                char fnamebuf[64];
                const char *lname;
                const char *fname;

                if (op == WS_ASM_CALL_LIB_S) {
                    WS_GET_UINT8(code + i + 1, lindex);
                    findex = arg;
                } else if (op == WS_ASM_CALL_LIB) {
                    WS_GET_UINT8(code + i + 1, findex);
                    WS_GET_UINT8(code + i + 2, lindex);
                } else {
                    WS_GET_UINT8(code + i + 1, findex);
                    WS_GET_UINT16(code + i + 2, lindex);
                }

                if (!ws_stdlib_function_name(lindex, findex, &lname, &fname)) {
                    snprintf(lnamebuf, sizeof(lnamebuf), "%d", lindex);
                    snprintf(fnamebuf, sizeof(lnamebuf), "%d", findex);
                    lname = lnamebuf;
                    fname = fnamebuf;
                }
                ws_fprintf(WS_STDOUT, "%s.%s\n", lname, fname);
            }
            break;

            /* URL calls. */
        case WS_ASM_CALL_URL:
            WS_GET_UINT8(code + i + 1, i8);
            WS_GET_UINT8(code + i + 2, j8);
            WS_GET_UINT8(code + i + 3, k8);
            ws_fprintf(WS_STDOUT, "%d.%d %d\n", i8, j8, k8);
            break;

        case WS_ASM_CALL_URL_W:
            WS_GET_UINT16(code + i + 1, i16);
            WS_GET_UINT16(code + i + 3, j16);
            WS_GET_UINT8(code + i + 5, i8);
            ws_fprintf(WS_STDOUT, "%d.%d %d\n", i16, j16, i8);
            break;

            /* Constant access. */
        case WS_ASM_LOAD_CONST_S:
        case WS_ASM_LOAD_CONST:
        case WS_ASM_LOAD_CONST_W:
            if (op == WS_ASM_LOAD_CONST_S)
                i16 = arg;
            else if (op == WS_ASM_LOAD_CONST) {
                WS_GET_UINT8(code + i + 1, i8);
                i16 = i8;
            } else
                WS_GET_UINT16(code + i + 1, i16);

            ws_fprintf(WS_STDOUT, "%d\n", i16);
            break;

            /* Operands with WsUInt8 argument. */
        case WS_ASM_LOAD_VAR:
        case WS_ASM_STORE_VAR:
        case WS_ASM_INCR_VAR:
        case WS_ASM_DECR_VAR:
        case WS_ASM_ADD_ASG:
        case WS_ASM_SUB_ASG:
            WS_GET_UINT8(code + i + 1, i8);
            ws_fprintf(WS_STDOUT, "%d\n", i8);
            break;

            /* The trivial cases. */
        default:
            ws_fprintf(WS_STDOUT, "\n");
            break;
        }

        i += WS_OPSIZE(op);
    }
}


void
ws_asm_linearize(WsCompiler *compiler)
{
    WsAsmIns *ins;
    WsBool process_again = WS_TRUE;

    /* Calculate all offsets and select real assembler instructions for
       our internal pseudo instructions.  This is continued as long as
       the code changes. */
    while (process_again) {
        WsUInt32 offset = 1;

        process_again = WS_FALSE;

        for (ins = compiler->asm_head; ins; ins = ins->next) {
            ins->offset = offset;

            switch (ins->type) {
            case WS_ASM_JUMP_FW_S:
                ins->ws_offset = (ins->ws_label->offset
                                  - (offset + WS_OPSIZE(ins->type)));
                break;

            case WS_ASM_JUMP_FW:
                ins->ws_offset = (ins->ws_label->offset
                                  - (offset + WS_OPSIZE(ins->type)));

                if (ins->ws_offset <= 31) {
                    ins->type = WS_ASM_JUMP_FW_S;
                    process_again = WS_TRUE;
                }
                break;

            case WS_ASM_JUMP_FW_W:
                ins->ws_offset = (ins->ws_label->offset
                                  - (offset + WS_OPSIZE(ins->type)));

                if (ins->ws_offset <= 31) {
                    ins->type = WS_ASM_JUMP_FW_S;
                    process_again = WS_TRUE;
                } else if (ins->ws_offset <= 255) {
                    ins->type = WS_ASM_JUMP_FW;
                    process_again = WS_TRUE;
                }
                break;

            case WS_ASM_JUMP_BW_S:
                ins->ws_offset = offset - ins->ws_label->offset;
                break;

            case WS_ASM_JUMP_BW:
                ins->ws_offset = offset - ins->ws_label->offset;

                if (ins->ws_offset <= 31) {
                    ins->type = WS_ASM_JUMP_BW_S;
                    process_again = WS_TRUE;
                }
                break;

            case WS_ASM_JUMP_BW_W:
                ins->ws_offset = offset - ins->ws_label->offset;

                if (ins->ws_offset <= 31) {
                    ins->type = WS_ASM_JUMP_BW_S;
                    process_again = WS_TRUE;
                } else if (ins->ws_offset <= 255) {
                    ins->type = WS_ASM_JUMP_BW;
                    process_again = WS_TRUE;
                }
                break;

            case WS_ASM_TJUMP_FW_S:
                ins->ws_offset = (ins->ws_label->offset
                                  - (offset + WS_OPSIZE(ins->type)));
                break;

            case WS_ASM_TJUMP_FW:
                ins->ws_offset = (ins->ws_label->offset
                                  - (offset + WS_OPSIZE(ins->type)));

                if (ins->ws_offset <= 31) {
                    ins->type = WS_ASM_TJUMP_FW_S;
                    process_again = WS_TRUE;
                }
                break;

            case WS_ASM_TJUMP_FW_W:
                ins->ws_offset = (ins->ws_label->offset
                                  - (offset + WS_OPSIZE(ins->type)));

                if (ins->ws_offset <= 31) {
                    ins->type = WS_ASM_TJUMP_FW_S;
                    process_again = WS_TRUE;
                } else if (ins->ws_offset <= 255) {
                    ins->type = WS_ASM_TJUMP_FW;
                    process_again = WS_TRUE;
                }
                break;

            case WS_ASM_TJUMP_BW:
                 ins->ws_offset = offset - ins->ws_label->offset;
                 break;

            case WS_ASM_TJUMP_BW_W:
                ins->ws_offset = offset - ins->ws_label->offset;

                if (ins->ws_offset <= 255) {
                    ins->type = WS_ASM_TJUMP_BW;
                    process_again = WS_TRUE;
                }
                break;

                /*
                 * The pseudo instructions.
                 */

            case WS_ASM_P_LABEL:
                /* Nothing here. */
                break;

            case WS_ASM_P_JUMP:
                if (ins->ws_label->offset == 0) {
                    /* A forward jump.  Let's assume the widest form. */
                    ins->type = WS_ASM_JUMP_FW_W;
                } else {
                    ins->ws_offset = offset - ins->ws_label->offset;

                    /* Jump backwards. */
                    if (ins->ws_offset <= 31) {
                        ins->type = WS_ASM_JUMP_BW_S;
                    } else if (ins->ws_offset <= 255) {
                        ins->type = WS_ASM_JUMP_BW;
                    } else {
                        ins->type = WS_ASM_JUMP_BW_W;
                    }
                }
                break;

            case WS_ASM_P_TJUMP:
                if (ins->ws_label->offset == 0) {
                    /* A forward jump.  Let's assume the widest form. */
                    ins->type = WS_ASM_TJUMP_FW_W;
                    process_again = WS_TRUE;
                } else {
                    ins->ws_offset = offset - ins->ws_label->offset;

                    /* Jump backwards. */
                    if (ins->ws_offset <= 255) {
                        ins->type = WS_ASM_TJUMP_BW;
                    } else {
                        ins->type = WS_ASM_TJUMP_BW_W;
                    }
                }
                break;

            case WS_ASM_P_CALL:
                if (ins->ws_findex <= 7) {
                    /* The most compact form. */
                    ins->type = WS_ASM_CALL_S;
                } else {
                    /* The wider form. */
                    ins->type = WS_ASM_CALL;
                }
                break;

            case WS_ASM_P_CALL_LIB:
                if (ins->ws_findex <= 7 && ins->ws_lindex <= 255) {
                    /* The most compact form. */
                    ins->type = WS_ASM_CALL_LIB_S;
                } else if (ins->ws_findex <= 255 && ins->ws_lindex <= 255) {
                    /* The quite compact form. */
                    ins->type = WS_ASM_CALL_LIB;
                } else {
                    /* The most liberal form. */
                    ins->type = WS_ASM_CALL_LIB_W;
                }
                break;

            case WS_ASM_P_CALL_URL:
                if (ins->ws_findex <= 255 && ins->ws_lindex <= 255)
                    /* The compact form. */
                    ins->type = WS_ASM_CALL_URL;
                else
                    ins->type = WS_ASM_CALL_URL_W;
                break;

            case WS_ASM_P_LOAD_VAR:
                if (ins->ws_vindex <= 31)
                    /* The compact form. */
                    ins->type = WS_ASM_LOAD_VAR_S;
                else
                    ins->type = WS_ASM_LOAD_VAR;
                break;

            case WS_ASM_P_STORE_VAR:
                if (ins->ws_vindex <= 15)
                    ins->type = WS_ASM_STORE_VAR_S;
                else
                    ins->type = WS_ASM_STORE_VAR;
                break;

            case WS_ASM_P_INCR_VAR:
                if (ins->ws_vindex <= 7)
                    ins->type = WS_ASM_INCR_VAR_S;
                else
                    ins->type = WS_ASM_INCR_VAR;
                break;

            case WS_ASM_P_LOAD_CONST:
                if (ins->ws_cindex <= 15)
                    ins->type = WS_ASM_LOAD_CONST_S;
                else if (ins->ws_cindex <= 255)
                    ins->type = WS_ASM_LOAD_CONST;
                else
                    ins->type = WS_ASM_LOAD_CONST_W;
                break;
            }

            gw_assert(ins->type == WS_ASM_P_LABEL || ins->type < 0x100);

            if (ins->type != WS_ASM_P_LABEL) {
                gw_assert(operands[ins->type].name != NULL);
                offset += operands[ins->type].size;
            }
        }
    }

    /* Ok, ready to linearize the byte-code. */
    for (ins = compiler->asm_head; ins; ins = ins->next) {
        if (ins->type == WS_ASM_P_LABEL)
            continue;

        gw_assert(ins->type <= 0xff);

        switch (ins->type) {
        case WS_ASM_JUMP_FW_S:
        case WS_ASM_JUMP_BW_S:
        case WS_ASM_TJUMP_FW_S:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE,
                                  WS_ASM_GLUE(ins->type, ins->ws_offset),
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_JUMP_FW:
        case WS_ASM_JUMP_BW:
        case WS_ASM_TJUMP_FW:
        case WS_ASM_TJUMP_BW:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_offset,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_JUMP_FW_W:
        case WS_ASM_JUMP_BW_W:
        case WS_ASM_TJUMP_FW_W:
        case WS_ASM_TJUMP_BW_W:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, ins->type,
                                  WS_ENC_UINT16, (WsUInt16) ins->ws_offset,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL_S:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE,
                                  WS_ASM_GLUE(ins->type, ins->ws_findex),
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_findex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL_LIB_S:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE,
                                  WS_ASM_GLUE(ins->type, ins->ws_findex),
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_lindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL_LIB:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_findex,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_lindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL_LIB_W:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_findex,
                                  WS_ENC_UINT16, (WsUInt16) ins->ws_lindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL_URL:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_lindex,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_findex,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_args,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CALL_URL_W:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT16, (WsUInt16) ins->ws_lindex,
                                  WS_ENC_UINT16, (WsUInt16) ins->ws_findex,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_args,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_LOAD_VAR_S:
        case WS_ASM_STORE_VAR_S:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE,
                                  WS_ASM_GLUE(ins->type, ins->ws_vindex),
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_LOAD_VAR:
        case WS_ASM_STORE_VAR:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_vindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_INCR_VAR_S:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE,
                                  WS_ASM_GLUE(ins->type, ins->ws_vindex),
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_INCR_VAR:
        case WS_ASM_DECR_VAR:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_vindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_LOAD_CONST_S:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE,
                                  WS_ASM_GLUE(ins->type, ins->ws_cindex),
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_LOAD_CONST:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_cindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_LOAD_CONST_W:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT16, (WsUInt16) ins->ws_cindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_ADD_ASG:
        case WS_ASM_SUB_ASG:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_UINT8, (WsUInt8) ins->ws_vindex,
                                  WS_ENC_END))
                goto error;
            break;

        case WS_ASM_CONST_0:
        case WS_ASM_CONST_1:
        case WS_ASM_CONST_M1:
        case WS_ASM_CONST_ES:
        case WS_ASM_CONST_INVALID:
        case WS_ASM_CONST_TRUE:
        case WS_ASM_CONST_FALSE:
        case WS_ASM_INCR:
        case WS_ASM_DECR:
        case WS_ASM_UMINUS:
        case WS_ASM_ADD:
        case WS_ASM_SUB:
        case WS_ASM_MUL:
        case WS_ASM_DIV:
        case WS_ASM_IDIV:
        case WS_ASM_REM:
        case WS_ASM_B_AND:
        case WS_ASM_B_OR:
        case WS_ASM_B_XOR:
        case WS_ASM_B_NOT:
        case WS_ASM_B_LSHIFT:
        case WS_ASM_B_RSSHIFT:
        case WS_ASM_B_RSZSHIFT:
        case WS_ASM_EQ:
        case WS_ASM_LE:
        case WS_ASM_LT:
        case WS_ASM_GE:
        case WS_ASM_GT:
        case WS_ASM_NE:
        case WS_ASM_NOT:
        case WS_ASM_SCAND:
        case WS_ASM_SCOR:
        case WS_ASM_TOBOOL:
        case WS_ASM_POP:
        case WS_ASM_TYPEOF:
        case WS_ASM_ISVALID:
        case WS_ASM_RETURN:
        case WS_ASM_RETURN_ES:
        case WS_ASM_DEBUG:
            if (!ws_encode_buffer(&compiler->byte_code,
                                  WS_ENC_BYTE, (WsByte) ins->type,
                                  WS_ENC_END))
                goto error;
            break;

        default:
            ws_fatal("ws_asm_linearize(): unknown instruction 0x%02x",
                     ins->type);
            break;
        }
    }

    /*
     * Avoid generating 0-length functions, because not all clients
     * handle them correctly.
     */
    if (ws_buffer_len(&compiler->byte_code) == 0) {
	if (!ws_encode_buffer(&compiler->byte_code,
	   		      WS_ENC_BYTE, (WsByte) WS_ASM_RETURN_ES,
			      WS_ENC_END))
	    goto error;
    }

    return;

    /*
     * Error handling.
     */

error:

    ws_error_memory(compiler);
    return;
}


/* Contructors for assembler instructions. */

static WsAsmIns *asm_alloc(WsCompiler *compiler, WsUInt16 type, WsUInt32 line)
{
    WsAsmIns *ins = ws_f_calloc(compiler->pool_asm, 1, sizeof(*ins));

    if (ins == NULL)
        ws_error_memory(compiler);
    else {
        ins->type = type;
        ins->line = line;
    }

    return ins;
}


WsAsmIns *ws_asm_label(WsCompiler *compiler, WsUInt32 line)
{
    WsAsmIns *ins = asm_alloc(compiler, WS_ASM_P_LABEL, line);

    if (ins)
        ins->ws_label_idx = compiler->next_label++;

    return ins;
}


WsAsmIns *ws_asm_branch(WsCompiler *compiler, WsUInt32 line, WsUInt16 inst,
                        WsAsmIns *label)
{
    WsAsmIns *ins = asm_alloc(compiler, inst, line);

    if (ins) {
        ins->ws_label = label;
        label->ws_label_refcount++;
    }

    return ins;
}


WsAsmIns *ws_asm_call(WsCompiler *compiler, WsUInt32 line, WsUInt8 findex)
{
    WsAsmIns *ins = asm_alloc(compiler, WS_ASM_P_CALL, line);

    if (ins)
        ins->ws_findex = findex;

    return ins;
}


WsAsmIns *ws_asm_call_lib(WsCompiler *compiler, WsUInt32 line, WsUInt8 findex,
                WsUInt16 lindex)
{
    WsAsmIns *ins = asm_alloc(compiler, WS_ASM_P_CALL_LIB, line);

    if (ins) {
        ins->ws_findex = findex;
        ins->ws_lindex = lindex;
    }

    return ins;
}


WsAsmIns *ws_asm_call_url(WsCompiler *compiler, WsUInt32 line, WsUInt16 findex,
                          WsUInt16 urlindex, WsUInt8 args)
{
    WsAsmIns *ins = asm_alloc(compiler, WS_ASM_P_CALL_URL, line);

    if (ins) {
        ins->ws_findex = findex;
        ins->ws_lindex = urlindex;
        ins->ws_args = args;
    }

    return ins;
}


WsAsmIns *ws_asm_variable(WsCompiler *compiler, WsUInt32 line, WsUInt16 inst,
                          WsUInt8 vindex)
{
    WsAsmIns *ins = asm_alloc(compiler, inst, line);

    if (ins)
        ins->ws_vindex = vindex;

    return ins;
}


WsAsmIns *ws_asm_load_const(WsCompiler *compiler, WsUInt32 line,
                            WsUInt16 cindex)
{
    WsAsmIns *ins = asm_alloc(compiler, WS_ASM_P_LOAD_CONST, line);

    if (ins)
        ins->ws_cindex = cindex;

    return ins;
}


WsAsmIns *ws_asm_ins(WsCompiler *compiler, WsUInt32 line, WsUInt8 opcode)
{
    return asm_alloc(compiler, opcode, line);
}
