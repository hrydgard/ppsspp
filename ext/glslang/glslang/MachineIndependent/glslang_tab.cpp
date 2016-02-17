/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "3.0.4"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* Copy the first part of user declarations.  */
#line 41 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:339  */


/* Based on:
ANSI C Yacc grammar

In 1985, Jeff Lee published his Yacc grammar (which is accompanied by a
matching Lex specification) for the April 30, 1985 draft version of the
ANSI C standard.  Tom Stockfisch reposted it to net.sources in 1987; that
original, as mentioned in the answer to question 17.25 of the comp.lang.c
FAQ, can be ftp'ed from ftp.uu.net, file usenet/net.sources/ansi.c.grammar.Z.

I intend to keep this version as close to the current C Standard grammar as
possible; please let me know if you discover discrepancies.

Jutta Degener, 1995
*/

#include "SymbolTable.h"
#include "ParseHelper.h"
#include "../Public/ShaderLang.h"

using namespace glslang;


#line 91 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:339  */

# ifndef YY_NULLPTR
#  if defined __cplusplus && 201103L <= __cplusplus
#   define YY_NULLPTR nullptr
#  else
#   define YY_NULLPTR 0
#  endif
# endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* In a future release of Bison, this section will be replaced
   by #include "glslang_tab.cpp.h".  */
#ifndef YY_YY_C_RELEASEBUILD_GLSLANG_GLSLANG_MACHINEINDEPENDENT_GLSLANG_TAB_CPP_H_INCLUDED
# define YY_YY_C_RELEASEBUILD_GLSLANG_GLSLANG_MACHINEINDEPENDENT_GLSLANG_TAB_CPP_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 1
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token type.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    ATTRIBUTE = 258,
    VARYING = 259,
    CONST = 260,
    BOOL = 261,
    FLOAT = 262,
    DOUBLE = 263,
    INT = 264,
    UINT = 265,
    BREAK = 266,
    CONTINUE = 267,
    DO = 268,
    ELSE = 269,
    FOR = 270,
    IF = 271,
    DISCARD = 272,
    RETURN = 273,
    SWITCH = 274,
    CASE = 275,
    DEFAULT = 276,
    SUBROUTINE = 277,
    BVEC2 = 278,
    BVEC3 = 279,
    BVEC4 = 280,
    IVEC2 = 281,
    IVEC3 = 282,
    IVEC4 = 283,
    UVEC2 = 284,
    UVEC3 = 285,
    UVEC4 = 286,
    VEC2 = 287,
    VEC3 = 288,
    VEC4 = 289,
    MAT2 = 290,
    MAT3 = 291,
    MAT4 = 292,
    CENTROID = 293,
    IN = 294,
    OUT = 295,
    INOUT = 296,
    UNIFORM = 297,
    PATCH = 298,
    SAMPLE = 299,
    BUFFER = 300,
    SHARED = 301,
    COHERENT = 302,
    VOLATILE = 303,
    RESTRICT = 304,
    READONLY = 305,
    WRITEONLY = 306,
    DVEC2 = 307,
    DVEC3 = 308,
    DVEC4 = 309,
    DMAT2 = 310,
    DMAT3 = 311,
    DMAT4 = 312,
    NOPERSPECTIVE = 313,
    FLAT = 314,
    SMOOTH = 315,
    LAYOUT = 316,
    MAT2X2 = 317,
    MAT2X3 = 318,
    MAT2X4 = 319,
    MAT3X2 = 320,
    MAT3X3 = 321,
    MAT3X4 = 322,
    MAT4X2 = 323,
    MAT4X3 = 324,
    MAT4X4 = 325,
    DMAT2X2 = 326,
    DMAT2X3 = 327,
    DMAT2X4 = 328,
    DMAT3X2 = 329,
    DMAT3X3 = 330,
    DMAT3X4 = 331,
    DMAT4X2 = 332,
    DMAT4X3 = 333,
    DMAT4X4 = 334,
    ATOMIC_UINT = 335,
    SAMPLER1D = 336,
    SAMPLER2D = 337,
    SAMPLER3D = 338,
    SAMPLERCUBE = 339,
    SAMPLER1DSHADOW = 340,
    SAMPLER2DSHADOW = 341,
    SAMPLERCUBESHADOW = 342,
    SAMPLER1DARRAY = 343,
    SAMPLER2DARRAY = 344,
    SAMPLER1DARRAYSHADOW = 345,
    SAMPLER2DARRAYSHADOW = 346,
    ISAMPLER1D = 347,
    ISAMPLER2D = 348,
    ISAMPLER3D = 349,
    ISAMPLERCUBE = 350,
    ISAMPLER1DARRAY = 351,
    ISAMPLER2DARRAY = 352,
    USAMPLER1D = 353,
    USAMPLER2D = 354,
    USAMPLER3D = 355,
    USAMPLERCUBE = 356,
    USAMPLER1DARRAY = 357,
    USAMPLER2DARRAY = 358,
    SAMPLER2DRECT = 359,
    SAMPLER2DRECTSHADOW = 360,
    ISAMPLER2DRECT = 361,
    USAMPLER2DRECT = 362,
    SAMPLERBUFFER = 363,
    ISAMPLERBUFFER = 364,
    USAMPLERBUFFER = 365,
    SAMPLERCUBEARRAY = 366,
    SAMPLERCUBEARRAYSHADOW = 367,
    ISAMPLERCUBEARRAY = 368,
    USAMPLERCUBEARRAY = 369,
    SAMPLER2DMS = 370,
    ISAMPLER2DMS = 371,
    USAMPLER2DMS = 372,
    SAMPLER2DMSARRAY = 373,
    ISAMPLER2DMSARRAY = 374,
    USAMPLER2DMSARRAY = 375,
    SAMPLEREXTERNALOES = 376,
    SAMPLER = 377,
    SAMPLERSHADOW = 378,
    TEXTURE1D = 379,
    TEXTURE2D = 380,
    TEXTURE3D = 381,
    TEXTURECUBE = 382,
    TEXTURE1DARRAY = 383,
    TEXTURE2DARRAY = 384,
    ITEXTURE1D = 385,
    ITEXTURE2D = 386,
    ITEXTURE3D = 387,
    ITEXTURECUBE = 388,
    ITEXTURE1DARRAY = 389,
    ITEXTURE2DARRAY = 390,
    UTEXTURE1D = 391,
    UTEXTURE2D = 392,
    UTEXTURE3D = 393,
    UTEXTURECUBE = 394,
    UTEXTURE1DARRAY = 395,
    UTEXTURE2DARRAY = 396,
    TEXTURE2DRECT = 397,
    ITEXTURE2DRECT = 398,
    UTEXTURE2DRECT = 399,
    TEXTUREBUFFER = 400,
    ITEXTUREBUFFER = 401,
    UTEXTUREBUFFER = 402,
    TEXTURECUBEARRAY = 403,
    ITEXTURECUBEARRAY = 404,
    UTEXTURECUBEARRAY = 405,
    TEXTURE2DMS = 406,
    ITEXTURE2DMS = 407,
    UTEXTURE2DMS = 408,
    TEXTURE2DMSARRAY = 409,
    ITEXTURE2DMSARRAY = 410,
    UTEXTURE2DMSARRAY = 411,
    SUBPASSINPUT = 412,
    SUBPASSINPUTMS = 413,
    ISUBPASSINPUT = 414,
    ISUBPASSINPUTMS = 415,
    USUBPASSINPUT = 416,
    USUBPASSINPUTMS = 417,
    IMAGE1D = 418,
    IIMAGE1D = 419,
    UIMAGE1D = 420,
    IMAGE2D = 421,
    IIMAGE2D = 422,
    UIMAGE2D = 423,
    IMAGE3D = 424,
    IIMAGE3D = 425,
    UIMAGE3D = 426,
    IMAGE2DRECT = 427,
    IIMAGE2DRECT = 428,
    UIMAGE2DRECT = 429,
    IMAGECUBE = 430,
    IIMAGECUBE = 431,
    UIMAGECUBE = 432,
    IMAGEBUFFER = 433,
    IIMAGEBUFFER = 434,
    UIMAGEBUFFER = 435,
    IMAGE1DARRAY = 436,
    IIMAGE1DARRAY = 437,
    UIMAGE1DARRAY = 438,
    IMAGE2DARRAY = 439,
    IIMAGE2DARRAY = 440,
    UIMAGE2DARRAY = 441,
    IMAGECUBEARRAY = 442,
    IIMAGECUBEARRAY = 443,
    UIMAGECUBEARRAY = 444,
    IMAGE2DMS = 445,
    IIMAGE2DMS = 446,
    UIMAGE2DMS = 447,
    IMAGE2DMSARRAY = 448,
    IIMAGE2DMSARRAY = 449,
    UIMAGE2DMSARRAY = 450,
    STRUCT = 451,
    VOID = 452,
    WHILE = 453,
    IDENTIFIER = 454,
    TYPE_NAME = 455,
    FLOATCONSTANT = 456,
    DOUBLECONSTANT = 457,
    INTCONSTANT = 458,
    UINTCONSTANT = 459,
    BOOLCONSTANT = 460,
    LEFT_OP = 461,
    RIGHT_OP = 462,
    INC_OP = 463,
    DEC_OP = 464,
    LE_OP = 465,
    GE_OP = 466,
    EQ_OP = 467,
    NE_OP = 468,
    AND_OP = 469,
    OR_OP = 470,
    XOR_OP = 471,
    MUL_ASSIGN = 472,
    DIV_ASSIGN = 473,
    ADD_ASSIGN = 474,
    MOD_ASSIGN = 475,
    LEFT_ASSIGN = 476,
    RIGHT_ASSIGN = 477,
    AND_ASSIGN = 478,
    XOR_ASSIGN = 479,
    OR_ASSIGN = 480,
    SUB_ASSIGN = 481,
    LEFT_PAREN = 482,
    RIGHT_PAREN = 483,
    LEFT_BRACKET = 484,
    RIGHT_BRACKET = 485,
    LEFT_BRACE = 486,
    RIGHT_BRACE = 487,
    DOT = 488,
    COMMA = 489,
    COLON = 490,
    EQUAL = 491,
    SEMICOLON = 492,
    BANG = 493,
    DASH = 494,
    TILDE = 495,
    PLUS = 496,
    STAR = 497,
    SLASH = 498,
    PERCENT = 499,
    LEFT_ANGLE = 500,
    RIGHT_ANGLE = 501,
    VERTICAL_BAR = 502,
    CARET = 503,
    AMPERSAND = 504,
    QUESTION = 505,
    INVARIANT = 506,
    PRECISE = 507,
    HIGH_PRECISION = 508,
    MEDIUM_PRECISION = 509,
    LOW_PRECISION = 510,
    PRECISION = 511,
    PACKED = 512,
    RESOURCE = 513,
    SUPERP = 514
  };
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED

union YYSTYPE
{
#line 66 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:355  */

    struct {
        glslang::TSourceLoc loc;
        union {
            glslang::TString *string;
            int i;
            unsigned int u;
            bool b;
            double d;
        };
        glslang::TSymbol* symbol;
    } lex;
    struct {
        glslang::TSourceLoc loc;
        glslang::TOperator op;
        union {
            TIntermNode* intermNode;
            glslang::TIntermNodePair nodePair;
            glslang::TIntermTyped* intermTypedNode;
        };
        union {
            glslang::TPublicType type;
            glslang::TFunction* function;
            glslang::TParameter param;
            glslang::TTypeLoc typeLine;
            glslang::TTypeList* typeList;
            glslang::TArraySizes* arraySizes;
            glslang::TIdentifierList* identifierList;
        };
    } interm;

#line 423 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:355  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif



int yyparse (glslang::TParseContext* pParseContext);

#endif /* !YY_YY_C_RELEASEBUILD_GLSLANG_GLSLANG_MACHINEINDEPENDENT_GLSLANG_TAB_CPP_H_INCLUDED  */

/* Copy the second part of user declarations.  */
#line 98 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:358  */


/* windows only pragma */
#ifdef _MSC_VER
    #pragma warning(disable : 4065)
    #pragma warning(disable : 4127)
    #pragma warning(disable : 4244)
#endif

#define parseContext (*pParseContext)
#define yyerror(context, msg) context->parserError(msg)

extern int yylex(YYSTYPE*, TParseContext&);


#line 454 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:358  */

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif

#ifndef YY_ATTRIBUTE
# if (defined __GNUC__                                               \
      && (2 < __GNUC__ || (__GNUC__ == 2 && 96 <= __GNUC_MINOR__)))  \
     || defined __SUNPRO_C && 0x5110 <= __SUNPRO_C
#  define YY_ATTRIBUTE(Spec) __attribute__(Spec)
# else
#  define YY_ATTRIBUTE(Spec) /* empty */
# endif
#endif

#ifndef YY_ATTRIBUTE_PURE
# define YY_ATTRIBUTE_PURE   YY_ATTRIBUTE ((__pure__))
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# define YY_ATTRIBUTE_UNUSED YY_ATTRIBUTE ((__unused__))
#endif

#if !defined _Noreturn \
     && (!defined __STDC_VERSION__ || __STDC_VERSION__ < 201112)
# if defined _MSC_VER && 1200 <= _MSC_VER
#  define _Noreturn __declspec (noreturn)
# else
#  define _Noreturn YY_ATTRIBUTE ((__noreturn__))
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(E) ((void) (E))
#else
# define YYUSE(E) /* empty */
#endif

#if defined __GNUC__ && 407 <= __GNUC__ * 100 + __GNUC_MINOR__
/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN \
    _Pragma ("GCC diagnostic push") \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")\
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# define YY_IGNORE_MAYBE_UNINITIALIZED_END \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif


#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYSIZE_T yynewbytes;                                            \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / sizeof (*yyptr);                          \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, (Count) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYSIZE_T yyi;                         \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  240
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   5659

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  260
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  100
/* YYNRULES -- Number of rules.  */
#define YYNRULES  411
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  543

/* YYTRANSLATE[YYX] -- Symbol number corresponding to YYX as returned
   by yylex, with out-of-bounds checking.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   514

#define YYTRANSLATE(YYX)                                                \
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, without out-of-bounds checking.  */
static const yytype_uint16 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
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
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,   206,   207,   208,   209,   210,   211,   212,   213,   214,
     215,   216,   217,   218,   219,   220,   221,   222,   223,   224,
     225,   226,   227,   228,   229,   230,   231,   232,   233,   234,
     235,   236,   237,   238,   239,   240,   241,   242,   243,   244,
     245,   246,   247,   248,   249,   250,   251,   252,   253,   254,
     255,   256,   257,   258,   259
};

