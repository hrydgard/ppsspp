                                                            -*- Autoconf -*-

# Java language support for Bison

# Copyright (C) 2007-2012 Free Software Foundation, Inc.

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

# b4_comment(TEXT)
# ----------------
m4_define([b4_comment], [/* m4_bpatsubst([$1], [
], [
   ])  */])


# b4_list2(LIST1, LIST2)
# --------------------------
# Join two lists with a comma if necessary.
m4_define([b4_list2],
	  [$1[]m4_ifval(m4_quote($1), [m4_ifval(m4_quote($2), [[, ]])])[]$2])


# b4_percent_define_get3(DEF, PRE, POST, NOT)
# -------------------------------------------
# Expand to the value of DEF surrounded by PRE and POST if it's %define'ed,
# otherwise NOT.
m4_define([b4_percent_define_get3],
	  [m4_ifval(m4_quote(b4_percent_define_get([$1])),
		[$2[]b4_percent_define_get([$1])[]$3], [$4])])



# b4_flag_value(BOOLEAN-FLAG)
# ---------------------------
m4_define([b4_flag_value], [b4_flag_if([$1], [true], [false])])


# b4_public_if(TRUE, FALSE)
# -------------------------
b4_percent_define_default([[public]], [[false]])
m4_define([b4_public_if],
[b4_percent_define_flag_if([public], [$1], [$2])])


# b4_abstract_if(TRUE, FALSE)
# ---------------------------
b4_percent_define_default([[abstract]], [[false]])
m4_define([b4_abstract_if],
[b4_percent_define_flag_if([abstract], [$1], [$2])])


# b4_final_if(TRUE, FALSE)
# ---------------------------
b4_percent_define_default([[final]], [[false]])
m4_define([b4_final_if],
[b4_percent_define_flag_if([final], [$1], [$2])])


# b4_strictfp_if(TRUE, FALSE)
# ---------------------------
b4_percent_define_default([[strictfp]], [[false]])
m4_define([b4_strictfp_if],
[b4_percent_define_flag_if([strictfp], [$1], [$2])])


# b4_lexer_if(TRUE, FALSE)
# ------------------------
m4_define([b4_lexer_if],
[b4_percent_code_ifdef([[lexer]], [$1], [$2])])


# b4_identification
# -----------------
m4_define([b4_identification],
[  /** Version number for the Bison executable that generated this parser.  */
  public static final String bisonVersion = "b4_version";

  /** Name of the skeleton that generated this parser.  */
  public static final String bisonSkeleton = b4_skeleton;
])


## ------------ ##
## Data types.  ##
## ------------ ##

# b4_int_type(MIN, MAX)
# ---------------------
# Return the smallest int type able to handle numbers ranging from
# MIN to MAX (included).
m4_define([b4_int_type],
[m4_if(b4_ints_in($@,   [-128],   [127]), [1], [byte],
       b4_ints_in($@, [-32768], [32767]), [1], [short],
					       [int])])

# b4_int_type_for(NAME)
# ---------------------
# Return the smallest int type able to handle numbers ranging from
# `NAME_min' to `NAME_max' (included).
m4_define([b4_int_type_for],
[b4_int_type($1_min, $1_max)])

# b4_null
# -------
m4_define([b4_null], [null])


## ------------------------- ##
## Assigning token numbers.  ##
## ------------------------- ##

# b4_token_enum(TOKEN-NAME, TOKEN-NUMBER)
# ---------------------------------------
# Output the definition of this token as an enum.
m4_define([b4_token_enum],
[  /** Token number, to be returned by the scanner.  */
  public static final int $1 = $2;
])


