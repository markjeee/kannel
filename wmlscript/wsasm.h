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
 * wsasm.h
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Byte-code assembler definitions.
 *
 */

#ifndef WSASM_H
#define WSASM_H

/********************* Opcodes for the ASM instructions *****************/

/* The instruction classes. */

/* Class 1: 1xxPPPPP */

#define WS_ASM_CLASS1P(op)	(((op) & 0x80) == 0x80)
#define WS_ASM_CLASS1_OP(op)	((op) & 0xe0)
#define WS_ASM_CLASS1_ARG(op)	((op) & 0x1f)

/* Class 2: 010xPPPP */

#define WS_ASM_CLASS2P(op)	(((op) & 0xe0) == 0x40)
#define WS_ASM_CLASS2_OP(op)	((op) & 0xf0)
#define WS_ASM_CLASS2_ARG(op)	((op) & 0x0f)

/* Class 3: 011xxPPP */

#define WS_ASM_CLASS3P(op)	(((op) & 0xe0) == 0x60)
#define WS_ASM_CLASS3_OP(op)	((op) & 0x78)
#define WS_ASM_CLASS3_ARG(op)	((op) & 0x07)

/* Class 4: 00xxxxxx */

#define WS_ASM_CLASS4P(op)	(((op) & 0xc0) == 0x00)
#define WS_ASM_CLASS4_OP(op)	(op)

/* Get the opcode of the operand `op'.  The operand `op' can belong to
   any of the classes 1-4. */
#define WS_ASM_OP(op)		\
    (WS_ASM_CLASS1P(op)		\
     ? WS_ASM_CLASS1_OP(op)	\
     : (WS_ASM_CLASS2P(op)	\
       ? WS_ASM_CLASS2_OP(op)	\
       : (WS_ASM_CLASS3P(op)	\
         ? WS_ASM_CLASS3_OP(op) \
         : WS_ASM_CLASS4_OP(op))))

/* Get the implicit argument of the operand `op'.  The operand `op'
   can belong to any of the classes 1-4.  For the class 4 operands,
   this returns 0. */
#define WS_ASM_ARG(op)			\
    (WS_ASM_CLASS1P(op)			\
     ? WS_ASM_CLASS1_ARG(op)		\
     : (WS_ASM_CLASS2P(op)		\
       ? WS_ASM_CLASS2_ARG(op)		\
       : (WS_ASM_CLASS3P(op)		\
         ? WS_ASM_CLASS3_ARG(op)	\
         : 0)))

/* Create an operand with implicit argument from the operand `op' and
   argument `arg'. */
#define WS_ASM_GLUE(op, arg)	((WsByte) (((WsByte) (op)) | ((WsByte) (arg))))


/* The instruction opcodes.             Opcode  Binary	 Size	*/

#define WS_ASM_JUMP_FW_S		0x80 /* 10000000   1	*/
#define WS_ASM_JUMP_FW			0x01 /* 00000001   2	*/
#define WS_ASM_JUMP_FW_W		0x02 /* 00000010   3	*/

#define WS_ASM_JUMP_BW_S		0xa0 /* 10100000   1	*/
#define WS_ASM_JUMP_BW			0x03 /* 00000011   2	*/
#define WS_ASM_JUMP_BW_W		0x04 /* 00000100   3	*/

#define WS_ASM_TJUMP_FW_S		0xc0 /* 11000000   1	*/
#define WS_ASM_TJUMP_FW			0x05 /* 00000101   2	*/
#define WS_ASM_TJUMP_FW_W		0x06 /* 00000110   3	*/

#define WS_ASM_TJUMP_BW			0x07 /* 00000111   2	*/
#define WS_ASM_TJUMP_BW_W		0x08 /* 00001000   3	*/

#define WS_ASM_CALL_S			0x60 /* 01100000   1	*/
#define WS_ASM_CALL			0x09 /* 00001001   2	*/

#define WS_ASM_CALL_LIB_S		0x68 /* 01101000   2	*/
#define WS_ASM_CALL_LIB			0x0a /* 00001010   3	*/
#define WS_ASM_CALL_LIB_W		0x0b /* 00001011   4	*/

#define WS_ASM_CALL_URL			0x0c /* 00001100   4	*/
#define WS_ASM_CALL_URL_W		0x0d /* 00001101   6	*/

