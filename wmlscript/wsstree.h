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
 * wsstree.h
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

#ifndef WSSTREE_H
#define WSSTREE_H

/********************* Linked list for syntax tree items ****************/

/* A list item. */
struct WsListItemRec
{
    struct WsListItemRec *next;
    void *data;
};

typedef struct WsListItemRec WsListItem;

/* The linked list object. */
struct WsListRec
{
    WsListItem *head;
    WsListItem *tail;
    WsUInt32 num_items;

    /* These are used in blocks to record the first and last line
       information.  They might also be used in other grammar
       constructs. */
    WsUInt32 first_line;
    WsUInt32 last_line;
};

typedef struct WsListRec WsList;

/* Create a new syntax tree linked list for the compiler `compiler'.
   The list is allocated from the `compiler->pool_stree' fast malloc
   pool. */
WsList *ws_list_new(WsCompilerPtr compiler);

/* Append the item `value' to the end of the list `list'.  The item is
   allocated from the `compiler->pool_stree' fast malloc pool. */
void ws_list_append(WsCompilerPtr compiler, WsList *list, void *value);

/********************* Namespace for arguments and locals ***************/

/* A namespace record. */
struct WsNamespaceRec
{
    /* The line where this argument or local variable is declared. */
    WsUInt32 line;

    /* The index of this variable. */
    WsUInt8 vindex;
};

typedef struct WsNamespaceRec WsNamespace;

/* Create a new variable hash. */
WsHashPtr ws_variable_hash_create(void);

/* Define the new local variable or argument `name' to the local
   variables namespace.  The argument `line' specifies the location
   where the variable `name' is defined.  The argument `variablep' is
   WS_TRUE for local variables and WS_FALSE for arguments.  The
   function performs all necessary initializations and sanity checks
   needed.  It will also report errors, etc.  The function returns
   NULL if there were any errors. */
WsNamespace *ws_variable_define(WsCompilerPtr compiler, WsUInt32 line,
                                WsBool variablep, char *name);

/* Lookup the variable `name' from the variables namespace.  The
   function returns NULL if the variable `name' is undefined.  The
   function does not report any errors. */
WsNamespace *ws_variable_lookup(WsCompilerPtr compiler, char *name);

/********************* Top-level declarations ***************************/

/* An external compilation unit pragma. */

struct WsPragmaUseRec
{
    /* The line number of the pragma. */
    WsUInt32 line;

    /* The byte-code pool constant index of the external compilation
       unit URL. */
    WsUInt16 urlindex;
};

typedef struct WsPragmaUseRec WsPragmaUse;

/* Create a hash for the external compilation unit pragmas. */
WsHashPtr ws_pragma_use_hash_create(void);

/* Add a new external compilation unit pragma to the compiler
   `compiler'.  The function inserts the URL string `url' to the
   byte-code structure of the compiler `compiler'.  It updates the
   external compilation unit hash to have mapping from the identifier
   `identifier' to the URL `url' (or its constant index).  The
   function reports errors if the identifier `identifier' does already
   have a mapping the external compilation unit namespace. */
void ws_pragma_use(WsCompilerPtr compiler, WsUInt32 line, char *identifier,
                   WsUtf8String *url);


/* MetaBody handling of the `use meta' pragmas. */

struct WsPragmaMetaBodyRec
{
    WsUtf8String *property_name;
    WsUtf8String *content;
    WsUtf8String *scheme;
};

typedef struct WsPragmaMetaBodyRec WsPragmaMetaBody;

/* Create a meta body pragma. */
WsPragmaMetaBody *ws_pragma_meta_body(WsCompilerPtr compiler,
                                      WsUtf8String *property_name,
                                      WsUtf8String *content,
                                      WsUtf8String *scheme);

/* Free the MetaBody `mb'. */
void ws_pragma_meta_body_free(WsCompilerPtr compiler, WsPragmaMetaBody *mb);


/* A top-level function declaration. */

struct WsFunctionRec
{
    WsUInt8 findex;
    WsBool externp;
    char *name;
    WsUInt32 line;
    WsList *params;
    WsList *block;

    /* The usage count of this function.  This is used when sorting the
       functions by their usage count. */
    WsUInt32 usage_count;
};

