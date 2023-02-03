
#include <foundation/memory.h>

#define STBI_MALLOC(sz)           memory_allocate(0, sz, 0, MEMORY_PERSISTENT)
#define STBI_REALLOC(p,newsz)     memory_reallocate(p, newsz, 0, memory_size(p), MEMORY_PERSISTENT)
#define STBI_FREE(p)              memory_deallocate(p)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
