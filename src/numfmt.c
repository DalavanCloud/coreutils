/* Reformat numbers like 11505426432 to the more human-readable 11G
   Copyright (C) 2012 Free Software Foundation, Inc.

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

#include <config.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/types.h>
#include <langinfo.h>

#include "mbsalign.h"
#include "argmatch.h"
#include "error.h"
#include "system.h"
#include "xstrtol.h"

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "numfmt"

#define AUTHORS proper_name ("Assaf Gordon")

#define BUFFER_SIZE (16 * 1024)

enum
{
  FROM_OPTION = CHAR_MAX + 1,
  FROM_UNIT_OPTION,
  TO_OPTION,
  TO_UNIT_OPTION,
  ROUND_OPTION,
  SUFFIX_OPTION,
  GROUPING_OPTION,
  PADDING_OPTION,
  FIELD_OPTION,
  DEBUG_OPTION,
  DEV_DEBUG_OPTION,
  HEADER_OPTION
};

enum scale_type
{
scale_none, /* the default: no scaling */
scale_auto, /* --from only */
scale_SI,
scale_IEC
};

static char const *const scale_from_args[] =
{
"none", "auto", "si", "iec", NULL
};
static enum scale_type const scale_from_types[] =
{
scale_none, scale_auto, scale_SI, scale_IEC
};

static char const *const scale_to_args[] =
{
"none", "si", "iec", NULL
};
static enum scale_type const scale_to_types[] =
{
scale_none, scale_SI, scale_IEC
};


enum round_type
{
round_ceiling,
round_floor,
round_nearest
};

static char const *const round_args[] =
{
"ceiling","floor","nearest", NULL
};

static enum round_type const round_types[] =
{
round_ceiling,round_floor,round_nearest
};

