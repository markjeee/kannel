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
 * wsstree.c
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Syntax tree creation, manipulation and byte-code assembler
 * generation.
 *
 */

#include "wsint.h"
#include "wsgram.h"

/* TODO: Constant folding. */

/********************* Misc syntax tree structures **********************/

WsVarDec *ws_variable_declaration(WsCompilerPtr compiler,
                                  char *name, WsExpression *expr)
{
    WsVarDec *vardec = ws_f_malloc(compiler->pool_stree, sizeof(*vardec));

    if (vardec == NULL)
        ws_error_memory(compiler);
    else {
        vardec->name = name;
        vardec->expr = expr;
    }

    return vardec;
}

WsFormalParm *ws_formal_parameter(WsCompilerPtr compiler,
                                  WsUInt32 line, char *name)
{
    WsFormalParm *parm = ws_f_malloc(compiler->pool_stree, sizeof(*parm));

    if (parm == NULL)
        ws_error_memory(compiler);
    else {
        parm->line = line;
        parm->name = name;
    }

    return parm;
}

/********************* Linked list **************************************/

WsList *ws_list_new(WsCompiler *compiler)
{
    WsList *list = ws_f_calloc(compiler->pool_stree, 1, sizeof(*list));

    if (list == NULL)
        ws_error_memory(compiler);

    return list;
}


void ws_list_append(WsCompiler *compiler, WsList *list, void *value)
{
    WsListItem *item;

    if (list == NULL)
        /* A recovery code for previous memory allocation problems. */
        return;

    item = ws_f_calloc(compiler->pool_stree, 1, sizeof(*item));
    if (item == NULL) {
        ws_error_memory(compiler);
        return;
    }

    item->data = value;

    if (list->tail) {
        list->tail->next = item;
        list->tail = item;
    } else
        list->head = list->tail = item;

    list->num_items++;
}

/********************* Namespace for arguments and locals ***************/

static void variable_hash_destructor(void *item, void *context)
{
    ws_free(item);
}


WsHashPtr ws_variable_hash_create(void)
{
    return ws_hash_create(variable_hash_destructor, NULL);
}


WsNamespace *ws_variable_define(WsCompilerPtr compiler, WsUInt32 line,
                                WsBool variablep, char *name)
{
    WsNamespace *ns;

    /* Is the symbol already defined? */
    ns = ws_hash_get(compiler->variables_hash, name);
    if (ns) {
        ws_src_error(compiler, line, "redeclaration of `%s'", name);
        ws_src_error(compiler, ns->line, "`%s' previously declared here", name);
        return NULL;
    }

    /* Can we still define more variables? */
    if (compiler->next_vindex > 255) {
        /* No we can't. */
        ws_src_error(compiler, line, "too many local variables");
        return NULL;
    }

    ns = ws_calloc(1, sizeof(*ns));
    if (ns == NULL) {
        ws_error_memory(compiler);
        return NULL;
    }

    ns->line = line;
    ns->vindex = compiler->next_vindex++;

    if (!ws_hash_put(compiler->variables_hash, name, ns)) {
        ws_free(ns);
        ws_error_memory(compiler);
        return NULL;
    }

    return ns;
}


WsNamespace *ws_variable_lookup(WsCompilerPtr compiler, char *name)
{
    return ws_hash_get(compiler->variables_hash, name);
}

/********************* Top-level declarations ***************************/

/* External compilation units. */

static void pragma_use_hash_destructor(void *item, void *context)
{
    ws_free(item);
}


WsHashPtr ws_pragma_use_hash_create(void)
{
    return ws_hash_create(pragma_use_hash_destructor, NULL);
}


void ws_pragma_use(WsCompilerPtr compiler, WsUInt32 line, char *identifier,
                   WsUtf8String *url)
{
    WsPragmaUse *u = ws_calloc(1, sizeof(*u));
    WsPragmaUse *uold;

    /* Do we already know this pragma? */
    uold = ws_hash_get(compiler->pragma_use_hash, identifier);
    if (uold) {
        ws_src_error(compiler, line, "redefinition of pragma `%s'", identifier);
        ws_src_error(compiler, uold->line, "`%s' previously defined here",
                     identifier);
        goto error_cleanup;
    }

    if (u == NULL)
        goto error;

    u->line = line;

    /* Insert the URL to the byte-code module. */
    if (!ws_bc_add_const_utf8_string(compiler->bc, &u->urlindex, url->data,
                                     url->len))
        goto error;

    /* Add it to the use pragma hash. */
    if (!ws_hash_put(compiler->pragma_use_hash, identifier, u))
        goto error;

    /* Cleanup. */

    ws_lexer_free_block(compiler, identifier);
    ws_lexer_free_utf8(compiler, url);

    return;

    /* Error handling. */

error:

    ws_error_memory(compiler);

error_cleanup:

    ws_free(u);
    ws_lexer_free_block(compiler, identifier);
    ws_lexer_free_utf8(compiler, url);
}

/* MetaBody of the `use meta' pragmas. */