#if YYDEBUG
  /* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   244,   244,   250,   253,   256,   260,   263,   267,   270,
     278,   281,   284,   287,   290,   295,   303,   310,   317,   323,
     327,   334,   337,   343,   350,   360,   368,   373,   403,   409,
     413,   417,   437,   438,   439,   440,   446,   447,   452,   457,
     466,   467,   472,   480,   481,   487,   496,   497,   502,   507,
     512,   520,   521,   528,   538,   539,   548,   549,   558,   559,
     568,   569,   577,   578,   586,   587,   595,   596,   596,   614,
     615,   629,   633,   637,   641,   646,   650,   654,   658,   662,
     666,   670,   677,   680,   690,   697,   702,   707,   715,   719,
     723,   727,   732,   737,   746,   746,   757,   761,   768,   775,
     778,   785,   793,   813,   831,   846,   869,   880,   890,   900,
     910,   919,   922,   926,   930,   935,   943,   948,   953,   958,
     963,   972,   983,  1010,  1019,  1026,  1033,  1043,  1049,  1052,
    1059,  1063,  1067,  1075,  1081,  1084,  1095,  1098,  1101,  1104,
    1108,  1112,  1119,  1123,  1135,  1149,  1154,  1160,  1166,  1173,
    1179,  1184,  1189,  1194,  1201,  1205,  1209,  1213,  1217,  1221,
    1227,  1239,  1242,  1247,  1251,  1260,  1265,  1273,  1277,  1287,
    1291,  1295,  1300,  1304,  1309,  1313,  1318,  1323,  1328,  1334,
    1340,  1346,  1351,  1356,  1361,  1366,  1371,  1376,  1382,  1388,
    1394,  1399,  1404,  1409,  1414,  1419,  1424,  1429,  1434,  1439,
    1444,  1449,  1454,  1460,  1466,  1472,  1478,  1484,  1490,  1496,
    1502,  1508,  1514,  1520,  1526,  1531,  1536,  1541,  1546,  1551,
    1556,  1561,  1566,  1571,  1576,  1581,  1586,  1591,  1596,  1601,
    1606,  1611,  1616,  1621,  1626,  1631,  1636,  1641,  1646,  1651,
    1656,  1661,  1666,  1671,  1676,  1681,  1686,  1691,  1696,  1701,
    1706,  1711,  1716,  1721,  1726,  1731,  1736,  1741,  1746,  1751,
    1756,  1761,  1766,  1771,  1776,  1781,  1786,  1791,  1796,  1801,
    1806,  1811,  1816,  1821,  1826,  1831,  1836,  1841,  1846,  1851,
    1856,  1861,  1866,  1871,  1876,  1881,  1886,  1891,  1896,  1901,
    1906,  1911,  1916,  1921,  1926,  1931,  1936,  1941,  1946,  1951,
    1956,  1961,  1966,  1971,  1976,  1981,  1986,  1991,  1996,  2001,
    2006,  2011,  2016,  2021,  2026,  2031,  2036,  2041,  2046,  2051,
    2056,  2061,  2066,  2071,  2077,  2083,  2089,  2095,  2101,  2107,
    2113,  2118,  2134,  2140,  2146,  2155,  2155,  2166,  2166,  2176,
    2179,  2192,  2210,  2234,  2238,  2244,  2249,  2260,  2263,  2269,
    2278,  2281,  2287,  2291,  2292,  2298,  2299,  2300,  2301,  2302,
    2303,  2304,  2308,  2309,  2313,  2309,  2325,  2326,  2330,  2330,
    2337,  2337,  2351,  2354,  2362,  2370,  2381,  2382,  2386,  2393,
    2397,  2405,  2409,  2422,  2422,  2442,  2445,  2451,  2463,  2475,
    2475,  2490,  2490,  2506,  2506,  2527,  2530,  2536,  2539,  2545,
    2549,  2556,  2561,  2566,  2573,  2591,  2600,  2604,  2611,  2614,
    2620,  2620
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || 0
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ATTRIBUTE", "VARYING", "CONST", "BOOL",
  "FLOAT", "DOUBLE", "INT", "UINT", "BREAK", "CONTINUE", "DO", "ELSE",
  "FOR", "IF", "DISCARD", "RETURN", "SWITCH", "CASE", "DEFAULT",
  "SUBROUTINE", "BVEC2", "BVEC3", "BVEC4", "IVEC2", "IVEC3", "IVEC4",
  "UVEC2", "UVEC3", "UVEC4", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3",
  "MAT4", "CENTROID", "IN", "OUT", "INOUT", "UNIFORM", "PATCH", "SAMPLE",
  "BUFFER", "SHARED", "COHERENT", "VOLATILE", "RESTRICT", "READONLY",
  "WRITEONLY", "DVEC2", "DVEC3", "DVEC4", "DMAT2", "DMAT3", "DMAT4",
  "NOPERSPECTIVE", "FLAT", "SMOOTH", "LAYOUT", "MAT2X2", "MAT2X3",
  "MAT2X4", "MAT3X2", "MAT3X3", "MAT3X4", "MAT4X2", "MAT4X3", "MAT4X4",
  "DMAT2X2", "DMAT2X3", "DMAT2X4", "DMAT3X2", "DMAT3X3", "DMAT3X4",
  "DMAT4X2", "DMAT4X3", "DMAT4X4", "ATOMIC_UINT", "SAMPLER1D", "SAMPLER2D",
  "SAMPLER3D", "SAMPLERCUBE", "SAMPLER1DSHADOW", "SAMPLER2DSHADOW",
  "SAMPLERCUBESHADOW", "SAMPLER1DARRAY", "SAMPLER2DARRAY",
  "SAMPLER1DARRAYSHADOW", "SAMPLER2DARRAYSHADOW", "ISAMPLER1D",
  "ISAMPLER2D", "ISAMPLER3D", "ISAMPLERCUBE", "ISAMPLER1DARRAY",
  "ISAMPLER2DARRAY", "USAMPLER1D", "USAMPLER2D", "USAMPLER3D",
  "USAMPLERCUBE", "USAMPLER1DARRAY", "USAMPLER2DARRAY", "SAMPLER2DRECT",
  "SAMPLER2DRECTSHADOW", "ISAMPLER2DRECT", "USAMPLER2DRECT",
  "SAMPLERBUFFER", "ISAMPLERBUFFER", "USAMPLERBUFFER", "SAMPLERCUBEARRAY",
  "SAMPLERCUBEARRAYSHADOW", "ISAMPLERCUBEARRAY", "USAMPLERCUBEARRAY",
  "SAMPLER2DMS", "ISAMPLER2DMS", "USAMPLER2DMS", "SAMPLER2DMSARRAY",
  "ISAMPLER2DMSARRAY", "USAMPLER2DMSARRAY", "SAMPLEREXTERNALOES",
  "SAMPLER", "SAMPLERSHADOW", "TEXTURE1D", "TEXTURE2D", "TEXTURE3D",
  "TEXTURECUBE", "TEXTURE1DARRAY", "TEXTURE2DARRAY", "ITEXTURE1D",
  "ITEXTURE2D", "ITEXTURE3D", "ITEXTURECUBE", "ITEXTURE1DARRAY",
  "ITEXTURE2DARRAY", "UTEXTURE1D", "UTEXTURE2D", "UTEXTURE3D",
  "UTEXTURECUBE", "UTEXTURE1DARRAY", "UTEXTURE2DARRAY", "TEXTURE2DRECT",
  "ITEXTURE2DRECT", "UTEXTURE2DRECT", "TEXTUREBUFFER", "ITEXTUREBUFFER",
  "UTEXTUREBUFFER", "TEXTURECUBEARRAY", "ITEXTURECUBEARRAY",
  "UTEXTURECUBEARRAY", "TEXTURE2DMS", "ITEXTURE2DMS", "UTEXTURE2DMS",
  "TEXTURE2DMSARRAY", "ITEXTURE2DMSARRAY", "UTEXTURE2DMSARRAY",
  "SUBPASSINPUT", "SUBPASSINPUTMS", "ISUBPASSINPUT", "ISUBPASSINPUTMS",
  "USUBPASSINPUT", "USUBPASSINPUTMS", "IMAGE1D", "IIMAGE1D", "UIMAGE1D",
  "IMAGE2D", "IIMAGE2D", "UIMAGE2D", "IMAGE3D", "IIMAGE3D", "UIMAGE3D",
  "IMAGE2DRECT", "IIMAGE2DRECT", "UIMAGE2DRECT", "IMAGECUBE", "IIMAGECUBE",
  "UIMAGECUBE", "IMAGEBUFFER", "IIMAGEBUFFER", "UIMAGEBUFFER",
  "IMAGE1DARRAY", "IIMAGE1DARRAY", "UIMAGE1DARRAY", "IMAGE2DARRAY",
  "IIMAGE2DARRAY", "UIMAGE2DARRAY", "IMAGECUBEARRAY", "IIMAGECUBEARRAY",
  "UIMAGECUBEARRAY", "IMAGE2DMS", "IIMAGE2DMS", "UIMAGE2DMS",
  "IMAGE2DMSARRAY", "IIMAGE2DMSARRAY", "UIMAGE2DMSARRAY", "STRUCT", "VOID",
  "WHILE", "IDENTIFIER", "TYPE_NAME", "FLOATCONSTANT", "DOUBLECONSTANT",
  "INTCONSTANT", "UINTCONSTANT", "BOOLCONSTANT", "LEFT_OP", "RIGHT_OP",
  "INC_OP", "DEC_OP", "LE_OP", "GE_OP", "EQ_OP", "NE_OP", "AND_OP",
  "OR_OP", "XOR_OP", "MUL_ASSIGN", "DIV_ASSIGN", "ADD_ASSIGN",
  "MOD_ASSIGN", "LEFT_ASSIGN", "RIGHT_ASSIGN", "AND_ASSIGN", "XOR_ASSIGN",
  "OR_ASSIGN", "SUB_ASSIGN", "LEFT_PAREN", "RIGHT_PAREN", "LEFT_BRACKET",
  "RIGHT_BRACKET", "LEFT_BRACE", "RIGHT_BRACE", "DOT", "COMMA", "COLON",
  "EQUAL", "SEMICOLON", "BANG", "DASH", "TILDE", "PLUS", "STAR", "SLASH",
  "PERCENT", "LEFT_ANGLE", "RIGHT_ANGLE", "VERTICAL_BAR", "CARET",
  "AMPERSAND", "QUESTION", "INVARIANT", "PRECISE", "HIGH_PRECISION",
  "MEDIUM_PRECISION", "LOW_PRECISION", "PRECISION", "PACKED", "RESOURCE",
  "SUPERP", "$accept", "variable_identifier", "primary_expression",
  "postfix_expression", "integer_expression", "function_call",
  "function_call_or_method", "function_call_generic",
  "function_call_header_no_parameters",
  "function_call_header_with_parameters", "function_call_header",
  "function_identifier", "unary_expression", "unary_operator",
  "multiplicative_expression", "additive_expression", "shift_expression",
  "relational_expression", "equality_expression", "and_expression",
  "exclusive_or_expression", "inclusive_or_expression",
  "logical_and_expression", "logical_xor_expression",
  "logical_or_expression", "conditional_expression", "$@1",
  "assignment_expression", "assignment_operator", "expression",
  "constant_expression", "declaration", "block_structure", "$@2",
  "identifier_list", "function_prototype", "function_declarator",
  "function_header_with_parameters", "function_header",
  "parameter_declarator", "parameter_declaration",
  "parameter_type_specifier", "init_declarator_list", "single_declaration",
  "fully_specified_type", "invariant_qualifier", "interpolation_qualifier",
  "layout_qualifier", "layout_qualifier_id_list", "layout_qualifier_id",
  "precise_qualifier", "type_qualifier", "single_type_qualifier",
  "storage_qualifier", "type_name_list", "type_specifier",
  "array_specifier", "type_specifier_nonarray", "precision_qualifier",
  "struct_specifier", "$@3", "$@4", "struct_declaration_list",
  "struct_declaration", "struct_declarator_list", "struct_declarator",
  "initializer", "initializer_list", "declaration_statement", "statement",
  "simple_statement", "compound_statement", "$@5", "$@6",
  "statement_no_new_scope", "statement_scoped", "$@7", "$@8",
  "compound_statement_no_new_scope", "statement_list",
  "expression_statement", "selection_statement",
  "selection_rest_statement", "condition", "switch_statement", "$@9",
  "switch_statement_list", "case_label", "iteration_statement", "$@10",
  "$@11", "$@12", "for_init_statement", "conditionopt",
  "for_rest_statement", "jump_statement", "translation_unit",
  "external_declaration", "function_definition", "$@13", YY_NULLPTR
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[NUM] -- (External) token number corresponding to the
   (internal) symbol number NUM (which must be that of a token).  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298,   299,   300,   301,   302,   303,   304,
     305,   306,   307,   308,   309,   310,   311,   312,   313,   314,
     315,   316,   317,   318,   319,   320,   321,   322,   323,   324,
     325,   326,   327,   328,   329,   330,   331,   332,   333,   334,
     335,   336,   337,   338,   339,   340,   341,   342,   343,   344,
     345,   346,   347,   348,   349,   350,   351,   352,   353,   354,
     355,   356,   357,   358,   359,   360,   361,   362,   363,   364,
     365,   366,   367,   368,   369,   370,   371,   372,   373,   374,
     375,   376,   377,   378,   379,   380,   381,   382,   383,   384,
     385,   386,   387,   388,   389,   390,   391,   392,   393,   394,
     395,   396,   397,   398,   399,   400,   401,   402,   403,   404,
     405,   406,   407,   408,   409,   410,   411,   412,   413,   414,
     415,   416,   417,   418,   419,   420,   421,   422,   423,   424,
     425,   426,   427,   428,   429,   430,   431,   432,   433,   434,
     435,   436,   437,   438,   439,   440,   441,   442,   443,   444,
     445,   446,   447,   448,   449,   450,   451,   452,   453,   454,
     455,   456,   457,   458,   459,   460,   461,   462,   463,   464,
     465,   466,   467,   468,   469,   470,   471,   472,   473,   474,
     475,   476,   477,   478,   479,   480,   481,   482,   483,   484,
     485,   486,   487,   488,   489,   490,   491,   492,   493,   494,
     495,   496,   497,   498,   499,   500,   501,   502,   503,   504,
     505,   506,   507,   508,   509,   510,   511,   512,   513,   514
};
# endif

#define YYPACT_NINF -466

#define yypact_value_is_default(Yystate) \
  (!!((Yystate) == (-466)))

#define YYTABLE_NINF -369

#define yytable_value_is_error(Yytable_value) \
  0

  /* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
     STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    2275,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -205,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -192,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -179,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -122,  -466,  -186,  -198,  -173,  -175,  3686,  -194,  -466,
    -121,  -466,  -466,  -466,  -466,  2749,  -466,  -466,  -466,  -141,
    -466,  -466,   527,  -466,  -466,   -97,   -37,  -117,  -466,  5459,
    -200,  -466,  -466,  -112,  -466,  3686,  -466,  -466,  -466,  3686,
     -71,   -44,  -466,  -191,  -142,  -466,  -466,  -466,  4117,   -82,
    -466,  -466,  -466,  -202,  -466,   -76,  -137,  -466,  -466,  3686,
     -73,  -466,  -196,   781,  -466,  -466,  -466,  -466,  -141,  -155,
    -466,  4342,  -152,  -466,   -38,  -466,  -177,  -466,  -466,  -466,
    -466,  -466,  -466,  5015,  5015,  5015,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -185,  -466,  -466,  -466,   -63,  -128,  5237,
     -61,  -466,  5015,  -106,  -100,  -157,  -183,   -78,   -81,   -79,
     -80,   -43,   -46,  -197,   -58,  -466,  4568,  -466,   -27,  5015,
    -466,   -37,  3686,  3686,   -25,  2984,  -466,  -466,  -466,   -62,
     -57,  -466,   -51,   -48,   -56,  4793,   -45,  5015,   -50,   -40,
     -41,  -466,  -466,  -153,  -466,  -466,  -147,  -466,  -198,   -39,
    -466,  -466,  -466,  -466,  1035,  -466,  -466,  -466,  -466,  -466,
    -466,   -82,  4342,  -143,  4342,  -466,  -466,  4342,  3686,  -466,
     -15,  -466,  -466,  -466,  -126,  -466,  -466,  5015,   -10,  -466,
    -466,  5015,   -36,  -466,  -466,  -466,  5015,  5015,  5015,  5015,
    5015,  5015,  5015,  5015,  5015,  5015,  5015,  5015,  5015,  5015,
    5015,  5015,  5015,  5015,  5015,  -466,  -466,  -466,   -35,  -466,
    -466,  -466,  -466,  3218,   -25,  -141,  -127,  -466,  -466,  -466,
    -466,  -466,  1289,  -466,  5015,  -466,  -466,  -108,  5015,   -91,
    -466,  -466,  -466,  1289,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  5015,  5015,  -466,  -466,  -466,
    -466,  4342,  -466,   -92,  -466,  3452,  -466,  -466,   -34,   -31,
    -466,  -466,  -466,  -466,  -466,  -106,  -106,  -100,  -100,  -157,
    -157,  -157,  -157,  -183,  -183,   -78,   -81,   -79,   -80,   -43,
     -46,  5015,  -466,  -466,  -107,   -82,   -25,  -466,    -4,  2036,
    -123,  -466,  -116,  -466,  2510,  1289,  -466,  -466,  -466,  -466,
    3890,  -466,  -466,   -83,  -466,  -466,   -29,  -466,  -466,  2510,
     -32,  -466,   -31,     1,  3686,   -24,   -26,  -466,  -466,  5015,
    5015,  -466,   -30,   -19,   196,   -20,  1797,  -466,   -18,   -22,
    1543,  -466,  -466,  -113,  5015,  1543,   -32,  -466,  -466,  1289,
    4342,  -466,  -466,  -466,   -17,   -31,  -466,  -466,  1289,   -14,
    -466,  -466,  -466
};

  /* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
     Performed when YYTABLE does not specify something else to do.  Zero
     means the default is an error.  */