#define WS_ASM_LOAD_VAR_S		0xe0 /* 11100000   1	*/
#define WS_ASM_LOAD_VAR			0x0e /* 00001110   2	*/

#define WS_ASM_STORE_VAR_S		0x40 /* 01000000   1	*/
#define WS_ASM_STORE_VAR		0x0f /* 00001111   2	*/

#define WS_ASM_INCR_VAR_S		0x70 /* 01110000   1	*/
#define WS_ASM_INCR_VAR			0x10 /* 00010000   2	*/

#define WS_ASM_DECR_VAR			0x11 /* 00010001   2	*/

#define WS_ASM_LOAD_CONST_S		0x50 /* 01010000   1	*/
#define WS_ASM_LOAD_CONST		0x12 /* 00010010   2	*/
#define WS_ASM_LOAD_CONST_W		0x13 /* 00010011   3	*/

#define WS_ASM_CONST_0			0x14 /* 00010100   1	*/
#define WS_ASM_CONST_1			0x15 /* 00010101   1	*/
#define WS_ASM_CONST_M1			0x16 /* 00010110   1	*/
#define WS_ASM_CONST_ES			0x17 /* 00010111   1	*/
#define WS_ASM_CONST_INVALID		0x18 /* 00011000   1	*/
#define WS_ASM_CONST_TRUE		0x19 /* 00011001   1	*/
#define WS_ASM_CONST_FALSE		0x1a /* 00011010   1	*/

#define WS_ASM_INCR			0x1b /* 00011011   1	*/
#define WS_ASM_DECR			0x1c /* 00011100   1	*/
#define WS_ASM_ADD_ASG			0x1d /* 00011101   2	*/
#define WS_ASM_SUB_ASG			0x1e /* 00011110   2	*/
#define WS_ASM_UMINUS 			0x1f /* 00011111   1	*/
#define WS_ASM_ADD			0x20 /* 00100000   1	*/
#define WS_ASM_SUB			0x21 /* 00100001   1	*/
#define WS_ASM_MUL			0x22 /* 00100010   1	*/
#define WS_ASM_DIV			0x23 /* 00100011   1	*/
#define WS_ASM_IDIV			0x24 /* 00100100   1	*/
#define WS_ASM_REM 			0x25 /* 00100101   1	*/

#define WS_ASM_B_AND			0x26 /* 00100110   1	*/
#define WS_ASM_B_OR 			0x27 /* 00100111   1	*/
#define WS_ASM_B_XOR 			0x28 /* 00101000   1	*/
#define WS_ASM_B_NOT 			0x29 /* 00101001   1	*/
#define WS_ASM_B_LSHIFT			0x2a /* 00101010   1	*/
#define WS_ASM_B_RSSHIFT		0x2b /* 00101011   1	*/
#define WS_ASM_B_RSZSHIFT		0x2c /* 00101100   1	*/

#define WS_ASM_EQ			0x2d /* 00101101   1	*/
#define WS_ASM_LE			0x2e /* 00101110   1	*/
#define WS_ASM_LT			0x2f /* 00101111   1	*/
#define WS_ASM_GE			0x30 /* 00110000   1	*/
#define WS_ASM_GT			0x31 /* 00110001   1	*/
#define WS_ASM_NE			0x32 /* 00110010   1	*/

#define WS_ASM_NOT			0x33 /* 00110011   1	*/
#define WS_ASM_SCAND			0x34 /* 00110100   1	*/
#define WS_ASM_SCOR 			0x35 /* 00110101   1	*/
#define WS_ASM_TOBOOL			0x36 /* 00110110   1	*/

#define WS_ASM_POP			0x37 /* 00110111   1	*/

#define WS_ASM_TYPEOF			0x38 /* 00111000   1	*/
#define WS_ASM_ISVALID			0x39 /* 00111001   1	*/

#define WS_ASM_RETURN 			0x3a /* 00111010   1	*/
#define WS_ASM_RETURN_ES		0x3b /* 00111011   1	*/

#define WS_ASM_DEBUG			0x3c /* 00111100   1	*/

/********************* Pseudo opcodes for assembler *********************/

/* These are pseudo opcodes grouping together several real byte-code
   opcodes.  These are used in the symbolic assembler. */