WsPragmaMetaBody *ws_pragma_meta_body(WsCompilerPtr compiler,
                                      WsUtf8String *property_name,
                                      WsUtf8String *content,
                                      WsUtf8String *scheme)
{
    WsPragmaMetaBody *mb = ws_calloc(1, sizeof(*mb));

    if (mb == NULL) {
        ws_error_memory(compiler);
        return NULL;
    }

    mb->property_name = property_name;
    mb->content = content;
    mb->scheme = scheme;

    return mb;
}


void ws_pragma_meta_body_free(WsCompilerPtr compiler, WsPragmaMetaBody *mb)
{
    if (mb == NULL)
        return;

    ws_lexer_free_utf8(compiler, mb->property_name);
    ws_lexer_free_utf8(compiler, mb->content);
    ws_lexer_free_utf8(compiler, mb->scheme);

    ws_free(mb);
}


/* Functions. */

static void function_hash_destructor(void *item, void *context)
{
    ws_free(item);
}


WsHashPtr ws_function_hash_create(void)
{
    return ws_hash_create(function_hash_destructor, NULL);
}


WsFunctionHash *ws_function_hash(WsCompilerPtr compiler, char *name)
{
    WsFunctionHash *i = ws_hash_get(compiler->functions_hash, name);

    if (i)
        return i;

    /* Must create a new mapping. */

    i = ws_calloc(1, sizeof(*i));
    if (i == NULL) {
        ws_error_memory(compiler);
        return NULL;
    }

    if (!ws_hash_put(compiler->functions_hash, name, i)) {
        ws_free(i);
        ws_error_memory(compiler);
        return NULL;
    }

    return i;
}


void ws_function(WsCompiler *compiler, WsBool externp, char *name,
                 WsUInt32 line, WsList *params, WsList *block)
{
    WsFunctionHash *hash;
    WsFunction *f = ws_realloc(compiler->functions,
                               ((compiler->num_functions + 1)
                                * sizeof(WsFunction)));

    if (f == NULL) {
        ws_free(name);
        ws_error_memory(compiler);
        return;
    }

    if (externp)
        compiler->num_extern_functions++;
    else
        compiler->num_local_functions++;

    compiler->functions = f;
    f = &compiler->functions[compiler->num_functions];

    f->findex = compiler->num_functions++;

    f->externp = externp;
    f->name = name;
    f->line = line;
    f->params = params;
    f->block = block;

    /* Update the function name hash. */

    hash = ws_function_hash(compiler, name);
    if (hash == NULL) {
        ws_error_memory(compiler);
        return;
    }

    if (hash->defined) {
        ws_src_error(compiler, line, "redefinition of `%s'", name);
        ws_src_error(compiler,
                     compiler->functions[hash->findex].line,
                     "`%s' previously defined here", name);
        return;
    }

    hash->defined = WS_TRUE;
    hash->findex = f->findex;
}

/********************* Expressions **************************************/