static const yytype_uint16 yydefact[] =
{
       0,   143,   144,   142,   174,   170,   171,   172,   173,   159,
     181,   182,   183,   184,   185,   186,   187,   188,   189,   175,
     176,   177,   190,   191,   192,   148,   146,   147,   145,   151,
     149,   150,   152,   153,   154,   155,   156,   157,   158,   178,
     179,   180,   202,   203,   204,   126,   125,   124,     0,   193,
     194,   195,   196,   197,   198,   199,   200,   201,   205,   206,
     207,   208,   209,   210,   211,   212,   213,   214,   215,   216,
     217,   218,   219,   220,   221,   222,   223,   224,   225,   228,
     229,   230,   231,   232,   233,   235,   236,   237,   238,   239,
     240,   242,   243,   244,   245,   246,   247,   248,   226,   227,
     234,   241,   249,   250,   251,   252,   253,   254,   323,   255,
     256,   257,   258,   259,   260,   261,   262,   264,   265,   266,
     267,   268,   269,   271,   272,   273,   274,   275,   276,   278,
     279,   280,   281,   282,   283,   263,   270,   277,   284,   285,
     286,   287,   288,   289,   324,   325,   326,   327,   328,   329,
     290,   291,   292,   293,   294,   295,   296,   297,   298,   299,
     300,   301,   302,   303,   304,   305,   306,   307,   308,   309,
     310,   311,   312,   313,   314,   315,   316,   317,   318,   319,
     320,   321,   322,     0,   169,   331,   123,   133,   332,   333,
     334,     0,   409,     0,   410,     0,   100,    99,     0,   111,
     116,   140,   139,   137,   141,     0,   134,   136,   121,   163,
     138,   330,     0,   406,   408,     0,     0,     0,   337,     0,
       0,    88,    85,     0,    98,     0,   107,   101,   109,     0,
     110,     0,    86,   117,     0,    91,   135,   122,     0,   164,
       1,   407,   161,     0,   132,   130,     0,   128,   335,     0,
       0,    89,     0,     0,   411,   102,   106,   108,   104,   112,
     103,     0,   118,    94,     0,    92,     0,     2,     6,     7,
       4,     5,     8,     0,     0,     0,   165,    34,    33,    35,
      32,     3,    10,    28,    12,    17,    18,     0,     0,    22,
       0,    36,     0,    40,    43,    46,    51,    54,    56,    58,
      60,    62,    64,    66,     0,    26,     0,   160,     0,     0,
     127,     0,     0,     0,     0,     0,   339,    87,    90,     0,
       0,   391,     0,     0,     0,     0,     0,     0,     0,     0,
     363,   372,   376,    36,    69,    82,     0,   352,     0,   121,
     355,   374,   354,   353,     0,   356,   357,   358,   359,   360,
     361,   105,     0,   113,     0,   347,   120,     0,     0,    96,
       0,    93,    29,    30,     0,    14,    15,     0,     0,    20,
      19,     0,   169,    23,    25,    31,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    67,   166,   167,     0,   162,
      84,   131,   129,     0,     0,   345,     0,   343,   338,   340,
     402,   401,     0,   393,     0,   405,   403,     0,     0,     0,
     388,   389,   362,     0,    72,    73,    75,    74,    77,    78,
      79,    80,    81,    76,    71,     0,     0,   377,   373,   375,
     115,     0,   350,     0,   119,     0,    97,     9,     0,    16,
      13,    24,    37,    38,    39,    42,    41,    44,    45,    49,
      50,    47,    48,    52,    53,    55,    57,    59,    61,    63,
      65,     0,   168,   336,     0,   346,     0,   341,     0,     0,
       0,   404,     0,   387,     0,   364,    70,    83,   114,   348,
       0,    95,    11,     0,   342,   344,     0,   396,   395,   398,
     370,   383,   381,     0,     0,     0,     0,   349,   351,     0,
       0,   397,     0,     0,   380,     0,     0,   378,     0,     0,
       0,   365,    68,     0,   399,     0,   370,   369,   371,   385,
       0,   367,   390,   366,     0,   400,   394,   379,   386,     0,
     382,   392,   384
};

  /* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,   -52,  -466,  -226,  -225,  -261,  -229,  -166,  -164,
    -167,  -165,  -162,  -161,  -466,  -227,  -466,  -258,  -466,  -269,
    -466,     4,  -466,  -466,  -466,     5,  -466,  -466,  -466,    -1,
       9,     6,  -466,  -466,  -465,  -466,  -466,  -466,  -466,   -75,
    -466,  -195,  -204,  -466,  -466,     0,  -212,  -466,    46,  -466,
    -466,  -466,  -297,  -299,  -160,  -238,  -340,  -466,  -240,  -337,
    -440,  -273,  -466,  -466,  -282,  -281,  -466,  -466,    23,  -413,
    -232,  -466,  -466,  -251,  -466,  -466,  -466,  -466,  -466,  -466,
    -466,  -466,  -466,  -466,  -466,  -466,  -466,    40,  -466,  -466
};

  /* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,   281,   282,   283,   448,   284,   285,   286,   287,   288,
     289,   290,   333,   292,   293,   294,   295,   296,   297,   298,
     299,   300,   301,   302,   303,   334,   471,   335,   435,   336,
     401,   337,   193,   358,   266,   338,   195,   196,   197,   226,
     227,   228,   198,   199,   200,   201,   202,   203,   246,   247,
     204,   205,   206,   207,   243,   305,   239,   209,   210,   211,
     312,   249,   315,   316,   406,   407,   356,   443,   340,   341,
     342,   343,   423,   506,   532,   514,   515,   516,   533,   344,
     345,   346,   517,   505,   347,   518,   539,   348,   349,   484,
     412,   479,   499,   512,   513,   350,   212,   213,   214,   223
};

  /* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
     positive, shift that token.  If negative, reduce the rule whose
     number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
     208,   236,   229,   355,   192,   194,   364,   439,   252,   244,
     485,   304,   440,   220,   442,   403,   409,   444,   394,   503,
     217,   262,   215,   365,   366,   236,   307,   383,   384,   238,
     229,   373,   308,   306,   503,   216,   260,   251,   238,   222,
     231,   318,   -27,   232,   367,   261,   351,   353,   368,   381,
     382,   221,   218,   395,   313,   224,   417,   360,   419,   225,
     361,   445,   385,   386,   424,   425,   426,   427,   428,   429,
     430,   431,   432,   433,   238,   478,   528,   306,   233,   398,
     531,   352,   400,   434,   357,   531,   306,   436,   238,   263,
     437,   310,   264,   441,   355,   265,   355,   311,   449,   355,
     370,   488,   447,   242,   409,   500,   371,   476,   436,   236,
     477,   436,   501,   451,   248,   534,   538,   313,   436,   253,
     313,   436,   459,   460,   461,   462,   436,   476,   258,   481,
     494,   188,   189,   190,   387,   388,   376,   377,   378,   379,
     489,   380,   490,   436,   483,   480,   409,   306,   439,   482,
     508,   436,   509,   455,   456,   259,   457,   458,   463,   464,
     309,   359,   245,   313,   317,   369,   374,   391,   389,   390,
     393,   392,   396,   399,   405,   410,   413,   486,   487,   414,
     411,   415,   418,   355,   446,   420,   291,   421,   -26,   450,
     540,   422,   -21,   475,   496,   472,   492,   230,   510,  -368,
     519,   439,   493,   436,   520,   237,   521,   524,   313,   525,
     526,   330,   208,   529,   530,   502,   192,   194,   542,   250,
     541,   362,   363,   465,   467,   230,   466,   468,   256,   230,
     502,   469,   355,   470,   255,   257,   402,   219,   495,   497,
     375,   523,   527,   536,   474,   537,   254,   498,   511,   314,
     313,   522,   241,   339,   291,   535,     0,   291,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   355,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   504,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     236,     0,     0,     0,   504,     0,     0,     0,     0,     0,
       0,     0,   314,   404,     0,   314,     0,     0,     0,     0,
       0,     0,     0,     0,   452,   453,   454,   291,   291,   291,
     291,   291,   291,   291,   291,   291,   291,   291,   291,   291,
     291,   291,   291,     0,   339,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   314,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   314,     0,     0,     0,     0,     0,     0,
       0,     0,   339,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   339,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   314,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   339,
       0,     0,     0,     0,   339,   339,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   339,
       0,     0,     0,     0,   237,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   339,     0,     0,     0,
     339,     0,     0,     0,     0,   339,     0,   240,     0,   339,
       1,     2,     3,     4,     5,     6,     7,     8,   339,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,     0,     0,   185,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   186,   187,
     188,   189,   190,   191,     1,     2,     3,     4,     5,     6,
       7,     8,   319,   320,   321,     0,   322,   323,   324,   325,
     326,   327,   328,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    21,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   158,   159,   160,   161,   162,   163,   164,   165,
     166,   167,   168,   169,   170,   171,   172,   173,   174,   175,
     176,   177,   178,   179,   180,   181,   182,   183,   184,   329,
     267,   185,   268,   269,   270,   271,   272,     0,     0,   273,
     274,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   275,     0,
       0,     0,   330,   331,     0,     0,     0,     0,   332,   277,
     278,   279,   280,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   186,   187,   188,   189,   190,   191,     1,     2,
       3,     4,     5,     6,     7,     8,   319,   320,   321,     0,
     322,   323,   324,   325,   326,   327,   328,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,   329,   267,   185,   268,   269,   270,   271,
     272,     0,     0,   273,   274,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   275,     0,     0,     0,   330,   438,     0,     0,
       0,     0,   332,   277,   278,   279,   280,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   186,   187,   188,   189,
     190,   191,     1,     2,     3,     4,     5,     6,     7,     8,
     319,   320,   321,     0,   322,   323,   324,   325,   326,   327,
     328,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     158,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   183,   184,   329,   267,   185,
     268,   269,   270,   271,   272,     0,     0,   273,   274,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   275,     0,     0,     0,
     330,     0,     0,     0,     0,     0,   332,   277,   278,   279,
     280,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     186,   187,   188,   189,   190,   191,     1,     2,     3,     4,
       5,     6,     7,     8,   319,   320,   321,     0,   322,   323,
     324,   325,   326,   327,   328,     9,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   158,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   179,   180,   181,   182,   183,
     184,   329,   267,   185,   268,   269,   270,   271,   272,     0,
       0,   273,   274,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     275,     0,     0,     0,   253,     0,     0,     0,     0,     0,
     332,   277,   278,   279,   280,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   186,   187,   188,   189,   190,   191,
       1,     2,     3,     4,     5,     6,     7,     8,   319,   320,
     321,     0,   322,   323,   324,   325,   326,   327,   328,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,   329,   267,   185,   268,   269,
     270,   271,   272,     0,     0,   273,   274,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   275,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   332,   277,   278,   279,   280,     1,
       2,     3,     4,     5,     6,     7,     8,     0,   186,   187,
     188,   189,   190,   191,     0,     0,     0,     0,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,     0,   267,   185,   268,   269,   270,
     271,   272,     0,     0,   273,   274,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   275,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   332,   277,   278,   279,   280,     1,     2,
       3,     4,     5,     6,     7,     8,     0,   186,   187,   188,
     189,   190,   191,     0,     0,     0,     0,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,     0,     0,   185,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     1,     2,     3,     4,     5,     6,     7,
       8,     0,     0,     0,     0,     0,   186,   187,   188,   189,
     190,   191,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   179,   180,   181,   182,   183,   184,     0,   267,
     185,   268,   269,   270,   271,   272,     0,     0,   273,   274,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   275,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,   277,   278,
     279,   280,     1,     2,     3,     4,     5,     6,     7,     8,
       0,   186,   187,   188,   189,   190,     0,     0,     0,     0,
       0,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     158,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   183,   184,     0,   234,   185,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   235,     1,     2,     3,
       4,     5,     6,     7,     8,     0,     0,     0,     0,     0,
     186,   187,   188,   189,   190,     0,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,     0,     0,   185,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   408,     0,     0,     0,
       0,     1,     2,     3,     4,     5,     6,     7,     8,     0,
       0,     0,     0,     0,     0,   186,   187,   188,   189,   190,
       9,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     179,   180,   181,   182,   183,   184,     0,     0,   185,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     473,     0,     0,     0,     0,     1,     2,     3,     4,     5,
       6,     7,     8,     0,     0,     0,     0,     0,     0,   186,
     187,   188,   189,   190,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
       0,     0,   185,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   491,     0,     0,     0,     0,     1,
       2,     3,     4,     5,     6,     7,     8,     0,     0,     0,
       0,     0,     0,   186,   187,   188,   189,   190,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,     0,     0,   185,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     4,     5,     6,     7,
       8,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   186,   187,   188,
     189,   190,    39,    40,    41,    42,    43,    44,     0,     0,
       0,     0,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   179,   180,   181,   182,   183,   184,     0,   267,
     185,   268,   269,   270,   271,   272,     0,     0,   273,   274,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,   275,     0,     0,
       0,   354,   507,     4,     5,     6,     7,     8,   277,   278,
     279,   280,     0,     0,     0,     0,     0,     0,     0,     0,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    39,
      40,    41,    42,    43,    44,     0,     0,     0,     0,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,     0,   267,   185,   268,   269,
     270,   271,   272,     0,     0,   273,   274,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   275,     0,     0,   276,     4,     5,
       6,     7,     8,     0,     0,   277,   278,   279,   280,     0,
       0,     0,     0,     0,     0,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    39,    40,    41,    42,    43,    44,
       0,     0,     0,     0,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
       0,   267,   185,   268,   269,   270,   271,   272,     0,     0,
     273,   274,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,   275,
       0,     0,     0,   354,     4,     5,     6,     7,     8,     0,
     277,   278,   279,   280,     0,     0,     0,     0,     0,     0,
       0,    10,    11,    12,    13,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
      39,    40,    41,    42,    43,    44,     0,     0,     0,     0,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     179,   180,   181,   182,   183,   184,     0,   267,   185,   268,
     269,   270,   271,   272,     0,     0,   273,   274,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   275,     0,     0,   397,     4,
       5,     6,     7,     8,     0,     0,   277,   278,   279,   280,
       0,     0,     0,     0,     0,     0,    10,    11,    12,    13,
      14,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    39,    40,    41,    42,    43,
      44,     0,     0,     0,     0,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   158,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   179,   180,   181,   182,   183,
     184,     0,   267,   185,   268,   269,   270,   271,   272,     0,
       0,   273,   274,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     275,     4,     5,     6,     7,     8,     0,     0,     0,     0,
     416,   277,   278,   279,   280,     0,     0,     0,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,    39,    40,    41,
      42,    43,    44,     0,     0,     0,     0,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,     0,   267,   185,   268,   269,   270,   271,
     272,     0,     0,   273,   274,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   275,     4,     5,     6,     7,     8,     0,     0,
       0,     0,     0,   277,   278,   279,   280,     0,     0,     0,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,    39,
      40,    41,    42,    43,    44,     0,     0,     0,     0,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   372,     0,   267,   185,   268,   269,
     270,   271,   272,     0,     0,   273,   274,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,   275,     4,     5,     6,     7,     8,
       0,     0,     0,     0,     0,   277,   278,   279,   280,     0,
       0,     0,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    39,    40,    41,    42,    43,    44,     0,     0,     0,
       0,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     158,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   183,   184,     0,     0,   185
};

static const yytype_int16 yycheck[] =
{
       0,   205,   197,   261,     0,     0,   275,   344,   220,    46,
     423,   238,   352,   199,   354,   312,   315,   357,   215,   484,
     199,   233,   227,   208,   209,   229,   228,   210,   211,   229,
     225,   289,   234,   229,   499,   227,   227,   237,   229,   237,
     234,   237,   227,   237,   229,   236,   258,   259,   233,   206,
     207,   237,   231,   250,   249,   228,   325,   234,   327,   234,
     237,   358,   245,   246,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   229,   412,   516,   229,   199,   306,
     520,   236,   309,   236,   236,   525,   229,   234,   229,   231,
     237,   228,   234,   236,   352,   237,   354,   234,   367,   357,
     228,   441,   228,   200,   403,   228,   234,   234,   234,   313,
     237,   234,   228,   371,   231,   228,   529,   312,   234,   231,
     315,   234,   383,   384,   385,   386,   234,   234,   199,   237,
     237,   253,   254,   255,   212,   213,   242,   243,   244,   239,
     232,   241,   234,   234,   235,   414,   445,   229,   485,   418,
     490,   234,   235,   379,   380,   199,   381,   382,   387,   388,
     236,   199,   199,   358,   237,   228,   227,   247,   249,   248,
     216,   214,   230,   200,   199,   237,   227,   435,   436,   227,
     237,   237,   227,   441,   199,   235,   238,   227,   227,   199,
     530,   232,   228,   405,   198,   230,   230,   197,   227,   231,
     199,   538,   471,   234,   228,   205,   232,   237,   403,   228,
      14,   231,   212,   231,   236,   484,   212,   212,   232,   219,
     237,   273,   274,   389,   391,   225,   390,   392,   229,   229,
     499,   393,   490,   394,   225,   229,   311,   191,   476,   479,
     292,   510,   515,   525,   404,   526,   223,   479,   499,   249,
     445,   509,   212,   253,   306,   524,    -1,   309,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   530,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   484,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     504,    -1,    -1,    -1,   499,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   312,   313,    -1,   315,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   376,   377,   378,   379,   380,   381,
     382,   383,   384,   385,   386,   387,   388,   389,   390,   391,
     392,   393,   394,    -1,   344,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   358,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   403,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   412,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   423,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   445,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   479,
      -1,    -1,    -1,    -1,   484,   485,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   499,
      -1,    -1,    -1,    -1,   504,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   516,    -1,    -1,    -1,
     520,    -1,    -1,    -1,    -1,   525,    -1,     0,    -1,   529,
       3,     4,     5,     6,     7,     8,     9,    10,   538,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     193,   194,   195,   196,   197,    -1,    -1,   200,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   251,   252,
     253,   254,   255,   256,     3,     4,     5,     6,     7,     8,
       9,    10,    11,    12,    13,    -1,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    48,
      49,    50,    51,    52,    53,    54,    55,    56,    57,    58,
      59,    60,    61,    62,    63,    64,    65,    66,    67,    68,
      69,    70,    71,    72,    73,    74,    75,    76,    77,    78,
      79,    80,    81,    82,    83,    84,    85,    86,    87,    88,
      89,    90,    91,    92,    93,    94,    95,    96,    97,    98,
      99,   100,   101,   102,   103,   104,   105,   106,   107,   108,
     109,   110,   111,   112,   113,   114,   115,   116,   117,   118,
     119,   120,   121,   122,   123,   124,   125,   126,   127,   128,
     129,   130,   131,   132,   133,   134,   135,   136,   137,   138,
     139,   140,   141,   142,   143,   144,   145,   146,   147,   148,
     149,   150,   151,   152,   153,   154,   155,   156,   157,   158,
     159,   160,   161,   162,   163,   164,   165,   166,   167,   168,
     169,   170,   171,   172,   173,   174,   175,   176,   177,   178,
     179,   180,   181,   182,   183,   184,   185,   186,   187,   188,
     189,   190,   191,   192,   193,   194,   195,   196,   197,   198,
     199,   200,   201,   202,   203,   204,   205,    -1,    -1,   208,
     209,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   227,    -1,
      -1,    -1,   231,   232,    -1,    -1,    -1,    -1,   237,   238,
     239,   240,   241,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   251,   252,   253,   254,   255,   256,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    -1,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,   198,   199,   200,   201,   202,   203,   204,
     205,    -1,    -1,   208,   209,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   227,    -1,    -1,    -1,   231,   232,    -1,    -1,
      -1,    -1,   237,   238,   239,   240,   241,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   251,   252,   253,   254,
     255,   256,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    13,    -1,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,   185,   186,   187,   188,   189,   190,
     191,   192,   193,   194,   195,   196,   197,   198,   199,   200,
     201,   202,   203,   204,   205,    -1,    -1,   208,   209,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   227,    -1,    -1,    -1,
     231,    -1,    -1,    -1,    -1,    -1,   237,   238,   239,   240,
     241,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     251,   252,   253,   254,   255,   256,     3,     4,     5,     6,
       7,     8,     9,    10,    11,    12,    13,    -1,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    38,    39,    40,    41,    42,    43,    44,    45,    46,
      47,    48,    49,    50,    51,    52,    53,    54,    55,    56,
      57,    58,    59,    60,    61,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   179,   180,   181,   182,   183,   184,   185,   186,
     187,   188,   189,   190,   191,   192,   193,   194,   195,   196,
     197,   198,   199,   200,   201,   202,   203,   204,   205,    -1,
      -1,   208,   209,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     227,    -1,    -1,    -1,   231,    -1,    -1,    -1,    -1,    -1,
     237,   238,   239,   240,   241,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   251,   252,   253,   254,   255,   256,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    -1,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     193,   194,   195,   196,   197,   198,   199,   200,   201,   202,
     203,   204,   205,    -1,    -1,   208,   209,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   227,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   237,   238,   239,   240,   241,     3,
       4,     5,     6,     7,     8,     9,    10,    -1,   251,   252,
     253,   254,   255,   256,    -1,    -1,    -1,    -1,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   158,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   179,   180,   181,   182,   183,
     184,   185,   186,   187,   188,   189,   190,   191,   192,   193,
     194,   195,   196,   197,    -1,   199,   200,   201,   202,   203,
     204,   205,    -1,    -1,   208,   209,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   227,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,   237,   238,   239,   240,   241,     3,     4,
       5,     6,     7,     8,     9,    10,    -1,   251,   252,   253,
     254,   255,   256,    -1,    -1,    -1,    -1,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43,    44,
      45,    46,    47,    48,    49,    50,    51,    52,    53,    54,
      55,    56,    57,    58,    59,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,    -1,    -1,   200,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,     3,     4,     5,     6,     7,     8,     9,
      10,    -1,    -1,    -1,    -1,    -1,   251,   252,   253,   254,
     255,   256,    22,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    38,    39,
      40,    41,    42,    43,    44,    45,    46,    47,    48,    49,
      50,    51,    52,    53,    54,    55,    56,    57,    58,    59,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,   185,   186,   187,   188,   189,
     190,   191,   192,   193,   194,   195,   196,   197,    -1,   199,
     200,   201,   202,   203,   204,   205,    -1,    -1,   208,   209,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   227,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   238,   239,
     240,   241,     3,     4,     5,     6,     7,     8,     9,    10,
      -1,   251,   252,   253,   254,   255,    -1,    -1,    -1,    -1,
      -1,    22,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    49,    50,
      51,    52,    53,    54,    55,    56,    57,    58,    59,    60,
      61,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,   185,   186,   187,   188,   189,   190,
     191,   192,   193,   194,   195,   196,   197,    -1,   199,   200,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   237,     3,     4,     5,
       6,     7,     8,     9,    10,    -1,    -1,    -1,    -1,    -1,
     251,   252,   253,   254,   255,    -1,    22,    23,    24,    25,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    44,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    57,    58,    59,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,    78,    79,    80,    81,    82,    83,    84,    85,
      86,    87,    88,    89,    90,    91,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     116,   117,   118,   119,   120,   121,   122,   123,   124,   125,
     126,   127,   128,   129,   130,   131,   132,   133,   134,   135,
     136,   137,   138,   139,   140,   141,   142,   143,   144,   145,
     146,   147,   148,   149,   150,   151,   152,   153,   154,   155,
     156,   157,   158,   159,   160,   161,   162,   163,   164,   165,
     166,   167,   168,   169,   170,   171,   172,   173,   174,   175,
     176,   177,   178,   179,   180,   181,   182,   183,   184,   185,
     186,   187,   188,   189,   190,   191,   192,   193,   194,   195,
     196,   197,    -1,    -1,   200,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,   232,    -1,    -1,    -1,
      -1,     3,     4,     5,     6,     7,     8,     9,    10,    -1,
      -1,    -1,    -1,    -1,    -1,   251,   252,   253,   254,   255,
      22,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    49,    50,    51,
      52,    53,    54,    55,    56,    57,    58,    59,    60,    61,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   194,   195,   196,   197,    -1,    -1,   200,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     232,    -1,    -1,    -1,    -1,     3,     4,     5,     6,     7,
       8,     9,    10,    -1,    -1,    -1,    -1,    -1,    -1,   251,
     252,   253,   254,   255,    22,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,    55,    56,    57,
      58,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     158,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   183,   184,   185,   186,   187,
     188,   189,   190,   191,   192,   193,   194,   195,   196,   197,
      -1,    -1,   200,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   232,    -1,    -1,    -1,    -1,     3,
       4,     5,     6,     7,     8,     9,    10,    -1,    -1,    -1,
      -1,    -1,    -1,   251,   252,   253,   254,   255,    22,    23,
      24,    25,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      44,    45,    46,    47,    48,    49,    50,    51,    52,    53,
      54,    55,    56,    57,    58,    59,    60,    61,    62,    63,
      64,    65,    66,    67,    68,    69,    70,    71,    72,    73,
      74,    75,    76,    77,    78,    79,    80,    81,    82,    83,
      84,    85,    86,    87,    88,    89,    90,    91,    92,    93,
      94,    95,    96,    97,    98,    99,   100,   101,   102,   103,
     104,   105,   106,   107,   108,   109,   110,   111,   112,   113,
     114,   115,   116,   117,   118,   119,   120,   121,   122,   123,
     124,   125,   126,   127,   128,   129,   130,   131,   132,   133,
     134,   135,   136,   137,   138,   139,   140,   141,   142,   143,
     144,   145,   146,   147,   148,   149,   150,   151,   152,   153,
     154,   155,   156,   157,   158,   159,   160,   161,   162,   163,
     164,   165,   166,   167,   168,   169,   170,   171,   172,   173,
     174,   175,   176,   177,   178,   179,   180,   181,   182,   183,
     184,   185,   186,   187,   188,   189,   190,   191,   192,   193,
     194,   195,   196,   197,    -1,    -1,   200,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,     6,     7,     8,     9,
      10,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    23,    24,    25,    26,    27,    28,    29,
      30,    31,    32,    33,    34,    35,    36,    37,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   251,   252,   253,
     254,   255,    52,    53,    54,    55,    56,    57,    -1,    -1,
      -1,    -1,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,    78,    79,
      80,    81,    82,    83,    84,    85,    86,    87,    88,    89,
      90,    91,    92,    93,    94,    95,    96,    97,    98,    99,
     100,   101,   102,   103,   104,   105,   106,   107,   108,   109,
     110,   111,   112,   113,   114,   115,   116,   117,   118,   119,
     120,   121,   122,   123,   124,   125,   126,   127,   128,   129,
     130,   131,   132,   133,   134,   135,   136,   137,   138,   139,
     140,   141,   142,   143,   144,   145,   146,   147,   148,   149,
     150,   151,   152,   153,   154,   155,   156,   157,   158,   159,
     160,   161,   162,   163,   164,   165,   166,   167,   168,   169,
     170,   171,   172,   173,   174,   175,   176,   177,   178,   179,
     180,   181,   182,   183,   184,   185,   186,   187,   188,   189,
     190,   191,   192,   193,   194,   195,   196,   197,    -1,   199,
     200,   201,   202,   203,   204,   205,    -1,    -1,   208,   209,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,   227,    -1,    -1,
      -1,   231,   232,     6,     7,     8,     9,    10,   238,   239,
     240,   241,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    52,
      53,    54,    55,    56,    57,    -1,    -1,    -1,    -1,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     193,   194,   195,   196,   197,    -1,   199,   200,   201,   202,
     203,   204,   205,    -1,    -1,   208,   209,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   227,    -1,    -1,   230,     6,     7,
       8,     9,    10,    -1,    -1,   238,   239,   240,   241,    -1,
      -1,    -1,    -1,    -1,    -1,    23,    24,    25,    26,    27,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    52,    53,    54,    55,    56,    57,
      -1,    -1,    -1,    -1,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    81,    82,    83,    84,    85,    86,    87,
      88,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
     108,   109,   110,   111,   112,   113,   114,   115,   116,   117,
     118,   119,   120,   121,   122,   123,   124,   125,   126,   127,
     128,   129,   130,   131,   132,   133,   134,   135,   136,   137,
     138,   139,   140,   141,   142,   143,   144,   145,   146,   147,
     148,   149,   150,   151,   152,   153,   154,   155,   156,   157,
     158,   159,   160,   161,   162,   163,   164,   165,   166,   167,
     168,   169,   170,   171,   172,   173,   174,   175,   176,   177,
     178,   179,   180,   181,   182,   183,   184,   185,   186,   187,
     188,   189,   190,   191,   192,   193,   194,   195,   196,   197,
      -1,   199,   200,   201,   202,   203,   204,   205,    -1,    -1,
     208,   209,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,   227,
      -1,    -1,    -1,   231,     6,     7,     8,     9,    10,    -1,
     238,   239,   240,   241,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    23,    24,    25,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      52,    53,    54,    55,    56,    57,    -1,    -1,    -1,    -1,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    78,    79,    80,    81,
      82,    83,    84,    85,    86,    87,    88,    89,    90,    91,
      92,    93,    94,    95,    96,    97,    98,    99,   100,   101,
     102,   103,   104,   105,   106,   107,   108,   109,   110,   111,
     112,   113,   114,   115,   116,   117,   118,   119,   120,   121,
     122,   123,   124,   125,   126,   127,   128,   129,   130,   131,
     132,   133,   134,   135,   136,   137,   138,   139,   140,   141,
     142,   143,   144,   145,   146,   147,   148,   149,   150,   151,
     152,   153,   154,   155,   156,   157,   158,   159,   160,   161,
     162,   163,   164,   165,   166,   167,   168,   169,   170,   171,
     172,   173,   174,   175,   176,   177,   178,   179,   180,   181,
     182,   183,   184,   185,   186,   187,   188,   189,   190,   191,
     192,   193,   194,   195,   196,   197,    -1,   199,   200,   201,
     202,   203,   204,   205,    -1,    -1,   208,   209,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,   227,    -1,    -1,   230,     6,
       7,     8,     9,    10,    -1,    -1,   238,   239,   240,   241,
      -1,    -1,    -1,    -1,    -1,    -1,    23,    24,    25,    26,
      27,    28,    29,    30,    31,    32,    33,    34,    35,    36,
      37,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    52,    53,    54,    55,    56,
      57,    -1,    -1,    -1,    -1,    62,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    78,    79,    80,    81,    82,    83,    84,    85,    86,
      87,    88,    89,    90,    91,    92,    93,    94,    95,    96,
      97,    98,    99,   100,   101,   102,   103,   104,   105,   106,
     107,   108,   109,   110,   111,   112,   113,   114,   115,   116,
     117,   118,   119,   120,   121,   122,   123,   124,   125,   126,
     127,   128,   129,   130,   131,   132,   133,   134,   135,   136,
     137,   138,   139,   140,   141,   142,   143,   144,   145,   146,
     147,   148,   149,   150,   151,   152,   153,   154,   155,   156,
     157,   158,   159,   160,   161,   162,   163,   164,   165,   166,
     167,   168,   169,   170,   171,   172,   173,   174,   175,   176,
     177,   178,   179,   180,   181,   182,   183,   184,   185,   186,
     187,   188,   189,   190,   191,   192,   193,   194,   195,   196,
     197,    -1,   199,   200,   201,   202,   203,   204,   205,    -1,
      -1,   208,   209,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
     227,     6,     7,     8,     9,    10,    -1,    -1,    -1,    -1,
     237,   238,   239,   240,   241,    -1,    -1,    -1,    23,    24,
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    52,    53,    54,
      55,    56,    57,    -1,    -1,    -1,    -1,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    78,    79,    80,    81,    82,    83,    84,
      85,    86,    87,    88,    89,    90,    91,    92,    93,    94,
      95,    96,    97,    98,    99,   100,   101,   102,   103,   104,
     105,   106,   107,   108,   109,   110,   111,   112,   113,   114,
     115,   116,   117,   118,   119,   120,   121,   122,   123,   124,
     125,   126,   127,   128,   129,   130,   131,   132,   133,   134,
     135,   136,   137,   138,   139,   140,   141,   142,   143,   144,
     145,   146,   147,   148,   149,   150,   151,   152,   153,   154,
     155,   156,   157,   158,   159,   160,   161,   162,   163,   164,
     165,   166,   167,   168,   169,   170,   171,   172,   173,   174,
     175,   176,   177,   178,   179,   180,   181,   182,   183,   184,
     185,   186,   187,   188,   189,   190,   191,   192,   193,   194,
     195,   196,   197,    -1,   199,   200,   201,   202,   203,   204,
     205,    -1,    -1,   208,   209,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,   227,     6,     7,     8,     9,    10,    -1,    -1,
      -1,    -1,    -1,   238,   239,   240,   241,    -1,    -1,    -1,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    52,
      53,    54,    55,    56,    57,    -1,    -1,    -1,    -1,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     193,   194,   195,   196,   197,    -1,   199,   200,   201,   202,
     203,   204,   205,    -1,    -1,   208,   209,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,   227,     6,     7,     8,     9,    10,
      -1,    -1,    -1,    -1,    -1,   238,   239,   240,   241,    -1,
      -1,    -1,    23,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    52,    53,    54,    55,    56,    57,    -1,    -1,    -1,
      -1,    62,    63,    64,    65,    66,    67,    68,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    86,    87,    88,    89,    90,
      91,    92,    93,    94,    95,    96,    97,    98,    99,   100,
     101,   102,   103,   104,   105,   106,   107,   108,   109,   110,
     111,   112,   113,   114,   115,   116,   117,   118,   119,   120,
     121,   122,   123,   124,   125,   126,   127,   128,   129,   130,
     131,   132,   133,   134,   135,   136,   137,   138,   139,   140,
     141,   142,   143,   144,   145,   146,   147,   148,   149,   150,
     151,   152,   153,   154,   155,   156,   157,   158,   159,   160,
     161,   162,   163,   164,   165,   166,   167,   168,   169,   170,
     171,   172,   173,   174,   175,   176,   177,   178,   179,   180,
     181,   182,   183,   184,   185,   186,   187,   188,   189,   190,
     191,   192,   193,   194,   195,   196,   197,    -1,    -1,   200
};

  /* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
     symbol of state STATE-NUM.  */
