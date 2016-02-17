/* A Bison parser, made by GNU Bison 3.0.4.  */

/* Bison interface for Yacc-like parsers in C

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
#line 66 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang.y" /* yacc.c:1909  */

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

#line 346 "C:/releasebuild/glslang/glslang/MachineIndependent/glslang_tab.cpp.h" /* yacc.c:1909  */
};

typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif



int yyparse (glslang::TParseContext* pParseContext);

#endif /* !YY_YY_C_RELEASEBUILD_GLSLANG_GLSLANG_MACHINEINDEPENDENT_GLSLANG_TAB_CPP_H_INCLUDED  */
