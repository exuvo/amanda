/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
 * Copyright (c) 2007-2012 Zmanda, Inc.  All Rights Reserved.
 * Copyright (c) 2013-2016 Carbonite, Inc.  All Rights Reserved.
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: amindex.c,v 1.15 2006/05/25 01:47:19 johnfranks Exp $
 *
 * index control
 */

#include "amanda.h"
#include "conffile.h"
#include "amindex.h"

char *
getstatefname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
  char *conf_indexdir;
  char *buf;
  char level_str[NUM_STR_SIZE];
  char datebuf[14 + 1];
  char *dc = NULL;
  char *pc;
  int ch;

  if (date != NULL) {
    dc = date;
    pc = datebuf;
    while (pc < datebuf + sizeof(datebuf)) {
      ch = *dc++;
      *pc++ = (char)ch;
      if (ch == '\0') {
        break;
      } else if (! isdigit (ch)) {
        pc--;
      }
    }
    datebuf[sizeof(datebuf)-1] = '\0';
    dc = datebuf;

    g_snprintf(level_str, sizeof(level_str), "%d", level);
  }

  host = sanitise_filename(host);
  if (disk != NULL) {
    disk = sanitise_filename(disk);
  }

  conf_indexdir = config_dir_relative(getconf_str(CNF_INDEXDIR));
  /*
   * Note: g_strjoin(NULL, ) will stop at the first NULL, which might be
   * "disk" or "dc" (datebuf) rather than the full file name.
   */
  buf = g_strjoin(NULL, conf_indexdir, "/",
		  host, "/",
		  disk, "/",
		  dc, "_",
		  level_str, ".state",
		  NULL);

  amfree(conf_indexdir);
  amfree(host);
  amfree(disk);

  return buf;
}

/*
 * Build "<indexdir>/<host>/<disk>/<date>_<level><suffix>".
 */
static char *
index_fname(
    char       *host,
    char       *disk,
    char       *date,
    int         level,
    const char *suffix)
{
  char *conf_indexdir;
  char *buf;
  char level_str[NUM_STR_SIZE];
  char datebuf[14 + 1];
  char *dc = NULL;
  char *pc;
  int ch;

  if (date != NULL) {
    dc = date;
    pc = datebuf;
    while (pc < datebuf + sizeof(datebuf)) {
      ch = *dc++;
      *pc++ = (char)ch;
      if (ch == '\0') {
        break;
      } else if (! isdigit (ch)) {
        pc--;
      }
    }
    datebuf[sizeof(datebuf)-1] = '\0';
    dc = datebuf;

    g_snprintf(level_str, sizeof(level_str), "%d", level);
  }

  host = sanitise_filename(host);
  if (disk != NULL) {
    disk = sanitise_filename(disk);
  }

  conf_indexdir = config_dir_relative(getconf_str(CNF_INDEXDIR));
  /*
   * Note: g_strjoin(NULL, ) will stop at the first NULL, which might be
   * "disk" or "dc" (datebuf) rather than the full file name.
   */
  buf = g_strjoin(NULL, conf_indexdir, "/",
		  host, "/",
		  disk, "/",
		  dc, "_",
		  level_str, suffix,
		  NULL);

  amfree(conf_indexdir);
  amfree(host);
  amfree(disk);

  return buf;
}

char *
getindexfname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, COMPRESS_SUFFIX);
}

char *
getindex_unsorted_fname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, "-unsorted");
}

char *
getindex_unsorted_gz_fname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, "-unsorted" COMPRESS_SUFFIX);
}

char *
getindex_sorted_fname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, "-sorted");
}

char *
getindex_sorted_gz_fname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, "-sorted" COMPRESS_SUFFIX);
}

/* index files are written compressed with INDEX_COMPRESS_PATH (zstd) */
char *
getindex_unsorted_zst_fname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, "-unsorted" INDEX_COMPRESS_SUFFIX);
}

char *
getindex_sorted_zst_fname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
    return index_fname(host, disk, date, level, "-sorted" INDEX_COMPRESS_SUFFIX);
}

/*
 * Program and option to uncompress an index file to stdout, chosen by its
 * suffix, so gzip-compressed indexes from older versions stay readable.
 */
const char *
index_uncompress_path(
    const char *filename)
{
    if (g_str_has_suffix(filename, INDEX_COMPRESS_SUFFIX))
	return INDEX_UNCOMPRESS_PATH;
    return UNCOMPRESS_PATH;
}

const char *
index_uncompress_opt(
    const char *filename)
{
    if (g_str_has_suffix(filename, INDEX_COMPRESS_SUFFIX))
	return INDEX_UNCOMPRESS_OPT;
    return UNCOMPRESS_OPT;
}

char *
getheaderfname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
  char *conf_indexdir;
  char *buf;
  char level_str[NUM_STR_SIZE];
  char datebuf[14 + 1];
  char *dc = NULL;
  char *pc;
  int ch;

  if (date != NULL) {
    dc = date;
    pc = datebuf;
    while (pc < datebuf + sizeof(datebuf)) {
      ch = *dc++;
      *pc++ = (char)ch;
      if (ch == '\0') {
        break;
      } else if (! isdigit (ch)) {
        pc--;
      }
    }
    datebuf[sizeof(datebuf)-1] = '\0';
    dc = datebuf;

    g_snprintf(level_str, sizeof(level_str), "%d", level);
  }

  host = sanitise_filename(host);
  if (disk != NULL) {
    disk = sanitise_filename(disk);
  }

  conf_indexdir = config_dir_relative(getconf_str(CNF_INDEXDIR));
  /*
   * Note: g_strjoin(NULL, ) will stop at the first NULL, which might be
   * "disk" or "dc" (datebuf) rather than the full file name.
   */
  buf = g_strjoin(NULL, conf_indexdir, "/",
		  host, "/",
		  disk, "/",
		  dc, "_",
		  level_str, ".header",
		  NULL);

  amfree(conf_indexdir);
  amfree(host);
  amfree(disk);

  return buf;
}

char *
getoldindexfname(
    char *	host,
    char *	disk,
    char *	date,
    int		level)
{
  char *conf_indexdir;
  char *buf;
  char level_str[NUM_STR_SIZE];
  char datebuf[14 + 1];
  char *dc = NULL;
  char *pc;
  int ch;

  if (date != NULL) {
    dc = date;
    pc = datebuf;
    while (pc < datebuf + sizeof(datebuf)) {
      ch = *dc++;
      *pc++ = (char)ch;
      if (ch == '\0') {
        break;
      } else if (! isdigit (ch)) {
        pc--;
      }
    }
    datebuf[sizeof(datebuf)-1] = '\0';
    dc = datebuf;

    g_snprintf(level_str, sizeof(level_str), "%d", level);
  }

  host = old_sanitise_filename(host);
  if (disk != NULL) {
    disk = old_sanitise_filename(disk);
  }

  conf_indexdir = config_dir_relative(getconf_str(CNF_INDEXDIR));
  /*
   * Note: g_strjoin(NULL, ) will stop at the first NULL, which might be
   * "disk" or "dc" (datebuf) rather than the full file name.
   */
  buf = g_strjoin(NULL, conf_indexdir, "/",
		  host, "/",
		  disk, "/",
		  dc, "_",
		  level_str, COMPRESS_SUFFIX,
		  NULL);

  amfree(conf_indexdir);
  amfree(host);
  amfree(disk);

  return buf;
}