static const yytype_uint16 yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    22,
      23,    24,    25,    26,    27,    28,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    44,    45,    46,    47,    48,    49,    50,    51,    52,
      53,    54,    55,    56,    57,    58,    59,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    78,    79,    80,    81,    82,
      83,    84,    85,    86,    87,    88,    89,    90,    91,    92,
      93,    94,    95,    96,    97,    98,    99,   100,   101,   102,
     103,   104,   105,   106,   107,   108,   109,   110,   111,   112,
     113,   114,   115,   116,   117,   118,   119,   120,   121,   122,
     123,   124,   125,   126,   127,   128,   129,   130,   131,   132,
     133,   134,   135,   136,   137,   138,   139,   140,   141,   142,
     143,   144,   145,   146,   147,   148,   149,   150,   151,   152,
     153,   154,   155,   156,   157,   158,   159,   160,   161,   162,
     163,   164,   165,   166,   167,   168,   169,   170,   171,   172,
     173,   174,   175,   176,   177,   178,   179,   180,   181,   182,
     183,   184,   185,   186,   187,   188,   189,   190,   191,   192,
     193,   194,   195,   196,   197,   200,   251,   252,   253,   254,
     255,   256,   291,   292,   295,   296,   297,   298,   302,   303,
     304,   305,   306,   307,   310,   311,   312,   313,   315,   317,
     318,   319,   356,   357,   358,   227,   227,   199,   231,   318,
     199,   237,   237,   359,   228,   234,   299,   300,   301,   311,
     315,   234,   237,   199,   199,   237,   312,   315,   229,   316,
       0,   357,   200,   314,    46,   199,   308,   309,   231,   321,
     315,   237,   316,   231,   338,   300,   299,   301,   199,   199,
     227,   236,   316,   231,   234,   237,   294,   199,   201,   202,
     203,   204,   205,   208,   209,   227,   230,   238,   239,   240,
     241,   261,   262,   263,   265,   266,   267,   268,   269,   270,
     271,   272,   273,   274,   275,   276,   277,   278,   279,   280,
     281,   282,   283,   284,   285,   315,   229,   228,   234,   236,
     228,   234,   320,   311,   315,   322,   323,   237,   237,    11,
      12,    13,    15,    16,    17,    18,    19,    20,    21,   198,
     231,   232,   237,   272,   285,   287,   289,   291,   295,   315,
     328,   329,   330,   331,   339,   340,   341,   344,   347,   348,
     355,   316,   236,   316,   231,   287,   326,   236,   293,   199,
     234,   237,   272,   272,   289,   208,   209,   229,   233,   228,
     228,   234,   197,   287,   227,   272,   242,   243,   244,   239,
     241,   206,   207,   210,   211,   245,   246,   212,   213,   249,
     248,   247,   214,   216,   215,   250,   230,   230,   285,   200,
     285,   290,   309,   322,   315,   199,   324,   325,   232,   323,
     237,   237,   350,   227,   227,   237,   237,   289,   227,   289,
     235,   227,   232,   332,   217,   218,   219,   220,   221,   222,
     223,   224,   225,   226,   236,   288,   234,   237,   232,   329,
     326,   236,   326,   327,   326,   322,   199,   228,   264,   289,
     199,   287,   272,   272,   272,   274,   274,   275,   275,   276,
     276,   276,   276,   277,   277,   278,   279,   280,   281,   282,
     283,   286,   230,   232,   324,   316,   234,   237,   329,   351,
     289,   237,   289,   235,   349,   339,   287,   287,   326,   232,
     234,   232,   230,   289,   237,   325,   198,   328,   340,   352,
     228,   228,   289,   304,   311,   343,   333,   232,   326,   235,
     227,   343,   353,   354,   335,   336,   337,   342,   345,   199,
     228,   232,   287,   289,   237,   228,    14,   331,   330,   231,
     236,   330,   334,   338,   228,   289,   334,   335,   339,   346,
     326,   237,   232
};

  /* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint16 yyr1[] =
{
       0,   260,   261,   262,   262,   262,   262,   262,   262,   262,
     263,   263,   263,   263,   263,   263,   264,   265,   266,   267,
     267,   268,   268,   269,   269,   270,   271,   271,   272,   272,
     272,   272,   273,   273,   273,   273,   274,   274,   274,   274,
     275,   275,   275,   276,   276,   276,   277,   277,   277,   277,
     277,   278,   278,   278,   279,   279,   280,   280,   281,   281,
     282,   282,   283,   283,   284,   284,   285,   286,   285,   287,
     287,   288,   288,   288,   288,   288,   288,   288,   288,   288,
     288,   288,   289,   289,   290,   291,   291,   291,   291,   291,
     291,   291,   291,   291,   293,   292,   294,   294,   295,   296,
     296,   297,   297,   298,   299,   299,   300,   300,   300,   300,
     301,   302,   302,   302,   302,   302,   303,   303,   303,   303,
     303,   304,   304,   305,   306,   306,   306,   307,   308,   308,
     309,   309,   309,   310,   311,   311,   312,   312,   312,   312,
     312,   312,   313,   313,   313,   313,   313,   313,   313,   313,
     313,   313,   313,   313,   313,   313,   313,   313,   313,   313,
     313,   314,   314,   315,   315,   316,   316,   316,   316,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   317,   317,   317,   317,   317,   317,   317,   317,
     317,   317,   318,   318,   318,   320,   319,   321,   319,   322,
     322,   323,   323,   324,   324,   325,   325,   326,   326,   326,
     327,   327,   328,   329,   329,   330,   330,   330,   330,   330,
     330,   330,   331,   332,   333,   331,   334,   334,   336,   335,
     337,   335,   338,   338,   339,   339,   340,   340,   341,   342,
     342,   343,   343,   345,   344,   346,   346,   347,   347,   349,
     348,   350,   348,   351,   348,   352,   352,   353,   353,   354,
     354,   355,   355,   355,   355,   355,   356,   356,   357,   357,
     359,   358
};

  /* YYR2[YYN] -- Number of symbols on the right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     1,     1,     1,     1,     1,     1,     3,
       1,     4,     1,     3,     2,     2,     1,     1,     1,     2,
       2,     2,     1,     2,     3,     2,     1,     1,     1,     2,
       2,     2,     1,     1,     1,     1,     1,     3,     3,     3,
       1,     3,     3,     1,     3,     3,     1,     3,     3,     3,
       3,     1,     3,     3,     1,     3,     1,     3,     1,     3,
       1,     3,     1,     3,     1,     3,     1,     0,     6,     1,
       3,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     1,     2,     2,     4,     2,     3,
       4,     2,     3,     4,     0,     6,     2,     3,     2,     1,
       1,     2,     3,     3,     2,     3,     2,     1,     2,     1,
       1,     1,     3,     4,     6,     5,     1,     2,     3,     5,
       4,     1,     2,     1,     1,     1,     1,     4,     1,     3,
       1,     3,     1,     1,     1,     2,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       4,     1,     3,     1,     2,     2,     3,     3,     4,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     0,     6,     0,     5,     1,
       2,     3,     4,     1,     3,     1,     2,     1,     3,     4,
       1,     3,     1,     1,     1,     1,     1,     1,     1,     1,
       1,     1,     2,     0,     0,     5,     1,     1,     0,     2,
       0,     2,     2,     3,     1,     2,     1,     2,     5,     3,
       1,     1,     4,     0,     8,     0,     1,     3,     2,     0,
       6,     0,     8,     0,     7,     1,     1,     1,     0,     2,
       3,     2,     2,     2,     3,     2,     1,     2,     1,     1,
       0,     3
};


#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)
#define YYEMPTY         (-2)
#define YYEOF           0

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                  \
do                                                              \
  if (yychar == YYEMPTY)                                        \
    {                                                           \
      yychar = (Token);                                         \
      yylval = (Value);                                         \
      YYPOPSTACK (yylen);                                       \
      yystate = *yyssp;                                         \
      goto yybackup;                                            \
    }                                                           \
  else                                                          \
    {                                                           \
      yyerror (pParseContext, YY_("syntax error: cannot back up")); \
      YYERROR;                                                  \
    }                                                           \
while (0)

/* Error token number */
#define YYTERROR        1
#define YYERRCODE       256



/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)

/* This macro is provided for backward compatibility. */
#ifndef YY_LOCATION_PRINT
# define YY_LOCATION_PRINT(File, Loc) ((void) 0)
#endif


# define YY_SYMBOL_PRINT(Title, Type, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Type, Value, pParseContext); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*----------------------------------------.
| Print this symbol's value on YYOUTPUT.  |
`----------------------------------------*/

static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, glslang::TParseContext* pParseContext)
{
  FILE *yyo = yyoutput;
  YYUSE (yyo);
  YYUSE (pParseContext);
  if (!yyvaluep)
    return;
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  YYUSE (yytype);
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, glslang::TParseContext* pParseContext)
{
  YYFPRINTF (yyoutput, "%s %s (",
             yytype < YYNTOKENS ? "token" : "nterm", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep, pParseContext);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yytype_int16 *yyssp, YYSTYPE *yyvsp, int yyrule, glslang::TParseContext* pParseContext)
{
  unsigned long int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       yystos[yyssp[yyi + 1 - yynrhs]],
                       &(yyvsp[(yyi + 1) - (yynrhs)])
                                              , pParseContext);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule, pParseContext); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif


#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
yystrlen (const char *yystr)
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
yystpcpy (char *yydest, const char *yysrc)
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
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
            if (yyres)
              yyres[yyn] = *yyp;
            yyn++;
            break;

          case '"':
            if (yyres)
              yyres[yyn] = '\0';
            return yyn;
          }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (YY_NULLPTR, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = YY_NULLPTR;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                {
                  YYSIZE_T yysize1 = yysize + yytnamerr (YY_NULLPTR, yytname[yyx]);
                  if (! (yysize <= yysize1
                         && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                    return 2;
                  yysize = yysize1;
                }
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  {
    YYSIZE_T yysize1 = yysize + yystrlen (yyformat);
    if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
      return 2;
    yysize = yysize1;
  }

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, glslang::TParseContext* pParseContext)
{
  YYUSE (yyvaluep);
  YYUSE (pParseContext);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YYUSE (yytype);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}




/*----------.
| yyparse.  |
`----------*/

int
yyparse (glslang::TParseContext* pParseContext)
{
/* The lookahead symbol.  */
int yychar;


/* The semantic value of the lookahead symbol.  */
/* Default value used for initialization, for pacifying older GCCs
   or non-GCC compilers.  */
YY_INITIAL_VALUE (static YYSTYPE yyval_default;)
YYSTYPE yylval YY_INITIAL_VALUE (= yyval_default);

    /* Number of syntax errors so far.  */
    int yynerrs;

    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       'yyss': related to states.
       'yyvs': related to semantic values.

       Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken = 0;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yyssp = yyss = yyssa;
  yyvsp = yyvs = yyvsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */
  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        YYSTYPE *yyvs1 = yyvs;
        yytype_int16 *yyss1 = yyss;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * sizeof (*yyssp),
                    &yyvs1, yysize * sizeof (*yyvsp),
                    &yystacksize);

        yyss = yyss1;
        yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yytype_int16 *yyss1 = yyss;
        union yyalloc *yyptr =
          (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
        if (! yyptr)
          goto yyexhaustedlab;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
                  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = yylex (&yylval, parseContext);
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

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
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 244 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleVariable((yyvsp[0].lex).loc, (yyvsp[0].lex).symbol, (yyvsp[0].lex).string);
    }
#line 3095 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 3:
#line 250 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 3103 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 4:
#line 253 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion((yyvsp[0].lex).i, (yyvsp[0].lex).loc, true);
    }
#line 3111 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 5:
#line 256 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "unsigned literal");
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion((yyvsp[0].lex).u, (yyvsp[0].lex).loc, true);
    }
