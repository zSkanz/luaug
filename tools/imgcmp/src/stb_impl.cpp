// The single translation unit that instantiates stb's header-only bodies.
//
// It is isolated so that no file we author ever compiles vendored code: this
// target is deliberately not held to luaug::warnings (architecture.md §8 --
// vendored trees keep upstream's warning bar, ours keeps ours). Configuration
// macros are set on the target, not here, so every consumer of the headers sees
// the same configuration (STBI_NO_STDIO in particular changes declarations).

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <stb_image.h>
#include <stb_image_write.h>