static struct option const longopts[] =
{
  {"from", required_argument, NULL, FROM_OPTION},
  {"from-unit", required_argument, NULL, FROM_UNIT_OPTION},
  {"to", required_argument, NULL, TO_OPTION},
  {"to-unit", required_argument, NULL, TO_UNIT_OPTION},
  {"round", required_argument, NULL, ROUND_OPTION},
  {"padding", required_argument, NULL, PADDING_OPTION},
  {"suffix", required_argument, NULL, SUFFIX_OPTION},
  {"grouping", no_argument, NULL, GROUPING_OPTION},
  {"delimiter", required_argument, NULL, 'd'},
  {"field", required_argument, NULL, FIELD_OPTION},
  {"debug", no_argument,NULL,DEBUG_OPTION},
  {"devdebug", no_argument,NULL,DEV_DEBUG_OPTION},
  {"header", optional_argument,NULL,HEADER_OPTION},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

/* If delimtier has this value, blanks separate fields.  */
enum { DELIMITER_DEFAULT = CHAR_MAX + 1 };

/* Maximum number of digits we can safely handle
 * without precision loss, if scaling is 'none' */
enum { MAX_UNSCALED_DIGITS = 18 };

/* Maximum number of digits we can work with.
 * This is equivalent to 999Y.
 * NOTE: 'long double' can handle more than that, but there's
 *       no official suffix assigned beyond Yotta (1000^8) */
enum { MAX_ACCEPTABLE_DIGITS = 27 } ;

static enum scale_type scale_from=scale_none;
static enum scale_type scale_to=scale_none;
static enum round_type _round=round_ceiling;
static const char *suffix = NULL;
static uintmax_t from_unit_size=1;
static uintmax_t to_unit_size=1;
static int grouping=0;
static char *padding_buffer=NULL;
static size_t padding_buffer_size=0;
static long int padding_width=0;
static int auto_padding=0; /* auto-pad each line based on skipped whitespace*/
static mbs_align_t padding_alignment=MBS_ALIGN_RIGHT;
static long int field=1;
static int delimiter = DELIMITER_DEFAULT;

/* if non-zero, the first 'header' lines from STDIN are skipped */
static uintmax_t header=0;

/* Debug for users: print warnings to STDERR about possible
   error (similar to sort's debug) */
static int debug=0;

/* debugging for developers - to be removed in final version? */
static int dev_debug=0;

/* will be set according to the current locale */
static const char *decimal_point;
static int decimal_point_length;

static inline int
valid_suffix ( const char suf )
{
  static const char *valid_suffixes="kKMGTPEZY";
  return (strchr (valid_suffixes,suf)!=NULL);
}

static inline int
suffix_power ( const char suf )
{
  switch (suf)
    {
    case 'K': /* kilo or kibi*/
      return 1;

    case 'M': /* mega or mebi */
      return 2;

    case 'G': /* giga or gibi */
      return 3;

    case 'T': /* tera or tebi */
      return 4;

    case 'P': /* peta or pebi */
      return 5;

    case 'E': /* exa or exbi */
      return 6;

    case 'Z': /* zetta or 2**70 */
      return 7;

    case 'Y': /* yotta or 2**80 */
      return 8;

    default: /* should never happen. assert? */
      return 0;
    }
}

static inline const char*
suffix_power_character ( unsigned int power )
{
  switch (power)
    {
    case 0:
      return "";

    case 1:
      return "K";

    case 2:
      return "M";

    case 3:
      return "G";

    case 4:
      return "T";

    case 5:
      return "P";

    case 6:
      return "E";

    case 7:
      return "Z";

    case 8:
      return "Y";

    default:
      return "(error)";
    }
}

/* Similar to 'powl(3)' but without requiring 'libm' */
static long double
powerld (long double base, unsigned int x)
{
  long double result=base;
  if (x==0)
    return 1; /* note for test coverage: this is never reached,
                 as 'powerld' won't be called if there's no suffix,
                 hence, no "power" */

  /* TODO: check for overflow, inf? */
  while (--x)
    result *= base;
  return result;
}

/* scales down 'val', returns 'updated val' and 'x', such that
 *   val*base^X = original val
 *   Similar to "frexpl(3)" but without requiring 'libm',
 *   allowing only integer scale, limited functionlity and error checking.*/
static long double
expld (long double val, unsigned int base, unsigned int /*output*/ *x)
{
  unsigned int power=0;
  while (val>=base)
    {
      ++power;
      val /= base;
    }
  if (x)
    *x = power;
  return val;
}

/* EXTREMELY limited 'round' - without 'libm' .
 * Assumes only positive, small  values */
static inline int
simple_round_nearest (long double val)
{
  return (int)(val+0.5);
}

/* EXTREMELY limited 'floor' - without 'libm' .
 * Assumes only positive, small  values */
static inline int
simple_round_floor (long double val)
{
  return (int)val;
}

/* EXTREMELY limited 'ceil' - without 'libm' .
 * Assumes only positive, small  values */
static inline int
simple_round_ceiling (long double val)
{
  return simple_round_floor (val)
         + (((val - simple_round_floor (val))>0) ? 1 : 0);
}

static inline int
__attribute ((pure))
simple_round (long double val, enum round_type t)
{
  switch (t)
    {
    case round_ceiling:
      return simple_round_ceiling (val);

    case round_floor:
      return simple_round_floor (val);

    case round_nearest:
      return simple_round_nearest (val);

    default:
      abort(); /* to silence to compiler - this should never happen */
    }
}

/* This function reads an *integer* string,
 * but returns the integer value in a 'long double'
 * hence, no 2^64 limitation.
 * other limitations: no locale'd grouping, no skipping white-space
 *
 *      999,000,000,000,000,000,000,000
 *         Y   Z   E   P   T   G   K
 *
 * Returns:
 *    SSE_OK - valid number.
 *    SSE_OK_PRECISION_LOSS - if more than 18 digits were used.
 *    SSE_OVERFLOW          - if more than 27 digits (999Y) were used.
 *    SSE_INVALID_NUMBER    - if no digits were found.
 */
enum simple_strtod_error
{
  SSE_OK=0,
  SSE_OK_PRECISION_LOSS,
  SSE_OVERFLOW,
  SSE_INVALID_NUMBER,