#line 3120 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 6:
#line 260 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion((yyvsp[0].lex).d, EbtFloat, (yyvsp[0].lex).loc, true);
    }
#line 3128 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 7:
#line 263 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double literal");
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion((yyvsp[0].lex).d, EbtDouble, (yyvsp[0].lex).loc, true);
    }
#line 3137 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 8:
#line 267 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion((yyvsp[0].lex).b, (yyvsp[0].lex).loc, true);
    }
#line 3145 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 9:
#line 270 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[-1].interm.intermTypedNode);
        if ((yyval.interm.intermTypedNode)->getAsConstantUnion())
            (yyval.interm.intermTypedNode)->getAsConstantUnion()->setExpression();
    }
#line 3155 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 10:
#line 278 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 3163 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 11:
#line 281 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBracketDereference((yyvsp[-2].lex).loc, (yyvsp[-3].interm.intermTypedNode), (yyvsp[-1].interm.intermTypedNode));
    }
#line 3171 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 12:
#line 284 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 3179 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 13:
#line 287 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleDotDereference((yyvsp[0].lex).loc, (yyvsp[-2].interm.intermTypedNode), *(yyvsp[0].lex).string);
    }
#line 3187 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 14:
#line 290 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.variableCheck((yyvsp[-1].interm.intermTypedNode));
        parseContext.lValueErrorCheck((yyvsp[0].lex).loc, "++", (yyvsp[-1].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = parseContext.handleUnaryMath((yyvsp[0].lex).loc, "++", EOpPostIncrement, (yyvsp[-1].interm.intermTypedNode));
    }
#line 3197 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 15:
#line 295 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.variableCheck((yyvsp[-1].interm.intermTypedNode));
        parseContext.lValueErrorCheck((yyvsp[0].lex).loc, "--", (yyvsp[-1].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = parseContext.handleUnaryMath((yyvsp[0].lex).loc, "--", EOpPostDecrement, (yyvsp[-1].interm.intermTypedNode));
    }
#line 3207 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 16:
#line 303 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.integerCheck((yyvsp[0].interm.intermTypedNode), "[]");
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 3216 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 17:
#line 310 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleFunctionCall((yyvsp[0].interm).loc, (yyvsp[0].interm).function, (yyvsp[0].interm).intermNode);
        delete (yyvsp[0].interm).function;
    }
#line 3225 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 18:
#line 317 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);
    }
#line 3233 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 19:
#line 323 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-1].interm);
        (yyval.interm).loc = (yyvsp[0].lex).loc;
    }
#line 3242 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 20:
#line 327 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-1].interm);
        (yyval.interm).loc = (yyvsp[0].lex).loc;
    }
#line 3251 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 21:
#line 334 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-1].interm);
    }
#line 3259 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 22:
#line 337 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);
    }
#line 3267 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 23:
#line 343 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        TParameter param = { 0, new TType };
        param.type->shallowCopy((yyvsp[0].interm.intermTypedNode)->getType());
        (yyvsp[-1].interm).function->addParameter(param);
        (yyval.interm).function = (yyvsp[-1].interm).function;
        (yyval.interm).intermNode = (yyvsp[0].interm.intermTypedNode);
    }
#line 3279 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 24:
#line 350 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        TParameter param = { 0, new TType };
        param.type->shallowCopy((yyvsp[0].interm.intermTypedNode)->getType());
        (yyvsp[-2].interm).function->addParameter(param);
        (yyval.interm).function = (yyvsp[-2].interm).function;
        (yyval.interm).intermNode = parseContext.intermediate.growAggregate((yyvsp[-2].interm).intermNode, (yyvsp[0].interm.intermTypedNode), (yyvsp[-1].lex).loc);
    }
#line 3291 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 25:
#line 360 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-1].interm);
    }
#line 3299 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 26:
#line 368 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // Constructor
        (yyval.interm).intermNode = 0;
        (yyval.interm).function = parseContext.handleConstructorCall((yyvsp[0].interm.type).loc, (yyvsp[0].interm.type));
    }
#line 3309 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 27:
#line 373 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        //
        // Should be a method or subroutine call, but we haven't recognized the arguments yet.
        //
        (yyval.interm).function = 0;
        (yyval.interm).intermNode = 0;

        TIntermMethod* method = (yyvsp[0].interm.intermTypedNode)->getAsMethodNode();
        if (method) {
            (yyval.interm).function = new TFunction(&method->getMethodName(), TType(EbtInt), EOpArrayLength);
            (yyval.interm).intermNode = method->getObject();
        } else {
            TIntermSymbol* symbol = (yyvsp[0].interm.intermTypedNode)->getAsSymbolNode();
            if (symbol) {
                parseContext.reservedErrorCheck(symbol->getLoc(), symbol->getName());
                TFunction *function = new TFunction(&symbol->getName(), TType(EbtVoid));
                (yyval.interm).function = function;
            } else
                parseContext.error((yyvsp[0].interm.intermTypedNode)->getLoc(), "function call, method, or subroutine call expected", "", "");
        }

        if ((yyval.interm).function == 0) {
            // error recover
            TString empty("");
            (yyval.interm).function = new TFunction(&empty, TType(EbtVoid), EOpNull);
        }
    }
#line 3341 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 28:
#line 403 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.variableCheck((yyvsp[0].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
        if (TIntermMethod* method = (yyvsp[0].interm.intermTypedNode)->getAsMethodNode())
            parseContext.error((yyvsp[0].interm.intermTypedNode)->getLoc(), "incomplete method syntax", method->getMethodName().c_str(), "");
    }
#line 3352 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 29:
#line 409 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.lValueErrorCheck((yyvsp[-1].lex).loc, "++", (yyvsp[0].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = parseContext.handleUnaryMath((yyvsp[-1].lex).loc, "++", EOpPreIncrement, (yyvsp[0].interm.intermTypedNode));
    }
#line 3361 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 30:
#line 413 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.lValueErrorCheck((yyvsp[-1].lex).loc, "--", (yyvsp[0].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = parseContext.handleUnaryMath((yyvsp[-1].lex).loc, "--", EOpPreDecrement, (yyvsp[0].interm.intermTypedNode));
    }
#line 3370 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 31:
#line 417 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-1].interm).op != EOpNull) {
            char errorOp[2] = {0, 0};
            switch((yyvsp[-1].interm).op) {
            case EOpNegative:   errorOp[0] = '-'; break;
            case EOpLogicalNot: errorOp[0] = '!'; break;
            case EOpBitwiseNot: errorOp[0] = '~'; break;
            default: break; // some compilers want this
            }
            (yyval.interm.intermTypedNode) = parseContext.handleUnaryMath((yyvsp[-1].interm).loc, errorOp, (yyvsp[-1].interm).op, (yyvsp[0].interm.intermTypedNode));
        } else {
            (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
            if ((yyval.interm.intermTypedNode)->getAsConstantUnion())
                (yyval.interm.intermTypedNode)->getAsConstantUnion()->setExpression();
        }
    }
#line 3391 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 32:
#line 437 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpNull; }
#line 3397 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 33:
#line 438 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpNegative; }
#line 3403 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 34:
#line 439 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpLogicalNot; }
#line 3409 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 35:
#line 440 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpBitwiseNot;
              parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "bitwise not"); }
#line 3416 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 36:
#line 446 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3422 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 37:
#line 447 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "*", EOpMul, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3432 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 38:
#line 452 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "/", EOpDiv, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3442 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 39:
#line 457 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[-1].lex).loc, "%");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "%", EOpMod, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3453 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 40:
#line 466 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3459 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 41:
#line 467 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "+", EOpAdd, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3469 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 42:
#line 472 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "-", EOpSub, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3479 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 43:
#line 480 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3485 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 44:
#line 481 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[-1].lex).loc, "bit shift left");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "<<", EOpLeftShift, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3496 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 45:
#line 487 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[-1].lex).loc, "bit shift right");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, ">>", EOpRightShift, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3507 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 46:
#line 496 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3513 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 47:
#line 497 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "<", EOpLessThan, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3523 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 48:
#line 502 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, ">", EOpGreaterThan, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3533 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 49:
#line 507 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "<=", EOpLessThanEqual, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3543 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 50:
#line 512 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, ">=", EOpGreaterThanEqual, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3553 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 51:
#line 520 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3559 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 52:
#line 521 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.arrayObjectCheck((yyvsp[-1].lex).loc, (yyvsp[-2].interm.intermTypedNode)->getType(), "array comparison");
        parseContext.opaqueCheck((yyvsp[-1].lex).loc, (yyvsp[-2].interm.intermTypedNode)->getType(), "==");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "==", EOpEqual, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3571 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 53:
#line 528 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.arrayObjectCheck((yyvsp[-1].lex).loc, (yyvsp[-2].interm.intermTypedNode)->getType(), "array comparison");
        parseContext.opaqueCheck((yyvsp[-1].lex).loc, (yyvsp[-2].interm.intermTypedNode)->getType(), "!=");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "!=", EOpNotEqual, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3583 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 54:
#line 538 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3589 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 55:
#line 539 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[-1].lex).loc, "bitwise and");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "&", EOpAnd, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3600 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 56:
#line 548 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3606 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 57:
#line 549 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[-1].lex).loc, "bitwise exclusive or");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "^", EOpExclusiveOr, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3617 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 58:
#line 558 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3623 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 59:
#line 559 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[-1].lex).loc, "bitwise inclusive or");
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "|", EOpInclusiveOr, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 3634 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 60:
#line 568 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3640 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 61:
#line 569 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "&&", EOpLogicalAnd, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3650 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 62:
#line 577 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3656 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 63:
#line 578 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "^^", EOpLogicalXor, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3666 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 64:
#line 586 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3672 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 65:
#line 587 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.handleBinaryMath((yyvsp[-1].lex).loc, "||", EOpLogicalOr, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
        if ((yyval.interm.intermTypedNode) == 0)
            (yyval.interm.intermTypedNode) = parseContext.intermediate.addConstantUnion(false, (yyvsp[-1].lex).loc);
    }
#line 3682 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 66:
#line 595 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3688 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 67:
#line 596 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        ++parseContext.controlFlowNestingLevel;
    }
#line 3696 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 68:
#line 599 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        --parseContext.controlFlowNestingLevel;
        parseContext.boolCheck((yyvsp[-4].lex).loc, (yyvsp[-5].interm.intermTypedNode));
        parseContext.rValueErrorCheck((yyvsp[-4].lex).loc, "?", (yyvsp[-5].interm.intermTypedNode));
        parseContext.rValueErrorCheck((yyvsp[-1].lex).loc, ":", (yyvsp[-2].interm.intermTypedNode));
        parseContext.rValueErrorCheck((yyvsp[-1].lex).loc, ":", (yyvsp[0].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addSelection((yyvsp[-5].interm.intermTypedNode), (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode), (yyvsp[-4].lex).loc);
        if ((yyval.interm.intermTypedNode) == 0) {
            parseContext.binaryOpError((yyvsp[-4].lex).loc, ":", (yyvsp[-2].interm.intermTypedNode)->getCompleteString(), (yyvsp[0].interm.intermTypedNode)->getCompleteString());
            (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
        }
    }
#line 3713 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 69:
#line 614 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode); }
#line 3719 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 70:
#line 615 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.arrayObjectCheck((yyvsp[-1].interm).loc, (yyvsp[-2].interm.intermTypedNode)->getType(), "array assignment");
        parseContext.opaqueCheck((yyvsp[-1].interm).loc, (yyvsp[-2].interm.intermTypedNode)->getType(), "=");
        parseContext.lValueErrorCheck((yyvsp[-1].interm).loc, "assign", (yyvsp[-2].interm.intermTypedNode));
        parseContext.rValueErrorCheck((yyvsp[-1].interm).loc, "assign", (yyvsp[0].interm.intermTypedNode));
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addAssign((yyvsp[-1].interm).op, (yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode), (yyvsp[-1].interm).loc);
        if ((yyval.interm.intermTypedNode) == 0) {
            parseContext.assignError((yyvsp[-1].interm).loc, "assign", (yyvsp[-2].interm.intermTypedNode)->getCompleteString(), (yyvsp[0].interm.intermTypedNode)->getCompleteString());
            (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
        }
    }
#line 3735 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 71:
#line 629 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).op = EOpAssign;
    }
#line 3744 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 72:
#line 633 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).op = EOpMulAssign;
    }
#line 3753 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 73:
#line 637 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).op = EOpDivAssign;
    }
#line 3762 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 74:
#line 641 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "%=");
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).op = EOpModAssign;
    }
#line 3772 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 75:
#line 646 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).op = EOpAddAssign;
    }
#line 3781 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 76:
#line 650 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).op = EOpSubAssign;
    }
#line 3790 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 77:
#line 654 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "bit-shift left assign");
        (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpLeftShiftAssign;
    }
#line 3799 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 78:
#line 658 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "bit-shift right assign");
        (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpRightShiftAssign;
    }
#line 3808 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 79:
#line 662 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "bitwise-and assign");
        (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpAndAssign;
    }
#line 3817 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 80:
#line 666 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "bitwise-xor assign");
        (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpExclusiveOrAssign;
    }
#line 3826 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 81:
#line 670 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "bitwise-or assign");
        (yyval.interm).loc = (yyvsp[0].lex).loc; (yyval.interm).op = EOpInclusiveOrAssign;
    }
#line 3835 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 82:
#line 677 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 3843 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 83:
#line 680 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.intermediate.addComma((yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode), (yyvsp[-1].lex).loc);
        if ((yyval.interm.intermTypedNode) == 0) {
            parseContext.binaryOpError((yyvsp[-1].lex).loc, ",", (yyvsp[-2].interm.intermTypedNode)->getCompleteString(), (yyvsp[0].interm.intermTypedNode)->getCompleteString());
            (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
        }
    }
#line 3855 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 84:
#line 690 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.constantValueCheck((yyvsp[0].interm.intermTypedNode), "");
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 3864 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 85:
#line 697 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.handleFunctionDeclarator((yyvsp[-1].interm).loc, *(yyvsp[-1].interm).function, true /* prototype */);
        (yyval.interm.intermNode) = 0;
        // TODO: 4.0 functionality: subroutines: make the identifier a user type for this signature
    }
#line 3874 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 86:
#line 702 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-1].interm).intermNode && (yyvsp[-1].interm).intermNode->getAsAggregate())
            (yyvsp[-1].interm).intermNode->getAsAggregate()->setOperator(EOpSequence);
        (yyval.interm.intermNode) = (yyvsp[-1].interm).intermNode;
    }
#line 3884 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 87:
#line 707 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.profileRequires((yyvsp[-3].lex).loc, ENoProfile, 130, 0, "precision statement");

        // lazy setting of the previous scope's defaults, has effect only the first time it is called in a particular scope
        parseContext.symbolTable.setPreviousDefaultPrecisions(&parseContext.defaultPrecision[0]);
        parseContext.setDefaultPrecision((yyvsp[-3].lex).loc, (yyvsp[-1].interm.type), (yyvsp[-2].interm.type).qualifier.precision);
        (yyval.interm.intermNode) = 0;
    }
#line 3897 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 88:
#line 715 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.declareBlock((yyvsp[-1].interm).loc, *(yyvsp[-1].interm).typeList);
        (yyval.interm.intermNode) = 0;
    }
#line 3906 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 89:
#line 719 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.declareBlock((yyvsp[-2].interm).loc, *(yyvsp[-2].interm).typeList, (yyvsp[-1].lex).string);
        (yyval.interm.intermNode) = 0;
    }
#line 3915 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 90:
#line 723 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.declareBlock((yyvsp[-3].interm).loc, *(yyvsp[-3].interm).typeList, (yyvsp[-2].lex).string, (yyvsp[-1].interm).arraySizes);
        (yyval.interm.intermNode) = 0;
    }
#line 3924 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 91:
#line 727 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalQualifierFixCheck((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).qualifier);
        parseContext.updateStandaloneQualifierDefaults((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type));
        (yyval.interm.intermNode) = 0;
    }
#line 3934 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 92:
#line 732 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.checkNoShaderLayouts((yyvsp[-2].interm.type).loc, (yyvsp[-2].interm.type).shaderQualifiers);
        parseContext.addQualifierToExisting((yyvsp[-2].interm.type).loc, (yyvsp[-2].interm.type).qualifier, *(yyvsp[-1].lex).string);
        (yyval.interm.intermNode) = 0;
    }
#line 3944 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 93:
#line 737 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.checkNoShaderLayouts((yyvsp[-3].interm.type).loc, (yyvsp[-3].interm.type).shaderQualifiers);
        (yyvsp[-1].interm.identifierList)->push_back((yyvsp[-2].lex).string);
        parseContext.addQualifierToExisting((yyvsp[-3].interm.type).loc, (yyvsp[-3].interm.type).qualifier, *(yyvsp[-1].interm.identifierList));
        (yyval.interm.intermNode) = 0;
    }
#line 3955 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 94:
#line 746 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { parseContext.nestedBlockCheck((yyvsp[-2].interm.type).loc); }
#line 3961 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 95:
#line 746 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        --parseContext.structNestingLevel;
        parseContext.blockName = (yyvsp[-4].lex).string;
        parseContext.globalQualifierFixCheck((yyvsp[-5].interm.type).loc, (yyvsp[-5].interm.type).qualifier);
        parseContext.checkNoShaderLayouts((yyvsp[-5].interm.type).loc, (yyvsp[-5].interm.type).shaderQualifiers);
        parseContext.currentBlockQualifier = (yyvsp[-5].interm.type).qualifier;
        (yyval.interm).loc = (yyvsp[-5].interm.type).loc;
        (yyval.interm).typeList = (yyvsp[-1].interm.typeList);
    }
#line 3975 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 96:
#line 757 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.identifierList) = new TIdentifierList;
        (yyval.interm.identifierList)->push_back((yyvsp[0].lex).string);
    }
#line 3984 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 97:
#line 761 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.identifierList) = (yyvsp[-2].interm.identifierList);
        (yyval.interm.identifierList)->push_back((yyvsp[0].lex).string);
    }
#line 3993 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 98:
#line 768 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).function = (yyvsp[-1].interm.function);
        (yyval.interm).loc = (yyvsp[0].lex).loc;
    }
#line 4002 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 99:
#line 775 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.function) = (yyvsp[0].interm.function);
    }
#line 4010 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 100:
#line 778 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.function) = (yyvsp[0].interm.function);
    }
#line 4018 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 101:
#line 785 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // Add the parameter
        (yyval.interm.function) = (yyvsp[-1].interm.function);
        if ((yyvsp[0].interm).param.type->getBasicType() != EbtVoid)
            (yyvsp[-1].interm.function)->addParameter((yyvsp[0].interm).param);
        else
            delete (yyvsp[0].interm).param.type;
    }
#line 4031 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 102:
#line 793 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        //
        // Only first parameter of one-parameter functions can be void
        // The check for named parameters not being void is done in parameter_declarator
        //
        if ((yyvsp[0].interm).param.type->getBasicType() == EbtVoid) {
            //
            // This parameter > first is void
            //
            parseContext.error((yyvsp[-1].lex).loc, "cannot be an argument type except for '(void)'", "void", "");
            delete (yyvsp[0].interm).param.type;
        } else {
            // Add the parameter
            (yyval.interm.function) = (yyvsp[-2].interm.function);
            (yyvsp[-2].interm.function)->addParameter((yyvsp[0].interm).param);
        }
    }
