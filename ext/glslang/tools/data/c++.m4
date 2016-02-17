                                                            -*- Autoconf -*-

# C++ skeleton for Bison

# Copyright (C) 2002-2012 Free Software Foundation, Inc.

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

m4_include(b4_pkgdatadir/[c.m4])

## ---------------- ##
## Default values.  ##
## ---------------- ##

# Default parser class name.
b4_percent_define_default([[parser_class_name]], [[parser]])

# Don't do that so that we remember whether we're using a user
# request, or the default value.
#
# b4_percent_define_default([[api.location.type]], [[location]])

b4_percent_define_default([[filename_type]], [[std::string]])
b4_percent_define_default([[namespace]], m4_defn([b4_prefix]))
b4_percent_define_default([[global_tokens_and_yystype]], [[false]])
b4_percent_define_default([[define_location_comparison]],
                          [m4_if(b4_percent_define_get([[filename_type]]),
                                 [std::string], [[true]], [[false]])])


## ----------- ##
## Namespace.  ##
## ----------- ##

m4_define([b4_namespace_ref], [b4_percent_define_get([[namespace]])])

# Don't permit an empty b4_namespace_ref.  Any `::parser::foo' appended to it
# would compile as an absolute reference with `parser' in the global namespace.
# b4_namespace_open would open an anonymous namespace and thus establish
# internal linkage.  This would compile.  However, it's cryptic, and internal
# linkage for the parser would be specified in all translation units that
# include the header, which is always generated.  If we ever need to permit
# internal linkage somehow, surely we can find a cleaner approach.
m4_if(m4_bregexp(b4_namespace_ref, [^[	 ]*$]), [-1], [],
[b4_complain_at(b4_percent_define_get_loc([[namespace]]),
                [[namespace reference is empty]])])

# Instead of assuming the C++ compiler will do it, Bison should reject any
# invalid b4_namepsace_ref that would be converted to a valid
# b4_namespace_open.  The problem is that Bison doesn't always output
# b4_namespace_ref to uncommented code but should reserve the ability to do so
# in future releases without risking breaking any existing user grammars.
# Specifically, don't allow empty names as b4_namespace_open would just convert
# those into anonymous namespaces, and that might tempt some users.
m4_if(m4_bregexp(b4_namespace_ref, [::[	 ]*::]), [-1], [],
[b4_complain_at(b4_percent_define_get_loc([[namespace]]),
                [[namespace reference has consecutive "::"]])])
m4_if(m4_bregexp(b4_namespace_ref, [::[	 ]*$]), [-1], [],
[b4_complain_at(b4_percent_define_get_loc([[namespace]]),
                [[namespace reference has a trailing "::"]])])

m4_define([b4_namespace_open],
[b4_user_code([b4_percent_define_get_syncline([[namespace]])
[namespace ]m4_bpatsubst(m4_dquote(m4_bpatsubst(m4_dquote(b4_namespace_ref),
                                                [^\(.\)[	 ]*::], [\1])),
                         [::], [ { namespace ])[ {]])])

m4_define([b4_namespace_close],
[b4_user_code([b4_percent_define_get_syncline([[namespace]])
m4_bpatsubst(m4_dquote(m4_bpatsubst(m4_dquote(b4_namespace_ref[ ]),
                                    [^\(.\)[	 ]*\(::\)?\([^][:]\|:[^:]\)*],
                                    [\1])),
             [::\([^][:]\|:[^:]\)*], [} ])[} // ]b4_namespace_ref])])


# b4_token_enums(LIST-OF-PAIRS-TOKEN-NAME-TOKEN-NUMBER)
# -----------------------------------------------------
# Output the definition of the tokens as enums.
m4_define([b4_token_enums],
[/* Tokens.  */
   enum yytokentype {
m4_map_sep([     b4_token_enum], [,
],
           [$@])
   };
])




## ----------------- ##
## Semantic Values.  ##
## ----------------- ##


# b4_lhs_value([TYPE])
# --------------------
# Expansion of $<TYPE>$.
m4_define([b4_lhs_value],
[(yyval[]m4_ifval([$1], [.$1]))])


# b4_rhs_value(RULE-LENGTH, NUM, [TYPE])
# --------------------------------------
# Expansion of $<TYPE>NUM, where the current rule has RULE-LENGTH
# symbols on RHS.
m4_define([b4_rhs_value],
[(yysemantic_stack_@{($1) - ($2)@}m4_ifval([$3], [.$3]))])

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
[(yylocation_stack_@{($1) - ($2)@})])


# b4_parse_param_decl
# -------------------
# Extra formal arguments of the constructor.
# Change the parameter names from "foo" into "foo_yyarg", so that
# there is no collision bw the user chosen attribute name, and the
# argument name in the constructor.
m4_define([b4_parse_param_decl],
[m4_ifset([b4_parse_param],
          [m4_map_sep([b4_parse_param_decl_1], [, ], [b4_parse_param])])])

m4_define([b4_parse_param_decl_1],
[$1_yyarg])



# b4_parse_param_cons
# -------------------
# Extra initialisations of the constructor.
m4_define([b4_parse_param_cons],
          [m4_ifset([b4_parse_param],
		    [
      b4_cc_constructor_calls(b4_parse_param)])])
m4_define([b4_cc_constructor_calls],
	  [m4_map_sep([b4_cc_constructor_call], [,
      ], [$@])])
m4_define([b4_cc_constructor_call],
	  [$2 ($2_yyarg)])

# b4_parse_param_vars
# -------------------
# Extra instance variables.
m4_define([b4_parse_param_vars],
          [m4_ifset([b4_parse_param],
		    [
    /* User arguments.  */
b4_cc_var_decls(b4_parse_param)])])
m4_define([b4_cc_var_decls],
	  [m4_map_sep([b4_cc_var_decl], [
], [$@])])
m4_define([b4_cc_var_decl],
	  [    $1;])


## ---------##
## Values.  ##
## ---------##

# b4_yylloc_default_define
# ------------------------
# Define YYLLOC_DEFAULT.
m4_define([b4_yylloc_default_define],
[[/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (/*CONSTCOND*/ false)
# endif
]])