typedef struct WsFunctionRec WsFunction;

/* Function hash item.  The function hash contains mapping from the
   function names and their usage counts to the actual function
   declaration. */
struct WsFunctionHashRec
{
    /* Does this mapping have a function declaration. */
    WsBool defined;

    /* If declared, this is the index. */
    WsUInt8 findex;

    WsUInt32 usage_count;
};

typedef struct WsFunctionHashRec WsFunctionHash;

/* Create a new hash for functions. */
WsHashPtr ws_function_hash_create(void);

/* Returns a pointer to the function hash item for the function name
   `name'.  The function creates a new hash slot if the name `name' is
   currently unknown.  The function returns NULL if the memory
   allocation failed. */
WsFunctionHash *ws_function_hash(WsCompilerPtr compiler, char *name);

/* Add a new function definition to the compiler `compiler'.  The
   argument `externp' specifies whether the function is extern or not.
   The argument `name' is the name of the function.  The function name
   is ws_malloc() allocated and must be freed when it is not needed
   anymore.  The argument `line' specifies the definition location of
   the function.  It is the line where the function name was in the
   source stream.  The argument `params' contains the formal
   parameters of the function and its body is specified in the
   argument `block'. */
void ws_function(WsCompilerPtr compiler, WsBool externp, char *name,
                 WsUInt32 line, WsList *params, WsList *block);

/********************* Expressions **************************************/

/* Expression types. */
typedef enum
{
    WS_EXPR_COMMA,
    WS_EXPR_ASSIGN,
    WS_EXPR_CONDITIONAL,
    WS_EXPR_LOGICAL,
    WS_EXPR_BINARY,
    WS_EXPR_UNARY,
    WS_EXPR_UNARY_VAR,
    WS_EXPR_POSTFIX_VAR,
    WS_EXPR_CALL,
    WS_EXPR_SYMBOL,
    WS_EXPR_CONST_INVALID,
    WS_EXPR_CONST_TRUE,
    WS_EXPR_CONST_FALSE,
    WS_EXPR_CONST_INTEGER,
    WS_EXPR_CONST_FLOAT,
    WS_EXPR_CONST_STRING
} WsExpressionType;

/* An expression. */
struct WsExpressionRec
{
    WsExpressionType type;
    WsUInt32 line;

    union
    {
        struct
        {
            struct WsExpressionRec *left;
            struct WsExpressionRec *right;
        }
        comma;

        struct
        {
            /* The identifier that is modified. */
            char *identifier;

            /* The type of the assignment.  This is the assignment token
               value: '=', tMULA, tDA, ... */
            int op;

            /* The expression to assign to the identifier `identifier'. */
            struct WsExpressionRec *expr;
        }
        assign;

        struct
        {
            struct WsExpressionRec *e_cond;
            struct WsExpressionRec *e_then;
            struct WsExpressionRec *e_else;
        }
        conditional;

        struct
        {
            /* The type is the opcode of the short-circuit logical byte-code
               operand. */
            int type;
            struct WsExpressionRec *left;
            struct WsExpressionRec *right;
        }
        logical;

        struct
        {
            /* The type is the opcode of the binary byte-code operand. */
            int type;
            struct WsExpressionRec *left;
            struct WsExpressionRec *right;
        }
        binary;

        struct
        {
            /* The type is the opcode of the unary byte-code operand. */
            int type;
            struct WsExpressionRec *expr;
        }
        unary;

        struct
        {
            /* Is this an unary addition or substraction. */
            WsBool addp;
            char *variable;
        }
        unary_var;

        struct
        {
            /* Is this a postfix addition or substraction. */
            WsBool addp;
            char *variable;
        }
        postfix_var;

        struct
        {
            /* The type of the call: ' ', '#', '.' */
            int type;

            /* The name of the external module or library. */
            char *base;

            /* The name of the function to call. */
            char *name;

            /* The arguments of the call. */
            WsList *arguments;
        }
        call;

