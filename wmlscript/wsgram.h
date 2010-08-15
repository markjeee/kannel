/* A Bison parser, made by GNU Bison 1.875.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     tINVALID = 258,
     tTRUE = 259,
     tFALSE = 260,
     tINTEGER = 261,
     tFLOAT = 262,
     tSTRING = 263,
     tIDENTIFIER = 264,
     tACCESS = 265,
     tAGENT = 266,
     tBREAK = 267,
     tCONTINUE = 268,
     tIDIV = 269,
     tIDIVA = 270,
     tDOMAIN = 271,
     tELSE = 272,
     tEQUIV = 273,
     tEXTERN = 274,
     tFOR = 275,
     tFUNCTION = 276,
     tHEADER = 277,
     tHTTP = 278,
     tIF = 279,
     tISVALID = 280,
     tMETA = 281,
     tNAME = 282,
     tPATH = 283,
     tRETURN = 284,
     tTYPEOF = 285,
     tUSE = 286,
     tUSER = 287,
     tVAR = 288,
     tWHILE = 289,
     tURL = 290,
     tDELETE = 291,
     tIN = 292,
     tLIB = 293,
     tNEW = 294,
     tNULL = 295,
     tTHIS = 296,
     tVOID = 297,
     tWITH = 298,
     tCASE = 299,
     tCATCH = 300,
     tCLASS = 301,
     tCONST = 302,
     tDEBUGGER = 303,
     tDEFAULT = 304,
     tDO = 305,
     tENUM = 306,
     tEXPORT = 307,
     tEXTENDS = 308,
     tFINALLY = 309,
     tIMPORT = 310,
     tPRIVATE = 311,
     tPUBLIC = 312,
     tSIZEOF = 313,
     tSTRUCT = 314,
     tSUPER = 315,
     tSWITCH = 316,
     tTHROW = 317,
     tTRY = 318,
     tEQ = 319,
     tLE = 320,
     tGE = 321,
     tNE = 322,
     tAND = 323,
     tOR = 324,
     tPLUSPLUS = 325,
     tMINUSMINUS = 326,
     tLSHIFT = 327,
     tRSSHIFT = 328,
     tRSZSHIFT = 329,
     tADDA = 330,
     tSUBA = 331,
     tMULA = 332,
     tDIVA = 333,
     tANDA = 334,
     tORA = 335,
     tXORA = 336,
     tREMA = 337,
     tLSHIFTA = 338,
     tRSSHIFTA = 339,
     tRSZSHIFTA = 340
   };
#endif
#define tINVALID 258
#define tTRUE 259
#define tFALSE 260
#define tINTEGER 261
#define tFLOAT 262
#define tSTRING 263
#define tIDENTIFIER 264
#define tACCESS 265
#define tAGENT 266
#define tBREAK 267
#define tCONTINUE 268
#define tIDIV 269
#define tIDIVA 270
#define tDOMAIN 271
#define tELSE 272
#define tEQUIV 273
#define tEXTERN 274
#define tFOR 275
#define tFUNCTION 276
#define tHEADER 277
#define tHTTP 278
#define tIF 279
#define tISVALID 280
#define tMETA 281
#define tNAME 282
#define tPATH 283
#define tRETURN 284
#define tTYPEOF 285
#define tUSE 286
#define tUSER 287
#define tVAR 288
#define tWHILE 289
#define tURL 290
#define tDELETE 291
#define tIN 292
#define tLIB 293
#define tNEW 294
#define tNULL 295
#define tTHIS 296
#define tVOID 297
#define tWITH 298
#define tCASE 299
#define tCATCH 300
#define tCLASS 301
#define tCONST 302
#define tDEBUGGER 303
#define tDEFAULT 304
#define tDO 305
#define tENUM 306
#define tEXPORT 307
#define tEXTENDS 308
#define tFINALLY 309
#define tIMPORT 310
#define tPRIVATE 311
#define tPUBLIC 312
#define tSIZEOF 313
#define tSTRUCT 314
#define tSUPER 315
#define tSWITCH 316
#define tTHROW 317
#define tTRY 318
#define tEQ 319
#define tLE 320
#define tGE 321
#define tNE 322
#define tAND 323
#define tOR 324
#define tPLUSPLUS 325
#define tMINUSMINUS 326
#define tLSHIFT 327
#define tRSSHIFT 328
#define tRSZSHIFT 329
#define tADDA 330
#define tSUBA 331
#define tMULA 332
#define tDIVA 333
#define tANDA 334
#define tORA 335
#define tXORA 336
#define tREMA 337
#define tLSHIFTA 338
#define tRSSHIFTA 339
#define tRSZSHIFTA 340




#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 34 "wmlscript/wsgram.y"
typedef union YYSTYPE {
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
} YYSTYPE;
/* Line 1240 of yacc.c.  */
#line 223 "y.tab.h"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



#if ! defined (YYLTYPE) && ! defined (YYLTYPE_IS_DECLARED)
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif




