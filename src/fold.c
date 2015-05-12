/* fold -- wrap each input line to fit in specified width.
   Copyright (C) 1991-2015 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* Written by David MacKenzie, djm@gnu.ai.mit.edu. */

#include <config.h>

#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>

/* Get mbstate_t, mbrtowc(), wcwidth().  */
#if HAVE_WCHAR_H
# include <wchar.h>
#endif

/* Get iswprint(), iswblank(), wcwidth().  */
#if HAVE_WCTYPE_H
# include <wctype.h>
#endif

#include "system.h"
#include "error.h"
#include "fadvise.h"
#include "xdectoint.h"

/* MB_LEN_MAX is incorrectly defined to be 1 in at least one GCC
      installation; work around this configuration error.  */
#if !defined MB_LEN_MAX || MB_LEN_MAX < 2
# undef MB_LEN_MAX
# define MB_LEN_MAX 16
#endif

/* Some systems, like BeOS, have multibyte encodings but lack mbstate_t.  */
#if HAVE_MBRTOWC && defined mbstate_t
# define mbrtowc(pwc, s, n, ps) (mbrtowc) (pwc, s, n, 0)
#endif

#define TAB_WIDTH 8

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "fold"

#define AUTHORS proper_name ("David MacKenzie")

#define FATAL_ERROR(Message)                                            \
  do                                                                    \
    {                                                                   \
      error (0, 0, (Message));                                          \
      usage (2);                                                        \
    }                                                                   \
  while (0)

enum operating_mode
{
  /* Fold texts by columns that are at the given positions. */
  column_mode,

  /* Fold texts by bytes that are at the given positions. */
  byte_mode,

  /* Fold texts by characters that are at the given positions. */
  character_mode,
};

/* The argument shows current mode. (Default: column_mode) */
static enum operating_mode operating_mode;

/* If nonzero, try to break on whitespace. */
static bool break_spaces;

/* If nonzero, at least one of the files we read was standard input. */
static bool have_read_stdin;

static char const shortopts[] = "bcsw:0::1::2::3::4::5::6::7::8::9::";

static struct option const longopts[] =
{
  {"bytes", no_argument, NULL, 'b'},
  {"characters", no_argument, NULL, 'c'},
  {"spaces", no_argument, NULL, 's'},
  {"width", required_argument, NULL, 'w'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    emit_try_help ();
  else
    {
      printf (_("\
Usage: %s [OPTION]... [FILE]...\n\
"),
              program_name);
      fputs (_("\
Wrap input lines in each FILE, writing to standard output.\n\
"), stdout);

      emit_stdin_note ();
      emit_mandatory_arg_note ();

      fputs (_("\
  -b, --bytes         count bytes rather than columns\n\
  -c, --characters    count characters rather than columns\n\
  -s, --spaces        break at spaces\n\
  -w, --width=WIDTH   use WIDTH columns instead of 80\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      emit_ancillary_info (PROGRAM_NAME);
    }
  exit (status);
}

/* Assuming the current column is COLUMN, return the column that
   printing C will move the cursor to.
   The first column is 0. */

static size_t
adjust_column (size_t column, char c)
{
  if (operating_mode != byte_mode)
    {
      if (c == '\b')
        {
          if (column > 0)
            column--;
        }
      else if (c == '\r')
        column = 0;
      else if (c == '\t')
        column += TAB_WIDTH - column % TAB_WIDTH;
      else /* if (isprint (c)) */
        column++;
    }
  else
    column++;
  return column;
}

/* Fold file FILENAME, or standard input if FILENAME is "-",
   to stdout, with maximum line length WIDTH.
   Return true if successful.  */

static void
fold_text (FILE *istream, size_t width, int *saved_errno)
{
  int c;
  size_t column = 0;		/* Screen column where next char will go. */
  size_t offset_out = 0;	/* Index in 'line_out' for next char. */
  static char *line_out = NULL;
  static size_t allocated_out = 0;

  fadvise (istream, FADVISE_SEQUENTIAL);

  while ((c = getc (istream)) != EOF)
    {
      if (offset_out + 1 >= allocated_out)
        line_out = X2REALLOC (line_out, &allocated_out);

      if (c == '\n')
        {
          line_out[offset_out++] = c;
          fwrite (line_out, sizeof (char), offset_out, stdout);
          column = offset_out = 0;
          continue;
        }

    rescan:
      column = adjust_column (column, c);

      if (column > width)
        {
          /* This character would make the line too long.
             Print the line plus a newline, and make this character
             start the next line. */
          if (break_spaces)
            {
              bool found_blank = false;
              size_t logical_end = offset_out;

              /* If LINE_OUT has no wide character,
                 put a new wide character in LINE_OUT
                 if column is bigger than width. */
              if (offset_out == 0)
                {
                  line_out[offset_out++] = c;
                  continue;
                }

              /* Look for the last blank. */
              while (logical_end)
                {
                  --logical_end;
                  if (isblank (to_uchar (line_out[logical_end])))
                    {
                      found_blank = true;
                      break;
                    }
                }

              if (found_blank)
                {
                  size_t i;

                  /* Found a blank.  Don't output the part after it. */
                  logical_end++;
                  fwrite (line_out, sizeof (char), (size_t) logical_end,
                          stdout);
                  putchar ('\n');
                  /* Move the remainder to the beginning of the next line.
                     The areas being copied here might overlap. */
                  memmove (line_out, line_out + logical_end,
                           offset_out - logical_end);
                  offset_out -= logical_end;
                  for (column = i = 0; i < offset_out; i++)
                    column = adjust_column (column, line_out[i]);
                  goto rescan;
                }
            }

          if (offset_out == 0)
            {
              line_out[offset_out++] = c;
              continue;
            }

          line_out[offset_out++] = '\n';
          fwrite (line_out, sizeof (char), (size_t) offset_out, stdout);
          column = offset_out = 0;
          goto rescan;
        }

      line_out[offset_out++] = c;
    }

  *saved_errno = errno;

  if (offset_out)
    fwrite (line_out, sizeof (char), (size_t) offset_out, stdout);

}

#if HAVE_MBRTOWC
static void
fold_multibyte_text (FILE *istream, size_t width, int *saved_errno)
{
  char buf[MB_LEN_MAX + BUFSIZ];  /* For spooling a read byte sequence. */
  size_t buflen = 0;        /* The length of the byte sequence in buf. */
  char *bufpos = buf;         /* Next read position of BUF. */
  wint_t wc;                /* A gotten wide character. */
  size_t mblength;        /* The byte size of a multibyte character which shows
                           as same character as WC. */
  mbstate_t state, state_bak;        /* State of the stream. */
  int convfail = 0;                /* 1, when conversion is failed. Otherwise 0. */

  static char *line_out = NULL;
  size_t offset_out = 0;        /* Index in `line_out' for next char. */
  static size_t allocated_out = 0;

  int increment;
  size_t column = 0;

  size_t last_blank_pos;
  size_t last_blank_column;
  int is_blank_seen;
  int last_blank_increment = 0;
  int is_bs_following_last_blank;
  size_t bs_following_last_blank_num;
  int is_cr_after_last_blank;

#define CLEAR_FLAGS                                \
   do                                                \
     {                                                \
        last_blank_pos = 0;                        \
        last_blank_column = 0;                        \
        is_blank_seen = 0;                        \
        is_bs_following_last_blank = 0;                \
        bs_following_last_blank_num = 0;        \
        is_cr_after_last_blank = 0;                \
     }                                                \
   while (0)

#define START_NEW_LINE                        \
   do                                        \
     {                                        \
      putchar ('\n');                        \
      column = 0;                        \
      offset_out = 0;                        \
      CLEAR_FLAGS;                        \
    }                                        \
   while (0)

  CLEAR_FLAGS;
  memset (&state, '\0', sizeof(mbstate_t));

  for (;; bufpos += mblength, buflen -= mblength)
    {
      if (buflen < MB_LEN_MAX && !feof (istream) && !ferror (istream))
        {
          memmove (buf, bufpos, buflen);
          buflen += fread (buf + buflen, sizeof(char), BUFSIZ, istream);
          bufpos = buf;
        }

      if (buflen < 1)
        break;

      /* Get a wide character. */
      state_bak = state;
      mblength = mbrtowc ((wchar_t *)&wc, bufpos, buflen, &state);

      switch (mblength)
        {
        case (size_t)-1:
        case (size_t)-2:
          convfail++;
          state = state_bak;
          /* Fall through. */

        case 0:
          mblength = 1;
          break;
        }

rescan:
      if (operating_mode == byte_mode)                        /* byte mode */
        increment = mblength;
      else if (operating_mode == character_mode)        /* character mode */
        increment = 1;
      else                                                /* column mode */
        {
          if (convfail)
            increment = 1;
          else
            {
              switch (wc)
                {
                case L'\n':
                  fwrite (line_out, sizeof(char), offset_out, stdout);
                  START_NEW_LINE;
                  continue;

                case L'\b':
                  increment = (column > 0) ? -1 : 0;
                  break;

                case L'\r':
                  increment = -1 * column;
                  break;

                case L'\t':
                  increment = 8 - column % 8;
                  break;

                default:
                  increment = wcwidth (wc);
                  increment = (increment < 0) ? 0 : increment;
                }
            }
        }

      if (column + increment > width && break_spaces && last_blank_pos)
        {
          fwrite (line_out, sizeof(char), last_blank_pos, stdout);
          putchar ('\n');

          offset_out = offset_out - last_blank_pos;
          column = column - last_blank_column + ((is_cr_after_last_blank)
              ? last_blank_increment : bs_following_last_blank_num);
          memmove (line_out, line_out + last_blank_pos, offset_out);
          CLEAR_FLAGS;
          goto rescan;
        }

      if (column + increment > width && column != 0)
        {
          fwrite (line_out, sizeof(char), offset_out, stdout);
          START_NEW_LINE;
          goto rescan;
        }

      if (allocated_out < offset_out + mblength)
        {
          line_out = X2REALLOC (line_out, &allocated_out);
        }

      memcpy (line_out + offset_out, bufpos, mblength);
      offset_out += mblength;
      column += increment;

      if (is_blank_seen && !convfail && wc == L'\r')
        is_cr_after_last_blank = 1;

      if (is_bs_following_last_blank && !convfail && wc == L'\b')
        ++bs_following_last_blank_num;
      else
        is_bs_following_last_blank = 0;

      if (break_spaces && !convfail && iswblank (wc))
        {
          last_blank_pos = offset_out;
          last_blank_column = column;
          is_blank_seen = 1;
          last_blank_increment = increment;
          is_bs_following_last_blank = 1;
          bs_following_last_blank_num = 0;
          is_cr_after_last_blank = 0;
        }
    }

  *saved_errno = errno;

  if (offset_out)
    fwrite (line_out, sizeof (char), (size_t) offset_out, stdout);

}
#endif

/* Fold file FILENAME, or standard input if FILENAME is "-",
   to stdout, with maximum line length WIDTH.
   Return 0 if successful, 1 if an error occurs. */

static bool
fold_file (char const *filename, size_t width)
{
  FILE *istream;
  int saved_errno;

  if (STREQ (filename, "-"))
    {
      istream = stdin;
      have_read_stdin = 1;
    }
  else
    istream = fopen (filename, "r");

  if (istream == NULL)
    {
      error (0, errno, "%s", filename);
      return 1;
    }

  /* Define how ISTREAM is being folded. */
#if HAVE_MBRTOWC
  if (MB_CUR_MAX > 1)
    fold_multibyte_text (istream, width, &saved_errno);
  else
#endif
    fold_text (istream, width, &saved_errno);

  if (ferror (istream))
    {
      error (0, saved_errno, "%s", filename);
      if (!STREQ (filename, "-"))
        fclose (istream);
      return false;
    }
  if (!STREQ (filename, "-") && fclose (istream) == EOF)
    {
      error (0, errno, "%s", filename);
      return false;
    }

  return true;
}

int
main (int argc, char **argv)
{
  size_t width = 80;
  int i;
  int optc;
  bool ok;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  operating_mode = column_mode;
  break_spaces = have_read_stdin = false;

  while ((optc = getopt_long (argc, argv, shortopts, longopts, NULL)) != -1)
    {
      char optargbuf[2];

      switch (optc)
        {
        case 'b':		/* Count bytes rather than columns. */
          if (operating_mode != column_mode)
            FATAL_ERROR (_("only one way of folding may be specified"));
          operating_mode = byte_mode;
          break;

        case 'c':
          if (operating_mode != column_mode)
            FATAL_ERROR (_("only one way of folding may be specified"));
          operating_mode = character_mode;
          break;

        case 's':		/* Break at word boundaries. */
          break_spaces = true;
          break;

        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
          if (optarg)
            optarg--;
          else
            {
              optargbuf[0] = optc;
              optargbuf[1] = '\0';
              optarg = optargbuf;
            }
          /* Fall through.  */
        case 'w':		/* Line width. */
          width = xdectoumax (optarg, 1, SIZE_MAX - TAB_WIDTH - 1, "",
                              _("invalid number of columns"), 0);
          break;

        case_GETOPT_HELP_CHAR;

        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

        default:
          usage (EXIT_FAILURE);
        }
    }

  if (argc == optind)
    ok = fold_file ("-", width);
  else
    {
      ok = true;
      for (i = optind; i < argc; i++)
        ok &= fold_file (argv[i], width);
    }

  if (have_read_stdin && fclose (stdin) == EOF)
    error (EXIT_FAILURE, errno, "-");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