        struct
	{
	    /* Separate sign bit, so that we can tell the difference
	     * between -2147483648 and +2147483648.  We have to deal 
	     * with both, because the former is parsed as "-" "2147483648".
	     * Sign is 1 for positive numbers, -1 for negative numbers,
	     * and can be either 1 or -1 for zero.
	     */
	     int sign;
	     WsUInt32 ival;
        } integer;

        char *symbol;

        WsUInt16 cindex;


        WsFloat fval;
        WsUtf8String string;
    } u;
};

typedef struct WsExpressionRec WsExpression;

/* Linearize the expression `expr' into symbolic byte-code
   assembler. */
void ws_expr_linearize(WsCompilerPtr compiler, WsExpression *expr);


/* Constructors for different expression types. */

/* Create a comma expression for `left' and `right'. */
WsExpression *ws_expr_comma(WsCompilerPtr compiler, WsUInt32 line,
                            WsExpression *left, WsExpression *right);

/* Create an assignment expression.  The argument `type' specifies the
   type of the expression.  It is the assignment token value. */
WsExpression *ws_expr_assign(WsCompilerPtr compiler, WsUInt32 line,
                             char *identifier, int op, WsExpression *expr);

/* Create a conditional expression with condition `e_cond' and
   expressions `e_then' and `e_else'. */
WsExpression *ws_expr_conditional(WsCompilerPtr compiler, WsUInt32 line,
                                  WsExpression *e_cond, WsExpression *e_then,
                                  WsExpression *e_else);

/* Create a logical expression of type `type'.  The argument `type' is
   the opcode of the logical shoft-circuit byte-code operand. */
WsExpression *ws_expr_logical(WsCompilerPtr compiler, WsUInt32 line,
                              int type, WsExpression *left,
                              WsExpression *right);

/* Create a binary expression of type `type'.  The argument `type' is
   the opcode of the binary byte-code operand. */
WsExpression *ws_expr_binary(WsCompilerPtr compiler, WsUInt32 line,
                             int type, WsExpression *left,
                             WsExpression *right);

/* Create an unary expression of type `type'.  The argument `type' is
   the opcode of the unary byte-code operand. */
WsExpression *ws_expr_unary(WsCompilerPtr compiler, WsUInt32 line,
                            int type, WsExpression *expr);

/* Create an unary variable modification expression.  The argument
   `addp' specified whether the expression is an addition (++) or a
   substraction (--) expression. */
WsExpression *ws_expr_unary_var(WsCompilerPtr compiler, WsUInt32 line,
                                WsBool addp, char *variable);

/* Create a postfix variable modification expression. The argument
   `addp' specified whether the expression is an addition (++) or a
   substraction (--) expression. */
WsExpression *ws_expr_postfix_var(WsCompilerPtr compiler, WsUInt32 line,
                                  WsBool addp, char *variable);

/* A generic call expression.  The argument `type' must be one of ' ',
   '#', or '.' for local, extern, or library function call
   respectively. */
WsExpression *ws_expr_call(WsCompilerPtr compiler, WsUInt32 linenum,
                           int type, char *base, char *name,
                           WsList *arguments);

/* A symbol reference expression. */
WsExpression *ws_expr_symbol(WsCompilerPtr compiler, WsUInt32 linenum,
                             char *identifier);

/* Constant `invalid'. */
WsExpression *ws_expr_const_invalid(WsCompilerPtr compiler, WsUInt32 linenum);

/* Constant `true'. */
WsExpression *ws_expr_const_true(WsCompilerPtr compiler, WsUInt32 linenum);

/* Constant `false'. */
WsExpression *ws_expr_const_false(WsCompilerPtr compiler, WsUInt32 linenum);

/* An unsigned 32 bit integer. */
WsExpression *ws_expr_const_integer(WsCompilerPtr compiler, WsUInt32 linenum,
                                    WsUInt32 ival);

/* A floating point number. */
WsExpression *ws_expr_const_float(WsCompilerPtr compiler, WsUInt32 linenum,
                                  WsFloat fval);

/* An UTF-8 encoded string. */
WsExpression *ws_expr_const_string(WsCompilerPtr compiler, WsUInt32 linenum,
                                   WsUtf8String *string);

/********************* Misc syntax tree structures **********************/

/* A variable declaration */
struct WsVarDecRec
{
    char *name;
    WsExpression *expr;
};

