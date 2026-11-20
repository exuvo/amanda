/*
 * Linux/glibc replacements for the few gnulib functions Amanda used:
 * full_read/full_write/safe_read, base64, get_fs_usage and physmem_total.
 * They keep gnulib's interfaces and semantics.
 */
#ifndef AMANDA_COMPAT_H
#define AMANDA_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Read/write COUNT bytes, retrying on EINTR and short transfers.  Return the
 * number of bytes transferred; if less than COUNT, errno says why (0 means
 * end of file for full_read, ENOSPC means no progress for full_write). */
size_t full_read(int fd, void *buf, size_t count);
size_t full_write(int fd, const void *buf, size_t count);

/* read()/write() that retry on EINTR; return SAFE_READ_ERROR on error */
#define SAFE_READ_ERROR ((size_t) -1)
#define SAFE_WRITE_ERROR ((size_t) -1)
size_t safe_read(int fd, void *buf, size_t count);
size_t safe_write(int fd, const void *buf, size_t count);

/* base64 encode IN into a newly allocated, NUL-terminated *OUT; return the
 * encoded length (not counting the NUL) */
size_t base64_encode_alloc(const char *in, size_t inlen, char **out);
/* decode IN into OUT (*OUTLEN bytes available, set to the decoded length);
 * return false if IN is not valid base64 or does not fit */
bool base64_decode(const char *in, size_t inlen, char *out, size_t *outlen);

struct fs_usage {
    uintmax_t fsu_blocksize;	/* Size of a block.  */
    uintmax_t fsu_blocks;	/* Total blocks. */
    uintmax_t fsu_bfree;	/* Free blocks available to superuser. */
    uintmax_t fsu_bavail;	/* Free blocks available to non-superuser. */
    int fsu_bavail_top_bit_set;	/* 1 if fsu_bavail represents a value < 0.  */
    uintmax_t fsu_files;	/* Total file nodes. */
    uintmax_t fsu_ffree;	/* Free file nodes. */
};
/* Fill *FSP with usage of the file system containing FILE; return 0, or -1
 * with errno set.  DISK is ignored (kept for gnulib compatibility). */
int get_fs_usage(char const *file, char const *disk, struct fs_usage *fsp);

/* total physical memory in bytes */
double physmem_total(void);

#endif /* AMANDA_COMPAT_H */
