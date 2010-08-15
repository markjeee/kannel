%{
/*
 *
 * wsgram.y
 *
 * Author: Markku Rossi <mtr@iki.fi>
 *
 * Copyright (c) 1999-2000 WAPIT OY LTD.
 *		 All rights reserved.
 *
 * Bison grammar for the WMLScript compiler.
 *
 */

#include "wmlscript/wsint.h"

#define YYPARSE_PARAM	pctx
#define YYLEX_PARAM	pctx

/* The required yyerror() function.  This is actually not used but to
   report the internal parser errors.  All other errors are reported
   by using the `wserror.h' functions. */
extern void yyerror(char *msg);

#if WS_DEBUG
/* Just for debugging purposes. */
WsCompilerPtr global_compiler = NULL;
#endif /* WS_DEBUG */

%}

/* The possible semantic values. */
%union
{
    WsUInt32 integer;
    WsFloat vfloat;
    char *identifier;
    WsUtf8String *string;

    WsBool boolean;
    WsList *list;
    WsFormalParm *parm;
    WsVarDec *vardec;

    WsPragmaMetaBody *meta_body;

    WsStatement *stmt;
    WsExpression *expr;
}

/* Tokens. */

/* Language literals. */
%token tINVALID tTRUE tFALSE tINTEGER tFLOAT tSTRING

/* Identifier. */
%token tIDENTIFIER

/* Keywords. */
%token tACCESS tAGENT tBREAK tCONTINUE tIDIV tIDIVA tDOMAIN tELSE tEQUIV
%token tEXTERN tFOR tFUNCTION tHEADER tHTTP tIF tISVALID tMETA tNAME tPATH
%token tRETURN tTYPEOF tUSE tUSER tVAR tWHILE tURL

/* Keywords not used by WMLScript */
%token tDELETE tIN tLIB tNEW tNULL tTHIS tVOID tWITH

/* Future reserved keywords. */
%token tCASE tCATCH tCLASS tCONST tDEBUGGER tDEFAULT tDO tENUM tEXPORT
%token tEXTENDS tFINALLY tIMPORT tPRIVATE tPUBLIC tSIZEOF tSTRUCT tSUPER
%token tSWITCH tTHROW tTRY

/* Punctuation. */
%token tEQ tLE tGE tNE tAND tOR tPLUSPLUS tMINUSMINUS
%token tLSHIFT tRSSHIFT tRSZSHIFT tADDA tSUBA tMULA tDIVA tANDA tORA tXORA
%token tREMA tLSHIFTA tRSSHIFTA tRSZSHIFTA

/* Assign semantic values to tokens and non-terminals. */

%type <integer> tINTEGER
%type <vfloat> tFLOAT
%type <string> tSTRING
%type <identifier> tIDENTIFIER

%type <string> MetaPropertyName MetaContent MetaScheme

%type <meta_body> MetaBody

%type <boolean> ExternOpt

%type <list> FormalParameterListOpt FormalParameterList
%type <list> StatementListOpt StatementList
%type <list> Block Arguments ArgumentList VariableDeclarationList

%type <vardec> VariableDeclaration

%type <stmt> Statement ReturnStatement VariableStatement IfStatement
%type <stmt> IterationStatement ForStatement

%type <expr> ExpressionOpt Expression AssignmentExpression
%type <expr> ConditionalExpression LogicalORExpression
%type <expr> LogicalANDExpression BitwiseORExpression
%type <expr> BitwiseXORExpression BitwiseANDExpression
%type <expr> EqualityExpression RelationalExpression ShiftExpression
%type <expr> AdditiveExpression MultiplicativeExpression UnaryExpression
%type <expr> PostfixExpression CallExpression PrimaryExpression
%type <expr> VariableInitializedOpt

/* Options for bison. */

/* Generate reentrant parser. */
%pure_parser

/* This grammar has one shift-reduce conflict.  It comes from the
   if-else statement. */
%expect 1

%%

/* A compilation unit. */

CompilationUnit:
	  Pragmas FunctionDeclarations
	| FunctionDeclarations
	| error
		{ ws_error_syntax(pctx, @1.first_line); }
	;

/* Pragmas. */

Pragmas:
	  Pragma
	| Pragmas Pragma
	;

Pragma:
	  tUSE PragmaDeclaration ';'
	| error
		{ ws_error_syntax(pctx, @1.first_line); }
	;

PragmaDeclaration:
	  ExternalCompilationUnitPragma
	| AccessControlPragma
	| MetaPragma
	;

ExternalCompilationUnitPragma:
	  tURL tIDENTIFIER tSTRING
	  	{ ws_pragma_use(pctx, @2.first_line, $2, $3); }
	;

