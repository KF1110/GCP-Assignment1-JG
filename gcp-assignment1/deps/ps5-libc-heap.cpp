// The PlayStation 5's C library heap, made big enough to be useful.
//
// A .cpp among the headers, because unlike everything else here it cannot be
// one: the SDK's start-up code looks these two symbols up before main() runs,
// so they have to be defined once in a translation unit that is linked, not
// included into several.  Every program lists this file; see CMakeLists.txt.
//
// <stdlib.h> declares both symbols, and the no-limit constant, so including it
// first is what gives these definitions the linkage the SDK expects.
//
// Without them the default heap is tiny.  SDL does not notice, because on this
// platform it allocates through sceKernelAllocateDirectMemory rather than
// malloc (see SDL_malloc_playstation.c), but everything else does: every
// std::vector, std::string and std::map, the nlohmann::json that reads a
// sprite sheet, and Dear ImGui's font atlas.
//
// Off the PS5 this file compiles to nothing.

#ifdef __PROSPERO__

#include <stdlib.h> // sceLibcHeapSize, SCE_LIBC_HEAP_SIZE_EXTENDED_ALLOC_NO_LIMIT

unsigned int sceLibcHeapExtendedAlloc = 1; // grow the heap on demand...
size_t sceLibcHeapSize = SCE_LIBC_HEAP_SIZE_EXTENDED_ALLOC_NO_LIMIT; // ...uncapped

#endif // __PROSPERO__
