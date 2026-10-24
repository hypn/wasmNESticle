// POSIX replacement for FILE.CPP (the original used DOS/Watcom/Borland/MSVC file APIs).
// Same FILEIO / enumdir interface as file.h.
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "file.h"

// Calls func for every entry of a directory that matches the wildcard in 'path'
// (e.g. "*.nes", or "dir/*.nes"). Matching is case-insensitive like DOS.
void enumdir(char *path, DIRFUNCPTR func, void *context)
{
 char dir[512];
 const char *pattern = path;
 const char *slash = strrchr(path, '/');
 if (slash)
  {
   size_t n = slash - path;
   if (n >= sizeof(dir)) return;
   memcpy(dir, path, n); dir[n] = 0;
   if (!dir[0]) strcpy(dir, "/");
   pattern = slash + 1;
  }
 else strcpy(dir, ".");

 DIR *d = opendir(dir);
 if (!d) return;
 while (struct dirent *e = readdir(d))
  {
   if (fnmatch(pattern, e->d_name, FNM_CASEFOLD) != 0) continue;
   if (!func(e->d_name, context)) break;
  }
 closedir(d);
}

//---------------------------------------------
// handle 0 means "not open" in the original, so fds are used as-is (open() never returns 0 here
// because stdin is already taken).

int FILEIO::open(char *filename)
{
 if (h) close();
 h = ::open(filename, O_RDWR);
 if (h == -1) h = ::open(filename, O_RDONLY); // read-only files (e.g. preloaded assets)
 if (h == -1) {h = 0; return -1;}
 return 0;
}

int FILEIO::create(char *filename)
{
 if (h) close();
 h = ::open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
 if (h == -1) {h = 0; return -1;}
 return 0;
}

void FILEIO::close()
{
 if (!h) return;
 ::close(h);
 h = 0;
}

// the original returned 0 on success, -1 on failure (including short reads)
int FILEIO::read(void *t, unsigned size)
{
 if (!h) return -1;
 ssize_t n = ::read(h, t, size);
 return (n < 0 || (unsigned)n < size) ? -1 : 0;
}

int FILEIO::write(void *t, unsigned size)
{
 if (!h) return -1;
 ssize_t n = ::write(h, t, size);
 return (n < 0 || (unsigned)n < size) ? -1 : 0;
}

unsigned FILEIO::size()
{
 if (!h) return 0;
 struct stat st;
 return fstat(h, &st) == 0 ? (unsigned)st.st_size : 0;
}

unsigned FILEIO::getpos()
{
 return h ? (unsigned)lseek(h, 0, SEEK_CUR) : 0;
}

void FILEIO::setpos(unsigned p)
{
 if (h) lseek(h, p, SEEK_SET);
}
