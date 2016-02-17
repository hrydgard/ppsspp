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

m4_include(b4_pkgdatadir/[c++.m4])

m4_define([b4_parser_class_name],
          [b4_percent_define_get([[parser_class_name]])])

# The header is mandatory.
b4_defines_if([],
              [b4_fatal([b4_skeleton[: using %%defines is mandatory]])])

b4_percent_define_ifdef([[api.location.type]], [],
  [# Backward compatibility.
  m4_define([b4_location_constructors])
  m4_include(b4_pkgdatadir/[location.cc])])
m4_include(b4_pkgdatadir/[stack.hh])

b4_defines_if(
[b4_output_begin([b4_spec_defines_file])
b4_copyright([Skeleton interface for Bison LALR(1) parsers in C++],
             [2002-2012])
[
/**
 ** \file ]b4_spec_defines_file[
 ** Define the ]b4_namespace_ref[::parser class.
 */

/* C++ LALR(1) parser skeleton written by Akim Demaille.  */

]b4_cpp_guard_open([b4_spec_defines_file])[

]b4_percent_code_get([[requires]])[

#include <string>
#include <iostream>
#include "stack.hh"
]b4_percent_define_ifdef([[api.location.type]], [],
                         [[#include "location.hh"]])[

]b4_YYDEBUG_define[

]b4_namespace_open[

  /// A Bison parser.
  class ]b4_parser_class_name[
  {
  public:
    /// Symbol semantic values.
#ifndef ]b4_api_PREFIX[STYPE
]m4_ifdef([b4_stype],
[    union semantic_type
    {
b4_user_stype
    };],
[m4_if(b4_tag_seen_flag, 0,
[[    typedef int semantic_type;]],
[[    typedef ]b4_api_PREFIX[STYPE semantic_type;]])])[
#else
    typedef ]b4_api_PREFIX[STYPE semantic_type;
#endif
    /// Symbol locations.
    typedef ]b4_percent_define_get([[api.location.type]],
                                   [[location]])[ location_type;
    /// Tokens.
    struct token
    {
      ]b4_token_enums(b4_tokens)[
    };
    /// Token type.
    typedef token::yytokentype token_type;

    /// Build a parser object.
    ]b4_parser_class_name[ (]b4_parse_param_decl[);
    virtual ~]b4_parser_class_name[ ();

    /// Parse.
    /// \returns  0 iff parsing succeeded.
    virtual int parse ();

#if ]b4_api_PREFIX[DEBUG
    /// The current debugging stream.
    std::ostream& debug_stream () const;
    /// Set the current debugging stream.
    void set_debug_stream (std::ostream &);

    /// Type for debugging levels.
    typedef int debug_level_type;
    /// The current debugging level.
    debug_level_type debug_level () const;
    /// Set the current debugging level.
    void set_debug_level (debug_level_type l);
#endif

  private:
    /// Report a syntax error.
    /// \param loc    where the syntax error is found.
    /// \param msg    a description of the syntax error.
    virtual void error (const location_type& loc, const std::string& msg);

    /// Generate an error message.
    /// \param state   the state where the error occurred.
    /// \param tok     the lookahead token.
    virtual std::string yysyntax_error_ (int yystate, int tok);

#if ]b4_api_PREFIX[DEBUG
    /// \brief Report a symbol value on the debug stream.
    /// \param yytype       The token type.
    /// \param yyvaluep     Its semantic value.
    /// \param yylocationp  Its location.
    virtual void yy_symbol_value_print_ (int yytype,
					 const semantic_type* yyvaluep,
					 const location_type* yylocationp);
    /// \brief Report a symbol on the debug stream.
    /// \param yytype       The token type.
    /// \param yyvaluep     Its semantic value.
    /// \param yylocationp  Its location.
    virtual void yy_symbol_print_ (int yytype,
				   const semantic_type* yyvaluep,
				   const location_type* yylocationp);
#endif


    /// State numbers.
    typedef int state_type;
    /// State stack type.
    typedef stack<state_type>    state_stack_type;
    /// Semantic value stack type.
    typedef stack<semantic_type> semantic_stack_type;
    /// location stack type.
    typedef stack<location_type> location_stack_type;

    /// The state stack.
    state_stack_type yystate_stack_;
    /// The semantic value stack.
    semantic_stack_type yysemantic_stack_;
    /// The location stack.
    location_stack_type yylocation_stack_;

    /// Whether the given \c yypact_ value indicates a defaulted state.
    /// \param yyvalue   the value to check
    static bool yy_pact_value_is_default_ (int yyvalue);

    /// Whether the given \c yytable_ value indicates a syntax error.
    /// \param yyvalue   the value to check
    static bool yy_table_value_is_error_ (int yyvalue);

    /// Internal symbol numbers.
    typedef ]b4_int_type_for([b4_translate])[ token_number_type;
    /* Tables.  */
    /// For a state, the index in \a yytable_ of its portion.
    static const ]b4_int_type_for([b4_pact])[ yypact_[];
    static const ]b4_int_type(b4_pact_ninf, b4_pact_ninf)[ yypact_ninf_;

    /// For a state, default reduction number.
    /// Unless\a  yytable_ specifies something else to do.
    /// Zero means the default is an error.
    static const ]b4_int_type_for([b4_defact])[ yydefact_[];

    static const ]b4_int_type_for([b4_pgoto])[ yypgoto_[];
    static const ]b4_int_type_for([b4_defgoto])[ yydefgoto_[];

    /// What to do in a state.
    /// \a yytable_[yypact_[s]]: what to do in state \a s.
    /// - if positive, shift that token.
    /// - if negative, reduce the rule which number is the opposite.
    /// - if zero, do what YYDEFACT says.
    static const ]b4_int_type_for([b4_table])[ yytable_[];
    static const ]b4_int_type(b4_table_ninf, b4_table_ninf)[ yytable_ninf_;

    static const ]b4_int_type_for([b4_check])[ yycheck_[];

    /// For a state, its accessing symbol.
    static const ]b4_int_type_for([b4_stos])[ yystos_[];

    /// For a rule, its LHS.
    static const ]b4_int_type_for([b4_r1])[ yyr1_[];
    /// For a rule, its RHS length.
    static const ]b4_int_type_for([b4_r2])[ yyr2_[]; ]b4_error_verbose_if([

    /// Convert the symbol name \a n to a form suitable for a diagnostic.
    static std::string yytnamerr_ (const char *n);])[

]b4_token_table_if([], [[#if ]b4_api_PREFIX[DEBUG]])[
    /// For a symbol, its name in clear.
    static const char* const yytname_[];
]b4_token_table_if([[#if ]b4_api_PREFIX[DEBUG]])[
    /// A type to store symbol numbers and -1.
    typedef ]b4_int_type_for([b4_rhs])[ rhs_number_type;
    /// A `-1'-separated list of the rules' RHS.
    static const rhs_number_type yyrhs_[];
    /// For each rule, the index of the first RHS symbol in \a yyrhs_.
    static const ]b4_int_type_for([b4_prhs])[ yyprhs_[];
    /// For each rule, its source line number.
    static const ]b4_int_type_for([b4_rline])[ yyrline_[];
    /// For each scanner token number, its symbol number.
    static const ]b4_int_type_for([b4_toknum])[ yytoken_number_[];
    /// Report on the debug stream that the rule \a r is going to be reduced.
    virtual void yy_reduce_print_ (int r);
    /// Print the state stack on the debug stream.
    virtual void yystack_print_ ();

    /* Debugging.  */
    int yydebug_;
    std::ostream* yycdebug_;
#endif

    /// Convert a scanner token number \a t to a symbol number.
    token_number_type yytranslate_ (int t);

    /// \brief Reclaim the memory associated to a symbol.
    /// \param yymsg        Why this token is reclaimed.
    ///                     If null, do not display the symbol, just free it.
    /// \param yytype       The symbol type.
    /// \param yyvaluep     Its semantic value.
    /// \param yylocationp  Its location.
    inline void yydestruct_ (const char* yymsg,
			     int yytype,
			     semantic_type* yyvaluep,
			     location_type* yylocationp);

    /// Pop \a n symbols the three stacks.
    inline void yypop_ (unsigned int n = 1);

    /* Constants.  */
    static const int yyeof_;
    /* LAST_ -- Last index in TABLE_.  */
    static const int yylast_;
    static const int yynnts_;
    static const int yyempty_;
    static const int yyfinal_;
    static const int yyterror_;
    static const int yyerrcode_;
    static const int yyntokens_;
    static const unsigned int yyuser_token_number_max_;
    static const token_number_type yyundef_token_;
]b4_parse_param_vars[
  };
]b4_namespace_close[

]b4_percent_define_flag_if([[global_tokens_and_yystype]],
[b4_token_defines(b4_tokens)

#ifndef ]b4_api_PREFIX[STYPE
 /* Redirection for backward compatibility.  */
# define ]b4_api_PREFIX[STYPE b4_namespace_ref::b4_parser_class_name::semantic_type
#endif
])[
]b4_percent_code_get([[provides]])[
]b4_cpp_guard_close([b4_spec_defines_file])
b4_output_end()
])


b4_output_begin([b4_parser_file_name])
b4_copyright([Skeleton implementation for Bison LALR(1) parsers in C++],
             [2002-2012])
b4_percent_code_get([[top]])[]dnl
m4_if(b4_prefix, [yy], [],
[
// Take the name prefix into account.
#define yylex   b4_prefix[]lex])[

/* First part of user declarations.  */
]b4_user_pre_prologue[

]b4_defines_if([[
#include "@basename(]b4_spec_defines_file[@)"]])[

/* User implementation prologue.  */
]b4_user_post_prologue[
]b4_percent_code_get[

]b4_null_define[

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* FIXME: INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
]b4_yylloc_default_define[

/* Suppress unused-variable warnings by "using" E.  */
#define YYUSE(e) ((void) (e))

/* Enable debugging if requested.  */
#if ]b4_api_PREFIX[DEBUG

/* A pseudo ostream that takes yydebug_ into account.  */
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)	\
do {							\
  if (yydebug_)						\
    {							\
      *yycdebug_ << Title << ' ';			\
      yy_symbol_print_ ((Type), (Value), (Location));	\
      *yycdebug_ << std::endl;				\
    }							\
} while (false)

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug_)				\
    yy_reduce_print_ (Rule);		\
} while (false)

# define YY_STACK_PRINT()		\
do {					\
  if (yydebug_)				\
    yystack_print_ ();			\
} while (false)

#else /* !]b4_api_PREFIX[DEBUG */

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Type, Value, Location) YYUSE(Type)
# define YY_REDUCE_PRINT(Rule)        static_cast<void>(0)
# define YY_STACK_PRINT()             static_cast<void>(0)

#endif /* !]b4_api_PREFIX[DEBUG */

#define yyerrok		(yyerrstatus_ = 0)
#define yyclearin	(yychar = yyempty_)

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

]b4_namespace_open[]b4_error_verbose_if([[

  /* Return YYSTR after stripping away unnecessary quotes and
     backslashes, so that it's suitable for yyerror.  The heuristic is
     that double-quoting is unnecessary unless the string contains an
     apostrophe, a comma, or backslash (other than backslash-backslash).
     YYSTR is taken from yytname.  */
  std::string
  ]b4_parser_class_name[::yytnamerr_ (const char *yystr)
  {
    if (*yystr == '"')
      {
        std::string yyr = "";
        char const *yyp = yystr;

        for (;;)
          switch (*++yyp)
            {
            case '\'':
            case ',':
              goto do_not_strip_quotes;

            case '\\':
              if (*++yyp != '\\')
                goto do_not_strip_quotes;
              /* Fall through.  */
            default:
              yyr += *yyp;
              break;

            case '"':
              return yyr;
            }
      do_not_strip_quotes: ;
      }

    return yystr;
  }
]])[

  /// Build a parser object.
  ]b4_parser_class_name::b4_parser_class_name[ (]b4_parse_param_decl[)]m4_ifset([b4_parse_param], [
    :])[
#if ]b4_api_PREFIX[DEBUG
    ]m4_ifset([b4_parse_param], [  ], [ :])[yydebug_ (false),
      yycdebug_ (&std::cerr)]m4_ifset([b4_parse_param], [,])[
#endif]b4_parse_param_cons[
  {
  }

  ]b4_parser_class_name::~b4_parser_class_name[ ()
  {
  }

#if ]b4_api_PREFIX[DEBUG
  /*--------------------------------.
  | Print this symbol on YYOUTPUT.  |
  `--------------------------------*/

  inline void
  ]b4_parser_class_name[::yy_symbol_value_print_ (int yytype,
			   const semantic_type* yyvaluep, const location_type* yylocationp)
  {
    YYUSE (yylocationp);
    YYUSE (yyvaluep);
    std::ostream& yyo = debug_stream ();
    std::ostream& yyoutput = yyo;
    YYUSE (yyoutput);
    switch (yytype)
      {
  ]m4_map([b4_symbol_actions], m4_defn([b4_symbol_printers]))dnl
[       default:
	  break;
      }
  }


  void
  ]b4_parser_class_name[::yy_symbol_print_ (int yytype,
			   const semantic_type* yyvaluep, const location_type* yylocationp)
  {
    *yycdebug_ << (yytype < yyntokens_ ? "token" : "nterm")
	       << ' ' << yytname_[yytype] << " ("
	       << *yylocationp << ": ";
    yy_symbol_value_print_ (yytype, yyvaluep, yylocationp);
    *yycdebug_ << ')';
  }
#endif

  void
  ]b4_parser_class_name[::yydestruct_ (const char* yymsg,
			   int yytype, semantic_type* yyvaluep, location_type* yylocationp)
  {
    YYUSE (yylocationp);
    YYUSE (yymsg);
    YYUSE (yyvaluep);

    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

    switch (yytype)
      {
  ]m4_map([b4_symbol_actions], m4_defn([b4_symbol_destructors]))[
	default:
	  break;
      }
  }

  void
  ]b4_parser_class_name[::yypop_ (unsigned int n)
  {
    yystate_stack_.pop (n);
    yysemantic_stack_.pop (n);
    yylocation_stack_.pop (n);
  }

#if ]b4_api_PREFIX[DEBUG
  std::ostream&
  ]b4_parser_class_name[::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  ]b4_parser_class_name[::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  ]b4_parser_class_name[::debug_level_type
  ]b4_parser_class_name[::debug_level () const
  {
    return yydebug_;
  }

  void
  ]b4_parser_class_name[::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif

  inline bool
  ]b4_parser_class_name[::yy_pact_value_is_default_ (int yyvalue)
  {
    return yyvalue == yypact_ninf_;
  }

  inline bool
  ]b4_parser_class_name[::yy_table_value_is_error_ (int yyvalue)
  {
    return yyvalue == yytable_ninf_;
  }

  int
  ]b4_parser_class_name[::parse ()
  {
    /// Lookahead and lookahead in internal form.
    int yychar = yyempty_;
    int yytoken = 0;

    // State.
    int yyn;
    int yylen = 0;
    int yystate = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// Semantic value of the lookahead.
    static semantic_type yyval_default;
    semantic_type yylval = yyval_default;
    /// Location of the lookahead.
    location_type yylloc;
    /// The locations where the error started and ended.
    location_type yyerror_range[3];

    /// $$.
    semantic_type yyval;
    /// @@$.
    location_type yyloc;

    int yyresult;

    // FIXME: This shoud be completely indented.  It is not yet to
    // avoid gratuitous conflicts when merging into the master branch.
    try
      {
    YYCDEBUG << "Starting parse" << std::endl;

]m4_ifdef([b4_initial_action], [
b4_dollar_pushdef([yylval], [], [yylloc])dnl
/* User initialization code.  */
b4_user_initial_action
b4_dollar_popdef])[]dnl

  [  /* Initialize the stacks.  The initial state will be pushed in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystate_stack_ = state_stack_type (0);
    yysemantic_stack_ = semantic_stack_type (0);
    yylocation_stack_ = location_stack_type (0);
    yysemantic_stack_.push (yylval);
    yylocation_stack_.push (yylloc);

    /* New state.  */
  yynewstate:
    yystate_stack_.push (yystate);
    YYCDEBUG << "Entering state " << yystate << std::endl;

    /* Accept?  */
    if (yystate == yyfinal_)
      goto yyacceptlab;

    goto yybackup;

    /* Backup.  */
  yybackup:

    /* Try to take a decision without lookahead.  */
    yyn = yypact_[yystate];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    /* Read a lookahead token.  */
    if (yychar == yyempty_)
      {
        YYCDEBUG << "Reading a token: ";
        yychar = ]b4_c_function_call([yylex], [int],
                                     [b4_api_PREFIX[STYPE*], [&yylval]][]dnl
b4_locations_if([, [[location*], [&yylloc]]])dnl
m4_ifdef([b4_lex_param], [, ]b4_lex_param))[;
      }

    /* Convert token to internal form.  */
    if (yychar <= yyeof_)
      {
	yychar = yytoken = yyeof_;
	YYCDEBUG << "Now at end of input." << std::endl;
      }
    else
      {
	yytoken = yytranslate_ (yychar);
	YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
      }

    /* If the proper action on seeing token YYTOKEN is to reduce or to
       detect an error, take that action.  */
    yyn += yytoken;
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yytoken)
      goto yydefault;

    /* Reduce or error.  */
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
	if (yy_table_value_is_error_ (yyn))
	  goto yyerrlab;
	yyn = -yyn;
	goto yyreduce;
      }

    /* Shift the lookahead token.  */
    YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

    /* Discard the token being shifted.  */
    yychar = yyempty_;

    yysemantic_stack_.push (yylval);
    yylocation_stack_.push (yylloc);

    /* Count tokens shifted since error; after three, turn off error
       status.  */
    if (yyerrstatus_)
      --yyerrstatus_;

    yystate = yyn;
    goto yynewstate;

  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[yystate];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;

  /*-----------------------------.
  | yyreduce -- Do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    /* If YYLEN is nonzero, implement the default value of the action:
       `$$ = $1'.  Otherwise, use the top of the stack.

       Otherwise, the following line sets YYVAL to garbage.
       This behavior is undocumented and Bison
       users should not rely upon it.  */
    if (yylen)
      yyval = yysemantic_stack_[yylen - 1];
    else
      yyval = yysemantic_stack_[0];

    // Compute the default @@$.
    {
      slice<location_type, location_stack_type> slice (yylocation_stack_, yylen);
      YYLLOC_DEFAULT (yyloc, slice, yylen);
    }

    // Perform the reduction.
    YY_REDUCE_PRINT (yyn);
    switch (yyn)
      {
        ]b4_user_actions[
      default:
        break;
      }

    /* User semantic actions sometimes alter yychar, and that requires
       that yytoken be updated with the new translation.  We take the
       approach of translating immediately before every use of yytoken.
       One alternative is translating here after every semantic action,
       but that translation would be missed if the semantic action
       invokes YYABORT, YYACCEPT, or YYERROR immediately after altering
       yychar.  In the case of YYABORT or YYACCEPT, an incorrect
       destructor might then be invoked immediately.  In the case of
       YYERROR, subsequent parser actions might lead to an incorrect
       destructor call or verbose syntax error message before the
       lookahead is translated.  */
    YY_SYMBOL_PRINT ("-> $$ =", yyr1_[yyn], &yyval, &yyloc);

    yypop_ (yylen);
    yylen = 0;
    YY_STACK_PRINT ();

    yysemantic_stack_.push (yyval);
    yylocation_stack_.push (yyloc);

    /* Shift the result of the reduction.  */
    yyn = yyr1_[yyn];
    yystate = yypgoto_[yyn - yyntokens_] + yystate_stack_[0];
    if (0 <= yystate && yystate <= yylast_
	&& yycheck_[yystate] == yystate_stack_[0])
      yystate = yytable_[yystate];
    else
      yystate = yydefgoto_[yyn - yyntokens_];
    goto yynewstate;

  /*------------------------------------.
  | yyerrlab -- here on detecting error |
  `------------------------------------*/
  yyerrlab:
    /* Make sure we have latest lookahead translation.  See comments at
       user semantic actions for why this is necessary.  */
    yytoken = yytranslate_ (yychar);

    /* If not already recovering from an error, report this error.  */
    if (!yyerrstatus_)
      {
	++yynerrs_;
	if (yychar == yyempty_)
	  yytoken = yyempty_;
	error (yylloc, yysyntax_error_ (yystate, yytoken));
      }

    yyerror_range[1] = yylloc;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */
        if (yychar <= yyeof_)
          {
            /* Return failure if at end of input.  */
            if (yychar == yyeof_)
              YYABORT;
          }
        else
          {
            yydestruct_ ("Error: discarding", yytoken, &yylval, &yylloc);
            yychar = yyempty_;
          }
      }

    /* Else will try to reuse lookahead token after shifting the error
       token.  */
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:

    /* Pacify compilers like GCC when the user code never invokes
       YYERROR and the label yyerrorlab therefore never appears in user
       code.  */
    if (false)
      goto yyerrorlab;

    yyerror_range[1] = yylocation_stack_[yylen - 1];
    /* Do not reclaim the symbols of the rule which action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    yystate = yystate_stack_[0];
    goto yyerrlab1;

  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;	/* Each real token shifted decrements this.  */

    for (;;)
      {
	yyn = yypact_[yystate];
	if (!yy_pact_value_is_default_ (yyn))
	{
	  yyn += yyterror_;
	  if (0 <= yyn && yyn <= yylast_ && yycheck_[yyn] == yyterror_)
	    {
	      yyn = yytable_[yyn];
	      if (0 < yyn)
		break;
	    }
	}

	/* Pop the current state because it cannot handle the error token.  */
	if (yystate_stack_.height () == 1)
	  YYABORT;

	yyerror_range[1] = yylocation_stack_[0];
	yydestruct_ ("Error: popping",
		     yystos_[yystate],
		     &yysemantic_stack_[0], &yylocation_stack_[0]);
	yypop_ ();
	yystate = yystate_stack_[0];
	YY_STACK_PRINT ();
      }

    yyerror_range[2] = yylloc;
    // Using YYLLOC is tempting, but would change the location of
    // the lookahead.  YYLOC is available though.
    YYLLOC_DEFAULT (yyloc, yyerror_range, 2);
    yysemantic_stack_.push (yylval);
    yylocation_stack_.push (yyloc);

    /* Shift the error token.  */
    YY_SYMBOL_PRINT ("Shifting", yystos_[yyn],
		     &yysemantic_stack_[0], &yylocation_stack_[0]);

    yystate = yyn;
    goto yynewstate;

    /* Accept.  */
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;

    /* Abort.  */
  yyabortlab:
    yyresult = 1;
    goto yyreturn;

  yyreturn:
    if (yychar != yyempty_)
      {
        /* Make sure we have latest lookahead translation.  See comments
           at user semantic actions for why this is necessary.  */
        yytoken = yytranslate_ (yychar);
        yydestruct_ ("Cleanup: discarding lookahead", yytoken, &yylval,
                     &yylloc);
      }

    /* Do not reclaim the symbols of the rule which action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    while (1 < yystate_stack_.height ())
      {
        yydestruct_ ("Cleanup: popping",
                     yystos_[yystate_stack_[0]],
                     &yysemantic_stack_[0],
                     &yylocation_stack_[0]);
        yypop_ ();
      }

    return yyresult;
    }
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack"
                 << std::endl;
        // Do not try to display the values of the reclaimed symbols,
        // as their printer might throw an exception.
        if (yychar != yyempty_)
          {
            /* Make sure we have latest lookahead translation.  See
               comments at user semantic actions for why this is
               necessary.  */
            yytoken = yytranslate_ (yychar);
            yydestruct_ (YY_NULL, yytoken, &yylval, &yylloc);
          }

        while (1 < yystate_stack_.height ())
          {
            yydestruct_ (YY_NULL,
                         yystos_[yystate_stack_[0]],
                         &yysemantic_stack_[0],
                         &yylocation_stack_[0]);
            yypop_ ();
          }
        throw;
      }
  }

  // Generate an error message.
  std::string
  ]b4_parser_class_name[::yysyntax_error_ (]dnl
b4_error_verbose_if([int yystate, int yytoken],
                    [int, int])[)
  {]b4_error_verbose_if([[
    std::string yyres;
    // Number of reported tokens (one for the "unexpected", one per
    // "expected").
    size_t yycount = 0;
    // Its maximum.
    enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
    // Arguments of yyformat.
    char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];

    /* There are many possibilities here to consider:
       - If this state is a consistent state with a default action, then
         the only way this function was invoked is if the default action
         is an error action.  In that case, don't check for expected
         tokens because there are none.
       - The only way there can be no lookahead present (in yytoken) is
         if this state is a consistent state with a default action.
         Thus, detecting the absence of a lookahead is sufficient to
         determine that there is no unexpected or expected token to
         report.  In that case, just report a simple "syntax error".
       - Don't assume there isn't a lookahead just because this state is
         a consistent state with a default action.  There might have
         been a previous inconsistent state, consistent state with a
         non-default action, or user semantic action that manipulated
         yychar.
       - Of course, the expected token list depends on states to have
         correct lookahead information, and it depends on the parser not
         to perform extra reductions after fetching a lookahead from the
         scanner and before detecting a syntax error.  Thus, state
         merging (from LALR or IELR) and default reductions corrupt the
         expected token list.  However, the list is correct for
         canonical LR with one exception: it will still contain any
         token that will not be accepted due to an error action in a
         later state.
    */
    if (yytoken != yyempty_)
      {
        yyarg[yycount++] = yytname_[yytoken];
        int yyn = yypact_[yystate];
        if (!yy_pact_value_is_default_ (yyn))
          {
            /* Start YYX at -YYN if negative to avoid negative indexes in
               YYCHECK.  In other words, skip the first -YYN actions for
               this state because they are default actions.  */
            int yyxbegin = yyn < 0 ? -yyn : 0;
            /* Stay within bounds of both yycheck and yytname.  */
            int yychecklim = yylast_ - yyn + 1;
            int yyxend = yychecklim < yyntokens_ ? yychecklim : yyntokens_;
            for (int yyx = yyxbegin; yyx < yyxend; ++yyx)
              if (yycheck_[yyx + yyn] == yyx && yyx != yyterror_
                  && !yy_table_value_is_error_ (yytable_[yyx + yyn]))
                {
                  if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                    {
                      yycount = 1;
                      break;
                    }
                  else
                    yyarg[yycount++] = yytname_[yyx];
                }
          }
      }

    char const* yyformat = YY_NULL;
    switch (yycount)
      {
#define YYCASE_(N, S)                         \
        case N:                               \
          yyformat = S;                       \
        break
        YYCASE_(0, YY_("syntax error"));
        YYCASE_(1, YY_("syntax error, unexpected %s"));
        YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
        YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
        YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
        YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
#undef YYCASE_
      }

    // Argument number.
    size_t yyi = 0;
    for (char const* yyp = yyformat; *yyp; ++yyp)
      if (yyp[0] == '%' && yyp[1] == 's' && yyi < yycount)
        {
          yyres += yytnamerr_ (yyarg[yyi++]);
          ++yyp;
        }
      else
        yyres += *yyp;
    return yyres;]], [[
    return YY_("syntax error");]])[
  }


  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
  const ]b4_int_type(b4_pact_ninf, b4_pact_ninf) b4_parser_class_name::yypact_ninf_ = b4_pact_ninf[;
  const ]b4_int_type_for([b4_pact])[
  ]b4_parser_class_name[::yypact_[] =
  {
    ]b4_pact[
  };

  /* YYDEFACT[S] -- default reduction number in state S.  Performed when
     YYTABLE doesn't specify something else to do.  Zero means the
     default is an error.  */
  const ]b4_int_type_for([b4_defact])[
  ]b4_parser_class_name[::yydefact_[] =
  {
    ]b4_defact[
  };

  /* YYPGOTO[NTERM-NUM].  */
  const ]b4_int_type_for([b4_pgoto])[
  ]b4_parser_class_name[::yypgoto_[] =
  {
    ]b4_pgoto[
  };

  /* YYDEFGOTO[NTERM-NUM].  */
  const ]b4_int_type_for([b4_defgoto])[
  ]b4_parser_class_name[::yydefgoto_[] =
  {
    ]b4_defgoto[
  };

  /* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule which
     number is the opposite.  If YYTABLE_NINF_, syntax error.  */
  const ]b4_int_type(b4_table_ninf, b4_table_ninf) b4_parser_class_name::yytable_ninf_ = b4_table_ninf[;
  const ]b4_int_type_for([b4_table])[
  ]b4_parser_class_name[::yytable_[] =
  {
    ]b4_table[
  };

  /* YYCHECK.  */
  const ]b4_int_type_for([b4_check])[
  ]b4_parser_class_name[::yycheck_[] =
  {
    ]b4_check[
  };

  /* STOS_[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
  const ]b4_int_type_for([b4_stos])[
  ]b4_parser_class_name[::yystos_[] =
  {
    ]b4_stos[
  };

#if ]b4_api_PREFIX[DEBUG
  /* TOKEN_NUMBER_[YYLEX-NUM] -- Internal symbol number corresponding
     to YYLEX-NUM.  */
  const ]b4_int_type_for([b4_toknum])[
  ]b4_parser_class_name[::yytoken_number_[] =
  {
    ]b4_toknum[
  };
#endif

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
  const ]b4_int_type_for([b4_r1])[
  ]b4_parser_class_name[::yyr1_[] =
  {
    ]b4_r1[
  };

  /* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
  const ]b4_int_type_for([b4_r2])[
  ]b4_parser_class_name[::yyr2_[] =
  {
    ]b4_r2[
  };

]b4_token_table_if([], [[#if ]b4_api_PREFIX[DEBUG]])[
  /* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
     First, the terminals, then, starting at \a yyntokens_, nonterminals.  */
  const char*
  const ]b4_parser_class_name[::yytname_[] =
  {
    ]b4_tname[
  };

]b4_token_table_if([[#if ]b4_api_PREFIX[DEBUG]])[
  /* YYRHS -- A `-1'-separated list of the rules' RHS.  */
  const ]b4_parser_class_name[::rhs_number_type
  ]b4_parser_class_name[::yyrhs_[] =
  {
    ]b4_rhs[
  };

  /* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
     YYRHS.  */
  const ]b4_int_type_for([b4_prhs])[
  ]b4_parser_class_name[::yyprhs_[] =
  {
    ]b4_prhs[
  };

  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
  const ]b4_int_type_for([b4_rline])[
  ]b4_parser_class_name[::yyrline_[] =
  {
    ]b4_rline[
  };

  // Print the state stack on the debug stream.
  void
  ]b4_parser_class_name[::yystack_print_ ()
  {
    *yycdebug_ << "Stack now";
    for (state_stack_type::const_iterator i = yystate_stack_.begin ();
	 i != yystate_stack_.end (); ++i)
      *yycdebug_ << ' ' << *i;
    *yycdebug_ << std::endl;
  }

  // Report on the debug stream that the rule \a yyrule is going to be reduced.
  void
  ]b4_parser_class_name[::yy_reduce_print_ (int yyrule)
  {
    unsigned int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    /* Print the symbols being reduced, and their result.  */
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
	       << " (line " << yylno << "):" << std::endl;
    /* The symbols being reduced.  */
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
		       yyrhs_[yyprhs_[yyrule] + yyi],
		       &]b4_rhs_value(yynrhs, yyi + 1)[,
		       &]b4_rhs_location(yynrhs, yyi + 1)[);
  }
#endif // ]b4_api_PREFIX[DEBUG

  /* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
  ]b4_parser_class_name[::token_number_type
  ]b4_parser_class_name[::yytranslate_ (int t)
  {
    static
    const token_number_type
    translate_table[] =
    {
      ]b4_translate[
    };
    if ((unsigned int) t <= yyuser_token_number_max_)
      return translate_table[t];
    else
      return yyundef_token_;
  }

  const int ]b4_parser_class_name[::yyeof_ = 0;
  const int ]b4_parser_class_name[::yylast_ = ]b4_last[;
  const int ]b4_parser_class_name[::yynnts_ = ]b4_nterms_number[;
  const int ]b4_parser_class_name[::yyempty_ = -2;
  const int ]b4_parser_class_name[::yyfinal_ = ]b4_final_state_number[;
  const int ]b4_parser_class_name[::yyterror_ = 1;
  const int ]b4_parser_class_name[::yyerrcode_ = 256;
  const int ]b4_parser_class_name[::yyntokens_ = ]b4_tokens_number[;

  const unsigned int ]b4_parser_class_name[::yyuser_token_number_max_ = ]b4_user_token_number_max[;
  const ]b4_parser_class_name[::token_number_type ]b4_parser_class_name[::yyundef_token_ = ]b4_undef_token_number[;

]b4_namespace_close[
]b4_epilogue[]dnl
b4_output_end()