AccessControlPragma:
	  tACCESS AccessControlSpecifier
	;

AccessControlSpecifier:
	  tDOMAIN tSTRING
	  	{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this to the byte-code */
		    if (!ws_bc_add_pragma_access_domain(compiler->bc, $2->data,
						        $2->len))
		        ws_error_memory(pctx);
		    ws_lexer_free_utf8(compiler, $2);
		}
	| tPATH tSTRING
	  	{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this to the byte-code */
		    if (!ws_bc_add_pragma_access_path(compiler->bc, $2->data,
						      $2->len))
		        ws_error_memory(pctx);

		    ws_lexer_free_utf8(compiler, $2);
		}
	| tDOMAIN tSTRING tPATH tSTRING
	  	{
		    WsCompiler *compiler = (WsCompiler *) pctx;
		    WsBool success = WS_TRUE;

		    /* Pass these to the byte-code */
		    if (!ws_bc_add_pragma_access_domain(compiler->bc, $2->data,
						        $2->len))
		        success = WS_FALSE;

		    if (!ws_bc_add_pragma_access_path(compiler->bc, $4->data,
						      $4->len))
		        success = WS_FALSE;

		    if (!success)
		        ws_error_memory(pctx);

		    ws_lexer_free_utf8(compiler, $2);
		    ws_lexer_free_utf8(compiler, $4);
		}
	;

MetaPragma:
	  tMETA MetaSpecifier
	;

MetaSpecifier:
	  MetaName
	| MetaHttpEquiv
	| MetaUserAgent
	;

MetaName:
	  tNAME MetaBody
		{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Meta information for the origin servers.  Show it
                     * to the user if requested. */
		    if (compiler->params.meta_name_cb)
		        (*compiler->params.meta_name_cb)(
					$2->property_name, $2->content,
					$2->scheme,
					compiler->params.meta_name_cb_context);

		    /* We do not need the MetaBody anymore. */
		    ws_pragma_meta_body_free(compiler, $2);
		}
	;

MetaHttpEquiv:
	  tHTTP tEQUIV MetaBody
	  	{
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Meta information HTTP header that should be
                     * included to an HTTP response header.  Show it to
                     * the user if requested. */
		    if (compiler->params.meta_http_equiv_cb)
		        (*compiler->params.meta_http_equiv_cb)(
				$3->property_name,
				$3->content,
				$3->scheme,
				compiler->params.meta_http_equiv_cb_context);

		    /* We do not need the MetaBody anymore. */
		    ws_pragma_meta_body_free(compiler, $3);
		}
	;

MetaUserAgent:
	  tUSER tAGENT MetaBody
		{
		    WsBool success;
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this pragma to the byte-code */
		    if ($3) {
		        if ($3->scheme)
		  	    success
			  = ws_bc_add_pragma_user_agent_property_and_scheme(
						compiler->bc,
						$3->property_name->data,
						$3->property_name->len,
						$3->content->data,
						$3->content->len,
						$3->scheme->data,
						$3->scheme->len);
		        else
		  	    success = ws_bc_add_pragma_user_agent_property(
						compiler->bc,
						$3->property_name->data,
						$3->property_name->len,
						$3->content->data,
						$3->content->len);

		        /* Free the MetaBody. */
		        ws_pragma_meta_body_free(compiler, $3);

		        if (!success)
		  	    ws_error_memory(pctx);
		    }
		}
	;

MetaBody:
	  MetaPropertyName MetaContent
		{ $$ = ws_pragma_meta_body(pctx, $1, $2, NULL); }
	| MetaPropertyName MetaContent MetaScheme
		{ $$ = ws_pragma_meta_body(pctx, $1, $2, $3); }
	;

MetaPropertyName: tSTRING;
MetaContent: tSTRING;
MetaScheme: tSTRING;

/* Function declarations. */

FunctionDeclarations:
	  FunctionDeclaration
	| FunctionDeclarations FunctionDeclaration
	;

FunctionDeclaration:
	  ExternOpt tFUNCTION tIDENTIFIER '(' FormalParameterListOpt ')' Block
	  SemicolonOpt
		{
		    char *name = ws_strdup($3);

		    ws_lexer_free_block(pctx, $3);

		    if (name)
		        ws_function(pctx, $1, name, @3.first_line, $5, $7);
		    else
		        ws_error_memory(pctx);
		}
	;

ExternOpt:
	  /* empty */	{ $$ = WS_FALSE; }
	| tEXTERN	{ $$ = WS_TRUE;  }
	;

