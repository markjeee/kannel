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

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 1

/* If NAME_PREFIX is specified substitute the variables and functions
   names.  */
#define yyparse ws_yy_parse
#define yylex   ws_yy_lex
#define yyerror ws_yy_error
#define yylval  ws_yy_lval
#define yychar  ws_yy_char
#define yydebug ws_yy_debug
#define yynerrs ws_yy_nerrs
#define yylloc ws_yy_lloc

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




/* Copy the first part of user declarations.  */
#line 1 "wmlscript/wsgram.y"

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



/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

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
/* Line 191 of yacc.c.  */
#line 302 "y.tab.c"
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


/* Copy the second part of user declarations.  */


/* Line 214 of yacc.c.  */
#line 326 "y.tab.c"

#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# if YYSTACK_USE_ALLOCA
#  define YYSTACK_ALLOC alloca
# else
#  ifndef YYSTACK_USE_ALLOCA
#   if defined (alloca) || defined (_ALLOCA_H)
#    define YYSTACK_ALLOC alloca
#   else
#    ifdef __GNUC__
#     define YYSTACK_ALLOC __builtin_alloca
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
# else
#  if defined (__STDC__) || defined (__cplusplus)
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   define YYSIZE_T size_t
#  endif
#  define YYSTACK_ALLOC malloc
#  define YYSTACK_FREE free
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (YYLTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short) + sizeof (YYSTYPE) + sizeof (YYLTYPE))	\
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  register YYSIZE_T yyi;		\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (0)
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  17
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   448

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  109
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  55
/* YYNRULES -- Number of rules. */
#define YYNRULES  146
/* YYNRULES -- Number of states. */
#define YYNSTATES  257

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   340