  /* the following are returned by 'simple_strtod_human' */
  SSE_FRACTION_FORBIDDEN_WITHOUT_SCALING,
  SSE_FRACTION_REQUIRES_SUFFIX,
  SSE_VALID_BUT_FORBIDDEN_SUFFIX,
  SSE_INVALID_SUFFIX
};

static enum simple_strtod_error
simple_strtod_int (const char *input_str,
                   char* /*output*/ *ptr, /*required, unlike in stdtod */
                   long double /*output*/ *value)
{
  enum simple_strtod_error e = SSE_OK;

  long double val=0;
  unsigned int digits=0;
  *ptr = (char*)input_str;
  while ( (*ptr) && isdigit ((**ptr)) )
    {
      int digit = (**ptr) - '0';
      if (digit<0 || digit>9)
        return SSE_INVALID_NUMBER; /* can this happen in some strange locale? */

      if (digits>MAX_UNSCALED_DIGITS)
           e = SSE_OK_PRECISION_LOSS;

      ++digits;
      if (digits>MAX_ACCEPTABLE_DIGITS)
        return SSE_OVERFLOW;

      val *= 10;
      val += digit;

      ++(*ptr);
    }
  if (digits==0)
    return SSE_INVALID_NUMBER;
  if (value)
    *value = val;

  return e;
}

/*
 * Accepts a floating-point value, represented as "NNNN[.NNNNN]" .
 */
static enum simple_strtod_error
simple_strtod_float (const char *input_str,
                   char* /*output*/ *ptr, /*required, unlike in stdtod */
                   long double /*output*/ *value,
                   int /*output*/ *have_fractions)
{
  enum simple_strtod_error e = SSE_OK;

  *have_fractions = 0 ;

  /* TODO: accept locale'd grouped values for the integral part */
  e = simple_strtod_int (input_str, ptr, value);
  if (e!=SSE_OK && e!=SSE_OK_PRECISION_LOSS)
    return e;


  /* optional decimal point + fraction */
  if (STREQ_LEN (*ptr,decimal_point,decimal_point_length))
    {
      char *ptr2;
      long double val_frac=0;

      (*ptr) += decimal_point_length;
      enum simple_strtod_error e2 = simple_strtod_int (*ptr,&ptr2,&val_frac);
      if (e2!=SSE_OK && e2!=SSE_OK_PRECISION_LOSS)
        return e2;
      if (e2==SSE_OK_PRECISION_LOSS)
        e = e2 ; /* propegate warning */

      int exponent = ptr2-*ptr; /* number of digits in the fractions */
      val_frac = ((long double)val_frac)/powerld (10,exponent);

      *value += val_frac;

      *have_fractions = 1 ;

      *ptr=ptr2;
    }
  return e;
}

static enum simple_strtod_error
simple_strtod_human (const char *input_str,
              char* /*output*/ *ptr, /*required, unlike in stdtod */
              long double /*output*/ *value, /* required */
              enum scale_type allowed_scaling)
{
  int have_fractions=0;
  int power=0;
  /* 'scale_auto' is checked below */
  int scale_base=(allowed_scaling==scale_IEC)?1024:1000;

  if (dev_debug)
    fprintf (stderr,"simple_stdtod_human:\n  input string: '%s'\n  " \
            "locale decimal-point: '%s'\n", input_str,decimal_point);

  enum simple_strtod_error e =
        simple_strtod_float (input_str, ptr, value,&have_fractions );
  if (e!=SSE_OK && e!=SSE_OK_PRECISION_LOSS)
    return e;

  if (dev_debug)
    fprintf (stderr,"  parsed numeric value: %Lf\n" \
                    "  have_fractions = %d\n", *value, have_fractions);

  if (have_fractions && allowed_scaling==scale_none)
        return SSE_FRACTION_FORBIDDEN_WITHOUT_SCALING;


