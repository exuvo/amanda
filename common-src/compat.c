/*
 * Linux/glibc replacements for the gnulib functions Amanda used; see compat.h.
 */
#include "config.h"
#include "compat.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <glib.h>

/* largest transfer handed to a single read()/write() */
#define SYS_BUFSIZE_MAX ((size_t) (INT_MAX >> 20 << 20))

size_t
safe_read(int fd, void *buf, size_t count)
{
    ssize_t r;

    if (count > SYS_BUFSIZE_MAX)
	count = SYS_BUFSIZE_MAX;
    do {
	r = read(fd, buf, count);
    } while (r < 0 && errno == EINTR);
    return (size_t) r;
}

size_t
safe_write(int fd, const void *buf, size_t count)
{
    ssize_t r;

    if (count > SYS_BUFSIZE_MAX)
	count = SYS_BUFSIZE_MAX;
    do {
	r = write(fd, buf, count);
    } while (r < 0 && errno == EINTR);
    return (size_t) r;
}

size_t
full_read(int fd, void *buf, size_t count)
{
    size_t total = 0;
    char *ptr = buf;

    while (count > 0) {
	size_t n = safe_read(fd, ptr, count);
	if (n == SAFE_READ_ERROR)
	    break;
	if (n == 0) {
	    errno = 0;		/* end of file */
	    break;
	}
	total += n;
	ptr += n;
	count -= n;
    }
    return total;
}

size_t
full_write(int fd, const void *buf, size_t count)
{
    size_t total = 0;
    const char *ptr = buf;

    while (count > 0) {
	size_t n = safe_write(fd, ptr, count);
	if (n == SAFE_WRITE_ERROR)
	    break;
	if (n == 0) {
	    errno = ENOSPC;	/* no progress */
	    break;
	}
	total += n;
	ptr += n;
	count -= n;
    }
    return total;
}

size_t
base64_encode_alloc(const char *in, size_t inlen, char **out)
{
    *out = g_base64_encode((const guchar *) in, inlen);
    return strlen(*out);
}

bool
base64_decode(const char *in, size_t inlen, char *out, size_t *outlen)
{
    static const char alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    gint state = 0;
    guint save = 0;
    size_t i, pad = 0;
    guchar *tmp;
    size_t n;

    /* strict: only alphabet characters, '=' padding at the end, length % 4 */
    if (inlen % 4 != 0)
	return false;
    for (i = 0; i < inlen; i++) {
	if (in[i] == '=') {
	    pad++;
	} else if (pad || !memchr(alphabet, in[i], sizeof(alphabet) - 1)) {
	    return false;
	}
    }
    if (pad > 2)
	return false;

    tmp = g_malloc(inlen / 4 * 3 + 3);
    n = g_base64_decode_step(in, inlen, tmp, &state, &save);
    if (n > *outlen) {
	g_free(tmp);
	return false;
    }
    memcpy(out, tmp, n);
    g_free(tmp);
    *outlen = n;
    return true;
}

int
get_fs_usage(char const *file, char const *disk, struct fs_usage *fsp)
{
    struct statvfs vfsd;

    (void) disk;
    if (statvfs(file, &vfsd) < 0)
	return -1;
    fsp->fsu_blocksize = vfsd.f_frsize ? vfsd.f_frsize : vfsd.f_bsize;
    fsp->fsu_blocks = vfsd.f_blocks;
    fsp->fsu_bfree = vfsd.f_bfree;
    fsp->fsu_bavail = vfsd.f_bavail;
    fsp->fsu_bavail_top_bit_set = 0;
    fsp->fsu_files = vfsd.f_files;
    fsp->fsu_ffree = vfsd.f_ffree;
    return 0;
}

double
physmem_total(void)
{
    double pages = sysconf(_SC_PHYS_PAGES);
    double pagesize = sysconf(_SC_PAGESIZE);

    if (pages >= 0 && pagesize >= 0)
	return pages * pagesize;
    return 64 * 1024 * 1024;	/* gnulib's guess when unknown */
}
