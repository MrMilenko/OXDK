/* xbox_platform.c: platform shims libSDLx assumes the C runtime ships
 * but the MS XDK CRT doesn't actually have.
 *
 * Used by the sdl_test sample. Drop this file into your own OXDK + libSDLx
 * project if you hit unresolved symbol errors for any of these names.
 */

#ifdef _XBOX

#include <xtl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>     /* real XDK struct stat layout */

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFF)
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#endif

/* POSIX stat. libSDLx's dirlist + various other POSIX-style callers use
 * this. The XDK CRT does ship `_stat()` (underscored) with the right
 * struct, but not the POSIX-spelled `stat()`. We back ours with
 * GetFileAttributesA which is available on Xbox and gives us the
 * directory bit cheaply.
 *
 * IMPORTANT: we use the real <sys/stat.h> struct (`st_mode` at offset 6,
 * after _dev_t + _ino_t). Defining your own local two-field struct here
 * gives a byte layout that doesn't match what callers see when they
 * include <sys/stat.h>, and `st_mode & _S_IFDIR` reads from the wrong
 * offset. Easy bug to chase if you're not looking for it.
 */
int __cdecl stat(const char* path, struct stat* st)
{
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return -1;
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_mode = (unsigned short)((attrs & FILE_ATTRIBUTE_DIRECTORY)
            ? _S_IFDIR : _S_IFREG);
    }
    return 0;
}

/* POSIX strdup, which libSDLx uses for various string copies. */
char* strdup(const char* s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* out = (char*)malloc(len);
    if (out) memcpy(out, s, len);
    return out;
}

/* libSDLx's SDL.c references SDL_CDROMInit / SDL_CDROMQuit. The cdrom .c
 * files redefine NT types that conflict with the kernel headers, so the
 * easy path is to compile without them and provide these no-op stubs. */
int  SDL_CDROMInit(void) { return 0; }
void SDL_CDROMQuit(void) {}

#endif /* _XBOX */