  /* no suffix / trailing characters */
  if (**ptr=='\0')
    {
      if (have_fractions)
        return SSE_FRACTION_REQUIRES_SUFFIX;

      if (dev_debug)
        fprintf (stderr,"  no fraction,suffix detected\n" \
                       "  returning value: %Lf (%LG)\n", *value, *value);

      return e;
    }

  /* process suffix */
  if (!valid_suffix (**ptr))
    return SSE_INVALID_SUFFIX;

  if (allowed_scaling==scale_none)
    return SSE_VALID_BUT_FORBIDDEN_SUFFIX;

  power = suffix_power (**ptr);
  (*ptr)++; /* skip first suffix character */

  if (allowed_scaling==scale_auto && **ptr=='i')
    {
      /* auto-scaling enabled, and the first suffix character
         is followed by an 'i' (e.g. Ki, Mi, Gi) */
      scale_base = 1024 ;
      (*ptr)++; /* skip second  ('i') suffix character */
      if (dev_debug)
        fprintf (stderr,"  Auto-scaling, found 'i', switching to base %d\n",
                scale_base);
    }

  long double multiplier = powerld (scale_base,power);

  if (dev_debug)
    fprintf (stderr, "  suffix power=%d^%d = %Lf\n",
          scale_base,power,multiplier);

  /* TODO: detect loss of precision and overflows */
  (*value) = (*value) * multiplier;

  if (dev_debug)
    fprintf (stderr,"  returning value: %Lf (%LG)\n", *value, *value);

  return e;
}


static void
simple_strtod_fatal (enum simple_strtod_error err,
                     char const *input_str)
{
  char const *msgid=NULL;

  switch (err)
    {
    case SSE_OK_PRECISION_LOSS:
    case SSE_OK:
      abort (); /* should never happen - this function isn't called when OK */

    case SSE_OVERFLOW:
      msgid = N_("value too large to be converted: '%s'");
      break;

    case SSE_INVALID_NUMBER:
      msgid = N_("invalid number: '%s'");
      break;

    case SSE_FRACTION_FORBIDDEN_WITHOUT_SCALING:
      msgid = N_("cannot process decimal-point value without scaling: '%s'" \
                 " (consider using --from)");
      break;

    case SSE_FRACTION_REQUIRES_SUFFIX:
      msgid = N_("decimal-point values require a suffix (e.g. K/M/G/T): '%s'");
      break;

    case SSE_VALID_BUT_FORBIDDEN_SUFFIX:
      msgid = N_("rejecting suffix in input: '%s' (consider using --from)");
      break;

    case SSE_INVALID_SUFFIX:
      msgid = N_("invalid suffix in input: '%s'");
      break;

    }

  error (EXIT_FAILURE,0,gettext (msgid), input_str);
}

static char*
double_to_human (long double val,
                      char* buf, size_t buf_size,
                      enum scale_type scale,
                      int group,
                      enum round_type round)
{
  if (dev_debug)
    fprintf (stderr,"double_to_human:\n");

  if (scale==scale_none)
    {
      if (dev_debug)
          fprintf (stderr,
                   (group)?"  no scaling, returning (grouped) value: %'.0Lf\n":
                           "  no scaling, returning value: %.0Lf\n",
                   val);

      int i = snprintf (buf,buf_size,(group)?"%'.0Lf":"%.0Lf",val);
      if (i<0 || i>=(int)buf_size)
        error (EXIT_FAILURE,0,_("failed to prepare value '%Lf' for printing"),
              val);
      return buf;
    }

  /* Scaling requested by user */
  double scale_base=(scale==scale_SI)?1000:1024;

  /* Normalize val to scale */
  unsigned int power=0;
  val = expld (val, scale_base, &power);
  if (dev_debug)
    fprintf (stderr,"  scaled value to %Lf * %0.f ^ %d\n",
             val, scale_base, power);