void ws_expr_linearize(WsCompiler *compiler, WsExpression *expr)
{
    WsListItem *li;
    WsAsmIns *ins;

    switch (expr->type) {
    case WS_EXPR_COMMA:
        /* Linearize left. */
        ws_expr_linearize(compiler, expr->u.comma.left);

        /* Pop its result. */
        ws_asm_link(compiler, ws_asm_ins(compiler, expr->line, WS_ASM_POP));

        /* Linearize right */
        ws_expr_linearize(compiler, expr->u.comma.right);
        break;

    case WS_EXPR_ASSIGN:
        {
            WsNamespace *ns = ws_variable_lookup(compiler,
                                                 expr->u.assign.identifier);

            if (ns == NULL) {
                /* Unknown identifier. */
                ws_src_error(compiler, expr->line, "unknown variable `%s'",
                             expr->u.symbol);
                return;
            }

            if (expr->u.assign.op == '=') {
                /* Evaluate the expression. */
                ws_expr_linearize(compiler, expr->u.assign.expr);

                /* Store the value to the variable. */
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line,
                                            WS_ASM_P_STORE_VAR, ns->vindex));
            } else if (expr->u.assign.op == tADDA) {
                /* Linearize the expression. */
                ws_expr_linearize(compiler, expr->u.assign.expr);

                /* Add it to the variable. */
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line,
                                            WS_ASM_ADD_ASG, ns->vindex));
            } else if (expr->u.assign.op == tSUBA) {
                /* Linearize the expression. */
                ws_expr_linearize(compiler, expr->u.assign.expr);

                /* Substract it from the variable. */
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line,
                                            WS_ASM_SUB_ASG, ns->vindex));
            } else {
                /* Load the old value from the variable. */
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line,
                                            WS_ASM_P_LOAD_VAR, ns->vindex));

                /* Evaluate the expression. */
                ws_expr_linearize(compiler, expr->u.assign.expr);

                /* Perform the operand. */
                ins = NULL;
                switch (expr->u.assign.op) {
                case tMULA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_MUL);
                    break;

                case tDIVA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_DIV);
                    break;

                case tREMA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_REM);
                    break;

                case tADDA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_ADD);
                    break;

                case tSUBA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_SUB);
                    break;

                case tLSHIFTA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_B_LSHIFT);
                    break;

                case tRSSHIFTA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_B_RSSHIFT);
                    break;

                case tRSZSHIFTA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_B_RSZSHIFT);
                    break;

                case tANDA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_B_AND);
                    break;

                case tXORA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_B_XOR);
                    break;

                case tORA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_B_OR);
                    break;

                case tIDIVA:
                    ins = ws_asm_ins(compiler, expr->line, WS_ASM_IDIV);
                    break;

                default:
                    ws_fatal("ws_expr_linearize(): unknown assignment operand %x",
                             expr->u.assign.op);
                    break;
                }
                ws_asm_link(compiler, ins);

                /* Store the value to the variable. */
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line,
                                            WS_ASM_P_STORE_VAR, ns->vindex));
            }
            /* The value of the assignment expression is the value
               assigned.  So, we must load the value from the variable.
               This would also be a good place for the `dup' operand but
               we lose since we don't have it. */
            ws_asm_link(compiler, ws_asm_variable(compiler, expr->line,
                                                  WS_ASM_P_LOAD_VAR, ns->vindex));
        }
        break;

    case WS_EXPR_CONDITIONAL:
        {
            WsAsmIns *l_else = ws_asm_label(compiler, expr->line);
            WsAsmIns *l_end = ws_asm_label(compiler, expr->line);

            /* Linearize condition. */
            ws_expr_linearize(compiler, expr->u.conditional.e_cond);

            /* If the result if false, jump to the else-branch. */
            ws_asm_link(compiler, ws_asm_branch(compiler, expr->line,
                                                WS_ASM_P_TJUMP, l_else));

            /* Linearize the then-expression and jump out. */
            ws_expr_linearize(compiler, expr->u.conditional.e_then);
            ws_asm_link(compiler, ws_asm_branch(compiler, expr->line,
                                                WS_ASM_P_JUMP, l_end));

            /* The else-branch. */
            ws_asm_link(compiler, l_else);
            ws_expr_linearize(compiler, expr->u.conditional.e_else);

            /* Insert the end label. */
            ws_asm_link(compiler, l_end);
        }
        break;

    case WS_EXPR_LOGICAL:
        {
            WsAsmIns *l_out = ws_asm_label(compiler, expr->line);

            /* Linearize the left-hand size expression. */
            ws_expr_linearize(compiler, expr->u.logical.left);

            /* Short-circuit check.  The type of the logical expression is
                      the short-circuit byte-code operand. */
            ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                             expr->u.logical.type));
            ws_asm_link(compiler, ws_asm_branch(compiler, expr->line,
                                                WS_ASM_P_TJUMP, l_out));

            /* Linearize the right-hand size expression. */
            ws_expr_linearize(compiler, expr->u.logical.right);

            /* The result of a logical expression should be boolean.
             * Control statements do automatic conversion, but typeof()
	     * does not. */
            ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                         WS_ASM_TOBOOL));

            /* Insert the end label. */
            ws_asm_link(compiler, l_out);
        }
        break;

    case WS_EXPR_BINARY:
        /* Linearize left and right. */
        ws_expr_linearize(compiler, expr->u.binary.left);
        ws_expr_linearize(compiler, expr->u.binary.right);

        /* The type of the binary expression is the byte-code opcode. */
        ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                         expr->u.binary.type));
        break;

    case WS_EXPR_UNARY:
        /* Linearize the expression. */
        ws_expr_linearize(compiler, expr->u.unary.expr);

        /* The type of the unary expression is the byte-code opcode. */
        ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                         expr->u.unary.type));
        break;

    case WS_EXPR_UNARY_VAR:
        {
            WsNamespace *ns = ws_variable_lookup(compiler,
                                                 expr->u.unary_var.variable);
            if (ns == NULL) {
                /* An unknown identifier. */
                ws_src_error(compiler, expr->line, "unknown variable `%s'",
                             expr->u.unary_var.variable);
                return;
            }

            /* First, do the operation. */
            if (expr->u.unary_var.addp)
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line, WS_ASM_P_INCR_VAR,
                                            ns->vindex));
            else
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line, WS_ASM_DECR_VAR,
                                            ns->vindex));

            /* Second, load the new value of the variable. */
            ws_asm_link(compiler, ws_asm_variable(compiler, expr->line,
                                                  WS_ASM_P_LOAD_VAR, ns->vindex));
        }
        break;

    case WS_EXPR_POSTFIX_VAR:
        {
            WsNamespace *ns = ws_variable_lookup(compiler,
                                                 expr->u.postfix_var.variable);
            if (ns == NULL) {
                /* An unknown identifier. */
                ws_src_error(compiler, expr->line, "unknown variable `%s'",
                             expr->u.postfix_var.variable);
                return;
            }

            /* First, load the old value of the variable. */
            ws_asm_link(compiler, ws_asm_variable(compiler, expr->line,
                                                  WS_ASM_P_LOAD_VAR, ns->vindex));

            /* Second, do the operation. */
            if (expr->u.unary_var.addp)
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line, WS_ASM_P_INCR_VAR,
                                            ns->vindex));
            else
                ws_asm_link(compiler,
                            ws_asm_variable(compiler, expr->line, WS_ASM_DECR_VAR,
                                            ns->vindex));
        }
        break;

    case WS_EXPR_CALL:
        /* First, evaluate the arguments. */
        for (li = expr->u.call.arguments->head; li; li = li->next)
            ws_expr_linearize(compiler, li->data);

        /* Second, emit the call instruction. */
        switch (expr->u.call.type) {
        case ' ': 		/* LocalScriptFunctionCall */
            {
                WsFunctionHash *f = ws_function_hash(compiler, expr->u.call.name);

                if (f == NULL || !f->defined)
                {
                    ws_src_error(compiler, expr->line,
                                 "unknown local function `%s'",
                                 expr->u.call.name);
                    return;
                }

                /* Check that the function is called with correct amount
                          of arguments. */
                if (expr->u.call.arguments->num_items
                    != compiler->functions[f->findex].params->num_items)
                {
                    ws_src_error(compiler, expr->line,
                                 "invalid amount of arguments for `%s': "
                                 "expected %u, got %u",
                                 expr->u.call.name,
                                 compiler->functions[f->findex].params->num_items,
                                 expr->u.call.arguments->num_items);
                    return;
                }

                /* Emit assembler. */
                ws_asm_link(compiler, ws_asm_call(compiler, expr->line,
                                                  f->findex));
            }
            break;

        case '#': 		/* ExternalScriptFunctionCall */
            {
                WsPragmaUse *use = ws_hash_get(compiler->pragma_use_hash,
                                               expr->u.call.base);
                WsUInt16 findex;

                if (use == NULL)
                {
                    ws_src_error(compiler, expr->line,
                                 "unknown external compilation unit `%s'",
                                 expr->u.call.base);
                    return;
                }

                /* Insert the function name to the byte-code pool. */
                if (!ws_bc_add_const_utf8_string(
                        compiler->bc, &findex,
                        (unsigned char *) expr->u.call.name,
                        strlen(expr->u.call.name)))
                {
                    ws_error_memory(compiler);
                    return;
                }

                /* Emit assembler. */
                ws_asm_link(compiler,
                            ws_asm_call_url(compiler, expr->line,
                                            findex, use->urlindex,
                                            expr->u.call.arguments->num_items));
            }
            break;

        case '.': 		/* LibraryFunctionCall */
            {
                WsUInt16 lindex;
                WsUInt8 findex;
                WsUInt8 num_args;
                WsBool lindex_found;
                WsBool findex_found;

                if (!ws_stdlib_function(expr->u.call.base, expr->u.call.name,
                                        &lindex, &findex, &num_args,
                                        &lindex_found, &findex_found))
                {
                    if (!lindex_found)
                        ws_src_error(compiler, expr->line,
                                     "unknown system library `%s'",
                                     expr->u.call.base);
                    else
                        ws_src_error(compiler, expr->line,
                                     "unknown library function `%s.%s'",
                                     expr->u.call.base, expr->u.call.name);

                    return;
                }
                /* Check the argument count. */
                if (expr->u.call.arguments->num_items != num_args)
                {
                    ws_src_error(compiler, expr->line,
                                 "invalid amount of arguments for `%s.%s': "
                                 "expected %u, got %u",
                                 expr->u.call.base, expr->u.call.name,
                                 num_args, expr->u.call.arguments->num_items);
                    return;
                }

                /* Emit assembler. */
                ws_asm_link(compiler, ws_asm_call_lib(compiler, expr->line, findex,
                                                      lindex));
            }
            break;

        default:
            ws_fatal("ws_expr_linearize(): unknown call expression type %x",
                     expr->u.call.type);
            break;
        }
        break;

    case WS_EXPR_SYMBOL:
        {
            WsNamespace *ns = ws_variable_lookup(compiler, expr->u.symbol);

            if (ns == NULL) {
                /* An unknown identifier. */
                ws_src_error(compiler, expr->line, "unknown variable `%s'",
                             expr->u.symbol);
                return;
            }

            /* Create a load instruction for the variable. */
            ws_asm_link(compiler, ws_asm_variable(compiler, expr->line,
                                                  WS_ASM_P_LOAD_VAR, ns->vindex));
        }
        break;

    case WS_EXPR_CONST_INVALID:
        ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                         WS_ASM_CONST_INVALID));
        break;

    case WS_EXPR_CONST_TRUE:
        ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                         WS_ASM_CONST_TRUE));
        break;

    case WS_EXPR_CONST_FALSE:
        ws_asm_link(compiler, ws_asm_ins(compiler, expr->line,
                                         WS_ASM_CONST_FALSE));
        break;


    case WS_EXPR_CONST_INTEGER:
        if (expr->u.integer.ival == 0)
            ins = ws_asm_ins(compiler, expr->line, WS_ASM_CONST_0);
        else if (expr->u.integer.ival == 1 && expr->u.integer.sign == 1)
            ins = ws_asm_ins(compiler, expr->line, WS_ASM_CONST_1);
        else {
            WsUInt16 cindex;
	    WsInt32 ival;

            if (expr->u.integer.sign >= 0) {
		if (expr->u.integer.ival > (WsUInt32) WS_INT32_MAX)
		    ws_src_error(compiler, expr->line,
                                 "integer literal too large");
                ival = expr->u.integer.ival;
	    } else {
                if (expr->u.integer.ival > (WsUInt32) WS_INT32_MAX + 1)
		    ws_src_error(compiler, expr->line, "integer too small");
                ival = - (WsInt32) expr->u.integer.ival;
	    }

            if (!ws_bc_add_const_int(compiler->bc, &cindex, ival)) {
                ws_error_memory(compiler);
                return;
            }
            ins = ws_asm_load_const(compiler, expr->line, cindex);
        }

        ws_asm_link(compiler, ins);
        break;

    case WS_EXPR_CONST_FLOAT:
        {
            WsUInt16 cindex;

            if (!ws_bc_add_const_float(compiler->bc, &cindex, expr->u.fval)) {
                ws_error_memory(compiler);
                return;
            }

            ws_asm_link(compiler, ws_asm_load_const(compiler, expr->line, cindex));
        }
        break;

    case WS_EXPR_CONST_STRING:
        if (expr->u.string.len == 0)
            ins = ws_asm_ins(compiler, expr->line, WS_ASM_CONST_ES);
        else {
            WsUInt16 cindex;

            if (!ws_bc_add_const_utf8_string(compiler->bc, &cindex,
                                             expr->u.string.data,
                                             expr->u.string.len)) {
                ws_error_memory(compiler);
                return;
            }
            ins = ws_asm_load_const(compiler, expr->line, cindex);
        }

        ws_asm_link(compiler, ins);
        break;
    }
}