#line 4053 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 103:
#line 813 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-2].interm.type).qualifier.storage != EvqGlobal && (yyvsp[-2].interm.type).qualifier.storage != EvqTemporary) {
            parseContext.error((yyvsp[-1].lex).loc, "no qualifiers allowed for function return",
                               GetStorageQualifierString((yyvsp[-2].interm.type).qualifier.storage), "");
        }
        if ((yyvsp[-2].interm.type).arraySizes)
            parseContext.arraySizeRequiredCheck((yyvsp[-2].interm.type).loc, *(yyvsp[-2].interm.type).arraySizes);

        // Add the function as a prototype after parsing it (we do not support recursion)
        TFunction *function;
        TType type((yyvsp[-2].interm.type));
        function = new TFunction((yyvsp[-1].lex).string, type);
        (yyval.interm.function) = function;
    }
#line 4072 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 104:
#line 831 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-1].interm.type).arraySizes) {
            parseContext.profileRequires((yyvsp[-1].interm.type).loc, ENoProfile, 120, E_GL_3DL_array_objects, "arrayed type");
            parseContext.profileRequires((yyvsp[-1].interm.type).loc, EEsProfile, 300, 0, "arrayed type");
            parseContext.arraySizeRequiredCheck((yyvsp[-1].interm.type).loc, *(yyvsp[-1].interm.type).arraySizes);
        }
        if ((yyvsp[-1].interm.type).basicType == EbtVoid) {
            parseContext.error((yyvsp[0].lex).loc, "illegal use of type 'void'", (yyvsp[0].lex).string->c_str(), "");
        }
        parseContext.reservedErrorCheck((yyvsp[0].lex).loc, *(yyvsp[0].lex).string);

        TParameter param = {(yyvsp[0].lex).string, new TType((yyvsp[-1].interm.type))};
        (yyval.interm).loc = (yyvsp[0].lex).loc;
        (yyval.interm).param = param;
    }
#line 4092 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 105:
#line 846 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-2].interm.type).arraySizes) {
            parseContext.profileRequires((yyvsp[-2].interm.type).loc, ENoProfile, 120, E_GL_3DL_array_objects, "arrayed type");
            parseContext.profileRequires((yyvsp[-2].interm.type).loc, EEsProfile, 300, 0, "arrayed type");
            parseContext.arraySizeRequiredCheck((yyvsp[-2].interm.type).loc, *(yyvsp[-2].interm.type).arraySizes);
        }
        parseContext.arrayDimCheck((yyvsp[-1].lex).loc, (yyvsp[-2].interm.type).arraySizes, (yyvsp[0].interm).arraySizes);

        parseContext.arraySizeRequiredCheck((yyvsp[0].interm).loc, *(yyvsp[0].interm).arraySizes);
        parseContext.reservedErrorCheck((yyvsp[-1].lex).loc, *(yyvsp[-1].lex).string);

        (yyvsp[-2].interm.type).arraySizes = (yyvsp[0].interm).arraySizes;

        TParameter param = { (yyvsp[-1].lex).string, new TType((yyvsp[-2].interm.type))};
        (yyval.interm).loc = (yyvsp[-1].lex).loc;
        (yyval.interm).param = param;
    }
#line 4114 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 106:
#line 869 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);
        if ((yyvsp[-1].interm.type).qualifier.precision != EpqNone)
            (yyval.interm).param.type->getQualifier().precision = (yyvsp[-1].interm.type).qualifier.precision;
        parseContext.precisionQualifierCheck((yyval.interm).loc, (yyval.interm).param.type->getBasicType(), (yyval.interm).param.type->getQualifier());

        parseContext.checkNoShaderLayouts((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).shaderQualifiers);
        parseContext.parameterTypeCheck((yyvsp[0].interm).loc, (yyvsp[-1].interm.type).qualifier.storage, *(yyval.interm).param.type);
        parseContext.paramCheckFix((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).qualifier, *(yyval.interm).param.type);

    }
#line 4130 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 107:
#line 880 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);

        parseContext.parameterTypeCheck((yyvsp[0].interm).loc, EvqIn, *(yyvsp[0].interm).param.type);
        parseContext.paramCheckFix((yyvsp[0].interm).loc, EvqTemporary, *(yyval.interm).param.type);
        parseContext.precisionQualifierCheck((yyval.interm).loc, (yyval.interm).param.type->getBasicType(), (yyval.interm).param.type->getQualifier());
    }
#line 4142 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 108:
#line 890 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);
        if ((yyvsp[-1].interm.type).qualifier.precision != EpqNone)
            (yyval.interm).param.type->getQualifier().precision = (yyvsp[-1].interm.type).qualifier.precision;
        parseContext.precisionQualifierCheck((yyvsp[-1].interm.type).loc, (yyval.interm).param.type->getBasicType(), (yyval.interm).param.type->getQualifier());

        parseContext.checkNoShaderLayouts((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).shaderQualifiers);
        parseContext.parameterTypeCheck((yyvsp[0].interm).loc, (yyvsp[-1].interm.type).qualifier.storage, *(yyval.interm).param.type);
        parseContext.paramCheckFix((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).qualifier, *(yyval.interm).param.type);
    }
#line 4157 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 109:
#line 900 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);

        parseContext.parameterTypeCheck((yyvsp[0].interm).loc, EvqIn, *(yyvsp[0].interm).param.type);
        parseContext.paramCheckFix((yyvsp[0].interm).loc, EvqTemporary, *(yyval.interm).param.type);
        parseContext.precisionQualifierCheck((yyval.interm).loc, (yyval.interm).param.type->getBasicType(), (yyval.interm).param.type->getQualifier());
    }
#line 4169 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 110:
#line 910 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        TParameter param = { 0, new TType((yyvsp[0].interm.type)) };
        (yyval.interm).param = param;
        if ((yyvsp[0].interm.type).arraySizes)
            parseContext.arraySizeRequiredCheck((yyvsp[0].interm.type).loc, *(yyvsp[0].interm.type).arraySizes);
    }
#line 4180 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 111:
#line 919 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[0].interm);
    }
#line 4188 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 112:
#line 922 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-2].interm);
        parseContext.declareVariable((yyvsp[0].lex).loc, *(yyvsp[0].lex).string, (yyvsp[-2].interm).type);
    }
#line 4197 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 113:
#line 926 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-3].interm);
        parseContext.declareVariable((yyvsp[-1].lex).loc, *(yyvsp[-1].lex).string, (yyvsp[-3].interm).type, (yyvsp[0].interm).arraySizes);
    }
#line 4206 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 114:
#line 930 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[-5].interm).type;
        TIntermNode* initNode = parseContext.declareVariable((yyvsp[-3].lex).loc, *(yyvsp[-3].lex).string, (yyvsp[-5].interm).type, (yyvsp[-2].interm).arraySizes, (yyvsp[0].interm.intermTypedNode));
        (yyval.interm).intermNode = parseContext.intermediate.growAggregate((yyvsp[-5].interm).intermNode, initNode, (yyvsp[-1].lex).loc);
    }
#line 4216 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 115:
#line 935 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[-4].interm).type;
        TIntermNode* initNode = parseContext.declareVariable((yyvsp[-2].lex).loc, *(yyvsp[-2].lex).string, (yyvsp[-4].interm).type, 0, (yyvsp[0].interm.intermTypedNode));
        (yyval.interm).intermNode = parseContext.intermediate.growAggregate((yyvsp[-4].interm).intermNode, initNode, (yyvsp[-1].lex).loc);
    }
#line 4226 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 116:
#line 943 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[0].interm.type);
        (yyval.interm).intermNode = 0;
        parseContext.declareTypeDefaults((yyval.interm).loc, (yyval.interm).type);
    }
#line 4236 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 117:
#line 948 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[-1].interm.type);
        (yyval.interm).intermNode = 0;
        parseContext.declareVariable((yyvsp[0].lex).loc, *(yyvsp[0].lex).string, (yyvsp[-1].interm.type));
    }
#line 4246 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 118:
#line 953 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[-2].interm.type);
        (yyval.interm).intermNode = 0;
        parseContext.declareVariable((yyvsp[-1].lex).loc, *(yyvsp[-1].lex).string, (yyvsp[-2].interm.type), (yyvsp[0].interm).arraySizes);
    }
#line 4256 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 119:
#line 958 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[-4].interm.type);
        TIntermNode* initNode = parseContext.declareVariable((yyvsp[-3].lex).loc, *(yyvsp[-3].lex).string, (yyvsp[-4].interm.type), (yyvsp[-2].interm).arraySizes, (yyvsp[0].interm.intermTypedNode));
        (yyval.interm).intermNode = parseContext.intermediate.growAggregate(0, initNode, (yyvsp[-1].lex).loc);
    }
#line 4266 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 120:
#line 963 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).type = (yyvsp[-3].interm.type);
        TIntermNode* initNode = parseContext.declareVariable((yyvsp[-2].lex).loc, *(yyvsp[-2].lex).string, (yyvsp[-3].interm.type), 0, (yyvsp[0].interm.intermTypedNode));
        (yyval.interm).intermNode = parseContext.intermediate.growAggregate(0, initNode, (yyvsp[-1].lex).loc);
    }
#line 4276 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 121:
#line 972 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);

        parseContext.globalQualifierTypeCheck((yyvsp[0].interm.type).loc, (yyvsp[0].interm.type).qualifier, (yyval.interm.type));
        if ((yyvsp[0].interm.type).arraySizes) {
            parseContext.profileRequires((yyvsp[0].interm.type).loc, ENoProfile, 120, E_GL_3DL_array_objects, "arrayed type");
            parseContext.profileRequires((yyvsp[0].interm.type).loc, EEsProfile, 300, 0, "arrayed type");
        }

        parseContext.precisionQualifierCheck((yyval.interm.type).loc, (yyval.interm.type).basicType, (yyval.interm.type).qualifier);
    }
#line 4292 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 122:
#line 983 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalQualifierFixCheck((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).qualifier);
        parseContext.globalQualifierTypeCheck((yyvsp[-1].interm.type).loc, (yyvsp[-1].interm.type).qualifier, (yyvsp[0].interm.type));

        if ((yyvsp[0].interm.type).arraySizes) {
            parseContext.profileRequires((yyvsp[0].interm.type).loc, ENoProfile, 120, E_GL_3DL_array_objects, "arrayed type");
            parseContext.profileRequires((yyvsp[0].interm.type).loc, EEsProfile, 300, 0, "arrayed type");
        }

        if ((yyvsp[0].interm.type).arraySizes && parseContext.arrayQualifierError((yyvsp[0].interm.type).loc, (yyvsp[-1].interm.type).qualifier))
            (yyvsp[0].interm.type).arraySizes = 0;

        parseContext.checkNoShaderLayouts((yyvsp[0].interm.type).loc, (yyvsp[-1].interm.type).shaderQualifiers);
        (yyvsp[0].interm.type).shaderQualifiers.merge((yyvsp[-1].interm.type).shaderQualifiers);
        parseContext.mergeQualifiers((yyvsp[0].interm.type).loc, (yyvsp[0].interm.type).qualifier, (yyvsp[-1].interm.type).qualifier, true);
        parseContext.precisionQualifierCheck((yyvsp[0].interm.type).loc, (yyvsp[0].interm.type).basicType, (yyvsp[0].interm.type).qualifier);

        (yyval.interm.type) = (yyvsp[0].interm.type);

        if (! (yyval.interm.type).qualifier.isInterpolation() &&
            ((parseContext.language == EShLangVertex   && (yyval.interm.type).qualifier.storage == EvqVaryingOut) ||
             (parseContext.language == EShLangFragment && (yyval.interm.type).qualifier.storage == EvqVaryingIn)))
            (yyval.interm.type).qualifier.smooth = true;
    }
#line 4321 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 123:
#line 1010 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "invariant");
        parseContext.profileRequires((yyval.interm.type).loc, ENoProfile, 120, 0, "invariant");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.invariant = true;
    }
#line 4332 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 124:
#line 1019 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "smooth");
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 130, 0, "smooth");
        parseContext.profileRequires((yyvsp[0].lex).loc, EEsProfile, 300, 0, "smooth");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.smooth = true;
    }
#line 4344 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 125:
#line 1026 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "flat");
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 130, 0, "flat");
        parseContext.profileRequires((yyvsp[0].lex).loc, EEsProfile, 300, 0, "flat");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.flat = true;
    }
#line 4356 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 126:
#line 1033 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "noperspective");
        parseContext.requireProfile((yyvsp[0].lex).loc, ~EEsProfile, "noperspective");
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 130, 0, "noperspective");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.nopersp = true;
    }
#line 4368 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 127:
#line 1043 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[-1].interm.type);
    }
#line 4376 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 128:
#line 1049 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4384 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 129:
#line 1052 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[-2].interm.type);
        (yyval.interm.type).shaderQualifiers.merge((yyvsp[0].interm.type).shaderQualifiers);
        parseContext.mergeObjectLayoutQualifiers((yyval.interm.type).qualifier, (yyvsp[0].interm.type).qualifier, false);
    }
#line 4394 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 130:
#line 1059 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        parseContext.setLayoutQualifier((yyvsp[0].lex).loc, (yyval.interm.type), *(yyvsp[0].lex).string);
    }
#line 4403 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 131:
#line 1063 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[-2].lex).loc);
        parseContext.setLayoutQualifier((yyvsp[-2].lex).loc, (yyval.interm.type), *(yyvsp[-2].lex).string, (yyvsp[0].interm.intermTypedNode));
    }
#line 4412 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 132:
#line 1067 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { // because "shared" is both an identifier and a keyword
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        TString strShared("shared");
        parseContext.setLayoutQualifier((yyvsp[0].lex).loc, (yyval.interm.type), strShared);
    }
#line 4422 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 133:
#line 1075 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
    }
#line 4430 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 134:
#line 1081 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4438 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 135:
#line 1084 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[-1].interm.type);
        if ((yyval.interm.type).basicType == EbtVoid)
            (yyval.interm.type).basicType = (yyvsp[0].interm.type).basicType;

        (yyval.interm.type).shaderQualifiers.merge((yyvsp[0].interm.type).shaderQualifiers);
        parseContext.mergeQualifiers((yyval.interm.type).loc, (yyval.interm.type).qualifier, (yyvsp[0].interm.type).qualifier, false);
    }
#line 4451 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 136:
#line 1095 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4459 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 137:
#line 1098 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4467 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 138:
#line 1101 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4475 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 139:
#line 1104 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // allow inheritance of storage qualifier from block declaration
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4484 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 140:
#line 1108 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // allow inheritance of storage qualifier from block declaration
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4493 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 141:
#line 1112 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // allow inheritance of storage qualifier from block declaration
        (yyval.interm.type) = (yyvsp[0].interm.type);
    }
#line 4502 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 142:
#line 1119 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqConst;  // will later turn into EvqConstReadOnly, if the initializer is not constant
    }
#line 4511 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 143:
#line 1123 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangVertex, "attribute");
        parseContext.checkDeprecated((yyvsp[0].lex).loc, ECoreProfile, 130, "attribute");
        parseContext.checkDeprecated((yyvsp[0].lex).loc, ENoProfile, 130, "attribute");
        parseContext.requireNotRemoved((yyvsp[0].lex).loc, ECoreProfile, 420, "attribute");
        parseContext.requireNotRemoved((yyvsp[0].lex).loc, EEsProfile, 300, "attribute");

        parseContext.globalCheck((yyvsp[0].lex).loc, "attribute");

        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqVaryingIn;
    }
#line 4528 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 144:
#line 1135 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.checkDeprecated((yyvsp[0].lex).loc, ENoProfile, 130, "varying");
        parseContext.checkDeprecated((yyvsp[0].lex).loc, ECoreProfile, 130, "varying");
        parseContext.requireNotRemoved((yyvsp[0].lex).loc, ECoreProfile, 420, "varying");
        parseContext.requireNotRemoved((yyvsp[0].lex).loc, EEsProfile, 300, "varying");

        parseContext.globalCheck((yyvsp[0].lex).loc, "varying");

        (yyval.interm.type).init((yyvsp[0].lex).loc);
        if (parseContext.language == EShLangVertex)
            (yyval.interm.type).qualifier.storage = EvqVaryingOut;
        else
            (yyval.interm.type).qualifier.storage = EvqVaryingIn;
    }
#line 4547 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 145:
#line 1149 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "inout");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqInOut;
    }
#line 4557 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 146:
#line 1154 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "in");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        // whether this is a parameter "in" or a pipeline "in" will get sorted out a bit later
        (yyval.interm.type).qualifier.storage = EvqIn;
    }
#line 4568 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 147:
#line 1160 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "out");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        // whether this is a parameter "out" or a pipeline "out" will get sorted out a bit later
        (yyval.interm.type).qualifier.storage = EvqOut;
    }
#line 4579 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 148:
#line 1166 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 120, 0, "centroid");
        parseContext.profileRequires((yyvsp[0].lex).loc, EEsProfile, 300, 0, "centroid");
        parseContext.globalCheck((yyvsp[0].lex).loc, "centroid");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.centroid = true;
    }
#line 4591 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 149:
#line 1173 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "patch");
        parseContext.requireStage((yyvsp[0].lex).loc, (EShLanguageMask)(EShLangTessControlMask | EShLangTessEvaluationMask), "patch");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.patch = true;
    }
#line 4602 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 150:
#line 1179 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "sample");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.sample = true;
    }
#line 4612 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 151:
#line 1184 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "uniform");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqUniform;
    }
#line 4622 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 152:
#line 1189 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalCheck((yyvsp[0].lex).loc, "buffer");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqBuffer;
    }
#line 4632 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 153:
#line 1194 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.profileRequires((yyvsp[0].lex).loc, ECoreProfile | ECompatibilityProfile, 430, 0, "shared");
        parseContext.profileRequires((yyvsp[0].lex).loc, EEsProfile, 310, 0, "shared");
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangCompute, "shared");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqShared;
    }
#line 4644 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 154:
#line 1201 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.coherent = true;
    }
#line 4653 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 155:
#line 1205 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.volatil = true;
    }
#line 4662 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 156:
#line 1209 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.restrict = true;
    }
#line 4671 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 157:
#line 1213 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.readonly = true;
    }
#line 4680 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 158:
#line 1217 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.writeonly = true;
    }
#line 4689 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 159:
#line 1221 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.spvRemoved((yyvsp[0].lex).loc, "subroutine");
        parseContext.globalCheck((yyvsp[0].lex).loc, "subroutine");
        (yyval.interm.type).init((yyvsp[0].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqUniform;
    }
#line 4700 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 160:
#line 1227 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.spvRemoved((yyvsp[-3].lex).loc, "subroutine");
        parseContext.globalCheck((yyvsp[-3].lex).loc, "subroutine");
        (yyval.interm.type).init((yyvsp[-3].lex).loc);
        (yyval.interm.type).qualifier.storage = EvqUniform;
        // TODO: 4.0 semantics: subroutines
        // 1) make sure each identifier is a type declared earlier with SUBROUTINE
        // 2) save all of the identifiers for future comparison with the declared function
    }
#line 4714 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 161:
#line 1239 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // TODO: 4.0 functionality: subroutine type to list
    }
#line 4722 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 162:
#line 1242 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
    }
#line 4729 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 163:
#line 1247 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
        (yyval.interm.type).qualifier.precision = parseContext.getDefaultPrecision((yyval.interm.type));
    }
#line 4738 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 164:
#line 1251 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.arrayDimCheck((yyvsp[0].interm).loc, (yyvsp[0].interm).arraySizes, 0);
        (yyval.interm.type) = (yyvsp[-1].interm.type);
        (yyval.interm.type).qualifier.precision = parseContext.getDefaultPrecision((yyval.interm.type));
        (yyval.interm.type).arraySizes = (yyvsp[0].interm).arraySizes;
    }
#line 4749 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 165:
#line 1260 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[-1].lex).loc;
        (yyval.interm).arraySizes = new TArraySizes;
        (yyval.interm).arraySizes->addInnerSize();
    }
