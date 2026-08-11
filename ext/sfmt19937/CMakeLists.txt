add_library(sfmt19937 STATIC
	SFMT.c
	SFMT.h
	SFMT-common.h
	SFMT-params.h
	SFMT-params19937.h
)
target_compile_definitions(sfmt19937 PRIVATE SFMT_MEXP=19937)