/* Constructors. */

static WsExpression *expr_alloc(WsCompiler *compiler,
                                WsExpressionType type, WsUInt32 line)
{
    WsExpression *expr = ws_f_calloc(compiler->pool_stree, 1, sizeof(*expr));

    if (expr == NULL)
        ws_error_memory(compiler);
    else {
        expr->type = type;
        expr->line = line;
    }

    return expr;
}


WsExpression *ws_expr_comma(WsCompilerPtr compiler, WsUInt32 line,
                            WsExpression *left, WsExpression *right)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_COMMA, line);

    if (expr) {
        expr->u.comma.left = left;
        expr->u.comma.right = right;
    }

    return expr;
}


WsExpression *ws_expr_assign(WsCompilerPtr compiler, WsUInt32 line,
                             char *identifier, int op, WsExpression *expr)
{
    WsExpression *e = expr_alloc(compiler, WS_EXPR_ASSIGN, line);

    if (e) {
        e->u.assign.identifier = ws_f_strdup(compiler->pool_stree, identifier);
        if (e->u.assign.identifier == NULL)
            ws_error_memory(compiler);

        e->u.assign.op = op;
        e->u.assign.expr = expr;
    }

    /* Free the identifier symbol since it allocated from the system
       heap. */
    ws_lexer_free_block(compiler, identifier);

    return e;
}


