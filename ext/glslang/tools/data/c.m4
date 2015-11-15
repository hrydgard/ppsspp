                                                            -*- Autoconf -*-

# C M4 Macros for Bison.

# Copyright (C) 2002, 2004-2012 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

m4_include(b4_pkgdatadir/[c-like.m4])

# b4_tocpp(STRING)
# ----------------
# Convert STRING into a valid C macro name.
m4_define([b4_tocpp],
[m4_toupper(m4_bpatsubst(m4_quote($1), [[^a-zA-Z0-9]+], [_]))])


# b4_cpp_guard(FILE)
# ------------------
# A valid C macro name to use as a CPP header guard for FILE.
m4_define([b4_cpp_guard],
[[YY_]b4_tocpp(m4_defn([b4_prefix])/[$1])[_INCLUDED]])


# b4_cpp_guard_open(FILE)
# b4_cpp_guard_close(FILE)
# ------------------------
# If FILE does not expand to nothing, open/close CPP inclusion guards for FILE.
m4_define([b4_cpp_guard_open],
[m4_ifval(m4_quote($1),
[#ifndef b4_cpp_guard([$1])
# define b4_cpp_guard([$1])])])

m4_define([b4_cpp_guard_close],
[m4_ifval(m4_quote($1),
[#endif b4_comment([!b4_cpp_guard([$1])])])])


## ---------------- ##
## Identification.  ##
## ---------------- ##

# b4_comment(TEXT)
# ----------------
m4_define([b4_comment], [/* m4_bpatsubst([$1], [
], [
   ])  */])

# b4_identification
# -----------------
# Depends on individual skeletons to define b4_pure_flag, b4_push_flag, or
# b4_pull_flag if they use the values of the %define variables api.pure or
# api.push-pull.
m4_define([b4_identification],
[[/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "]b4_version["

/* Skeleton name.  */
#define YYSKELETON_NAME ]b4_skeleton[]m4_ifdef([b4_pure_flag], [[

/* Pure parsers.  */
#define YYPURE ]b4_pure_flag])[]m4_ifdef([b4_push_flag], [[

/* Push parsers.  */
#define YYPUSH ]b4_push_flag])[]m4_ifdef([b4_pull_flag], [[

/* Pull parsers.  */
#define YYPULL ]b4_pull_flag])[
]])


## ---------------- ##
## Default values.  ##
## ---------------- ##

# b4_api_prefix, b4_api_PREFIX
# ----------------------------
# Corresponds to %define api.prefix
b4_percent_define_default([[api.prefix]], [[yy]])
m4_define([b4_api_prefix],
[b4_percent_define_get([[api.prefix]])])
m4_define([b4_api_PREFIX],
[m4_toupper(b4_api_prefix)])


# b4_prefix
# ---------
# If the %name-prefix is not given, it is api.prefix.
m4_define_default([b4_prefix], [b4_api_prefix])

# If the %union is not named, its name is YYSTYPE.
m4_define_default([b4_union_name], [b4_api_PREFIX[]STYPE])


## ------------------------ ##
## Pure/impure interfaces.  ##
## ------------------------ ##

# b4_user_args
# ------------
m4_define([b4_user_args],
[m4_ifset([b4_parse_param], [, b4_c_args(b4_parse_param)])])


# b4_parse_param
# --------------
# If defined, b4_parse_param arrives double quoted, but below we prefer
# it to be single quoted.
m4_define([b4_parse_param],
b4_parse_param)


# b4_parse_param_for(DECL, FORMAL, BODY)
# ---------------------------------------
# Iterate over the user parameters, binding the declaration to DECL,
# the formal name to FORMAL, and evaluating the BODY.
m4_define([b4_parse_param_for],
[m4_foreach([$1_$2], m4_defn([b4_parse_param]),
[m4_pushdef([$1], m4_unquote(m4_car($1_$2)))dnl
m4_pushdef([$2], m4_shift($1_$2))dnl
$3[]dnl
m4_popdef([$2])dnl
m4_popdef([$1])dnl
])])

# b4_parse_param_use
# ------------------
# `YYUSE' all the parse-params.
m4_define([b4_parse_param_use],
[b4_parse_param_for([Decl], [Formal], [  YYUSE (Formal);
])dnl
])


## ------------ ##
## Data Types.  ##
## ------------ ##

# b4_int_type(MIN, MAX)
# ---------------------
# Return the smallest int type able to handle numbers ranging from
# MIN to MAX (included).
m4_define([b4_int_type],
[m4_if(b4_ints_in($@,      [0],   [255]), [1], [unsigned char],
       b4_ints_in($@,   [-128],   [127]), [1], [signed char],

       b4_ints_in($@,      [0], [65535]), [1], [unsigned short int],
       b4_ints_in($@, [-32768], [32767]), [1], [short int],

       m4_eval([0 <= $1]),                [1], [unsigned int],

                                               [int])])


# b4_int_type_for(NAME)
# ---------------------
# Return the smallest int type able to handle numbers ranging from
# `NAME_min' to `NAME_max' (included).
m4_define([b4_int_type_for],
[b4_int_type($1_min, $1_max)])


# b4_table_value_equals(TABLE, VALUE, LITERAL)
# --------------------------------------------
# Without inducing a comparison warning from the compiler, check if the
# literal value LITERAL equals VALUE from table TABLE, which must have
# TABLE_min and TABLE_max defined.  YYID must be defined as an identity
# function that suppresses warnings about constant conditions.
m4_define([b4_table_value_equals],
[m4_if(m4_eval($3 < m4_indir([b4_]$1[_min])
               || m4_indir([b4_]$1[_max]) < $3), [1],
       [[YYID (0)]],
       [(!!(($2) == ($3)))])])


## ---------##
## Values.  ##
## ---------##


# b4_null_define
# --------------
# Portability issues: define a YY_NULL appropriate for the current
# language (C, C++98, or C++11).
m4_define([b4_null_define],
[# ifndef YY_NULL
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULL nullptr
#  else
#   define YY_NULL 0
#  endif
# endif[]dnl
])


# b4_null
# -------
# Return a null pointer constant.
m4_define([b4_null], [YY_NULL])



## ------------------------- ##
## Assigning token numbers.  ##
## ------------------------- ##

# b4_token_define(TOKEN-NAME, TOKEN-NUMBER)
# -----------------------------------------
# Output the definition of this token as #define.
m4_define([b4_token_define],
[#define $1 $2
])


# b4_token_defines(LIST-OF-PAIRS-TOKEN-NAME-TOKEN-NUMBER)
# -------------------------------------------------------
# Output the definition of the tokens (if there are) as #defines.
m4_define([b4_token_defines],
[m4_if([$#$1], [1], [],
[/* Tokens.  */
m4_map([b4_token_define], [$@])])
])


# b4_token_enum(TOKEN-NAME, TOKEN-NUMBER)
# ---------------------------------------
# Output the definition of this token as an enum.
m4_define([b4_token_enum],
[$1 = $2])


# b4_token_enums(LIST-OF-PAIRS-TOKEN-NAME-TOKEN-NUMBER)
# -----------------------------------------------------
# Output the definition of the tokens (if there are) as enums.
m4_define([b4_token_enums],
[m4_if([$#$1], [1], [],
[[/* Tokens.  */
#ifndef ]b4_api_PREFIX[TOKENTYPE
# define ]b4_api_PREFIX[TOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum ]b4_api_prefix[tokentype {
]m4_map_sep([     b4_token_enum], [,
],
           [$@])[
   };
#endif
]])])


# b4_token_enums_defines(LIST-OF-PAIRS-TOKEN-NAME-TOKEN-NUMBER)
# -------------------------------------------------------------
# Output the definition of the tokens (if there are any) as enums and, if POSIX
# Yacc is enabled, as #defines.
m4_define([b4_token_enums_defines],
[b4_token_enums($@)b4_yacc_if([b4_token_defines($@)], [])
])



## --------------------------------------------- ##
## Defining C functions in both K&R and ANSI-C.  ##
## --------------------------------------------- ##


# b4_modern_c
# -----------
# A predicate useful in #if to determine whether C is ancient or modern.
#
# If __STDC__ is defined, the compiler is modern.  IBM xlc 7.0 when run
# as 'cc' doesn't define __STDC__ (or __STDC_VERSION__) for pedantic
# reasons, but it defines __C99__FUNC__ so check that as well.
# Microsoft C normally doesn't define these macros, but it defines _MSC_VER.
# Consider a C++ compiler to be modern if it defines __cplusplus.
#
m4_define([b4_c_modern],
  [[(defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)]])

# b4_c_function_def(NAME, RETURN-VALUE, [DECL1, NAME1], ...)
# ----------------------------------------------------------
# Declare the function NAME.
m4_define([b4_c_function_def],
[#if b4_c_modern
b4_c_ansi_function_def($@)
#else
$2
$1 (b4_c_knr_formal_names(m4_shift2($@)))
b4_c_knr_formal_decls(m4_shift2($@))
#endif[]dnl
])


# b4_c_ansi_function_def(NAME, RETURN-VALUE, [DECL1, NAME1], ...)
# ---------------------------------------------------------------
# Declare the function NAME in ANSI.
m4_define([b4_c_ansi_function_def],
[$2
$1 (b4_c_ansi_formals(m4_shift2($@)))[]dnl
])


# b4_c_ansi_formals([DECL1, NAME1], ...)
# --------------------------------------
# Output the arguments ANSI-C definition.
m4_define([b4_c_ansi_formals],
[m4_if([$#], [0], [void],
       [$#$1], [1], [void],
               [m4_map_sep([b4_c_ansi_formal], [, ], [$@])])])

m4_define([b4_c_ansi_formal],
[$1])


# b4_c_knr_formal_names([DECL1, NAME1], ...)
# ------------------------------------------
# Output the argument names.
m4_define([b4_c_knr_formal_names],
[m4_map_sep([b4_c_knr_formal_name], [, ], [$@])])

m4_define([b4_c_knr_formal_name],
[$2])


# b4_c_knr_formal_decls([DECL1, NAME1], ...)
# ------------------------------------------
# Output the K&R argument declarations.
m4_define([b4_c_knr_formal_decls],
[m4_map_sep([b4_c_knr_formal_decl],
            [
],
            [$@])])

m4_define([b4_c_knr_formal_decl],
[    $1;])



## ------------------------------------------------------------ ##
## Declaring (prototyping) C functions in both K&R and ANSI-C.  ##
## ------------------------------------------------------------ ##


# b4_c_ansi_function_decl(NAME, RETURN-VALUE, [DECL1, NAME1], ...)
# ----------------------------------------------------------------
# Declare the function NAME ANSI C style.
m4_define([b4_c_ansi_function_decl],
[$2 $1 (b4_c_ansi_formals(m4_shift2($@)));[]dnl
])



# b4_c_function_decl(NAME, RETURN-VALUE, [DECL1, NAME1], ...)
# -----------------------------------------------------------
# Declare the function NAME in both K&R and ANSI C.
m4_define([b4_c_function_decl],
[#if defined __STDC__ || defined __cplusplus
b4_c_ansi_function_decl($@)
#else
$2 $1 ();
#endif[]dnl
])



## --------------------- ##
## Calling C functions.  ##
## --------------------- ##


# b4_c_function_call(NAME, RETURN-VALUE, [DECL1, NAME1], ...)
# -----------------------------------------------------------
# Call the function NAME with arguments NAME1, NAME2 etc.
m4_define([b4_c_function_call],
[$1 (b4_c_args(m4_shift2($@)))[]dnl
])


# b4_c_args([DECL1, NAME1], ...)
# ------------------------------
# Output the arguments NAME1, NAME2...
m4_define([b4_c_args],
[m4_map_sep([b4_c_arg], [, ], [$@])])

m4_define([b4_c_arg],
[$2])


## ----------- ##
## Synclines.  ##
## ----------- ##

# b4_sync_start(LINE, FILE)
# -----------------------
m4_define([b4_sync_start], [[#]line $1 $2])


## -------------- ##
## User actions.  ##
## -------------- ##

# b4_case(LABEL, STATEMENTS)
# --------------------------
m4_define([b4_case],
[  case $1:
$2
    break;])

# b4_symbol_actions(FILENAME, LINENO,
#                   SYMBOL-TAG, SYMBOL-NUM,
#                   SYMBOL-ACTION, SYMBOL-TYPENAME)
# -------------------------------------------------
# Issue the code for a symbol action (e.g., %printer).
#
# Define b4_dollar_dollar([TYPE-NAME]), and b4_at_dollar, which are
# invoked where $<TYPE-NAME>$ and @$ were specified by the user.
m4_define([b4_symbol_actions],
[b4_dollar_pushdef([(*yyvaluep)], [$6], [(*yylocationp)])dnl
      case $4: /* $3 */
b4_syncline([$2], [$1])
        $5;
b4_syncline([@oline@], [@ofile@])
        break;
b4_dollar_popdef[]dnl
])


# b4_yydestruct_generate(FUNCTION-DECLARATOR)
# -------------------------------------------
# Generate the "yydestruct" function, which declaration is issued using
# FUNCTION-DECLARATOR, which may be "b4_c_ansi_function_def" for ISO C
# or "b4_c_function_def" for K&R.
m4_define_default([b4_yydestruct_generate],
[[/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
]$1([yydestruct],
    [static void],
    [[const char *yymsg],    [yymsg]],
    [[int yytype],           [yytype]],
    [[YYSTYPE *yyvaluep],    [yyvaluep]][]dnl
b4_locations_if(            [, [[YYLTYPE *yylocationp], [yylocationp]]])[]dnl
m4_ifset([b4_parse_param], [, b4_parse_param]))[
{
  YYUSE (yyvaluep);
]b4_locations_if([  YYUSE (yylocationp);
])dnl
b4_parse_param_use[]dnl
[
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {
]m4_map([b4_symbol_actions], m4_defn([b4_symbol_destructors]))[
      default:
        break;
    }
}]dnl
])


# b4_yy_symbol_print_generate(FUNCTION-DECLARATOR)
# ------------------------------------------------
# Generate the "yy_symbol_print" function, which declaration is issued using
# FUNCTION-DECLARATOR, which may be "b4_c_ansi_function_def" for ISO C
# or "b4_c_function_def" for K&R.
m4_define_default([b4_yy_symbol_print_generate],
[[
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
]$1([yy_symbol_value_print],
    [static void],
               [[FILE *yyoutput],                       [yyoutput]],
               [[int yytype],                           [yytype]],
               [[YYSTYPE const * const yyvaluep],       [yyvaluep]][]dnl
b4_locations_if([, [[YYLTYPE const * const yylocationp], [yylocationp]]])[]dnl
m4_ifset([b4_parse_param], [, b4_parse_param]))[
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  if (!yyvaluep)
    return;
]b4_locations_if([  YYUSE (yylocationp);
])dnl
b4_parse_param_use[]dnl
[# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
]m4_map([b4_symbol_actions], m4_defn([b4_symbol_printers]))dnl
[      default:
        break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

]$1([yy_symbol_print],
    [static void],
               [[FILE *yyoutput],                       [yyoutput]],
               [[int yytype],                           [yytype]],
               [[YYSTYPE const * const yyvaluep],       [yyvaluep]][]dnl
b4_locations_if([, [[YYLTYPE const * const yylocationp], [yylocationp]]])[]dnl
m4_ifset([b4_parse_param], [, b4_parse_param]))[
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

]b4_locations_if([  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
])dnl
[  yy_symbol_value_print (yyoutput, yytype, yyvaluep]dnl
b4_locations_if([, yylocationp])[]b4_user_args[);
  YYFPRINTF (yyoutput, ")");
}]dnl
])

## -------------- ##
## Declarations.  ##
## -------------- ##

# b4_declare_yylstype
# -------------------
# Declarations that might either go into the header (if --defines) or
# in the parser body.  Declare YYSTYPE/YYLTYPE, and yylval/yylloc.
m4_define([b4_declare_yylstype],
[[#if ! defined ]b4_api_PREFIX[STYPE && ! defined ]b4_api_PREFIX[STYPE_IS_DECLARED
]m4_ifdef([b4_stype],
[[typedef union ]b4_union_name[
{
]b4_user_stype[
} ]b4_api_PREFIX[STYPE;
# define ]b4_api_PREFIX[STYPE_IS_TRIVIAL 1]],
[m4_if(b4_tag_seen_flag, 0,
[[typedef int ]b4_api_PREFIX[STYPE;
# define ]b4_api_PREFIX[STYPE_IS_TRIVIAL 1]])])[
# define ]b4_api_prefix[stype ]b4_api_PREFIX[STYPE /* obsolescent; will be withdrawn */
# define ]b4_api_PREFIX[STYPE_IS_DECLARED 1
#endif]b4_locations_if([[

#if ! defined ]b4_api_PREFIX[LTYPE && ! defined ]b4_api_PREFIX[LTYPE_IS_DECLARED
typedef struct ]b4_api_PREFIX[LTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} ]b4_api_PREFIX[LTYPE;
# define ]b4_api_prefix[ltype ]b4_api_PREFIX[LTYPE /* obsolescent; will be withdrawn */
# define ]b4_api_PREFIX[LTYPE_IS_DECLARED 1
# define ]b4_api_PREFIX[LTYPE_IS_TRIVIAL 1
#endif]])

b4_pure_if([], [[extern ]b4_api_PREFIX[STYPE ]b4_prefix[lval;
]b4_locations_if([[extern ]b4_api_PREFIX[LTYPE ]b4_prefix[lloc;]])])[]dnl
])

# b4_YYDEBUG_define
# ------------------
m4_define([b4_YYDEBUG_define],
[[/* Enabling traces.  */
]m4_if(b4_api_prefix, [yy],
[[#ifndef YYDEBUG
# define YYDEBUG ]b4_debug_flag[
#endif]],
[[#ifndef ]b4_api_PREFIX[DEBUG
# if defined YYDEBUG
#  if YYDEBUG
#   define ]b4_api_PREFIX[DEBUG 1
#  else
#   define ]b4_api_PREFIX[DEBUG 0
#  endif
# else /* ! defined YYDEBUG */
#  define ]b4_api_PREFIX[DEBUG ]b4_debug_flag[
# endif /* ! defined YYDEBUG */
#endif  /* ! defined ]b4_api_PREFIX[DEBUG */]])[]dnl
])

# b4_declare_yydebug
# ------------------
m4_define([b4_declare_yydebug],
[b4_YYDEBUG_define[
#if ]b4_api_PREFIX[DEBUG
extern int ]b4_prefix[debug;
#endif][]dnl
])

# b4_yylloc_default_define
# ------------------------
# Define YYLLOC_DEFAULT.
m4_define([b4_yylloc_default_define],
[[/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (YYID (N))                                                     \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (YYID (0))
#endif
]])

# b4_yy_location_print_define
# ---------------------------
# Define YY_LOCATION_PRINT.
m4_define([b4_yy_location_print_define],
[b4_locations_if([[
/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef __attribute__
/* This feature is available in gcc versions 2.5 and later.  */
# if (! defined __GNUC__ || __GNUC__ < 2 \
      || (__GNUC__ == 2 && __GNUC_MINOR__ < 5))
#  define __attribute__(Spec) /* empty */
# endif
#endif

#ifndef YY_LOCATION_PRINT
# if defined ]b4_api_PREFIX[LTYPE_IS_TRIVIAL && ]b4_api_PREFIX[LTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

__attribute__((__unused__))
]b4_c_function_def([yy_location_print_],
    [static unsigned],
               [[FILE *yyo],                    [yyo]],
               [[YYLTYPE const * const yylocp], [yylocp]])[
{
  unsigned res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += fprintf (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += fprintf (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += fprintf (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += fprintf (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += fprintf (yyo, "-%d", end_col);
    }
  return res;
 }

#  define YY_LOCATION_PRINT(File, Loc)          \
  yy_location_print_ (File, &(Loc))

# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif]],
[[/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif]])
])

# b4_yyloc_default
# ----------------
# Expand to a possible default value for yylloc.
m4_define([b4_yyloc_default],
[[
# if defined ]b4_api_PREFIX[LTYPE_IS_TRIVIAL && ]b4_api_PREFIX[LTYPE_IS_TRIVIAL
  = { ]m4_join([, ],
               m4_defn([b4_location_initial_line]),
               m4_defn([b4_location_initial_column]),
               m4_defn([b4_location_initial_line]),
               m4_defn([b4_location_initial_column]))[ }
# endif
]])