#define WS_ASM_P_LABEL		0x0100
#define WS_ASM_P_JUMP		0x0200
#define WS_ASM_P_TJUMP		0x0300
#define WS_ASM_P_CALL		0x0400
#define WS_ASM_P_CALL_LIB	0x0500
#define WS_ASM_P_CALL_URL	0x0600
#define WS_ASM_P_LOAD_VAR	0x0700
#define WS_ASM_P_STORE_VAR	0x0800
#define WS_ASM_P_INCR_VAR	0x0900
#define WS_ASM_P_LOAD_CONST	0x0a00

/* Check whether the instruction `ins' is a pseudo-branch
   instruction. */
#define WS_ASM_P_BRANCH(ins) \
    ((ins)->type == WS_ASM_P_JUMP || (ins)->type == WS_ASM_P_TJUMP)

/********************* Symbolic assembler instructions ******************/

#define ws_label_idx		u.ivalues.i1
#define ws_label_refcount	u.ivalues.i2
#define ws_findex		u.ivalues.i1
#define ws_lindex		u.ivalues.i2
#define ws_args			u.ivalues.i3
#define ws_vindex		u.ivalues.i1
#define ws_cindex		u.ivalues.i1
#define ws_label		u.branch.label
#define ws_offset		u.branch.offset

struct WsAsmInsRec
{
    struct WsAsmInsRec *next;
    struct WsAsmInsRec *prev;
    WsUInt16 type;

    /* The source stream line number. */
    WsUInt32 line;

    /* The operands offset in the linearized byte-code stream. */
    WsUInt32 offset;

    union
    {
        /* The target label for branch instructions. */
        struct
        {
            struct WsAsmInsRec *label;

            /* The offset argument of the branch operand.  This is the
               adjustment that must be performed for the pc after this
               instruction. */
            WsUInt32 offset;
        }
        branch;

        struct
        {
            WsUInt32 i1;
            WsUInt16 i2;
            WsUInt16 i3;
        }
        ivalues;
    } u;
};

typedef struct WsAsmInsRec WsAsmIns;


/* Link the instruction `ins' to the end of the symbolic assembler
   chain, currently being constructed in `compiler'. */
void ws_asm_link(WsCompilerPtr compiler, WsAsmIns *ins);

/* Print the current assembler instructions of the compiler
   `compiler'. */
void ws_asm_print(WsCompilerPtr compiler);

/* Disassemble the byte-code `code', `len' to the standard output. */
void ws_asm_dasm(WsCompilerPtr compiler, const unsigned char *code,
                 size_t len);

/* Linearize the assembler, currently being constructed in `compiler',
   into `compiler->byte_code'. */
void ws_asm_linearize(WsCompilerPtr compiler);

/* Constructors for assembler instructions. */

/* Create a label instruction. */
WsAsmIns *ws_asm_label(WsCompilerPtr compiler, WsUInt32 line);

/* Create a branch instruction `ins' to label `label'. */
WsAsmIns *ws_asm_branch(WsCompilerPtr compiler, WsUInt32 line, WsUInt16 ins,
                        WsAsmIns *label);

/* Create a local call instruction to function `findex'. */
WsAsmIns *ws_asm_call(WsCompilerPtr compiler, WsUInt32 line, WsUInt8 findex);

/* Create a library call instruction to function `findex' from the
   libary `lindex'. */
WsAsmIns *ws_asm_call_lib(WsCompilerPtr compiler, WsUInt32 line,
                          WsUInt8 findex, WsUInt16 lindex);

/* Create an URL call instruction for function `findex' from the URL
   `urlindex' with `args' arguments.  The arguments `urlindex' and
   `findex' pont to the constant pool. */
WsAsmIns *ws_asm_call_url(WsCompilerPtr compiler, WsUInt32 line,
                          WsUInt16 findex, WsUInt16 urlindex, WsUInt8 args);

/* Create a variable modification instruction `ins' for the variable
   `vindex'. */
WsAsmIns *ws_asm_variable(WsCompilerPtr compiler, WsUInt32 line, WsUInt16 ins,
                          WsUInt8 vindex);

/* Create a constant loading instruction for the constant `cindex'. */
WsAsmIns *ws_asm_load_const(WsCompilerPtr compiler, WsUInt32 line,
                            WsUInt16 cindex);

/* Create an instruction `ins'. */
WsAsmIns *ws_asm_ins(WsCompilerPtr compiler, WsUInt32 line, WsUInt8 opcode);

#endif /* not WSASM_H */