WsExpression *ws_expr_conditional(WsCompilerPtr compiler, WsUInt32 line,
                                  WsExpression *e_cond, WsExpression *e_then,
                                  WsExpression *e_else)
{
    WsExpression *e = expr_alloc(compiler, WS_EXPR_CONDITIONAL, line);

    if (e) {
        e->u.conditional.e_cond = e_cond;
        e->u.conditional.e_then = e_then;
        e->u.conditional.e_else = e_else;
    }

    return e;
}


WsExpression *ws_expr_logical(WsCompilerPtr compiler, WsUInt32 line,
                              int type, WsExpression *left, WsExpression *right)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_LOGICAL, line);

    if (expr) {
        expr->u.logical.type = type;
        expr->u.logical.left = left;
        expr->u.logical.right = right;
    }

    return expr;
}


WsExpression *ws_expr_binary(WsCompilerPtr compiler, WsUInt32 line,
                             int type, WsExpression *left, WsExpression *right)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_BINARY, line);

    if (expr) {
        expr->u.binary.type = type;
        expr->u.binary.left = left;
        expr->u.binary.right = right;
    }

    return expr;
}


WsExpression *ws_expr_unary(WsCompilerPtr compiler, WsUInt32 line, int type,
                            WsExpression *expression)
{
    WsExpression *expr;

    /* Handle negative integers here as a special case of constant folding,
     * in order to get -2147483648 right. */
    if (type == WS_ASM_UMINUS && expression->type == WS_EXPR_CONST_INTEGER) {
        expression->u.integer.sign = - expression->u.integer.sign;
        return expression;
    }

    expr = expr_alloc(compiler, WS_EXPR_UNARY, line);
    if (expr) {
        expr->u.unary.type = type;
        expr->u.unary.expr = expression;
    }

    return expr;
}