typedef struct WsVarDecRec WsVarDec;

/* Create a new variable declaration */
WsVarDec *ws_variable_declaration(WsCompilerPtr compiler,
                                  char *name, WsExpression *expr);

/* A function formal parameter */
struct WsFormalParmRec
{
    WsUInt32 line;
    char *name;
};

typedef struct WsFormalParmRec WsFormalParm;

/* Create a new formal parameter */
WsFormalParm *ws_formal_parameter(WsCompilerPtr compiler,
                                  WsUInt32 line, char *name);


/********************* Statements ***************************************/

/* Statement types. */
typedef enum
{
    WS_STMT_BLOCK,
    WS_STMT_VARIABLE,
    WS_STMT_EMPTY,
    WS_STMT_EXPR,
    WS_STMT_IF,
    WS_STMT_FOR,
    WS_STMT_WHILE,
    WS_STMT_CONTINUE,
    WS_STMT_BREAK,
    WS_STMT_RETURN
} WsStatementType;

/* A statement. */
struct WsStatementRec
{
    WsStatementType type;
    WsUInt32 first_line;
    WsUInt32 last_line;

    union
    {
        WsList *block;
        WsList *var;
        WsExpression *expr;

        struct
        {
            WsExpression *expr;
            struct WsStatementRec *s_then;
            struct WsStatementRec *s_else;
        }
        s_if ;

        struct
        {
            WsList *init;
            WsExpression *e1;
            WsExpression *e2;
            WsExpression *e3;
            struct WsStatementRec *stmt;
        }
        s_for ;

        struct
        {
            WsExpression *expr;
            struct WsStatementRec *stmt;
        }
        s_while ;
    } u;
};

typedef struct WsStatementRec WsStatement;

/* Linearize the statement `stmt' into symbolic byte-code
   assembler. */
void ws_stmt_linearize(WsCompilerPtr compiler, WsStatement *stmt);

/* Constructors for statements. */

/* Create a new block statement from the statements `block'.  The
   arguments `first_line' and `last_line' specify the first and last
   line numbers of the block (the line numbers of the '{' and '}'
   tokens). */
WsStatement *ws_stmt_block(WsCompilerPtr compiler, WsUInt32 first_line,
                           WsUInt32 last_line, WsList *block);

/* Create a new variable initialization statement. */
WsStatement *ws_stmt_variable(WsCompilerPtr compiler, WsUInt32 line,
                              WsList *variables);

/* Create a new empty statement. */
WsStatement *ws_stmt_empty(WsCompilerPtr compiler, WsUInt32 line);

/* Create a new expression statement. */
WsStatement *ws_stmt_expr(WsCompilerPtr compiler, WsUInt32 line,
                          WsExpression *expr);

/* Create a new if statement. */
WsStatement *ws_stmt_if (WsCompilerPtr compiler, WsUInt32 line,
                         WsExpression *expr, WsStatement *s_then,
                         WsStatement *s_else);

/* Create a new for statement.  Only one of the arguments `init' and
   `e1' can be defined.  The init must be given for statements which
   has a VariableDeclarationList in the initialization block.  For the
   C-like statements, the argument `e1' must be given for the
   initialization expression. */
WsStatement *ws_stmt_for (WsCompilerPtr compiler, WsUInt32 line, WsList *init,
                          WsExpression *e1, WsExpression *e2, WsExpression *e3,
                          WsStatement *stmt);

/* Create a new while statement. */
WsStatement *ws_stmt_while (WsCompilerPtr compiler, WsUInt32 line,
                            WsExpression *expr, WsStatement *stmt);

/* Create a new continue statement. */
WsStatement *ws_stmt_continue(WsCompilerPtr compiler, WsUInt32 line);

/* Create a new break statement. */
WsStatement *ws_stmt_break(WsCompilerPtr compiler, WsUInt32 line);

/* Create a new return statement.  The argument `expr' is the
   expression to return.  If it is NULL, the return statement returns
   an empty string. */
WsStatement *ws_stmt_return(WsCompilerPtr compiler, WsUInt32 line,
                            WsExpression *expr);

#endif /* not WSSTREE_H */