#define YYTRANSLATE(YYX) 						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,   106,     2,   107,     2,   104,    97,     2,
      87,    88,   102,   100,    89,   101,   108,   103,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    94,    86,
      98,    92,    99,    93,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    96,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    90,    95,    91,   105,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short yyprhs[] =
{
       0,     0,     3,     6,     8,    10,    12,    15,    19,    21,
      23,    25,    27,    31,    34,    37,    40,    45,    48,    50,
      52,    54,    57,    61,    65,    68,    72,    74,    76,    78,
      80,    83,    92,    93,    95,    96,    98,    99,   101,   103,
     107,   109,   111,   113,   116,   118,   120,   123,   126,   128,
     132,   134,   135,   137,   139,   142,   146,   149,   151,   155,
     158,   159,   162,   170,   176,   182,   184,   194,   205,   209,
     210,   212,   214,   218,   220,   224,   228,   232,   236,   240,
     244,   248,   252,   256,   260,   264,   268,   272,   274,   280,
     282,   286,   288,   292,   294,   298,   300,   304,   306,   310,
     312,   316,   320,   322,   326,   330,   334,   338,   340,   344,
     348,   352,   354,   358,   362,   364,   368,   372,   376,   380,
     382,   385,   388,   391,   394,   397,   400,   403,   406,   408,
     411,   414,   416,   419,   424,   429,   431,   433,   435,   437,
     439,   441,   443,   447,   450,   454,   456
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const short yyrhs[] =
{
     110,     0,    -1,   111,   126,    -1,   126,    -1,     1,    -1,
     112,    -1,   111,   112,    -1,    31,   113,    86,    -1,     1,
      -1,   114,    -1,   115,    -1,   117,    -1,    35,     9,     8,
      -1,    10,   116,    -1,    16,     8,    -1,    28,     8,    -1,
      16,     8,    28,     8,    -1,    26,   118,    -1,   119,    -1,
     120,    -1,   121,    -1,    27,   122,    -1,    23,    18,   122,
      -1,    32,    11,   122,    -1,   123,   124,    -1,   123,   124,
     125,    -1,     8,    -1,     8,    -1,     8,    -1,   127,    -1,
     126,   127,    -1,   128,    21,     9,    87,   129,    88,   133,
     130,    -1,    -1,    19,    -1,    -1,   131,    -1,    -1,    86,
      -1,     9,    -1,   131,    89,     9,    -1,   133,    -1,   136,
      -1,    86,    -1,   145,    86,    -1,   140,    -1,   141,    -1,
      13,    86,    -1,    12,    86,    -1,   143,    -1,    90,   134,
      91,    -1,     1,    -1,    -1,   135,    -1,   132,    -1,   135,
     132,    -1,    33,   137,    86,    -1,    33,     1,    -1,   138,
      -1,   137,    89,   138,    -1,     9,   139,    -1,    -1,    92,
     147,    -1,    24,    87,   145,    88,   132,    17,   132,    -1,
      24,    87,   145,    88,   132,    -1,    34,    87,   145,    88,
     132,    -1,   142,    -1,    20,    87,   144,    86,   144,    86,
     144,    88,   132,    -1,    20,    87,    33,   137,    86,   144,
      86,   144,    88,   132,    -1,    29,   144,    86,    -1,    -1,
     145,    -1,   146,    -1,   145,    89,   146,    -1,   147,    -1,
       9,    92,   146,    -1,     9,    77,   146,    -1,     9,    78,
     146,    -1,     9,    82,   146,    -1,     9,    75,   146,    -1,
       9,    76,   146,    -1,     9,    83,   146,    -1,     9,    84,
     146,    -1,     9,    85,   146,    -1,     9,    79,   146,    -1,
       9,    81,   146,    -1,     9,    80,   146,    -1,     9,    15,
     146,    -1,   148,    -1,   148,    93,   146,    94,   146,    -1,
     149,    -1,   148,    69,   149,    -1,   150,    -1,   149,    68,
     150,    -1,   151,    -1,   150,    95,   151,    -1,   152,    -1,
     151,    96,   152,    -1,   153,    -1,   152,    97,   153,    -1,
     154,    -1,   153,    64,   154,    -1,   153,    67,   154,    -1,
     155,    -1,   154,    98,   155,    -1,   154,    99,   155,    -1,
     154,    65,   155,    -1,   154,    66,   155,    -1,   156,    -1,
     155,    72,   156,    -1,   155,    73,   156,    -1,   155,    74,
     156,    -1,   157,    -1,   156,   100,   157,    -1,   156,   101,
     157,    -1,   158,    -1,   157,   102,   158,    -1,   157,   103,
     158,    -1,   157,    14,   158,    -1,   157,   104,   158,    -1,
     159,    -1,    30,   158,    -1,    25,   158,    -1,    70,     9,
      -1,    71,     9,    -1,   100,   158,    -1,   101,   158,    -1,
     105,   158,    -1,   106,   158,    -1,   160,    -1,     9,    70,
      -1,     9,    71,    -1,   161,    -1,     9,   162,    -1,     9,
     107,     9,   162,    -1,     9,   108,     9,   162,    -1,     9,
      -1,     3,    -1,     4,    -1,     5,    -1,     6,    -1,     7,
      -1,     8,    -1,    87,   145,    88,    -1,    87,    88,    -1,
      87,   163,    88,    -1,   146,    -1,   163,    89,   146,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short yyrline[] =
{
       0,   122,   122,   123,   124,   131,   132,   136,   137,   142,
     143,   144,   148,   153,   157,   167,   178,   201,   205,   206,
     207,   211,   229,   249,   284,   286,   290,   291,   292,   297,
     298,   302,   317,   318,   323,   324,   327,   329,   333,   351,
     372,   380,   381,   383,   385,   386,   387,   389,   391,   394,
     402,   411,   412,   416,   421,   426,   428,   433,   438,   443,
     459,   460,   465,   467,   472,   474,   478,   481,   487,   494,
     495,   500,   501,   506,   507,   509,   511,   513,   515,   517,
     519,   521,   523,   525,   527,   529,   531,   536,   537,   542,
     543,   548,   549,   554,   555,   560,   561,   566,   567,   572,
     573,   575,   580,   581,   583,   585,   587,   592,   593,   595,
     597,   602,   603,   605,   610,   611,   613,   615,   617,   622,
     623,   625,   627,   629,   631,   647,   649,   651,   656,   657,
     659,   664,   665,   675,   677,   682,   684,   686,   688,   690,
     692,   694,   696,   701,   703,   708,   713
};
#endif

#if YYDEBUG || YYERROR_VERBOSE
/* YYTNME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "tINVALID", "tTRUE", "tFALSE", "tINTEGER", 
  "tFLOAT", "tSTRING", "tIDENTIFIER", "tACCESS", "tAGENT", "tBREAK", 
  "tCONTINUE", "tIDIV", "tIDIVA", "tDOMAIN", "tELSE", "tEQUIV", "tEXTERN", 
  "tFOR", "tFUNCTION", "tHEADER", "tHTTP", "tIF", "tISVALID", "tMETA", 
  "tNAME", "tPATH", "tRETURN", "tTYPEOF", "tUSE", "tUSER", "tVAR", 
  "tWHILE", "tURL", "tDELETE", "tIN", "tLIB", "tNEW", "tNULL", "tTHIS", 
  "tVOID", "tWITH", "tCASE", "tCATCH", "tCLASS", "tCONST", "tDEBUGGER", 
  "tDEFAULT", "tDO", "tENUM", "tEXPORT", "tEXTENDS", "tFINALLY", 
  "tIMPORT", "tPRIVATE", "tPUBLIC", "tSIZEOF", "tSTRUCT", "tSUPER", 
  "tSWITCH", "tTHROW", "tTRY", "tEQ", "tLE", "tGE", "tNE", "tAND", "tOR", 
  "tPLUSPLUS", "tMINUSMINUS", "tLSHIFT", "tRSSHIFT", "tRSZSHIFT", "tADDA", 
  "tSUBA", "tMULA", "tDIVA", "tANDA", "tORA", "tXORA", "tREMA", 
  "tLSHIFTA", "tRSSHIFTA", "tRSZSHIFTA", "';'", "'('", "')'", "','", 
  "'{'", "'}'", "'='", "'?'", "':'", "'|'", "'^'", "'&'", "'<'", "'>'", 
  "'+'", "'-'", "'*'", "'/'", "'%'", "'~'", "'!'", "'#'", "'.'", 
  "$accept", "CompilationUnit", "Pragmas", "Pragma", "PragmaDeclaration", 
  "ExternalCompilationUnitPragma", "AccessControlPragma", 
  "AccessControlSpecifier", "MetaPragma", "MetaSpecifier", "MetaName", 
  "MetaHttpEquiv", "MetaUserAgent", "MetaBody", "MetaPropertyName", 
  "MetaContent", "MetaScheme", "FunctionDeclarations", 
  "FunctionDeclaration", "ExternOpt", "FormalParameterListOpt", 
  "SemicolonOpt", "FormalParameterList", "Statement", "Block", 
  "StatementListOpt", "StatementList", "VariableStatement", 
  "VariableDeclarationList", "VariableDeclaration", 
  "VariableInitializedOpt", "IfStatement", "IterationStatement", 
  "ForStatement", "ReturnStatement", "ExpressionOpt", "Expression", 
  "AssignmentExpression", "ConditionalExpression", "LogicalORExpression", 
  "LogicalANDExpression", "BitwiseORExpression", "BitwiseXORExpression", 
  "BitwiseANDExpression", "EqualityExpression", "RelationalExpression", 
  "ShiftExpression", "AdditiveExpression", "MultiplicativeExpression", 
  "UnaryExpression", "PostfixExpression", "CallExpression", 
  "PrimaryExpression", "Arguments", "ArgumentList", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,    59,    40,    41,    44,
     123,   125,    61,    63,    58,   124,    94,    38,    60,    62,
      43,    45,    42,    47,    37,   126,    33,    35,    46
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,   109,   110,   110,   110,   111,   111,   112,   112,   113,
     113,   113,   114,   115,   116,   116,   116,   117,   118,   118,
     118,   119,   120,   121,   122,   122,   123,   124,   125,   126,
     126,   127,   128,   128,   129,   129,   130,   130,   131,   131,
     132,   132,   132,   132,   132,   132,   132,   132,   132,   133,
     133,   134,   134,   135,   135,   136,   136,   137,   137,   138,
     139,   139,   140,   140,   141,   141,   142,   142,   143,   144,
     144,   145,   145,   146,   146,   146,   146,   146,   146,   146,
     146,   146,   146,   146,   146,   146,   146,   147,   147,   148,
     148,   149,   149,   150,   150,   151,   151,   152,   152,   153,
     153,   153,   154,   154,   154,   154,   154,   155,   155,   155,
     155,   156,   156,   156,   157,   157,   157,   157,   157,   158,
     158,   158,   158,   158,   158,   158,   158,   158,   159,   159,
     159,   160,   160,   160,   160,   161,   161,   161,   161,   161,
     161,   161,   161,   162,   162,   163,   163
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     2,     1,     1,     1,     2,     3,     1,     1,
       1,     1,     3,     2,     2,     2,     4,     2,     1,     1,
       1,     2,     3,     3,     2,     3,     1,     1,     1,     1,
       2,     8,     0,     1,     0,     1,     0,     1,     1,     3,
       1,     1,     1,     2,     1,     1,     2,     2,     1,     3,
       1,     0,     1,     1,     2,     3,     2,     1,     3,     2,
       0,     2,     7,     5,     5,     1,     9,    10,     3,     0,
       1,     1,     3,     1,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     1,     5,     1,
       3,     1,     3,     1,     3,     1,     3,     1,     3,     1,
       3,     3,     1,     3,     3,     3,     3,     1,     3,     3,
       3,     1,     3,     3,     1,     3,     3,     3,     3,     1,
       2,     2,     2,     2,     2,     2,     2,     2,     1,     2,
       2,     1,     2,     4,     4,     1,     1,     1,     1,     1,
       1,     1,     3,     2,     3,     1,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       0,     8,    33,     0,     0,     0,     5,     3,    29,     0,
       0,     0,     0,     0,     9,    10,    11,     1,     8,     6,
       2,    30,     0,     0,     0,    13,     0,     0,     0,    17,
      18,    19,    20,     0,     7,     0,    14,    15,     0,    26,
      21,     0,     0,    12,    34,     0,    22,    27,    24,    23,
      38,     0,    35,    16,    28,    25,     0,     0,    50,     0,
      36,    39,   136,   137,   138,   139,   140,   141,   135,     0,
       0,     0,     0,     0,    69,     0,     0,     0,     0,     0,
      42,     0,     0,     0,     0,     0,    53,    40,     0,     0,
      41,    44,    45,    65,    48,     0,    71,    73,    87,    89,
      91,    93,    95,    97,    99,   102,   107,   111,   114,   119,
     128,   131,    37,    31,     0,   129,   130,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   132,    47,    46,    69,     0,   135,   121,     0,
      70,   120,    56,    60,     0,    57,     0,   122,   123,     0,
     124,   125,   126,   127,    49,    54,    43,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    86,
      78,    79,    75,    76,    83,    85,    84,    77,    80,    81,
      82,   143,   145,     0,    74,     0,     0,     0,     0,     0,
      68,     0,    59,    55,     0,     0,   142,    72,    90,     0,
      92,    94,    96,    98,   100,   101,   105,   106,   103,   104,
     108,   109,   110,   112,   113,   117,   115,   116,   118,   144,
       0,   133,   134,     0,    69,     0,    61,    58,     0,     0,
     146,    69,     0,    63,    64,    88,     0,    69,     0,    69,
       0,    62,     0,     0,     0,    66,    67
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short yydefgoto[] =
{
      -1,     4,     5,     6,    13,    14,    15,    25,    16,    29,
      30,    31,    32,    40,    41,    48,    55,     7,     8,     9,
      51,   113,    52,    86,    87,    88,    89,    90,   144,   145,
     202,    91,    92,    93,    94,   139,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   132,   193
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -135
static const short yypact[] =
{
      59,    39,  -135,    28,    68,    63,  -135,    38,  -135,    62,
      37,    54,    13,   -43,  -135,  -135,  -135,  -135,  -135,  -135,
      38,  -135,    57,    67,    87,  -135,    92,   108,   109,  -135,
    -135,  -135,  -135,   121,  -135,    47,   123,  -135,   108,  -135,
    -135,   141,   108,  -135,   155,   164,  -135,  -135,   166,  -135,
    -135,    89,   107,  -135,  -135,  -135,     1,   188,  -135,   132,
     112,  -135,  -135,  -135,  -135,  -135,  -135,  -135,   340,   115,
     120,   124,   125,   296,   308,   296,    35,   126,   198,   201,
    -135,   308,   296,   296,   296,   296,  -135,  -135,   129,   175,
    -135,  -135,  -135,  -135,  -135,   -83,  -135,  -135,   -48,   146,
     122,   119,   127,    12,   -25,    45,     5,    44,  -135,  -135,
    -135,  -135,  -135,  -135,   308,  -135,  -135,   308,   308,   308,
     308,   308,   308,   308,   308,   308,   308,   308,   222,   308,
     207,   212,  -135,  -135,  -135,   264,   308,   -38,  -135,   148,
     147,  -135,  -135,   143,    -1,  -135,   308,  -135,  -135,    43,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,   308,   296,   308,
     296,   296,   296,   296,   296,   296,   296,   296,   296,   296,
     296,   296,   296,   296,   296,   296,   296,   296,   296,  -135,
    -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,    66,  -135,   152,   152,   231,   156,    70,
    -135,   296,  -135,  -135,   231,    82,  -135,  -135,   146,   149,
     122,   119,   127,    12,   -25,   -25,    45,    45,    45,    45,
       5,     5,     5,    44,    44,  -135,  -135,  -135,  -135,  -135,
     308,  -135,  -135,    25,   308,    22,  -135,  -135,    22,   308,
    -135,   308,   158,   224,  -135,  -135,   162,   308,    22,   308,
     161,  -135,   163,    22,    22,  -135,  -135
};

/* YYPGOTO[NTERM-NUM].  */
static const short yypgoto[] =
{
    -135,  -135,  -135,   245,  -135,  -135,  -135,  -135,  -135,  -135,
    -135,  -135,  -135,    29,  -135,  -135,  -135,   248,    17,  -135,
    -135,  -135,  -135,   -85,   199,  -135,  -135,  -135,    60,    50,
    -135,  -135,  -135,  -135,  -135,  -134,   -74,  -109,    55,  -135,
     100,    99,   102,    98,   101,   -22,   -65,   -46,    20,    14,
    -135,  -135,  -135,   -10,  -135
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -53
static const short yytable[] =
{
     140,   198,    58,   156,   155,   179,   157,   149,   180,   181,
     182,   183,   184,   185,   186,   187,   188,   189,   190,   192,
     194,   158,    33,    58,    21,    62,    63,    64,    65,    66,
      67,    68,   115,   116,    69,    70,   142,    21,    10,    -4,
     166,   167,    71,    34,   143,   159,    72,    73,   207,   128,
     209,    74,    75,    23,    11,    76,    77,     2,   175,   -32,
       1,   140,   199,    12,    18,    24,    35,    46,    17,   130,
     131,    49,   205,   168,   169,    36,   164,    26,     2,   165,
     -32,    27,     2,    22,   -32,   203,    28,   138,   204,   141,
       3,    59,    78,    79,     3,    37,   150,   151,   152,   153,
     242,   216,   217,   218,   219,   173,   174,   246,    80,    81,
      38,   241,    59,   250,   204,   252,    39,   170,   171,   172,
      42,   240,    82,    83,   220,   221,   222,    84,    85,    43,
     245,   206,   157,    58,    44,    62,    63,    64,    65,    66,
      67,    68,   214,   215,    69,    70,   176,   177,   178,    47,
     243,    45,    71,   244,   229,   230,    72,    73,   235,   157,
     140,    74,    75,   251,    50,    76,    77,   140,   255,   256,
     238,   157,    53,   140,    54,   140,    58,    56,    62,    63,
      64,    65,    66,    67,    68,   231,   232,    69,    70,   225,
     226,   227,   228,   223,   224,    71,    57,    61,   112,    72,
      73,   133,    78,    79,    74,    75,   134,   147,    76,    77,
     148,   135,   136,   146,   160,   162,   195,   161,    80,    81,
     154,   196,    59,   -51,   163,    62,    63,    64,    65,    66,
      67,    68,    82,    83,   200,   201,   157,    84,    85,   128,
     143,   248,   234,   239,   247,    78,    79,    73,   249,   253,
      19,   254,    75,    20,   237,    60,   236,   233,   208,   210,
     212,    80,    81,   211,   213,    59,   -52,    62,    63,    64,
      65,    66,    67,    68,     0,    82,    83,     0,     0,     0,
      84,    85,     0,     0,     0,     0,     0,     0,     0,    73,
       0,     0,    78,    79,    75,     0,     0,   197,     0,    62,
      63,    64,    65,    66,    67,   137,     0,     0,     0,    81,
     191,    62,    63,    64,    65,    66,    67,    68,     0,     0,
       0,    73,    82,    83,     0,     0,    75,    84,    85,     0,
       0,     0,     0,    73,    78,    79,     0,     0,    75,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    81,     0,     0,     0,   114,     0,     0,     0,     0,
       0,     0,     0,     0,    82,    83,    78,    79,     0,    84,
      85,     0,     0,     0,     0,     0,     0,     0,    78,    79,
       0,     0,     0,    81,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    81,    82,    83,     0,     0,
       0,    84,    85,     0,     0,     0,     0,     0,    82,    83,
     115,   116,     0,    84,    85,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,     0,   128,     0,     0,
       0,     0,   129,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   130,   131
};

static const short yycheck[] =
{
      74,   135,     1,    86,    89,   114,    89,    81,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,    69,     9,     1,     7,     3,     4,     5,     6,     7,
       8,     9,    70,    71,    12,    13,     1,    20,    10,     0,
      65,    66,    20,    86,     9,    93,    24,    25,   157,    87,
     159,    29,    30,    16,    26,    33,    34,    19,    14,    21,
       1,   135,   136,    35,     1,    28,     9,    38,     0,   107,
     108,    42,   146,    98,    99,     8,    64,    23,    19,    67,
      21,    27,    19,    21,    21,    86,    32,    73,    89,    75,
      31,    90,    70,    71,    31,     8,    82,    83,    84,    85,
     234,   166,   167,   168,   169,   100,   101,   241,    86,    87,
      18,    86,    90,   247,    89,   249,     8,    72,    73,    74,
      11,   230,   100,   101,   170,   171,   172,   105,   106,     8,
     239,    88,    89,     1,    87,     3,     4,     5,     6,     7,
       8,     9,   164,   165,    12,    13,   102,   103,   104,     8,
     235,    28,    20,   238,    88,    89,    24,    25,    88,    89,
     234,    29,    30,   248,     9,    33,    34,   241,   253,   254,
      88,    89,     8,   247,     8,   249,     1,    88,     3,     4,
       5,     6,     7,     8,     9,   195,   196,    12,    13,   175,
     176,   177,   178,   173,   174,    20,    89,     9,    86,    24,
      25,    86,    70,    71,    29,    30,    86,     9,    33,    34,
       9,    87,    87,    87,    68,    96,     9,    95,    86,    87,
      91,     9,    90,    91,    97,     3,     4,     5,     6,     7,
       8,     9,   100,   101,    86,    92,    89,   105,   106,    87,
       9,    17,    86,    94,    86,    70,    71,    25,    86,    88,
       5,    88,    30,     5,   204,    56,   201,   197,   158,   160,
     162,    86,    87,   161,   163,    90,    91,     3,     4,     5,
       6,     7,     8,     9,    -1,   100,   101,    -1,    -1,    -1,
     105,   106,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    25,
      -1,    -1,    70,    71,    30,    -1,    -1,    33,    -1,     3,
       4,     5,     6,     7,     8,     9,    -1,    -1,    -1,    87,
      88,     3,     4,     5,     6,     7,     8,     9,    -1,    -1,
      -1,    25,   100,   101,    -1,    -1,    30,   105,   106,    -1,
      -1,    -1,    -1,    25,    70,    71,    -1,    -1,    30,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    87,    -1,    -1,    -1,    15,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   100,   101,    70,    71,    -1,   105,
     106,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    70,    71,
      -1,    -1,    -1,    87,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    87,   100,   101,    -1,    -1,
      -1,   105,   106,    -1,    -1,    -1,    -1,    -1,   100,   101,
      70,    71,    -1,   105,   106,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    -1,    87,    -1,    -1,
      -1,    -1,    92,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   107,   108
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     1,    19,    31,   110,   111,   112,   126,   127,   128,
      10,    26,    35,   113,   114,   115,   117,     0,     1,   112,
     126,   127,    21,    16,    28,   116,    23,    27,    32,   118,
     119,   120,   121,     9,    86,     9,     8,     8,    18,     8,
     122,   123,    11,     8,    87,    28,   122,     8,   124,   122,
       9,   129,   131,     8,     8,   125,    88,    89,     1,    90,
     133,     9,     3,     4,     5,     6,     7,     8,     9,    12,
      13,    20,    24,    25,    29,    30,    33,    34,    70,    71,
      86,    87,   100,   101,   105,   106,   132,   133,   134,   135,
     136,   140,   141,   142,   143,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,    86,   130,    15,    70,    71,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    87,    92,
     107,   108,   162,    86,    86,    87,    87,     9,   158,   144,
     145,   158,     1,     9,   137,   138,    87,     9,     9,   145,
     158,   158,   158,   158,    91,   132,    86,    89,    69,    93,
      68,    95,    96,    97,    64,    67,    65,    66,    98,    99,
      72,    73,    74,   100,   101,    14,   102,   103,   104,   146,
     146,   146,   146,   146,   146,   146,   146,   146,   146,   146,
     146,    88,   146,   163,   146,     9,     9,    33,   144,   145,
      86,    92,   139,    86,    89,   145,    88,   146,   149,   146,
     150,   151,   152,   153,   154,   154,   155,   155,   155,   155,
     156,   156,   156,   157,   157,   158,   158,   158,   158,    88,
      89,   162,   162,   137,    86,    88,   147,   138,    88,    94,
     146,    86,   144,   132,   132,   146,   144,    86,    17,    86,
     144,   132,   144,    88,    88,   132,   132
};

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# if defined (__STDC__) || defined (__cplusplus)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# endif
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		do				\
			  {				\
			    yylerrsp = yylsp;		\
			    *++yylerrsp = yyloc;	\
			    goto yyerrlab1;		\
			  }				\
			while (0)


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    { 								\
      yyerror ("syntax error: cannot back up");\
      YYERROR;							\
    }								\
while (0)

#define YYTERROR	1
#define YYERRCODE	256

/* YYLLOC_DEFAULT -- Compute the default location (before the actions
   are run).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)         \
  Current.first_line   = Rhs[1].first_line;      \
  Current.first_column = Rhs[1].first_column;    \
  Current.last_line    = Rhs[N].last_line;       \
  Current.last_column  = Rhs[N].last_column;
#endif

/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, &yylloc, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, &yylloc)
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (0)

# define YYDSYMPRINT(Args)			\
do {						\
  if (yydebug)					\
    yysymprint Args;				\
} while (0)

# define YYDSYMPRINTF(Title, Token, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr, 					\
                  Token, Value, Location);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (cinluded).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short *bottom, short *top)
#else
static void
yy_stack_print (bottom, top)
    short *bottom;
    short *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned int yylineno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %u), ",
             yyrule - 1, yylineno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname [yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname [yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YYDSYMPRINT(Args)
# define YYDSYMPRINTF(Title, Token, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   SIZE_MAX < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#if YYMAXDEPTH == 0
# undef YYMAXDEPTH
#endif

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  register const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
{
  register char *yyd = yydest;
  register const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

#endif /* !YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  if (yytype < YYNTOKENS)
    {
      YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
# ifdef YYPRINT
      YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
    }
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yydestruct (int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yytype, yyvaluep, yylocationp)
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  switch (yytype)
    {

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */






/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  /* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;
/* Location data for the lookahead symbol.  */
YYLTYPE yylloc;

  register int yystate;
  register int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short	yyssa[YYINITDEPTH];
  short *yyss = yyssa;
  register short *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  register YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  YYLTYPE *yylerrsp;

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
  yylsp = yyls;
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow ("parser stack overflow",
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyoverflowlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyoverflowlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	short *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyoverflowlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
	YYSTACK_RELOCATE (yyls);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

/* Do appropriate processing given the current state.  */
/* Read a lookahead token if we need one and don't already have one.  */
/* yyresume: */

  /* First try to decide what to do without reference to lookahead token.  */

  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YYDSYMPRINTF ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Shift the lookahead token.  */
  YYDPRINTF ((stderr, "Shifting token %s, ", yytname[yytoken]));

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
  *++yylsp = yylloc;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 4:
#line 125 "wmlscript/wsgram.y"
    { ws_error_syntax(pctx, yylsp[0].first_line); }
    break;

  case 8:
#line 138 "wmlscript/wsgram.y"
    { ws_error_syntax(pctx, yylsp[0].first_line); }
    break;

  case 12:
#line 149 "wmlscript/wsgram.y"
    { ws_pragma_use(pctx, yylsp[-1].first_line, yyvsp[-1].identifier, yyvsp[0].string); }
    break;

  case 14:
#line 158 "wmlscript/wsgram.y"
    {
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this to the byte-code */
		    if (!ws_bc_add_pragma_access_domain(compiler->bc, yyvsp[0].string->data,
						        yyvsp[0].string->len))
		        ws_error_memory(pctx);
		    ws_lexer_free_utf8(compiler, yyvsp[0].string);
		}
    break;

  case 15:
#line 168 "wmlscript/wsgram.y"
    {
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this to the byte-code */
		    if (!ws_bc_add_pragma_access_path(compiler->bc, yyvsp[0].string->data,
						      yyvsp[0].string->len))
		        ws_error_memory(pctx);

		    ws_lexer_free_utf8(compiler, yyvsp[0].string);
		}
    break;

  case 16:
#line 179 "wmlscript/wsgram.y"
    {
		    WsCompiler *compiler = (WsCompiler *) pctx;
		    WsBool success = WS_TRUE;

		    /* Pass these to the byte-code */
		    if (!ws_bc_add_pragma_access_domain(compiler->bc, yyvsp[-2].string->data,
						        yyvsp[-2].string->len))
		        success = WS_FALSE;

		    if (!ws_bc_add_pragma_access_path(compiler->bc, yyvsp[0].string->data,
						      yyvsp[0].string->len))
		        success = WS_FALSE;

		    if (!success)
		        ws_error_memory(pctx);

		    ws_lexer_free_utf8(compiler, yyvsp[-2].string);
		    ws_lexer_free_utf8(compiler, yyvsp[0].string);
		}
    break;

  case 21:
#line 212 "wmlscript/wsgram.y"
    {
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Meta information for the origin servers.  Show it
                     * to the user if requested. */
		    if (compiler->params.meta_name_cb)
		        (*compiler->params.meta_name_cb)(
					yyvsp[0].meta_body->property_name, yyvsp[0].meta_body->content,
					yyvsp[0].meta_body->scheme,
					compiler->params.meta_name_cb_context);

		    /* We do not need the MetaBody anymore. */
		    ws_pragma_meta_body_free(compiler, yyvsp[0].meta_body);
		}
    break;

  case 22:
#line 230 "wmlscript/wsgram.y"
    {
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Meta information HTTP header that should be
                     * included to an HTTP response header.  Show it to
                     * the user if requested. */
		    if (compiler->params.meta_http_equiv_cb)
		        (*compiler->params.meta_http_equiv_cb)(
				yyvsp[0].meta_body->property_name,
				yyvsp[0].meta_body->content,
				yyvsp[0].meta_body->scheme,
				compiler->params.meta_http_equiv_cb_context);

		    /* We do not need the MetaBody anymore. */
		    ws_pragma_meta_body_free(compiler, yyvsp[0].meta_body);
		}
    break;

  case 23:
#line 250 "wmlscript/wsgram.y"
    {
		    WsBool success;
		    WsCompiler *compiler = (WsCompiler *) pctx;

		    /* Pass this pragma to the byte-code */
		    if (yyvsp[0].meta_body) {
		        if (yyvsp[0].meta_body->scheme)
		  	    success
			  = ws_bc_add_pragma_user_agent_property_and_scheme(
						compiler->bc,
						yyvsp[0].meta_body->property_name->data,
						yyvsp[0].meta_body->property_name->len,
						yyvsp[0].meta_body->content->data,
						yyvsp[0].meta_body->content->len,
						yyvsp[0].meta_body->scheme->data,
						yyvsp[0].meta_body->scheme->len);
		        else
		  	    success = ws_bc_add_pragma_user_agent_property(
						compiler->bc,
						yyvsp[0].meta_body->property_name->data,
						yyvsp[0].meta_body->property_name->len,
						yyvsp[0].meta_body->content->data,
						yyvsp[0].meta_body->content->len);

		        /* Free the MetaBody. */
		        ws_pragma_meta_body_free(compiler, yyvsp[0].meta_body);

		        if (!success)
		  	    ws_error_memory(pctx);
		    }
		}
    break;

  case 24:
#line 285 "wmlscript/wsgram.y"
    { yyval.meta_body = ws_pragma_meta_body(pctx, yyvsp[-1].string, yyvsp[0].string, NULL); }
    break;

  case 25:
#line 287 "wmlscript/wsgram.y"
    { yyval.meta_body = ws_pragma_meta_body(pctx, yyvsp[-2].string, yyvsp[-1].string, yyvsp[0].string); }
    break;

  case 31:
#line 304 "wmlscript/wsgram.y"
    {
		    char *name = ws_strdup(yyvsp[-5].identifier);

		    ws_lexer_free_block(pctx, yyvsp[-5].identifier);

		    if (name)
		        ws_function(pctx, yyvsp[-7].boolean, name, yylsp[-5].first_line, yyvsp[-3].list, yyvsp[-1].list);
		    else
		        ws_error_memory(pctx);
		}
    break;

  case 32:
#line 317 "wmlscript/wsgram.y"
    { yyval.boolean = WS_FALSE; }
    break;

  case 33:
#line 318 "wmlscript/wsgram.y"
    { yyval.boolean = WS_TRUE;  }
    break;

  case 34:
#line 323 "wmlscript/wsgram.y"
    { yyval.list = ws_list_new(pctx); }
    break;

  case 38:
#line 334 "wmlscript/wsgram.y"
    {
                    char *id;
                    WsFormalParm *parm;

		    id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree, yyvsp[0].identifier);
                    parm = ws_formal_parameter(pctx, yylsp[0].first_line, id);

		    ws_lexer_free_block(pctx, yyvsp[0].identifier);

		    if (id == NULL || parm == NULL) {
		        ws_error_memory(pctx);
		        yyval.list = NULL;
		    } else {
		        yyval.list = ws_list_new(pctx);
		        ws_list_append(pctx, yyval.list, parm);
		    }
		}
    break;

  case 39:
#line 352 "wmlscript/wsgram.y"
    {
                    char *id;
                    WsFormalParm *parm;

		    id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree, yyvsp[0].identifier);
                    parm = ws_formal_parameter(pctx, yylsp[-2].first_line, id);

		    ws_lexer_free_block(pctx, yyvsp[0].identifier);

		    if (id == NULL || parm == NULL) {
		        ws_error_memory(pctx);
		        yyval.list = NULL;
		    } else
		        ws_list_append(pctx, yyvsp[-2].list, parm);
		}
    break;

  case 40:
#line 373 "wmlscript/wsgram.y"
    {
		    if (yyvsp[0].list)
		        yyval.stmt = ws_stmt_block(pctx, yyvsp[0].list->first_line, yyvsp[0].list->last_line,
				           yyvsp[0].list);
		    else
		        yyval.stmt = NULL;
		}
    break;

  case 42:
#line 382 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_empty(pctx, yylsp[0].first_line); }
    break;

  case 43:
#line 384 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_expr(pctx, yyvsp[-1].expr->line, yyvsp[-1].expr); }
    break;

  case 46:
#line 388 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_continue(pctx, yylsp[-1].first_line); }
    break;

  case 47:
#line 390 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_break(pctx, yylsp[-1].first_line); }
    break;

  case 49:
#line 395 "wmlscript/wsgram.y"
    {
		    yyval.list = yyvsp[-1].list;
		    if (yyval.list) {
		        yyval.list->first_line = yylsp[-2].first_line;
		        yyval.list->last_line = yylsp[0].first_line;
		    }
		}
    break;

  case 50:
#line 403 "wmlscript/wsgram.y"
    {
		    ws_error_syntax(pctx, yylsp[0].first_line);
		    yyval.list = NULL;
		}
    break;

  case 51:
#line 411 "wmlscript/wsgram.y"
    { yyval.list = ws_list_new(pctx); }
    break;

  case 53:
#line 417 "wmlscript/wsgram.y"
    {
		    yyval.list = ws_list_new(pctx);
		    ws_list_append(pctx, yyval.list, yyvsp[0].stmt);
		}
    break;

  case 54:
#line 422 "wmlscript/wsgram.y"
    { ws_list_append(pctx, yyvsp[-1].list, yyvsp[0].stmt); }
    break;

  case 55:
#line 427 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_variable(pctx, yylsp[-2].first_line, yyvsp[-1].list); }
    break;

  case 56:
#line 429 "wmlscript/wsgram.y"
    { ws_error_syntax(pctx, yylsp[0].first_line); }
    break;

  case 57:
#line 434 "wmlscript/wsgram.y"
    {
		    yyval.list = ws_list_new(pctx);
		    ws_list_append(pctx, yyval.list, yyvsp[0].vardec);
		}
    break;

  case 58:
#line 439 "wmlscript/wsgram.y"
    { ws_list_append(pctx, yyvsp[-2].list, yyvsp[0].vardec); }
    break;

  case 59:
#line 444 "wmlscript/wsgram.y"
    {
		    char *id = ws_f_strdup(((WsCompiler *) pctx)->pool_stree,
					   yyvsp[-1].identifier);

		    ws_lexer_free_block(pctx, yyvsp[-1].identifier);
		    if (id == NULL) {
		        ws_error_memory(pctx);
		        yyval.vardec = NULL;
		    } else
		        yyval.vardec = ws_variable_declaration(pctx, id, yyvsp[0].expr);
		}
    break;

  case 60:
#line 459 "wmlscript/wsgram.y"
    { yyval.expr = NULL; }
    break;

  case 61:
#line 461 "wmlscript/wsgram.y"
    { yyval.expr = yyvsp[0].expr; }
    break;

  case 62:
#line 466 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_if(pctx, yylsp[-6].first_line, yyvsp[-4].expr, yyvsp[-2].stmt, yyvsp[0].stmt); }
    break;

  case 63:
#line 468 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_if(pctx, yylsp[-4].first_line, yyvsp[-2].expr, yyvsp[0].stmt, NULL); }
    break;

  case 64:
#line 473 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_while(pctx, yylsp[-4].first_line, yyvsp[-2].expr, yyvsp[0].stmt); }
    break;

  case 66:
#line 480 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_for(pctx, yylsp[-8].first_line, NULL, yyvsp[-6].expr, yyvsp[-4].expr, yyvsp[-2].expr, yyvsp[0].stmt); }
    break;

  case 67:
#line 483 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_for(pctx, yylsp[-9].first_line, yyvsp[-6].list, NULL, yyvsp[-4].expr, yyvsp[-2].expr, yyvsp[0].stmt); }
    break;

  case 68:
#line 488 "wmlscript/wsgram.y"
    { yyval.stmt = ws_stmt_return(pctx, yylsp[-2].first_line, yyvsp[-1].expr); }
    break;

  case 69:
#line 494 "wmlscript/wsgram.y"
    { yyval.expr = NULL; }
    break;

  case 72:
#line 502 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_comma(pctx, yylsp[-1].first_line, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 74:
#line 508 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, '=', yyvsp[0].expr); }
    break;

  case 75:
#line 510 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tMULA, yyvsp[0].expr); }
    break;

  case 76:
#line 512 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tDIVA, yyvsp[0].expr); }
    break;

  case 77:
#line 514 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tREMA, yyvsp[0].expr); }
    break;

  case 78:
#line 516 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tADDA, yyvsp[0].expr); }
    break;

  case 79:
#line 518 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tSUBA, yyvsp[0].expr); }
    break;

  case 80:
#line 520 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tLSHIFTA, yyvsp[0].expr); }
    break;

  case 81:
#line 522 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tRSSHIFTA, yyvsp[0].expr); }
    break;

  case 82:
#line 524 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tRSZSHIFTA, yyvsp[0].expr); }
    break;

  case 83:
#line 526 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tANDA, yyvsp[0].expr); }
    break;

  case 84:
#line 528 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tXORA, yyvsp[0].expr); }
    break;

  case 85:
#line 530 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tORA, yyvsp[0].expr); }
    break;

  case 86:
#line 532 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_assign(pctx, yylsp[-2].first_line, yyvsp[-2].identifier, tIDIVA, yyvsp[0].expr); }
    break;

  case 88:
#line 538 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_conditional(pctx, yylsp[-3].first_line, yyvsp[-4].expr, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 90:
#line 544 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_logical(pctx, yylsp[-1].first_line, WS_ASM_SCOR, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 92:
#line 550 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_logical(pctx, yylsp[-1].first_line, WS_ASM_SCAND, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 94:
#line 556 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_OR, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 96:
#line 562 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_XOR, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 98:
#line 568 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_AND, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 100:
#line 574 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_EQ, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 101:
#line 576 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_NE, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 103:
#line 582 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_LT, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 104:
#line 584 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_GT, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 105:
#line 586 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_LE, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 106:
#line 588 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_GE, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 108:
#line 594 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_LSHIFT, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 109:
#line 596 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_RSSHIFT, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 110:
#line 598 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_B_RSZSHIFT, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 112:
#line 604 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_ADD, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 113:
#line 606 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_SUB, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 115:
#line 612 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_MUL, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 116:
#line 614 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_DIV, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 117:
#line 616 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_IDIV, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 118:
#line 618 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_REM, yyvsp[-2].expr, yyvsp[0].expr); }
    break;

  case 120:
#line 624 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_TYPEOF, yyvsp[0].expr); }
    break;

  case 121:
#line 626 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_ISVALID, yyvsp[0].expr); }
    break;

  case 122:
#line 628 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary_var(pctx, yylsp[-1].first_line, WS_TRUE, yyvsp[0].identifier); }
    break;

  case 123:
#line 630 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary_var(pctx, yylsp[-1].first_line, WS_FALSE, yyvsp[0].identifier); }
    break;

  case 124:
#line 632 "wmlscript/wsgram.y"
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
                    yyval.expr = ws_expr_binary(pctx, yylsp[-1].first_line, WS_ASM_SUB, yyvsp[0].expr,
                              ws_expr_const_integer(pctx, yylsp[-1].first_line, 0));
		}
    break;

  case 125:
#line 648 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_UMINUS, yyvsp[0].expr); }
    break;

  case 126:
#line 650 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_B_NOT, yyvsp[0].expr); }
    break;

  case 127:
#line 652 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_unary(pctx, yylsp[-1].first_line, WS_ASM_NOT, yyvsp[0].expr); }
    break;

  case 129:
#line 658 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_postfix_var(pctx, yylsp[-1].first_line, WS_TRUE, yyvsp[-1].identifier); }
    break;

  case 130:
#line 660 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_postfix_var(pctx, yylsp[-1].first_line, WS_FALSE, yyvsp[-1].identifier); }
    break;

  case 132:
#line 666 "wmlscript/wsgram.y"
    {
		    WsFunctionHash *f = ws_function_hash(pctx, yyvsp[-1].identifier);

		    /* Add an usage count for the local script function. */
		    if (f)
		      f->usage_count++;

		    yyval.expr = ws_expr_call(pctx, yylsp[-1].first_line, ' ', NULL, yyvsp[-1].identifier, yyvsp[0].list);
		}
    break;

  case 133:
#line 676 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_call(pctx, yylsp[-1].first_line, '#', yyvsp[-3].identifier, yyvsp[-1].identifier, yyvsp[0].list); }
    break;

  case 134:
#line 678 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_call(pctx, yylsp[-1].first_line, '.', yyvsp[-3].identifier, yyvsp[-1].identifier, yyvsp[0].list); }
    break;

  case 135:
#line 683 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_symbol(pctx, yylsp[0].first_line, yyvsp[0].identifier); }
    break;

  case 136:
#line 685 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_const_invalid(pctx, yylsp[0].first_line); }
    break;

  case 137:
#line 687 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_const_true(pctx, yylsp[0].first_line); }
    break;

  case 138:
#line 689 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_const_false(pctx, yylsp[0].first_line); }
    break;

  case 139:
#line 691 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_const_integer(pctx, yylsp[0].first_line, yyvsp[0].integer); }
    break;

  case 140:
#line 693 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_const_float(pctx, yylsp[0].first_line, yyvsp[0].vfloat); }
    break;

  case 141:
#line 695 "wmlscript/wsgram.y"
    { yyval.expr = ws_expr_const_string(pctx, yylsp[0].first_line, yyvsp[0].string); }
    break;

  case 142:
#line 697 "wmlscript/wsgram.y"
    { yyval.expr = yyvsp[-1].expr; }
    break;

  case 143:
#line 702 "wmlscript/wsgram.y"
    { yyval.list = ws_list_new(pctx); }
    break;

  case 144:
#line 704 "wmlscript/wsgram.y"
    { yyval.list = yyvsp[-1].list; }
    break;

  case 145:
#line 709 "wmlscript/wsgram.y"
    {
		    yyval.list = ws_list_new(pctx);
		    ws_list_append(pctx, yyval.list, yyvsp[0].expr);
		}
    break;

  case 146:
#line 714 "wmlscript/wsgram.y"
    { ws_list_append(pctx, yyvsp[-2].list, yyvsp[0].expr); }
    break;


    }