WsExpression *ws_expr_unary_var(WsCompilerPtr compiler, WsUInt32 line,
                                WsBool addp, char *variable)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_UNARY_VAR, line);

    if (expr) {
        expr->u.unary_var.addp = addp;
        expr->u.unary_var.variable = ws_f_strdup(compiler->pool_stree, variable);
        if (expr->u.unary_var.variable == NULL)
            ws_error_memory(compiler);
    }
    ws_lexer_free_block(compiler, variable);

    return expr;
}


WsExpression *ws_expr_postfix_var(WsCompilerPtr compiler, WsUInt32 line,
                                  WsBool addp, char *variable)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_POSTFIX_VAR, line);

    if (expr) {
        expr->u.postfix_var.addp = addp;
        expr->u.postfix_var.variable = ws_f_strdup(compiler->pool_stree,
                                       variable);
        if (expr->u.postfix_var.variable == NULL)
            ws_error_memory(compiler);
    }
    ws_lexer_free_block(compiler, variable);

    return expr;
}


WsExpression *ws_expr_call(WsCompiler *compiler, WsUInt32 line,
                           int type, char *base, char *name, WsList *arguments)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_CALL, line);

    if (expr) {
        expr->u.call.type = type;
        expr->u.call.base = ws_f_strdup(compiler->pool_stree, base);
        expr->u.call.name = ws_f_strdup(compiler->pool_stree, name);
        expr->u.call.arguments = arguments;

        if ((base && expr->u.call.base == NULL)
            || (name && expr->u.call.name == NULL))
            ws_error_memory(compiler);
    }

    ws_lexer_free_block(compiler, base);
    ws_lexer_free_block(compiler, name);

    return expr;
}


WsExpression *ws_expr_symbol(WsCompiler *compiler, WsUInt32 line,
                             char *identifier)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_SYMBOL, line);

    if (expr) {
        expr->u.symbol = ws_f_strdup(compiler->pool_stree, identifier);
        if (expr->u.symbol == NULL)
            ws_error_memory(compiler);
    }

    ws_lexer_free_block(compiler, identifier);

    return expr;
}


WsExpression *ws_expr_const_invalid(WsCompiler *compiler, WsUInt32 line)
{
    return expr_alloc(compiler, WS_EXPR_CONST_INVALID, line);
}


WsExpression *ws_expr_const_true(WsCompiler *compiler, WsUInt32 line)
{
    return expr_alloc(compiler, WS_EXPR_CONST_TRUE, line);
}


WsExpression *ws_expr_const_false(WsCompiler *compiler, WsUInt32 line)
{
    return expr_alloc(compiler, WS_EXPR_CONST_FALSE, line);
}


WsExpression *ws_expr_const_integer(WsCompiler *compiler, WsUInt32 line,
                                    WsUInt32 ival)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_CONST_INTEGER, line);

    if (expr) {
        expr->u.integer.sign = 1;
        expr->u.integer.ival = ival;
    }

    return expr;
}


WsExpression *ws_expr_const_float(WsCompiler *compiler, WsUInt32 line,
                                  WsFloat fval)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_CONST_FLOAT, line);

    if (expr)
        expr->u.fval = fval;

    return expr;
}


WsExpression *ws_expr_const_string(WsCompiler *compiler, WsUInt32 line,
                                   WsUtf8String *string)
{
    WsExpression *expr = expr_alloc(compiler, WS_EXPR_CONST_STRING, line);

    if (expr) {
        expr->u.string.len = string->len;
        expr->u.string.data = ws_f_memdup(compiler->pool_stree,
                                          string->data, string->len);
        if (expr->u.string.data == NULL)
            ws_error_memory(compiler);
    }

    ws_lexer_free_utf8(compiler, string);

    return expr;
}

/********************* Statements ***************************************/

/* Linearize the variable declaration list `list'. */
static void linearize_variable_init(WsCompiler *compiler, WsList *list,
                                    WsUInt32 line)
{
    WsNamespace *ns;
    WsListItem *li;

    /* For each variable, declared with this list. */
    for (li = list->head; li; li = li->next) {
        WsVarDec *vardec = li->data;

        ns = ws_variable_define(compiler, line, WS_TRUE, vardec->name);
        if (ns && vardec->expr) {
            ws_expr_linearize(compiler, vardec->expr);

            /* Emit an instruction to store the initialization
               value to the variable. */
            ws_asm_link(compiler,
                        ws_asm_variable(compiler, line, WS_ASM_P_STORE_VAR,
                                        ns->vindex));
        }
    }
}