#line 4759 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 166:
#line 1265 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm).loc = (yyvsp[-2].lex).loc;
        (yyval.interm).arraySizes = new TArraySizes;

        TArraySize size;
        parseContext.arraySizeCheck((yyvsp[-1].interm.intermTypedNode)->getLoc(), (yyvsp[-1].interm.intermTypedNode), size);
        (yyval.interm).arraySizes->addInnerSize(size);
    }
#line 4772 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 167:
#line 1273 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-2].interm);
        (yyval.interm).arraySizes->addInnerSize();
    }
#line 4781 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 168:
#line 1277 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm) = (yyvsp[-3].interm);

        TArraySize size;
        parseContext.arraySizeCheck((yyvsp[-1].interm.intermTypedNode)->getLoc(), (yyvsp[-1].interm.intermTypedNode), size);
        (yyval.interm).arraySizes->addInnerSize(size);
    }
#line 4793 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 169:
#line 1287 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtVoid;
    }
#line 4802 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 170:
#line 1291 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
    }
#line 4811 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 171:
#line 1295 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
    }
#line 4821 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 172:
#line 1300 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtInt;
    }
#line 4830 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 173:
#line 1304 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "unsigned integer");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtUint;
    }
#line 4840 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 174:
#line 1309 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtBool;
    }
#line 4849 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 175:
#line 1313 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setVector(2);
    }
#line 4859 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 176:
#line 1318 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setVector(3);
    }
#line 4869 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 177:
#line 1323 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setVector(4);
    }
#line 4879 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 178:
#line 1328 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double vector");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setVector(2);
    }
#line 4890 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 179:
#line 1334 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double vector");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setVector(3);
    }
#line 4901 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 180:
#line 1340 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double vector");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setVector(4);
    }
#line 4912 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 181:
#line 1346 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtBool;
        (yyval.interm.type).setVector(2);
    }
#line 4922 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 182:
#line 1351 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtBool;
        (yyval.interm.type).setVector(3);
    }
#line 4932 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 183:
#line 1356 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtBool;
        (yyval.interm.type).setVector(4);
    }
#line 4942 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 184:
#line 1361 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtInt;
        (yyval.interm.type).setVector(2);
    }
#line 4952 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 185:
#line 1366 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtInt;
        (yyval.interm.type).setVector(3);
    }
#line 4962 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 186:
#line 1371 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtInt;
        (yyval.interm.type).setVector(4);
    }
#line 4972 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 187:
#line 1376 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "unsigned integer vector");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtUint;
        (yyval.interm.type).setVector(2);
    }
#line 4983 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 188:
#line 1382 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "unsigned integer vector");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtUint;
        (yyval.interm.type).setVector(3);
    }
#line 4994 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 189:
#line 1388 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.fullIntegerCheck((yyvsp[0].lex).loc, "unsigned integer vector");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtUint;
        (yyval.interm.type).setVector(4);
    }
#line 5005 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 190:
#line 1394 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(2, 2);
    }
#line 5015 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 191:
#line 1399 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(3, 3);
    }
#line 5025 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 192:
#line 1404 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(4, 4);
    }
#line 5035 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 193:
#line 1409 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(2, 2);
    }
#line 5045 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 194:
#line 1414 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(2, 3);
    }
#line 5055 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 195:
#line 1419 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(2, 4);
    }
#line 5065 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 196:
#line 1424 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(3, 2);
    }
#line 5075 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 197:
#line 1429 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(3, 3);
    }
#line 5085 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 198:
#line 1434 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(3, 4);
    }
#line 5095 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 199:
#line 1439 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(4, 2);
    }
#line 5105 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 200:
#line 1444 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(4, 3);
    }
#line 5115 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 201:
#line 1449 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtFloat;
        (yyval.interm.type).setMatrix(4, 4);
    }
#line 5125 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 202:
#line 1454 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(2, 2);
    }
#line 5136 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 203:
#line 1460 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(3, 3);
    }
#line 5147 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 204:
#line 1466 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(4, 4);
    }
#line 5158 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 205:
#line 1472 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(2, 2);
    }
#line 5169 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 206:
#line 1478 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(2, 3);
    }
#line 5180 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 207:
#line 1484 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(2, 4);
    }
#line 5191 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 208:
#line 1490 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(3, 2);
    }
#line 5202 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 209:
#line 1496 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(3, 3);
    }
#line 5213 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 210:
#line 1502 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(3, 4);
    }
#line 5224 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 211:
#line 1508 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(4, 2);
    }
#line 5235 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 212:
#line 1514 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(4, 3);
    }
#line 5246 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 213:
#line 1520 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.doubleCheck((yyvsp[0].lex).loc, "double matrix");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtDouble;
        (yyval.interm.type).setMatrix(4, 4);
    }
#line 5257 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 214:
#line 1526 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.vulkanRemoved((yyvsp[0].lex).loc, "atomic counter types");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtAtomicUint;
    }
#line 5267 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 215:
#line 1531 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd1D);
    }
#line 5277 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 216:
#line 1536 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D);
    }
#line 5287 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 217:
#line 1541 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd3D);
    }
#line 5297 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 218:
#line 1546 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdCube);
    }
#line 5307 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 219:
#line 1551 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd1D, false, true);
    }
#line 5317 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 220:
#line 1556 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D, false, true);
    }
#line 5327 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 221:
#line 1561 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdCube, false, true);
    }
#line 5337 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 222:
#line 1566 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd1D, true);
    }
#line 5347 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 223:
#line 1571 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D, true);
    }
#line 5357 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 224:
#line 1576 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd1D, true, true);
    }
#line 5367 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 225:
#line 1581 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D, true, true);
    }
#line 5377 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 226:
#line 1586 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdCube, true);
    }
#line 5387 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 227:
#line 1591 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdCube, true, true);
    }
#line 5397 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 228:
#line 1596 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd1D);
    }
#line 5407 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 229:
#line 1601 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd2D);
    }
#line 5417 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 230:
#line 1606 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd3D);
    }
#line 5427 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 231:
#line 1611 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, EsdCube);
    }
#line 5437 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 232:
#line 1616 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd1D, true);
    }
#line 5447 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 233:
#line 1621 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd2D, true);
    }
#line 5457 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 234:
#line 1626 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, EsdCube, true);
    }
#line 5467 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 235:
#line 1631 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd1D);
    }
#line 5477 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 236:
#line 1636 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd2D);
    }
#line 5487 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 237:
#line 1641 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd3D);
    }
#line 5497 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 238:
#line 1646 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, EsdCube);
    }
#line 5507 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 239:
#line 1651 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd1D, true);
    }
#line 5517 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 240:
#line 1656 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd2D, true);
    }
#line 5527 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 241:
#line 1661 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, EsdCube, true);
    }
#line 5537 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 242:
#line 1666 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdRect);
    }
#line 5547 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 243:
#line 1671 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdRect, false, true);
    }
#line 5557 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 244:
#line 1676 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, EsdRect);
    }
#line 5567 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 245:
#line 1681 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, EsdRect);
    }
#line 5577 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 246:
#line 1686 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, EsdBuffer);
    }
#line 5587 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 247:
#line 1691 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, EsdBuffer);
    }
#line 5597 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 248:
#line 1696 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, EsdBuffer);
    }
#line 5607 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 249:
#line 1701 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D, false, false, true);
    }
#line 5617 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 250:
#line 1706 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd2D, false, false, true);
    }
#line 5627 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 251:
#line 1711 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd2D, false, false, true);
    }
#line 5637 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 252:
#line 1716 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D, true, false, true);
    }
#line 5647 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 253:
#line 1721 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtInt, Esd2D, true, false, true);
    }
#line 5657 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 254:
#line 1726 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtUint, Esd2D, true, false, true);
    }
#line 5667 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 255:
#line 1731 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setPureSampler(false);
    }
#line 5677 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 256:
#line 1736 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setPureSampler(true);
    }
#line 5687 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 257:
#line 1741 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd1D);
    }
#line 5697 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 258:
#line 1746 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd2D);
    }
#line 5707 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 259:
#line 1751 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd3D);
    }
#line 5717 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 260:
#line 1756 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, EsdCube);
    }
#line 5727 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 261:
#line 1761 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd1D, true);
    }
#line 5737 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 262:
#line 1766 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd2D, true);
    }
#line 5747 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 263:
#line 1771 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, EsdCube, true);
    }
#line 5757 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 264:
#line 1776 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd1D);
    }
#line 5767 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 265:
#line 1781 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd2D);
    }
#line 5777 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 266:
#line 1786 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd3D);
    }
#line 5787 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 267:
#line 1791 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, EsdCube);
    }
#line 5797 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 268:
#line 1796 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd1D, true);
    }
#line 5807 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 269:
#line 1801 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd2D, true);
    }
#line 5817 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 270:
#line 1806 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, EsdCube, true);
    }
#line 5827 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 271:
#line 1811 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd1D);
    }
#line 5837 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 272:
#line 1816 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd2D);
    }
#line 5847 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 273:
#line 1821 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd3D);
    }
#line 5857 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 274:
#line 1826 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, EsdCube);
    }
#line 5867 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 275:
#line 1831 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd1D, true);
    }
#line 5877 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 276:
#line 1836 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd2D, true);
    }
#line 5887 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 277:
#line 1841 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, EsdCube, true);
    }
#line 5897 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 278:
#line 1846 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, EsdRect);
    }
#line 5907 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 279:
#line 1851 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, EsdRect);
    }
#line 5917 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 280:
#line 1856 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, EsdRect);
    }
#line 5927 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 281:
#line 1861 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, EsdBuffer);
    }
#line 5937 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 282:
#line 1866 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, EsdBuffer);
    }
#line 5947 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 283:
#line 1871 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, EsdBuffer);
    }
#line 5957 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 284:
#line 1876 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd2D, false, false, true);
    }
#line 5967 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 285:
#line 1881 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd2D, false, false, true);
    }
#line 5977 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 286:
#line 1886 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd2D, false, false, true);
    }
#line 5987 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 287:
#line 1891 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtFloat, Esd2D, true, false, true);
    }
#line 5997 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 288:
#line 1896 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtInt, Esd2D, true, false, true);
    }
#line 6007 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 289:
#line 1901 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setTexture(EbtUint, Esd2D, true, false, true);
    }
#line 6017 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 290:
#line 1906 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd1D);
    }
#line 6027 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 291:
#line 1911 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd1D);
    }
#line 6037 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 292:
#line 1916 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd1D);
    }
#line 6047 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 293:
#line 1921 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd2D);
    }
#line 6057 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 294:
#line 1926 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd2D);
    }
#line 6067 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 295:
#line 1931 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd2D);
    }
#line 6077 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 296:
#line 1936 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd3D);
    }
#line 6087 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 297:
#line 1941 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd3D);
    }
#line 6097 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 298:
#line 1946 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd3D);
    }
#line 6107 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 299:
#line 1951 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, EsdRect);
    }
#line 6117 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 300:
#line 1956 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, EsdRect);
    }
#line 6127 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 301:
#line 1961 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, EsdRect);
    }
#line 6137 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 302:
#line 1966 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, EsdCube);
    }
#line 6147 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 303:
#line 1971 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, EsdCube);
    }
#line 6157 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 304:
#line 1976 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, EsdCube);
    }
#line 6167 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 305:
#line 1981 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, EsdBuffer);
    }
#line 6177 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 306:
#line 1986 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, EsdBuffer);
    }
#line 6187 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 307:
#line 1991 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, EsdBuffer);
    }
#line 6197 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 308:
#line 1996 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd1D, true);
    }
#line 6207 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 309:
#line 2001 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd1D, true);
    }
#line 6217 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 310:
#line 2006 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd1D, true);
    }
#line 6227 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 311:
#line 2011 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd2D, true);
    }
#line 6237 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 312:
#line 2016 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd2D, true);
    }
#line 6247 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 313:
#line 2021 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd2D, true);
    }
#line 6257 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 314:
#line 2026 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, EsdCube, true);
    }
#line 6267 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 315:
#line 2031 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, EsdCube, true);
    }
#line 6277 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 316:
#line 2036 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, EsdCube, true);
    }
#line 6287 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 317:
#line 2041 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd2D, false, false, true);
    }
#line 6297 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 318:
#line 2046 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd2D, false, false, true);
    }
#line 6307 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 319:
#line 2051 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd2D, false, false, true);
    }
#line 6317 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 320:
#line 2056 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtFloat, Esd2D, true, false, true);
    }
#line 6327 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 321:
#line 2061 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtInt, Esd2D, true, false, true);
    }
#line 6337 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 322:
#line 2066 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setImage(EbtUint, Esd2D, true, false, true);
    }
#line 6347 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 323:
#line 2071 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {  // GL_OES_EGL_image_external
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.set(EbtFloat, Esd2D);
        (yyval.interm.type).sampler.external = true;
    }
#line 6358 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 324:
#line 2077 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangFragment, "subpass input");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setSubpass(EbtFloat);
    }
#line 6369 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 325:
#line 2083 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangFragment, "subpass input");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setSubpass(EbtFloat, true);
    }
#line 6380 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 326:
#line 2089 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangFragment, "subpass input");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setSubpass(EbtInt);
    }
#line 6391 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 327:
#line 2095 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangFragment, "subpass input");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setSubpass(EbtInt, true);
    }
#line 6402 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 328:
#line 2101 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangFragment, "subpass input");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setSubpass(EbtUint);
    }
#line 6413 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 329:
#line 2107 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[0].lex).loc, EShLangFragment, "subpass input");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        (yyval.interm.type).basicType = EbtSampler;
        (yyval.interm.type).sampler.setSubpass(EbtUint, true);
    }
#line 6424 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 330:
#line 2113 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.type) = (yyvsp[0].interm.type);
        (yyval.interm.type).qualifier.storage = parseContext.symbolTable.atGlobalLevel() ? EvqGlobal : EvqTemporary;
        parseContext.structTypeCheck((yyval.interm.type).loc, (yyval.interm.type));
    }
#line 6434 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 331:
#line 2118 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        //
        // This is for user defined type names.  The lexical phase looked up the
        // type.
        //
        if (const TVariable* variable = ((yyvsp[0].lex).symbol)->getAsVariable()) {
            const TType& structure = variable->getType();
            (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
            (yyval.interm.type).basicType = EbtStruct;
            (yyval.interm.type).userDef = &structure;
        } else
            parseContext.error((yyvsp[0].lex).loc, "expected type name", (yyvsp[0].lex).string->c_str(), "");
    }
#line 6452 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 332:
#line 2134 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 130, 0, "highp precision qualifier");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        if (parseContext.profile == EEsProfile)
            (yyval.interm.type).qualifier.precision = EpqHigh;
    }
#line 6463 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 333:
#line 2140 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 130, 0, "mediump precision qualifier");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        if (parseContext.profile == EEsProfile)
            (yyval.interm.type).qualifier.precision = EpqMedium;
    }
#line 6474 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 334:
#line 2146 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.profileRequires((yyvsp[0].lex).loc, ENoProfile, 130, 0, "lowp precision qualifier");
        (yyval.interm.type).init((yyvsp[0].lex).loc, parseContext.symbolTable.atGlobalLevel());
        if (parseContext.profile == EEsProfile)
            (yyval.interm.type).qualifier.precision = EpqLow;
    }
#line 6485 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 335:
#line 2155 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { parseContext.nestedStructCheck((yyvsp[-2].lex).loc); }
#line 6491 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 336:
#line 2155 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        TType* structure = new TType((yyvsp[-1].interm.typeList), *(yyvsp[-4].lex).string);
        parseContext.structArrayCheck((yyvsp[-4].lex).loc, *structure);
        TVariable* userTypeDef = new TVariable((yyvsp[-4].lex).string, *structure, true);
        if (! parseContext.symbolTable.insert(*userTypeDef))
            parseContext.error((yyvsp[-4].lex).loc, "redefinition", (yyvsp[-4].lex).string->c_str(), "struct");
        (yyval.interm.type).init((yyvsp[-5].lex).loc);
        (yyval.interm.type).basicType = EbtStruct;
        (yyval.interm.type).userDef = structure;
        --parseContext.structNestingLevel;
    }
#line 6507 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 337:
#line 2166 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { parseContext.nestedStructCheck((yyvsp[-1].lex).loc); }
#line 6513 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 338:
#line 2166 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        TType* structure = new TType((yyvsp[-1].interm.typeList), TString(""));
        (yyval.interm.type).init((yyvsp[-4].lex).loc);
        (yyval.interm.type).basicType = EbtStruct;
        (yyval.interm.type).userDef = structure;
        --parseContext.structNestingLevel;
    }
#line 6525 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 339:
#line 2176 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.typeList) = (yyvsp[0].interm.typeList);
    }
#line 6533 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 340:
#line 2179 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.typeList) = (yyvsp[-1].interm.typeList);
        for (unsigned int i = 0; i < (yyvsp[0].interm.typeList)->size(); ++i) {
            for (unsigned int j = 0; j < (yyval.interm.typeList)->size(); ++j) {
                if ((*(yyval.interm.typeList))[j].type->getFieldName() == (*(yyvsp[0].interm.typeList))[i].type->getFieldName())
                    parseContext.error((*(yyvsp[0].interm.typeList))[i].loc, "duplicate member name:", "", (*(yyvsp[0].interm.typeList))[i].type->getFieldName().c_str());
            }
            (yyval.interm.typeList)->push_back((*(yyvsp[0].interm.typeList))[i]);
        }
    }
#line 6548 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 341:
#line 2192 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-2].interm.type).arraySizes) {
            parseContext.profileRequires((yyvsp[-2].interm.type).loc, ENoProfile, 120, E_GL_3DL_array_objects, "arrayed type");
            parseContext.profileRequires((yyvsp[-2].interm.type).loc, EEsProfile, 300, 0, "arrayed type");
            if (parseContext.profile == EEsProfile)
                parseContext.arraySizeRequiredCheck((yyvsp[-2].interm.type).loc, *(yyvsp[-2].interm.type).arraySizes);
        }

        (yyval.interm.typeList) = (yyvsp[-1].interm.typeList);

        parseContext.voidErrorCheck((yyvsp[-2].interm.type).loc, (*(yyvsp[-1].interm.typeList))[0].type->getFieldName(), (yyvsp[-2].interm.type).basicType);
        parseContext.precisionQualifierCheck((yyvsp[-2].interm.type).loc, (yyvsp[-2].interm.type).basicType, (yyvsp[-2].interm.type).qualifier);

        for (unsigned int i = 0; i < (yyval.interm.typeList)->size(); ++i) {
            parseContext.arrayDimCheck((yyvsp[-2].interm.type).loc, (*(yyval.interm.typeList))[i].type, (yyvsp[-2].interm.type).arraySizes);
            (*(yyval.interm.typeList))[i].type->mergeType((yyvsp[-2].interm.type));
        }
    }
#line 6571 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 342:
#line 2210 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.globalQualifierFixCheck((yyvsp[-3].interm.type).loc, (yyvsp[-3].interm.type).qualifier);
        if ((yyvsp[-2].interm.type).arraySizes) {
            parseContext.profileRequires((yyvsp[-2].interm.type).loc, ENoProfile, 120, E_GL_3DL_array_objects, "arrayed type");
            parseContext.profileRequires((yyvsp[-2].interm.type).loc, EEsProfile, 300, 0, "arrayed type");
            if (parseContext.profile == EEsProfile)
                parseContext.arraySizeRequiredCheck((yyvsp[-2].interm.type).loc, *(yyvsp[-2].interm.type).arraySizes);
        }

        (yyval.interm.typeList) = (yyvsp[-1].interm.typeList);

        parseContext.checkNoShaderLayouts((yyvsp[-3].interm.type).loc, (yyvsp[-3].interm.type).shaderQualifiers);
        parseContext.voidErrorCheck((yyvsp[-2].interm.type).loc, (*(yyvsp[-1].interm.typeList))[0].type->getFieldName(), (yyvsp[-2].interm.type).basicType);
        parseContext.mergeQualifiers((yyvsp[-2].interm.type).loc, (yyvsp[-2].interm.type).qualifier, (yyvsp[-3].interm.type).qualifier, true);
        parseContext.precisionQualifierCheck((yyvsp[-2].interm.type).loc, (yyvsp[-2].interm.type).basicType, (yyvsp[-2].interm.type).qualifier);

        for (unsigned int i = 0; i < (yyval.interm.typeList)->size(); ++i) {
            parseContext.arrayDimCheck((yyvsp[-3].interm.type).loc, (*(yyval.interm.typeList))[i].type, (yyvsp[-2].interm.type).arraySizes);
            (*(yyval.interm.typeList))[i].type->mergeType((yyvsp[-2].interm.type));
        }
    }