FormalParameterListOpt:
	  /* empty */
		{ $$ = ws_list_new(pctx); }
	| FormalParameterList
	;

SemicolonOpt:
	  /* empty */
	| ';'
	;

FormalParameterList:
	  tIDENTIFIER
		{
                    char *id;
                    WsFormalParm *parm;

		    id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree, $1);
                    parm = ws_formal_parameter(pctx, @1.first_line, id);

		    ws_lexer_free_block(pctx, $1);

		    if (id == NULL || parm == NULL) {
		        ws_error_memory(pctx);
		        $$ = NULL;
		    } else {
		        $$ = ws_list_new(pctx);
		        ws_list_append(pctx, $$, parm);
		    }
		}
	| FormalParameterList ',' tIDENTIFIER
		{
                    char *id;
                    WsFormalParm *parm;

		    id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree, $3);
                    parm = ws_formal_parameter(pctx, @1.first_line, id);

		    ws_lexer_free_block(pctx, $3);

		    if (id == NULL || parm == NULL) {
		        ws_error_memory(pctx);
		        $$ = NULL;
		    } else
		        ws_list_append(pctx, $1, parm);
		}
	;

/* Statements. */

Statement:
	  Block
		{
		    if ($1)
		        $$ = ws_stmt_block(pctx, $1->first_line, $1->last_line,
				           $1);
		    else
		        $$ = NULL;
		}
	| VariableStatement
	| ';'			/* EmptyStatement */
		{ $$ = ws_stmt_empty(pctx, @1.first_line); }
	| Expression ';'	/* ExpressionStatement */
		{ $$ = ws_stmt_expr(pctx, $1->line, $1); }
	| IfStatement
	| IterationStatement
	| tCONTINUE ';'		/* ContinueStatement */
		{ $$ = ws_stmt_continue(pctx, @1.first_line); }
	| tBREAK ';'		/* BreakStatement */
		{ $$ = ws_stmt_break(pctx, @1.first_line); }
	| ReturnStatement
	;

Block:	'{' StatementListOpt '}'
		{
		    $$ = $2;
		    if ($$) {
		        $$->first_line = @1.first_line;
		        $$->last_line = @3.first_line;
		    }
		}
	| error
		{
		    ws_error_syntax(pctx, @1.first_line);
		    $$ = NULL;
		}
	;

StatementListOpt:
	  /* empty */
		{ $$ = ws_list_new(pctx); }
	| StatementList
	;

StatementList:
	  Statement
		{
		    $$ = ws_list_new(pctx);
		    ws_list_append(pctx, $$, $1);
		}
	| StatementList Statement
		{ ws_list_append(pctx, $1, $2); }
	;

VariableStatement:
	  tVAR VariableDeclarationList ';'
		{ $$ = ws_stmt_variable(pctx, @1.first_line, $2); }
	| tVAR error
		{ ws_error_syntax(pctx, @2.first_line); }
	;

VariableDeclarationList:
	  VariableDeclaration
		{
		    $$ = ws_list_new(pctx);
		    ws_list_append(pctx, $$, $1);
		}
	| VariableDeclarationList ',' VariableDeclaration
		{ ws_list_append(pctx, $1, $3); }
	;

VariableDeclaration:
	  tIDENTIFIER VariableInitializedOpt
		{
		    char *id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree,
					   $1);

		    ws_lexer_free_block(pctx, $1);
		    if (id == NULL) {
		        ws_error_memory(pctx);
		        $$ = NULL;
		    } else
		        $$ = ws_variable_declaration(pctx, id, $2);
		}
	;

VariableInitializedOpt:
	  /* empty */
		{ $$ = NULL; }
	| '=' ConditionalExpression
		{ $$ = $2; }
	;

IfStatement:
	  tIF '(' Expression ')' Statement tELSE Statement
		{ $$ = ws_stmt_if(pctx, @1.first_line, $3, $5, $7); }
	| tIF '(' Expression ')' Statement
		{ $$ = ws_stmt_if(pctx, @1.first_line, $3, $5, NULL); }
	;

IterationStatement:
	  tWHILE '(' Expression ')' Statement
		{ $$ = ws_stmt_while(pctx, @1.first_line, $3, $5); }
	| ForStatement
	;

ForStatement:
	  tFOR '(' ExpressionOpt ';' ExpressionOpt ';' ExpressionOpt ')'
			Statement
	  	{ $$ = ws_stmt_for(pctx, @1.first_line, NULL, $3, $5, $7, $9); }
	| tFOR '(' tVAR VariableDeclarationList ';' ExpressionOpt ';'
	       		ExpressionOpt ')' Statement
	  	{ $$ = ws_stmt_for(pctx, @1.first_line, $4, NULL, $6, $8, $10); }
	;