void ws_stmt_linearize(WsCompiler *compiler, WsStatement *stmt)
{
    WsListItem *li;
    WsAsmIns *ins;

    switch (stmt->type) {
    case WS_STMT_BLOCK:
        for (li = stmt->u.block->head; li; li = li->next)
            ws_stmt_linearize(compiler, li->data);
        break;

    case WS_STMT_VARIABLE:
        linearize_variable_init(compiler, stmt->u.var, stmt->first_line);
        break;

    case WS_STMT_EMPTY:
        /* Nothing here. */
        break;

    case WS_STMT_EXPR:
        ws_expr_linearize(compiler, stmt->u.expr);

        /* Pop the expressions result from the stack.  Otherwise loops
           could eventually cause stack overflows. */
        ws_asm_link(compiler, ws_asm_ins(compiler, stmt->last_line, WS_ASM_POP));
        break;

    case WS_STMT_IF:
        {
            WsAsmIns *l_else = ws_asm_label(compiler,
                                            (stmt->u.s_if.s_else
                                             ? stmt->u.s_if.s_else->first_line
                                             : stmt->last_line));
            WsAsmIns *l_end = ws_asm_label(compiler, stmt->last_line);

            /* Linearize the expression. */
            ws_expr_linearize(compiler, stmt->u.s_if.expr);

            /* If the result is false, jump to the else-branch. */
            ws_asm_link(compiler, ws_asm_branch(compiler, stmt->first_line,
                                                WS_ASM_P_TJUMP, l_else));

            /* Else, execute the then-branch and jump to the end. */
            ws_stmt_linearize(compiler, stmt->u.s_if.s_then);
            ws_asm_link(compiler, ws_asm_branch(compiler, stmt->last_line,
                                                WS_ASM_P_JUMP, l_end));

            /* Then else-branch. */
            ws_asm_link(compiler, l_else);

            /* Linearize the else-branch if it is present. */
            if (stmt->u.s_if.s_else)
                ws_stmt_linearize(compiler, stmt->u.s_if.s_else);

            /* Insert the end label. */
            ws_asm_link(compiler, l_end);
        }
        break;

    case WS_STMT_FOR:
        {
            WsAsmIns *l_loop = ws_asm_label(compiler, stmt->first_line);
            WsAsmIns *l_cont = ws_asm_label(compiler, stmt->first_line);
            WsAsmIns *l_break = ws_asm_label(compiler, stmt->first_line);
            WsContBreak *cb;

            /* Store the labels to the compiler. */

            cb = ws_f_calloc(compiler->pool_stree, 1, sizeof(*cb));
            if (cb == NULL) {
                ws_error_memory(compiler);
                return;
            }

            cb->next = compiler->cont_break;
            compiler->cont_break = cb;

            cb->l_cont = l_cont;
            cb->l_break = l_break;

            /* Linearize the possible init code. */
            if (stmt->u.s_for.init)
                linearize_variable_init(compiler, stmt->u.s_for.init,
                                        stmt->first_line);
            else if (stmt->u.s_for.e1) {
                /* Linearize the init. */
                ws_expr_linearize(compiler, stmt->u.s_for.e1);

                /* Pop the result. */
                ws_asm_link(compiler, ws_asm_ins(compiler, stmt->first_line,
                                                 WS_ASM_POP));
            }

            /* Insert the loop label. */
            ws_asm_link(compiler, l_loop);

            /* Linearize the condition. */
            if (stmt->u.s_for.e2) {
                ws_expr_linearize(compiler, stmt->u.s_for.e2);

                /* If false, jump out. */
                ws_asm_link(compiler, ws_asm_branch(compiler, stmt->first_line,
                                                    WS_ASM_P_TJUMP, l_break));
            }

            /* Linearize the body statement. */
            ws_stmt_linearize(compiler, stmt->u.s_for.stmt);

            /* Link the continue label. */
            ws_asm_link(compiler, l_cont);

            /* Linearize the update expression. */
            if (stmt->u.s_for.e3) {
                ws_expr_linearize(compiler, stmt->u.s_for.e3);

                /* Pop the result. */
                ws_asm_link(compiler, ws_asm_ins(compiler, stmt->first_line,
                                                 WS_ASM_POP));
            }

            /* Jump to the loop label to check the condition. */
            ws_asm_link(compiler, ws_asm_branch(compiler, stmt->last_line,
                                                WS_ASM_P_JUMP, l_loop));

            /* Insert the break label. */
            ws_asm_link(compiler, l_break);

            /* Pop the cont-break block. */
            compiler->cont_break = compiler->cont_break->next;
        }
        break;

    case WS_STMT_WHILE:
        {
            WsAsmIns *l_cont = ws_asm_label(compiler, stmt->first_line);
            WsAsmIns *l_break = ws_asm_label(compiler,
                                             stmt->u.s_while.stmt->last_line);
            WsContBreak *cb;

            /* Store the labels to the compiler. */

            cb = ws_f_calloc(compiler->pool_stree, 1, sizeof(*cb));
            if (cb == NULL) {
                ws_error_memory(compiler);
                return;
            }

            cb->next = compiler->cont_break;
            compiler->cont_break = cb;

            cb->l_cont = l_cont;
            cb->l_break = l_break;

            /* Insert the continue label. */
            ws_asm_link(compiler, l_cont);

            /* Linearize the expression. */
            ws_expr_linearize(compiler, stmt->u.s_while.expr);

            /* If false, jump out. */
            ws_asm_link(compiler, ws_asm_branch(compiler, stmt->first_line,
                                                WS_ASM_P_TJUMP, l_break));

            /* Linearize the body statement. */
            ws_stmt_linearize(compiler, stmt->u.s_while.stmt);

            /* And jump to the continue label to check the expression. */
            ws_asm_link(compiler, ws_asm_branch(compiler, stmt->last_line,
                                                WS_ASM_P_JUMP, l_cont));

            /* Insert the break label. */
            ws_asm_link(compiler, l_break);

            /* Pop the cont-break block. */
            compiler->cont_break = compiler->cont_break->next;
        }
        break;

    case WS_STMT_CONTINUE:
        if (compiler->cont_break == NULL)
            ws_src_error(compiler, stmt->first_line,
                         "continue statement not within a loop");

        ws_asm_link(compiler, ws_asm_branch(compiler, stmt->first_line,
                                            WS_ASM_P_JUMP,
                                            compiler->cont_break->l_cont));
        break;

    case WS_STMT_BREAK:
        if (compiler->cont_break == NULL)
            ws_src_error(compiler, stmt->first_line,
                         "break statement not within a loop");

        ws_asm_link(compiler, ws_asm_branch(compiler, stmt->first_line,
                                            WS_ASM_P_JUMP,
                                            compiler->cont_break->l_break));
        break;

    case WS_STMT_RETURN:
        if (stmt->u.expr) {
            /* Linearize the return value and return it. */
            ws_expr_linearize(compiler, stmt->u.expr);
            ins = ws_asm_ins(compiler, stmt->first_line, WS_ASM_RETURN);
        } else
            /* Return an empty string. */
            ins = ws_asm_ins(compiler, stmt->first_line, WS_ASM_RETURN_ES);

        ws_asm_link(compiler, ins);
        break;
    }
}