  /* Perform rounding */
  int ten_or_less = 0;
  if (val<10)
    {
      /* for values less than 10, we allow one decimal-point digit,
       * so adjust before rounding */
      ten_or_less = 1;
      val *= 10;
    }
  val = simple_round (val, round);
  /* two special cases after rounding:
   * 1. a "999.99" can turn into 1000 - so scale down
   * 2. a "9.99" can turn into 10 - so don't display decimal-point
   */
  if (val>=scale_base)
    {
      val /= scale_base;
      power++;
    }
  if (ten_or_less)
      val /= 10;

  /* should "7.0" be printed as "7" ?
   * if removing the ".0" is preffered, enable the fourth condition */
  int show_decimal_point = (val!=0) && (val<10) && (power>0) ;
                                /* && (val>simple_round_floor (val)))*/

  if (dev_debug)
    fprintf (stderr,"  after rounding, value=%Lf * %0.f ^ %d\n",
             val, scale_base, power);

  snprintf (buf,buf_size, (show_decimal_point)?"%.1Lf%s":"%.0Lf%s",
           val,suffix_power_character (power));


  if (dev_debug)
    fprintf (stderr,"  returning value: '%s'\n", buf);

  return buf;
}

/* Convert a string of decimal digits, N_STRING, with an optional suffinx
   to an integral value.  Upon successful conversion,
   return that value.  If it cannot be converted, give a diagnostic and exit.
*/
static uintmax_t
string_to_integer (const char *n_string)
{
  strtol_error s_err;
  char* ptr=NULL;
  uintmax_t n;

  s_err = xstrtoumax (n_string, &ptr, 10, &n, "KMGTPEZY");

  if (s_err != LONGINT_OK || *ptr!='\0')
      error (EXIT_FAILURE, 0, _("invalid unit size: '%s'"), n_string);

  return n;
}


static void
setup_padding_buffer ( size_t min_size )
{
  if (padding_buffer_size > min_size)
    return ;

  padding_buffer_size = min_size+1;
  padding_buffer = realloc (padding_buffer, padding_buffer_size);
  if (!padding_buffer)
    error (EXIT_FAILURE,0, _("out of memory (requested %zu bytes)"),
           padding_buffer_size);
}

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    emit_try_help ();
  else
    {
      printf (_("\
Usage: %s [OPTIONS] [NUMBER]\n\
"),
          program_name);
      fputs (_("\
Reformats NUMBER(s) to/from human-readable values.\n\
Numbers can be processed either from stdin or command arguments.\n\
\n\
"), stdout);
      fputs (_("\
  --from=UNIT     auto-scale input numbers to UNITs. Default is 'none'.\n\
                  See UNIT below.\n\
  --from-unit=N   specify the input unit size (instead of the default 1).\n\
  --to=UNIT       auto-scale output numbers to UNITs.\n\
                  See UNIT below.\n\
  --to-unit=N     specify the output unit size (instead of the default 1).\n\
  --round=METHOD  round input numbers. METHOD can be:\n\
                  ceiling (the default), floor, nearest\n\
  --suffix=SUFFIX add SUFFIX to output numbers, and accept optional SUFFIX\n\
                  in input numbers.\n\
  --padding=N     pad the output to N characters.\n\
                  Positive N will right-aligned. Negative N will left-align.\n\
                  Note: if the output is wider than N, padding is ignored.\n\
                  Default is to automatically pad if whitespace is found.\n\
  --grouping      group digits together (e.g. 1,000,000).\n\
                  Uses the locale-defined grouping (i.e. have no effect\n\
                  in C/POSIX locales).\n\
  --header[=N]    print (without converting) the first N header lines.\n\
                  N defaults to 1 if not specified.\n\
  --field N       replace the number in input field N (default is 1)\n\
  -d, --delimiter=X  use X instead of whitespace for field delimiter\n\
  --debug         print warnings about invalid input.\n\
  \n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);

      fputs (_("\
\n\
UNIT options:\n\
 none:\n\
      No auto-scaling is done. Suffixes will trigger an error.\n\
 auto ('--from' only):\n\
      1K  = 1000\n\
      1Ki = 1024\n\
      1G  = 1000000\n\
      1Gi = 1048576\n\
 si:\n\
      1K = 1000\n\
      1G  = 1000000\n\
      ...\n\
 iec:\n\
      1K = 1024\n\
      1G = 1048576\n\
      ...\n\
\n\
"), stdout);

      printf (_("\
\n\
Examples:\n\
  %s --to=si 1000           -> \"1K\"\n\
  echo 1K | %s --from=si    -> \"1000\"\n\
  echo 1K | %s --from=iec   -> \"1024\"\n\
  df | %s --header --field 2 --to=si\n\
  ls -l | %s --header --field 5 --to=iec\n\
  ls -lh | %s --header --field 5 --from=iec --padding=10\n\
"),
              program_name, program_name, program_name,
              program_name, program_name, program_name);
      emit_ancillary_info ();
    }
  exit (status);
}

/* Finds newline chacaters in a string, and replaces them with NULL */
static inline void
chomp (char *s)
{
  while (*s != 0) {
      if (*s=='\n' || *s=='\r') {
          *s = 0 ;
          return;
      }
      ++s;
  }
  return ;
}


/*
   Parses a numeric value (with optional suffix) from a string.
   Returns a long double value.

   If there's an error converting the string to value - exits with
   an error.

   If there are any trailing characters after the number
   (besides a valid suffix) - exits with an error.
*/
static enum simple_strtod_error
parse_human_number (const char* str, long double /*output*/ *value)
{
  char *ptr=NULL;

  enum simple_strtod_error e =
    simple_strtod_human (str, &ptr, value, scale_from);
  if (e!= SSE_OK && e!=SSE_OK_PRECISION_LOSS)
    simple_strtod_fatal (e,str);

  if ( ptr && *ptr!='\0' )
       error (EXIT_FAILURE,0,_("invalid suffix in input '%s': '%s'"),
              str, ptr);
  return e;
}


/*
   Prints the given number, using the requested representation.
   The number is printed to STDOUT,
   with padding and alignment.
*/
static void
print_number (const long double val)
{
  /* Generate Output */
  char buf[128];

  /* Can't reliably print too-large values without auto-scaling */
  unsigned int x;
  expld (val,10,&x);
  if (scale_to==scale_none && x>MAX_UNSCALED_DIGITS)
    error (EXIT_FAILURE,0,_("value too large to be printed: '%Lg'" \
                             " (consider using --to)"), val);
  if (x>MAX_ACCEPTABLE_DIGITS-1)
    error (EXIT_FAILURE,0,_("value too large to be printed: '%Lg'" \
                             " (cannot handle values > 999Y)"), val);

  char *s = double_to_human (val, buf, sizeof (buf),
                             scale_to, grouping, _round);

  if (dev_debug)
    fprintf (stderr,"Formatting output:\n  value: %Lf\n  humanized: '%s'\n",
            val,s);

  if (padding_width && strlen (s)<padding_width)
    {
      size_t w = padding_width;
      mbsalign (s,padding_buffer,padding_buffer_size,&w,
               padding_alignment,  MBA_UNIBYTE_ONLY );

      if (dev_debug)
        fprintf (stderr,"  After padding: '%s'\n", padding_buffer);

      fputs (padding_buffer,stdout);
    }
  else
    {
      fputs (s,stdout);
    }
}

/*
   Converts the given number to the requested representation,
   and handles automatic suffix addition.
 */
static void
process_suffixed_number (char* text)
{
  if (suffix && strlen (text)>strlen (suffix))
    {
      char *possible_suffix = text+strlen (text)-strlen (suffix);

      if (STREQ (suffix,possible_suffix))
        {
          /* trim suffix, ONLY if it's at the end of the text */
          *possible_suffix='\0';
          if (dev_debug)
            fprintf (stderr,"Trimming suffix '%s'\n", suffix);
        }
      else
        {
          if (dev_debug)
            fprintf (stderr,"No valid suffix found\n");
        }
    }

  /* Skip white space - always*/
  char *p = text;
  while ( *p && isblank (*p) )
    ++p;
  const unsigned int skip_count = text-p;

  /* setup auto-padding */
  if (auto_padding)
    {
      if (skip_count>0 || field>1)
        {
          padding_width = strlen (text);
          setup_padding_buffer (padding_width);
        }
      else
       {
         padding_width = 0;
       }
      if (dev_debug)
        fprintf (stderr, "Setting Auto-Padding to %ld characters\n",
                padding_width);
    }

  long double val=0;
  enum simple_strtod_error e = parse_human_number (p,&val);
  if (e==SSE_OK_PRECISION_LOSS && debug)
    error (0,0,_("large input value '%s': possible precision loss"),p);

  if (from_unit_size!=1 || to_unit_size != 1)
          val = (val * from_unit_size) / to_unit_size;

  print_number (val);

  if (suffix)
    fputs (suffix,stdout);
}

/*
   Skips the requested number of fields in the input string.

   Returns a pointer to the *delimiter* of the requested field,
   or a pointer to NULL (if reached the end of the string)
 */
static inline char*
__attribute ((pure))
skip_fields (char* buf, int fields)
{
  char* ptr = buf;
  if (delimiter != DELIMITER_DEFAULT)
    while (*ptr && fields--)
      {
        while (*ptr && *ptr == delimiter)
          ++ptr;
        while (*ptr && *ptr != delimiter)
          ++ptr;
      }
  else
    while (*ptr && fields--)
      {
        while (*ptr && isblank (*ptr))
          ++ptr;
        while (*ptr && !isblank (*ptr))
          ++ptr;
      }
  return ptr;
}

/*
   Parses a delimited string, and extracts the requested field.
   NOTE: the input buffer is modified.

   Output:
     _prefix, _data, _suffix will point to the relevant positions
     in the input string, or be NULL if not such part exist.
 */
static void
extract_fields (char* line, int _field,
                char** /*out*/ _prefix,
                char** /*out*/ _data,
                char** /*out*/ _suffix)
{
  char* ptr = line ;
  *_prefix = NULL;
  *_data = NULL ;
  *_suffix = NULL ;

  if (dev_debug)
    fprintf (stderr,"Extracting Fields:\n  input: '%s'\n  field: %d\n",
        line, _field);

  if (field>1)
    {
      /* skip the requested number of fields */
      *_prefix = line;
      ptr = skip_fields (line, field-1);
      if (*ptr=='\0')
        {
          /* not enough fields in the input - print warning? */
          if (dev_debug)
            fprintf (stderr,"  TOO FEW FIELDS!\n  prefix: '%s'\n",
                *_prefix);
          return ;
        }

      *ptr='\0';
      ++ptr;
    }

  *_data = ptr;
  *_suffix = skip_fields (*_data,1);
  if (**_suffix)
    {
      /* there is a suffix (i.e. the field is not the last on the line),
         so null-terminate the _data before it */
      **_suffix = '\0';
      ++(*_suffix);
    }
  else
    *_suffix=NULL;

  if (dev_debug)
    fprintf (stderr,"  prefix: '%s'\n  number: '%s'\n  suffix: '%s'\n",
        *_prefix,*_data,*_suffix);
}


/*
   Converts a number in a given line of text.
 */
static void
process_line (char* line)
{
  char *pre,*num,*suf;
  extract_fields (line,field,&pre,&num,&suf);
  if (pre)
      fputs (pre,stdout);

  if (pre && num)
      fputc ((delimiter==DELIMITER_DEFAULT)?' ':delimiter, stdout);

  if (num)
    process_suffixed_number (num);
  if (!num && debug)
    error (0,0,_("input line is too short, " \
                 "no numbers found to convert in field %ld"), field);

  if (suf)
    {
      fputc ((delimiter==DELIMITER_DEFAULT)?' ':delimiter, stdout);
      fputs (suf,stdout);
    }

  fputs ("\n",stdout);
}

int
main (int argc, char **argv)
{
  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  decimal_point = nl_langinfo (RADIXCHAR);
  if (decimal_point==NULL || strlen (decimal_point)==0)
    decimal_point = ".";
  decimal_point_length=strlen (decimal_point);

  atexit (close_stdout);

  while (true)
    {
      int c = getopt_long (argc, argv, "d:", longopts, NULL);

      if (c == -1)
        break;

      switch (c)
        {
        case FROM_OPTION:
          scale_from = XARGMATCH ("--from", optarg,
                                  scale_from_args, scale_from_types);
          break;

        case FROM_UNIT_OPTION:
          from_unit_size = string_to_integer (optarg);
          break;

        case TO_OPTION:
          scale_to = XARGMATCH ("--to", optarg, scale_to_args, scale_to_types);
          break;

        case TO_UNIT_OPTION:
          to_unit_size = string_to_integer (optarg);
          break;

        case ROUND_OPTION:
          _round = XARGMATCH ("--round", optarg, round_args, round_types);
          break;

        case GROUPING_OPTION:
          grouping = 1;
          break;

        case PADDING_OPTION:
          if (xstrtol (optarg,NULL,10, &padding_width,"")!=LONGINT_OK
              || padding_width==0)
            error (EXIT_FAILURE,0,_("invalid padding value '%s'"),optarg);
          if (padding_width<0)
            {
              padding_alignment = MBS_ALIGN_LEFT;
              padding_width = -padding_width;
            }
          setup_padding_buffer (padding_width);
          break;

        case FIELD_OPTION:
          if (xstrtol (optarg,NULL,10, &field,"")!=LONGINT_OK
                || field<=0)
              error (EXIT_FAILURE,0,_("invalid field value '%s'"),optarg);
          break;

        case 'd':
          if (strlen (optarg)!=1)
            error (EXIT_FAILURE,0,_("delimiter must be exactly one character"));
          delimiter=optarg[0];
          break;

        case SUFFIX_OPTION:
          suffix = optarg;
          break;

        case DEBUG_OPTION:
          debug=1;
          break;

        case DEV_DEBUG_OPTION:
          dev_debug=1;
          debug=1;
          break;

        case HEADER_OPTION:
          if (optarg)
            {
              if (xstrtoumax (optarg,NULL,10,&header,"")!=LONGINT_OK
                  || header==0)
                error (EXIT_FAILURE,0,_("invalid header value '%s'"), optarg);
            }
          else
            {
              header=1;
            }
          break;

        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

        default:
          usage (EXIT_FAILURE);
        }
    }

  auto_padding = (padding_width==0 && delimiter==DELIMITER_DEFAULT);

  if (grouping)
    {
      if (scale_to!=scale_none)
        error (EXIT_FAILURE,0,_("--grouping cannot be combined with --to"));
      if (debug && (strlen (nl_langinfo (THOUSEP))==0))
            error (0,0,_("--grouping has no effect in this locale"));
    }

  /* Warn about no-op */
  if (debug && scale_from==scale_none && scale_to==scale_none
      && !grouping && (padding_width==0))
      error (0,0,_("no conversion option specified"));

  if (argc > optind)
    {
      if (debug && header)
        error (0,0,_("--header ignored with command-line input"));

      for (; optind < argc; optind++)
        process_line (argv[optind]);
    }
  else
    {
      char buf[BUFFER_SIZE + 1];

      /* FIXME: check if the line is too long,
                'buf' doesn't contain a CRLF, and so this isn't
                the entire line */
      while ( header-- && fgets (buf,BUFFER_SIZE,stdin) != NULL )
          fputs (buf,stdout);

      while ( fgets (buf,BUFFER_SIZE,stdin) != NULL )
        {
          chomp (buf);
          process_line (buf);
        }

      if (ferror (stdin))
          error (0,errno,_("error reading input"));
    }

  free (padding_buffer);

  exit (EXIT_SUCCESS);
}
