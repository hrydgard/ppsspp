#include "base/basictypes.h"

// Allocates a buffer that should be freed using free().
short *wav_read(const char *filename,
								int *num_samples, int *sample_rate,
								int *num_channels);
// TODO: Non-allocating version.