/* Constructors. */

static WsStatement *stmt_alloc(WsCompiler *compiler, WsStatementType type,
                               WsUInt32 first_line, WsUInt32 last_line)
{
    WsStatement *stmt = ws_f_calloc(compiler->pool_stree, 1, sizeof(*stmt));

    if (stmt == NULL)
        ws_error_memory(compiler);
    else {
        stmt->type = type;
        stmt->first_line = first_line;
        stmt->last_line = last_line;
    }

    return stmt;
}


WsStatement *ws_stmt_block(WsCompiler *compiler, WsUInt32 fline,
                           WsUInt32 lline, WsList *block)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_BLOCK, fline, lline);

    if (stmt)
        stmt->u.block = block;

    return stmt;
}


WsStatement *ws_stmt_variable(WsCompilerPtr compiler, WsUInt32 line,
                              WsList *variables)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_VARIABLE, line, line);

    if (stmt)
        stmt->u.var = variables;

    return stmt;
}


WsStatement *ws_stmt_empty(WsCompiler *compiler, WsUInt32 line)
{
    return stmt_alloc(compiler, WS_STMT_EMPTY, line, line);
}


WsStatement *ws_stmt_expr(WsCompiler *compiler, WsUInt32 line,
                          WsExpression *expr)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_EXPR, line, line);

    if (stmt)
        stmt->u.expr = expr;

    return stmt;
}


WsStatement *ws_stmt_if(WsCompiler *compiler, WsUInt32 line,
                        WsExpression *expr, WsStatement *s_then,
                        WsStatement *s_else)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_IF, line, line);

    if (stmt) {
        stmt->u.s_if.expr = expr;
        stmt->u.s_if.s_then = s_then;
        stmt->u.s_if.s_else = s_else;
    }

    return stmt;
}


WsStatement *ws_stmt_for(WsCompilerPtr compiler, WsUInt32 line, WsList *init,
                         WsExpression *e1, WsExpression *e2, WsExpression *e3,
                         WsStatement *stmt_body)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_FOR, line, line);

    if (stmt) {
        stmt->u.s_for.init = init;
        stmt->u.s_for.e1 = e1;
        stmt->u.s_for.e2 = e2;
        stmt->u.s_for.e3 = e3;
        stmt->u.s_for.stmt = stmt_body;
    }

    return stmt;
}


WsStatement *ws_stmt_while(WsCompiler *compiler, WsUInt32 line,
                           WsExpression *expr, WsStatement *stmt_arg)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_WHILE, line, line);

    if (stmt) {
        stmt->u.s_while.expr = expr;
        stmt->u.s_while.stmt = stmt_arg;
    }

    return stmt;
}


WsStatement *ws_stmt_continue(WsCompiler *compiler, WsUInt32 line)
{
    return stmt_alloc(compiler, WS_STMT_CONTINUE, line, line);
}


WsStatement *ws_stmt_break(WsCompiler *compiler, WsUInt32 line)
{
    return stmt_alloc(compiler, WS_STMT_BREAK, line, line);
}


WsStatement *ws_stmt_return(WsCompilerPtr compiler, WsUInt32 line,
                            WsExpression *expr)
{
    WsStatement *stmt = stmt_alloc(compiler, WS_STMT_RETURN, line, line);

    if (stmt)
        stmt->u.expr = expr;

    return stmt;
}