/* Line 999 of yacc.c.  */
#line 2221 "y.tab.c"

  yyvsp -= yylen;
  yyssp -= yylen;
  yylsp -= yylen;

  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  YYSIZE_T yysize = 0;
	  int yytype = YYTRANSLATE (yychar);
	  char *yymsg;
	  int yyx, yycount;

	  yycount = 0;
	  /* Start YYX at -YYN if negative to avoid negative indexes in
	     YYCHECK.  */
	  for (yyx = yyn < 0 ? -yyn : 0;
	       yyx < (int) (sizeof (yytname) / sizeof (char *)); yyx++)
	    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	      yysize += yystrlen (yytname[yyx]) + 15, yycount++;
	  yysize += yystrlen ("syntax error, unexpected ") + 1;
	  yysize += yystrlen (yytname[yytype]);
	  yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg != 0)
	    {
	      char *yyp = yystpcpy (yymsg, "syntax error, unexpected ");
	      yyp = yystpcpy (yyp, yytname[yytype]);

	      if (yycount < 5)
		{
		  yycount = 0;
		  for (yyx = yyn < 0 ? -yyn : 0;
		       yyx < (int) (sizeof (yytname) / sizeof (char *));
		       yyx++)
		    if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
		      {
			const char *yyq = ! yycount ? ", expecting " : " or ";
			yyp = yystpcpy (yyp, yyq);
			yyp = yystpcpy (yyp, yytname[yyx]);
			yycount++;
		      }
		}
	      yyerror (yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    yyerror ("syntax error; also virtual memory exhausted");
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror ("syntax error");
    }

  yylerrsp = yylsp;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      /* Return failure if at end of input.  */
      if (yychar == YYEOF)
        {
	  /* Pop the error token.  */
          YYPOPSTACK;
	  /* Pop the rest of the stack.  */
	  while (yyss < yyssp)
	    {
	      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
	      yydestruct (yystos[*yyssp], yyvsp, yylsp);
	      YYPOPSTACK;
	    }
	  YYABORT;
        }

      YYDSYMPRINTF ("Error: discarding", yytoken, &yylval, &yylloc);
      yydestruct (yytoken, &yylval, &yylloc);
      yychar = YYEMPTY;
      *++yylerrsp = yylloc;
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*----------------------------------------------------.
| yyerrlab1 -- error raised explicitly by an action.  |
`----------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      YYDSYMPRINTF ("Error: popping", yystos[*yyssp], yyvsp, yylsp);
      yydestruct (yystos[yystate], yyvsp, yylsp);
      yyvsp--;
      yystate = *--yyssp;
      yylsp--;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  YYDPRINTF ((stderr, "Shifting error token, "));

  *++yyvsp = yylval;
  YYLLOC_DEFAULT (yyloc, yylsp, (yylerrsp - yylsp));
  *++yylsp = yyloc;

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*----------------------------------------------.
| yyoverflowlab -- parser overflow comes here.  |
`----------------------------------------------*/
yyoverflowlab:
  yyerror ("parser stack overflow");
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 717 "wmlscript/wsgram.y"


void
yyerror(char *msg)
{
#if WS_DEBUG
  fprintf(stderr, "*** %s:%d: wsc: %s - this msg will be removed ***\n",
	  global_compiler->input_name, global_compiler->linenum, msg);
#endif /* WS_DEBUG */
}