ReturnStatement:
	  tRETURN ExpressionOpt ';'
		{ $$ = ws_stmt_return(pctx, @1.first_line, $2); }
	;

/* Expressions. */

ExpressionOpt:
	  /* empty */	{ $$ = NULL; }
	| Expression
	;


Expression:
	  AssignmentExpression
	| Expression ',' AssignmentExpression
		{ $$ = ws_expr_comma(pctx, @2.first_line, $1, $3); }
	;

AssignmentExpression:
	  ConditionalExpression
	| tIDENTIFIER	'='		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, '=', $3); }
	| tIDENTIFIER	tMULA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tMULA, $3); }
	| tIDENTIFIER	tDIVA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tDIVA, $3); }
	| tIDENTIFIER	tREMA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tREMA, $3); }
	| tIDENTIFIER	tADDA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tADDA, $3); }
	| tIDENTIFIER	tSUBA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tSUBA, $3); }
	| tIDENTIFIER	tLSHIFTA	AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tLSHIFTA, $3); }
	| tIDENTIFIER	tRSSHIFTA	AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tRSSHIFTA, $3); }
	| tIDENTIFIER	tRSZSHIFTA	AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tRSZSHIFTA, $3); }
	| tIDENTIFIER	tANDA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tANDA, $3); }
	| tIDENTIFIER	tXORA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tXORA, $3); }
	| tIDENTIFIER	tORA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tORA, $3); }
	| tIDENTIFIER	tIDIVA		AssignmentExpression
		{ $$ = ws_expr_assign(pctx, @1.first_line, $1, tIDIVA, $3); }
	;

ConditionalExpression:
	  LogicalORExpression
	| LogicalORExpression '?' AssignmentExpression ':' AssignmentExpression
		{ $$ = ws_expr_conditional(pctx, @2.first_line, $1, $3, $5); }
	;

LogicalORExpression:
	  LogicalANDExpression
	| LogicalORExpression tOR LogicalANDExpression
		{ $$ = ws_expr_logical(pctx, @2.first_line, WS_ASM_SCOR, $1, $3); }
	;

LogicalANDExpression:
	  BitwiseORExpression
	| LogicalANDExpression tAND BitwiseORExpression
		{ $$ = ws_expr_logical(pctx, @2.first_line, WS_ASM_SCAND, $1, $3); }
	;

BitwiseORExpression:
	  BitwiseXORExpression
	| BitwiseORExpression '|' BitwiseXORExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_B_OR, $1, $3); }
	;

BitwiseXORExpression:
	  BitwiseANDExpression
	| BitwiseXORExpression '^' BitwiseANDExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_B_XOR, $1, $3); }
	;

BitwiseANDExpression:
	  EqualityExpression
	| BitwiseANDExpression '&' EqualityExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_B_AND, $1, $3); }
	;

EqualityExpression:
	  RelationalExpression
	| EqualityExpression tEQ RelationalExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_EQ, $1, $3); }
	| EqualityExpression tNE RelationalExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_NE, $1, $3); }
	;

RelationalExpression:
	  ShiftExpression
	| RelationalExpression '<' ShiftExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_LT, $1, $3); }
	| RelationalExpression '>' ShiftExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_GT, $1, $3); }
	| RelationalExpression tLE ShiftExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_LE, $1, $3); }
	| RelationalExpression tGE ShiftExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_GE, $1, $3); }
	;

ShiftExpression:
	  AdditiveExpression
	| ShiftExpression tLSHIFT AdditiveExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_B_LSHIFT, $1, $3); }
	| ShiftExpression tRSSHIFT AdditiveExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_B_RSSHIFT, $1, $3); }
	| ShiftExpression tRSZSHIFT AdditiveExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_B_RSZSHIFT, $1, $3); }
	;

AdditiveExpression:
	  MultiplicativeExpression
	| AdditiveExpression '+' MultiplicativeExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_ADD, $1, $3); }
	| AdditiveExpression '-' MultiplicativeExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_SUB, $1, $3); }
	;

MultiplicativeExpression:
	  UnaryExpression
	| MultiplicativeExpression '*' UnaryExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_MUL, $1, $3); }
	| MultiplicativeExpression '/' UnaryExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_DIV, $1, $3); }
	| MultiplicativeExpression tIDIV UnaryExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_IDIV, $1, $3); }
	| MultiplicativeExpression '%' UnaryExpression
		{ $$ = ws_expr_binary(pctx, @2.first_line, WS_ASM_REM, $1, $3); }
	;

UnaryExpression:
	  PostfixExpression
	| tTYPEOF UnaryExpression
		{ $$ = ws_expr_unary(pctx, @1.first_line, WS_ASM_TYPEOF, $2); }
	| tISVALID UnaryExpression
		{ $$ = ws_expr_unary(pctx, @1.first_line, WS_ASM_ISVALID, $2); }
	| tPLUSPLUS tIDENTIFIER
		{ $$ = ws_expr_unary_var(pctx, @1.first_line, WS_TRUE, $2); }
	| tMINUSMINUS tIDENTIFIER
		{ $$ = ws_expr_unary_var(pctx, @1.first_line, WS_FALSE, $2); }
	| '+' UnaryExpression
		{
                    /* There is no direct way to compile unary `+'.
                     * It doesn't do anything except require type conversion
		     * (section 7.2, 7.3.2), and we do that by converting
		     * it to a binary expression: `UnaryExpression - 0'.
                     * Using `--UnaryExpression' would not be correct because
                     * it might overflow if UnaryExpression is the smallest
                     * possible integer value (see 6.2.7.1).
                     * Using `UnaryExpression + 0' would not be correct
                     * because binary `+' accepts strings, which makes the
		     * type conversion different.
                     */
                    $$ = ws_expr_binary(pctx, @1.first_line, WS_ASM_SUB, $2,
                              ws_expr_const_integer(pctx, @1.first_line, 0));
		}
	| '-' UnaryExpression
		{ $$ = ws_expr_unary(pctx, @1.first_line, WS_ASM_UMINUS, $2); }
	| '~' UnaryExpression
		{ $$ = ws_expr_unary(pctx, @1.first_line, WS_ASM_B_NOT, $2); }
	| '!' UnaryExpression
		{ $$ = ws_expr_unary(pctx, @1.first_line, WS_ASM_NOT, $2); }
	;

PostfixExpression:
	  CallExpression
	| tIDENTIFIER tPLUSPLUS
		{ $$ = ws_expr_postfix_var(pctx, @1.first_line, WS_TRUE, $1); }
	| tIDENTIFIER tMINUSMINUS
		{ $$ = ws_expr_postfix_var(pctx, @1.first_line, WS_FALSE, $1); }
	;

CallExpression:
	  PrimaryExpression
	| tIDENTIFIER Arguments                 /* LocalScriptFunctionCall */
		{
		    WsFunctionHash *f = ws_function_hash(pctx, $1);

		    /* Add an usage count for the local script function. */
		    if (f)
		      f->usage_count++;

		    $$ = ws_expr_call(pctx, @1.first_line, ' ', NULL, $1, $2);
		}
	| tIDENTIFIER '#' tIDENTIFIER Arguments /* ExternalScriptFunctionCall*/
		{ $$ = ws_expr_call(pctx, @3.first_line, '#', $1, $3, $4); }
	| tIDENTIFIER '.' tIDENTIFIER Arguments /* LibraryFunctionCall */
		{ $$ = ws_expr_call(pctx, @3.first_line, '.', $1, $3, $4); }
	;

PrimaryExpression:
	  tIDENTIFIER
		{ $$ = ws_expr_symbol(pctx, @1.first_line, $1); }
	| tINVALID
		{ $$ = ws_expr_const_invalid(pctx, @1.first_line); }
	| tTRUE
		{ $$ = ws_expr_const_true(pctx, @1.first_line); }
	| tFALSE
		{ $$ = ws_expr_const_false(pctx, @1.first_line); }
	| tINTEGER
		{ $$ = ws_expr_const_integer(pctx, @1.first_line, $1); }
	| tFLOAT
		{ $$ = ws_expr_const_float(pctx, @1.first_line, $1); }
	| tSTRING
		{ $$ = ws_expr_const_string(pctx, @1.first_line, $1); }
	| '(' Expression ')'
		{ $$ = $2; }
	;

Arguments:
	  '(' ')'
		{ $$ = ws_list_new(pctx); }
	| '(' ArgumentList ')'
		{ $$ = $2; }
	;

ArgumentList:
	  AssignmentExpression
		{
		    $$ = ws_list_new(pctx);
		    ws_list_append(pctx, $$, $1);
		}
	| ArgumentList ',' AssignmentExpression
		{ ws_list_append(pctx, $1, $3); }
	;

%%

void
yyerror(char *msg)
{
#if WS_DEBUG
  fprintf(stderr, "*** %s:%d: wsc: %s - this msg will be removed ***\n",
	  global_compiler->input_name, global_compiler->linenum, msg);
#endif /* WS_DEBUG */
}
