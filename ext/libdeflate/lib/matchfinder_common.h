/*
 * matchfinder_common.h - common code for Lempel-Ziv matchfinding
 */

#ifndef LIB_MATCHFINDER_COMMON_H
#define LIB_MATCHFINDER_COMMON_H

#include "lib_common.h"
#include "unaligned.h"

#ifndef MATCHFINDER_WINDOW_ORDER
#  error "MATCHFINDER_WINDOW_ORDER must be defined!"
#endif

#define MATCHFINDER_WINDOW_SIZE (1UL << MATCHFINDER_WINDOW_ORDER)

typedef s16 mf_pos_t;

#define MATCHFINDER_INITVAL ((mf_pos_t)-MATCHFINDER_WINDOW_SIZE)

/*
 * Required alignment of the matchfinder buffer pointer and size.  The values
 * here come from the AVX-2 implementation, which is the worst case.
 */
#define MATCHFINDER_MEM_ALIGNMENT	32
#define MATCHFINDER_SIZE_ALIGNMENT	128

#undef matchfinder_init
#undef matchfinder_rebase
#ifdef _aligned_attribute
#  if defined(__arm__) || defined(__aarch64__)
#    include "arm/matchfinder_impl.h"
#  elif defined(__i386__) || defined(__x86_64__)
#    include "x86/matchfinder_impl.h"
#  endif
#endif

/*
 * Initialize the hash table portion of the matchfinder.
 *
 * Essentially, this is an optimized memset().
 *
 * 'data' must be aligned to a MATCHFINDER_MEM_ALIGNMENT boundary, and
 * 'size' must be a multiple of MATCHFINDER_SIZE_ALIGNMENT.
 */
#ifndef matchfinder_init
static forceinline void
matchfinder_init(mf_pos_t *data, size_t size)
{
	size_t num_entries = size / sizeof(*data);
	size_t i;

	for (i = 0; i < num_entries; i++)
		data[i] = MATCHFINDER_INITVAL;
}
#endif

/*
 * Slide the matchfinder by WINDOW_SIZE bytes.
 *
 * This must be called just after each WINDOW_SIZE bytes have been run through
 * the matchfinder.
 *
 * This will subtract WINDOW_SIZE bytes from each entry in the array specified.
 * The effect is that all entries are updated to be relative to the current
 * position, rather than the position WINDOW_SIZE bytes prior.
 *
 * Underflow is detected and replaced with signed saturation.  This ensures that
 * once the sliding window has passed over a position, that position forever
 * remains out of bounds.
 *
 * The array passed in must contain all matchfinder data that is
 * position-relative.  Concretely, this will include the hash table as well as
 * the table of positions that is used to link together the sequences in each
 * hash bucket.  Note that in the latter table, the links are 1-ary in the case
 * of "hash chains", and 2-ary in the case of "binary trees".  In either case,
 * the links need to be rebased in the same way.
 *
 * 'data' must be aligned to a MATCHFINDER_MEM_ALIGNMENT boundary, and
 * 'size' must be a multiple of MATCHFINDER_SIZE_ALIGNMENT.
 */
#ifndef matchfinder_rebase
static forceinline void
matchfinder_rebase(mf_pos_t *data, size_t size)
{
	size_t num_entries = size / sizeof(*data);
	size_t i;

	if (MATCHFINDER_WINDOW_SIZE == 32768) {
		/* Branchless version for 32768 byte windows.  If the value was
		 * already negative, clear all bits except the sign bit; this
		 * changes the value to -32768.  Otherwise, set the sign bit;
		 * this is equivalent to subtracting 32768.  */
		for (i = 0; i < num_entries; i++) {
			u16 v = data[i];
			u16 sign_bit = v & 0x8000;
			v &= sign_bit - ((sign_bit >> 15) ^ 1);
			v |= 0x8000;
			data[i] = v;
		}
		return;
	}

	for (i = 0; i < num_entries; i++) {
		if (data[i] >= 0)
			data[i] -= (mf_pos_t)-MATCHFINDER_WINDOW_SIZE;
		else
			data[i] = (mf_pos_t)-MATCHFINDER_WINDOW_SIZE;
	}
}
#endif

/*
 * The hash function: given a sequence prefix held in the low-order bits of a
 * 32-bit value, multiply by a carefully-chosen large constant.  Discard any
 * bits of the product that don't fit in a 32-bit value, but take the
 * next-highest @num_bits bits of the product as the hash value, as those have
 * the most randomness.
 */
static forceinline u32
lz_hash(u32 seq, unsigned num_bits)
{
	return (u32)(seq * 0x1E35A7BD) >> (32 - num_bits);
}

/*
 * Return the number of bytes at @matchptr that match the bytes at @strptr, up
 * to a maximum of @max_len.  Initially, @start_len bytes are matched.
 */
static forceinline unsigned
lz_extend(const u8 * const strptr, const u8 * const matchptr,
	  const unsigned start_len, const unsigned max_len)
{
	unsigned len = start_len;
	machine_word_t v_word;

	if (UNALIGNED_ACCESS_IS_FAST) {

		if (likely(max_len - len >= 4 * WORDBYTES)) {

		#define COMPARE_WORD_STEP				\
			v_word = load_word_unaligned(&matchptr[len]) ^	\
				 load_word_unaligned(&strptr[len]);	\
			if (v_word != 0)				\
				goto word_differs;			\
			len += WORDBYTES;				\

			COMPARE_WORD_STEP
			COMPARE_WORD_STEP
			COMPARE_WORD_STEP
			COMPARE_WORD_STEP
		#undef COMPARE_WORD_STEP
		}

		while (len + WORDBYTES <= max_len) {
			v_word = load_word_unaligned(&matchptr[len]) ^
				 load_word_unaligned(&strptr[len]);
			if (v_word != 0)
				goto word_differs;
			len += WORDBYTES;
		}
	}

	while (len < max_len && matchptr[len] == strptr[len])
		len++;
	return len;

word_differs:
	if (CPU_IS_LITTLE_ENDIAN())
		len += (bsfw(v_word) >> 3);
	else
		len += (WORDBITS - 1 - bsrw(v_word)) >> 3;
	return len;
}

#endif /* LIB_MATCHFINDER_COMMON_H */