#line 6597 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 343:
#line 2234 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.typeList) = new TTypeList;
        (yyval.interm.typeList)->push_back((yyvsp[0].interm.typeLine));
    }
#line 6606 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 344:
#line 2238 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.typeList)->push_back((yyvsp[0].interm.typeLine));
    }
#line 6614 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 345:
#line 2244 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.typeLine).type = new TType(EbtVoid);
        (yyval.interm.typeLine).loc = (yyvsp[0].lex).loc;
        (yyval.interm.typeLine).type->setFieldName(*(yyvsp[0].lex).string);
    }
#line 6624 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 346:
#line 2249 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.arrayDimCheck((yyvsp[-1].lex).loc, (yyvsp[0].interm).arraySizes, 0);

        (yyval.interm.typeLine).type = new TType(EbtVoid);
        (yyval.interm.typeLine).loc = (yyvsp[-1].lex).loc;
        (yyval.interm.typeLine).type->setFieldName(*(yyvsp[-1].lex).string);
        (yyval.interm.typeLine).type->newArraySizes(*(yyvsp[0].interm).arraySizes);
    }
#line 6637 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 347:
#line 2260 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 6645 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 348:
#line 2263 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        const char* initFeature = "{ } style initializers";
        parseContext.requireProfile((yyvsp[-2].lex).loc, ~EEsProfile, initFeature);
        parseContext.profileRequires((yyvsp[-2].lex).loc, ~EEsProfile, 420, E_GL_ARB_shading_language_420pack, initFeature);
        (yyval.interm.intermTypedNode) = (yyvsp[-1].interm.intermTypedNode);
    }
#line 6656 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 349:
#line 2269 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        const char* initFeature = "{ } style initializers";
        parseContext.requireProfile((yyvsp[-3].lex).loc, ~EEsProfile, initFeature);
        parseContext.profileRequires((yyvsp[-3].lex).loc, ~EEsProfile, 420, E_GL_ARB_shading_language_420pack, initFeature);
        (yyval.interm.intermTypedNode) = (yyvsp[-2].interm.intermTypedNode);
    }
#line 6667 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 350:
#line 2278 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.intermediate.growAggregate(0, (yyvsp[0].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode)->getLoc());
    }
#line 6675 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 351:
#line 2281 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = parseContext.intermediate.growAggregate((yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.intermTypedNode));
    }
#line 6683 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 352:
#line 2287 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6689 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 353:
#line 2291 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6695 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 354:
#line 2292 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6701 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 355:
#line 2298 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6707 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 356:
#line 2299 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6713 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 357:
#line 2300 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6719 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 358:
#line 2301 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6725 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 359:
#line 2302 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6731 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 360:
#line 2303 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6737 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 361:
#line 2304 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6743 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 362:
#line 2308 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = 0; }
#line 6749 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 363:
#line 2309 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.push();
        ++parseContext.statementNestingLevel;
    }
#line 6758 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 364:
#line 2313 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.pop(&parseContext.defaultPrecision[0]);
        --parseContext.statementNestingLevel;
    }
#line 6767 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 365:
#line 2317 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-2].interm.intermNode) && (yyvsp[-2].interm.intermNode)->getAsAggregate())
            (yyvsp[-2].interm.intermNode)->getAsAggregate()->setOperator(EOpSequence);
        (yyval.interm.intermNode) = (yyvsp[-2].interm.intermNode);
    }
#line 6777 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 366:
#line 2325 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6783 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 367:
#line 2326 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode); }
#line 6789 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 368:
#line 2330 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        ++parseContext.controlFlowNestingLevel;
    }
#line 6797 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 369:
#line 2333 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        --parseContext.controlFlowNestingLevel;
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 6806 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 370:
#line 2337 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.push();
        ++parseContext.statementNestingLevel;
        ++parseContext.controlFlowNestingLevel;
    }
#line 6816 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 371:
#line 2342 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.pop(&parseContext.defaultPrecision[0]);
        --parseContext.statementNestingLevel;
        --parseContext.controlFlowNestingLevel;
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 6827 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 372:
#line 2351 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = 0;
    }
#line 6835 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 373:
#line 2354 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[-1].interm.intermNode) && (yyvsp[-1].interm.intermNode)->getAsAggregate())
            (yyvsp[-1].interm.intermNode)->getAsAggregate()->setOperator(EOpSequence);
        (yyval.interm.intermNode) = (yyvsp[-1].interm.intermNode);
    }
#line 6845 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 374:
#line 2362 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = parseContext.intermediate.makeAggregate((yyvsp[0].interm.intermNode));
        if ((yyvsp[0].interm.intermNode) && (yyvsp[0].interm.intermNode)->getAsBranchNode() && ((yyvsp[0].interm.intermNode)->getAsBranchNode()->getFlowOp() == EOpCase ||
                                            (yyvsp[0].interm.intermNode)->getAsBranchNode()->getFlowOp() == EOpDefault)) {
            parseContext.wrapupSwitchSubsequence(0, (yyvsp[0].interm.intermNode));
            (yyval.interm.intermNode) = 0;  // start a fresh subsequence for what's after this case
        }
    }
#line 6858 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 375:
#line 2370 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if ((yyvsp[0].interm.intermNode) && (yyvsp[0].interm.intermNode)->getAsBranchNode() && ((yyvsp[0].interm.intermNode)->getAsBranchNode()->getFlowOp() == EOpCase ||
                                            (yyvsp[0].interm.intermNode)->getAsBranchNode()->getFlowOp() == EOpDefault)) {
            parseContext.wrapupSwitchSubsequence((yyvsp[-1].interm.intermNode) ? (yyvsp[-1].interm.intermNode)->getAsAggregate() : 0, (yyvsp[0].interm.intermNode));
            (yyval.interm.intermNode) = 0;  // start a fresh subsequence for what's after this case
        } else
            (yyval.interm.intermNode) = parseContext.intermediate.growAggregate((yyvsp[-1].interm.intermNode), (yyvsp[0].interm.intermNode));
    }
#line 6871 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 376:
#line 2381 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = 0; }
#line 6877 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 377:
#line 2382 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    { (yyval.interm.intermNode) = static_cast<TIntermNode*>((yyvsp[-1].interm.intermTypedNode)); }
#line 6883 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 378:
#line 2386 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.boolCheck((yyvsp[-4].lex).loc, (yyvsp[-2].interm.intermTypedNode));
        (yyval.interm.intermNode) = parseContext.intermediate.addSelection((yyvsp[-2].interm.intermTypedNode), (yyvsp[0].interm.nodePair), (yyvsp[-4].lex).loc);
    }
#line 6892 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 379:
#line 2393 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.nodePair).node1 = (yyvsp[-2].interm.intermNode);
        (yyval.interm.nodePair).node2 = (yyvsp[0].interm.intermNode);
    }
#line 6901 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 380:
#line 2397 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.nodePair).node1 = (yyvsp[0].interm.intermNode);
        (yyval.interm.nodePair).node2 = 0;
    }
#line 6910 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 381:
#line 2405 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
        parseContext.boolCheck((yyvsp[0].interm.intermTypedNode)->getLoc(), (yyvsp[0].interm.intermTypedNode));
    }
#line 6919 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 382:
#line 2409 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.boolCheck((yyvsp[-2].lex).loc, (yyvsp[-3].interm.type));

        TType type((yyvsp[-3].interm.type));
        TIntermNode* initNode = parseContext.declareVariable((yyvsp[-2].lex).loc, *(yyvsp[-2].lex).string, (yyvsp[-3].interm.type), 0, (yyvsp[0].interm.intermTypedNode));
        if (initNode)
            (yyval.interm.intermTypedNode) = initNode->getAsTyped();
        else
            (yyval.interm.intermTypedNode) = 0;
    }
#line 6934 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 383:
#line 2422 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        // start new switch sequence on the switch stack
        ++parseContext.controlFlowNestingLevel;
        ++parseContext.statementNestingLevel;
        parseContext.switchSequenceStack.push_back(new TIntermSequence);
        parseContext.switchLevel.push_back(parseContext.statementNestingLevel);
        parseContext.symbolTable.push();
    }
#line 6947 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 384:
#line 2430 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = parseContext.addSwitch((yyvsp[-7].lex).loc, (yyvsp[-5].interm.intermTypedNode), (yyvsp[-1].interm.intermNode) ? (yyvsp[-1].interm.intermNode)->getAsAggregate() : 0);
        delete parseContext.switchSequenceStack.back();
        parseContext.switchSequenceStack.pop_back();
        parseContext.switchLevel.pop_back();
        parseContext.symbolTable.pop(&parseContext.defaultPrecision[0]);
        --parseContext.statementNestingLevel;
        --parseContext.controlFlowNestingLevel;
    }
#line 6961 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 385:
#line 2442 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = 0;
    }
#line 6969 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 386:
#line 2445 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 6977 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 387:
#line 2451 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = 0;
        if (parseContext.switchLevel.size() == 0)
            parseContext.error((yyvsp[-2].lex).loc, "cannot appear outside switch statement", "case", "");
        else if (parseContext.switchLevel.back() != parseContext.statementNestingLevel)
            parseContext.error((yyvsp[-2].lex).loc, "cannot be nested inside control flow", "case", "");
        else {
            parseContext.constantValueCheck((yyvsp[-1].interm.intermTypedNode), "case");
            parseContext.integerCheck((yyvsp[-1].interm.intermTypedNode), "case");
            (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpCase, (yyvsp[-1].interm.intermTypedNode), (yyvsp[-2].lex).loc);
        }
    }
#line 6994 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 388:
#line 2463 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = 0;
        if (parseContext.switchLevel.size() == 0)
            parseContext.error((yyvsp[-1].lex).loc, "cannot appear outside switch statement", "default", "");
        else if (parseContext.switchLevel.back() != parseContext.statementNestingLevel)
            parseContext.error((yyvsp[-1].lex).loc, "cannot be nested inside control flow", "default", "");
        else
            (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpDefault, (yyvsp[-1].lex).loc);
    }
#line 7008 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 389:
#line 2475 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if (! parseContext.limits.whileLoops)
            parseContext.error((yyvsp[-1].lex).loc, "while loops not available", "limitation", "");
        parseContext.symbolTable.push();
        ++parseContext.loopNestingLevel;
        ++parseContext.statementNestingLevel;
        ++parseContext.controlFlowNestingLevel;
    }
#line 7021 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 390:
#line 2483 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.pop(&parseContext.defaultPrecision[0]);
        (yyval.interm.intermNode) = parseContext.intermediate.addLoop((yyvsp[0].interm.intermNode), (yyvsp[-2].interm.intermTypedNode), 0, true, (yyvsp[-5].lex).loc);
        --parseContext.loopNestingLevel;
        --parseContext.statementNestingLevel;
        --parseContext.controlFlowNestingLevel;
    }
#line 7033 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 391:
#line 2490 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        ++parseContext.loopNestingLevel;
        ++parseContext.statementNestingLevel;
        ++parseContext.controlFlowNestingLevel;
    }
#line 7043 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 392:
#line 2495 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if (! parseContext.limits.whileLoops)
            parseContext.error((yyvsp[-7].lex).loc, "do-while loops not available", "limitation", "");

        parseContext.boolCheck((yyvsp[0].lex).loc, (yyvsp[-2].interm.intermTypedNode));

        (yyval.interm.intermNode) = parseContext.intermediate.addLoop((yyvsp[-5].interm.intermNode), (yyvsp[-2].interm.intermTypedNode), 0, false, (yyvsp[-4].lex).loc);
        --parseContext.loopNestingLevel;
        --parseContext.statementNestingLevel;
        --parseContext.controlFlowNestingLevel;
    }
#line 7059 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 393:
#line 2506 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.push();
        ++parseContext.loopNestingLevel;
        ++parseContext.statementNestingLevel;
        ++parseContext.controlFlowNestingLevel;
    }
#line 7070 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 394:
#line 2512 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.symbolTable.pop(&parseContext.defaultPrecision[0]);
        (yyval.interm.intermNode) = parseContext.intermediate.makeAggregate((yyvsp[-3].interm.intermNode), (yyvsp[-5].lex).loc);
        TIntermLoop* forLoop = parseContext.intermediate.addLoop((yyvsp[0].interm.intermNode), reinterpret_cast<TIntermTyped*>((yyvsp[-2].interm.nodePair).node1), reinterpret_cast<TIntermTyped*>((yyvsp[-2].interm.nodePair).node2), true, (yyvsp[-6].lex).loc);
        if (! parseContext.limits.nonInductiveForLoops)
            parseContext.inductiveLoopCheck((yyvsp[-6].lex).loc, (yyvsp[-3].interm.intermNode), forLoop);
        (yyval.interm.intermNode) = parseContext.intermediate.growAggregate((yyval.interm.intermNode), forLoop, (yyvsp[-6].lex).loc);
        (yyval.interm.intermNode)->getAsAggregate()->setOperator(EOpSequence);
        --parseContext.loopNestingLevel;
        --parseContext.statementNestingLevel;
        --parseContext.controlFlowNestingLevel;
    }
#line 7087 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 395:
#line 2527 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 7095 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 396:
#line 2530 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 7103 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 397:
#line 2536 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = (yyvsp[0].interm.intermTypedNode);
    }
#line 7111 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 398:
#line 2539 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermTypedNode) = 0;
    }
#line 7119 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 399:
#line 2545 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.nodePair).node1 = (yyvsp[-1].interm.intermTypedNode);
        (yyval.interm.nodePair).node2 = 0;
    }
#line 7128 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 400:
#line 2549 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.nodePair).node1 = (yyvsp[-2].interm.intermTypedNode);
        (yyval.interm.nodePair).node2 = (yyvsp[0].interm.intermTypedNode);
    }
#line 7137 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 401:
#line 2556 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if (parseContext.loopNestingLevel <= 0)
            parseContext.error((yyvsp[-1].lex).loc, "continue statement only allowed in loops", "", "");
        (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpContinue, (yyvsp[-1].lex).loc);
    }
#line 7147 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 402:
#line 2561 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        if (parseContext.loopNestingLevel + parseContext.switchSequenceStack.size() <= 0)
            parseContext.error((yyvsp[-1].lex).loc, "break statement only allowed in switch and loops", "", "");
        (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpBreak, (yyvsp[-1].lex).loc);
    }
#line 7157 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 403:
#line 2566 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpReturn, (yyvsp[-1].lex).loc);
        if (parseContext.currentFunctionType->getBasicType() != EbtVoid)
            parseContext.error((yyvsp[-1].lex).loc, "non-void function must return a value", "return", "");
        if (parseContext.inMain)
            parseContext.postMainReturn = true;
    }
#line 7169 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 404:
#line 2573 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.functionReturnsValue = true;
        if (parseContext.currentFunctionType->getBasicType() == EbtVoid) {
            parseContext.error((yyvsp[-2].lex).loc, "void function cannot return a value", "return", "");
            (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpReturn, (yyvsp[-2].lex).loc);
        } else if (*(parseContext.currentFunctionType) != (yyvsp[-1].interm.intermTypedNode)->getType()) {
            TIntermTyped* converted = parseContext.intermediate.addConversion(EOpReturn, *parseContext.currentFunctionType, (yyvsp[-1].interm.intermTypedNode));
            if (converted) {
                if (parseContext.version < 420)
                    parseContext.warn((yyvsp[-2].lex).loc, "type conversion on return values was not explicitly allowed until version 420", "return", "");
                (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpReturn, converted, (yyvsp[-2].lex).loc);
            } else {
                parseContext.error((yyvsp[-2].lex).loc, "type does not match, or is not convertible to, the function's return type", "return", "");
                (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpReturn, (yyvsp[-1].interm.intermTypedNode), (yyvsp[-2].lex).loc);
            }
        } else
            (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpReturn, (yyvsp[-1].interm.intermTypedNode), (yyvsp[-2].lex).loc);
    }
#line 7192 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 405:
#line 2591 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        parseContext.requireStage((yyvsp[-1].lex).loc, EShLangFragment, "discard");
        (yyval.interm.intermNode) = parseContext.intermediate.addBranch(EOpKill, (yyvsp[-1].lex).loc);
    }
#line 7201 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 406:
#line 2600 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
        parseContext.intermediate.setTreeRoot((yyval.interm.intermNode));
    }
#line 7210 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 407:
#line 2604 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = parseContext.intermediate.growAggregate((yyvsp[-1].interm.intermNode), (yyvsp[0].interm.intermNode));
        parseContext.intermediate.setTreeRoot((yyval.interm.intermNode));
    }
#line 7219 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 408:
#line 2611 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 7227 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 409:
#line 2614 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyval.interm.intermNode) = (yyvsp[0].interm.intermNode);
    }
#line 7235 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 410:
#line 2620 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        (yyvsp[0].interm).function = parseContext.handleFunctionDeclarator((yyvsp[0].interm).loc, *(yyvsp[0].interm).function, false /* not prototype */);
        (yyvsp[0].interm).intermNode = parseContext.handleFunctionDefinition((yyvsp[0].interm).loc, *(yyvsp[0].interm).function);
    }
#line 7244 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;

  case 411:
#line 2624 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1646  */
    {
        //   May be best done as post process phase on intermediate code
        if (parseContext.currentFunctionType->getBasicType() != EbtVoid && ! parseContext.functionReturnsValue)
            parseContext.error((yyvsp[-2].interm).loc, "function does not return a value:", "", (yyvsp[-2].interm).function->getName().c_str());
        parseContext.symbolTable.pop(&parseContext.defaultPrecision[0]);
        (yyval.interm.intermNode) = parseContext.intermediate.growAggregate((yyvsp[-2].interm).intermNode, (yyvsp[0].interm.intermNode));
        parseContext.intermediate.setAggregateOperator((yyval.interm.intermNode), EOpFunction, (yyvsp[-2].interm).function->getType(), (yyvsp[-2].interm).loc);
        (yyval.interm.intermNode)->getAsAggregate()->setName((yyvsp[-2].interm).function->getMangledName().c_str());

        // store the pragma information for debug and optimize and other vendor specific
        // information. This information can be queried from the parse tree
        (yyval.interm.intermNode)->getAsAggregate()->setOptimize(parseContext.contextPragma.optimize);
        (yyval.interm.intermNode)->getAsAggregate()->setDebug(parseContext.contextPragma.debug);
        (yyval.interm.intermNode)->getAsAggregate()->addToPragmaTable(parseContext.contextPragma.pragmaTable);
    }
#line 7264 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
    break;


#line 7268 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp" /* yacc.c:1646  */
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (pParseContext, YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (pParseContext, yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }



  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, pParseContext);
          yychar = YYEMPTY;
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
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
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


      yydestruct ("Error: popping",
                  yystos[yystate], yyvsp, pParseContext);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

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

#if !defined yyoverflow || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (pParseContext, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, pParseContext);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  yystos[*yyssp], yyvsp, pParseContext);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  return yyresult;
}
#line 2641 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1906  */

