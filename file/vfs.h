#include "base/basictypes.h"

class AssetReader;
// Virtual file system.

void VFSRegister(const char *prefix, AssetReader *reader);
void VFSShutdown();

// Use delete [] to release the memory.
// Always allocates an extra '\0' at the end, so that it
// can be used for text like shader sources.
uint8_t *VFSReadFile(const char *filename, size_t *size);