# b4_token_enums(LIST-OF-PAIRS-TOKEN-NAME-TOKEN-NUMBER)
# -----------------------------------------------------
# Output the definition of the tokens (if there are) as enums.
m4_define([b4_token_enums],
[m4_if([$#$1], [1], [],
[/* Tokens.  */
m4_map([b4_token_enum], [$@])])
])

# b4-case(ID, CODE)
# -----------------
# We need to fool Java's stupid unreachable code detection.
m4_define([b4_case], [  case $1:
  if (yyn == $1)
    $2;
  break;
    ])


## ---------------- ##
## Default values.  ##
## ---------------- ##

m4_define([b4_yystype], [b4_percent_define_get([[stype]])])
b4_percent_define_default([[stype]], [[Object]])

# %name-prefix
m4_define_default([b4_prefix], [[YY]])

b4_percent_define_default([[parser_class_name]], [b4_prefix[]Parser])
m4_define([b4_parser_class_name], [b4_percent_define_get([[parser_class_name]])])

b4_percent_define_default([[lex_throws]], [[java.io.IOException]])
m4_define([b4_lex_throws], [b4_percent_define_get([[lex_throws]])])

b4_percent_define_default([[throws]], [])
m4_define([b4_throws], [b4_percent_define_get([[throws]])])

b4_percent_define_default([[api.location.type]], [Location])
m4_define([b4_location_type], [b4_percent_define_get([[api.location.type]])])

b4_percent_define_default([[api.position.type]], [Position])
m4_define([b4_position_type], [b4_percent_define_get([[api.position.type]])])


## ----------------- ##
## Semantic Values.  ##
## ----------------- ##


# b4_lhs_value([TYPE])
# --------------------
# Expansion of $<TYPE>$.
m4_define([b4_lhs_value], [yyval])


# b4_rhs_value(RULE-LENGTH, NUM, [TYPE])
# --------------------------------------
# Expansion of $<TYPE>NUM, where the current rule has RULE-LENGTH
# symbols on RHS.
#
# In this simple implementation, %token and %type have class names
# between the angle brackets.
m4_define([b4_rhs_value],
[(m4_ifval($3, [($3)])[](yystack.valueAt ($1-($2))))])

# b4_lhs_location()
# -----------------
# Expansion of @$.
m4_define([b4_lhs_location],
[(yyloc)])


# b4_rhs_location(RULE-LENGTH, NUM)
# ---------------------------------
# Expansion of @NUM, where the current rule has RULE-LENGTH symbols
# on RHS.
m4_define([b4_rhs_location],
[yystack.locationAt ($1-($2))])


# b4_lex_param
# b4_parse_param
# --------------
# If defined, b4_lex_param arrives double quoted, but below we prefer
# it to be single quoted.  Same for b4_parse_param.

# TODO: should be in bison.m4
m4_define_default([b4_lex_param], [[]])
m4_define([b4_lex_param], b4_lex_param)
m4_define([b4_parse_param], b4_parse_param)

# b4_lex_param_decl
# -------------------
# Extra formal arguments of the constructor.
m4_define([b4_lex_param_decl],
[m4_ifset([b4_lex_param],
          [b4_remove_comma([$1],
			   b4_param_decls(b4_lex_param))],
	  [$1])])

m4_define([b4_param_decls],
	  [m4_map([b4_param_decl], [$@])])
m4_define([b4_param_decl], [, $1])

m4_define([b4_remove_comma], [m4_ifval(m4_quote($1), [$1, ], [])m4_shift2($@)])



# b4_parse_param_decl
# -------------------
# Extra formal arguments of the constructor.
m4_define([b4_parse_param_decl],
[m4_ifset([b4_parse_param],
          [b4_remove_comma([$1],
			   b4_param_decls(b4_parse_param))],
	  [$1])])



# b4_lex_param_call
# -------------------
# Delegating the lexer parameters to the lexer constructor.
m4_define([b4_lex_param_call],
          [m4_ifset([b4_lex_param],
	            [b4_remove_comma([$1],
				     b4_param_calls(b4_lex_param))],
	            [$1])])
m4_define([b4_param_calls],
	  [m4_map([b4_param_call], [$@])])
m4_define([b4_param_call], [, $2])



# b4_parse_param_cons
# -------------------
# Extra initialisations of the constructor.
m4_define([b4_parse_param_cons],
          [m4_ifset([b4_parse_param],
		    [b4_constructor_calls(b4_parse_param)])])

m4_define([b4_constructor_calls],
	  [m4_map([b4_constructor_call], [$@])])
m4_define([b4_constructor_call],
	  [this.$2 = $2;
	  ])



# b4_parse_param_vars
# -------------------
# Extra instance variables.
m4_define([b4_parse_param_vars],
          [m4_ifset([b4_parse_param],
		    [
    /* User arguments.  */
b4_var_decls(b4_parse_param)])])

m4_define([b4_var_decls],
	  [m4_map_sep([b4_var_decl], [
], [$@])])
m4_define([b4_var_decl],
	  [    protected final $1;])



# b4_maybe_throws(THROWS)
# -----------------------
# Expand to either an empty string or "throws THROWS".
m4_define([b4_maybe_throws],
	  [m4_ifval($1, [throws $1])])
