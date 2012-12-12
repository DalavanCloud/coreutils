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
#include "human.h"

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

static enum scale_type scale_from=scale_none;
static enum scale_type scale_to=scale_none;
static enum round_type _round=round_ceiling;
static const char *suffix = NULL;
static uintmax_t from_unit_size=1;
static uintmax_t to_unit_size=1;
static int human_print_options=0; /* see enum in 'human.c' */
static int grouping=0;
static char *padding_buffer=NULL;
static long int padding_width=0;
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

#define __strtol_t uintmax_t

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
    case 'k': /* special case: lower case 'k' is acceptable */
    case 'K': /* kilo/kibi */
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

static long double
powerld (long double base, unsigned int x)
{
  long double result=base;
  if (x==0)
    return 1;
  /* TODO: check for overflow, inf? */
  while (--x)
    result *= base;
  return result;
}

enum human_strtod_error
{
  OK=0,
  NO_DIGITS_FOUND,
  INTEGRAL_OVERFLOW,
  INVALID_NUMBER,
  FRACTION_FORBIDDEN_WITHOUT_SCALING,
  FRACTION_NO_DIGITS_FOUND,
  FRACTION_OVERFLOW,
  FRACTION_INVALID_NUMBER,
  FRACTION_REQUIRES_SUFFIX,
  VALID_BUT_FORBIDDEN_SUFFIX,
  INVALID_SUFFIX,

};

static enum human_strtod_error
human_strtod (const char *input_str,
              char* /*output*/ *ptr, /*required, unlike in stdtod */
              long double /*output*/ *value, /* required */
              enum scale_type allowed_scaling)
{
  char *pi, *pf;
  uintmax_t integral,fraction=0;
  int have_fractions=0;
  int power=0;
  /* 'scale_auto' is checked below */
  int scale_base=(allowed_scaling==scale_IEC)?1024:1000;
  const int base = 10 ;

  const char *decimal_point = nl_langinfo (RADIXCHAR);
  if (decimal_point==NULL || strlen (decimal_point)==0)
    decimal_point = ".";
  const int decimal_point_length=strlen (decimal_point);

  if (dev_debug)
    fprintf (stderr,"string-to-double:\n  input string: '%s'\n  " \
            "locale decimal-point: '%s'\n", input_str,decimal_point);

  errno = 0;
  /* TODO: accept locale'd grouped values for the integral part */
  integral = strtoumax (input_str, &pi, base);
  if (pi==input_str)
    return NO_DIGITS_FOUND;
  if (errno==ERANGE)
    return INTEGRAL_OVERFLOW;
  if (errno!=0)
    return INVALID_NUMBER; /* should never happen? */

  *value = integral;

  *ptr = pi;

  if (dev_debug)
    fprintf (stderr,"  integral-part: %lu\n", integral);

  /* optional decimal point + fraction */
  if (STREQ_LEN (pi,decimal_point,decimal_point_length))
    {
      if (allowed_scaling==scale_none)
        return FRACTION_FORBIDDEN_WITHOUT_SCALING;

      errno = 0 ;
      pi += decimal_point_length;
      if (!isdigit (*pi))
        return FRACTION_NO_DIGITS_FOUND;
      fraction = strtoumax (pi, &pf, base);
      if (pf==pi)
        return FRACTION_NO_DIGITS_FOUND;
      if (errno==ERANGE)
        return FRACTION_OVERFLOW;
      if (errno!=0)
        return FRACTION_INVALID_NUMBER;

      int exponent = pf-pi;
      long double f = ((long double)fraction)/powerld (10,exponent);

      have_fractions=1;

      if (dev_debug)
        fprintf (stderr,"  fraction-part: %lu (exponent=%d)\n" \
                       "  fraction-value: %Lf\n",
              fraction,exponent,f);

      *value += f;

      if (dev_debug)
        fprintf (stderr,"  combined-value: %Lf\n", *value);

      *ptr= pf;
    }

  /* no suffix / trailing characters */
  if (**ptr=='\0')
    {
      if (have_fractions)
        return FRACTION_REQUIRES_SUFFIX;

      if (dev_debug)
        fprintf (stderr,"  no fraction,suffix detected\n" \
                       "  returning value: %Lf\n", *value);

      return OK;
    }

  /* process suffix */
  if (!valid_suffix (**ptr))
    return INVALID_SUFFIX;

  if (allowed_scaling==scale_none)
    return VALID_BUT_FORBIDDEN_SUFFIX;

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

  (*value) = (*value) * multiplier;

  if (dev_debug)
    fprintf (stderr, "  returning value: %Lf\n", *value);

  return OK;
}


static void
human_strtod_fatal (enum human_strtod_error err,
                     char const *input_str)
{
  char const *msgid;

  switch (err)
    {
    default:
      abort ();

    case NO_DIGITS_FOUND:
      msgid = N_("no digits found: '%s'");
      break;

    case INTEGRAL_OVERFLOW:
      msgid = N_("value too large to be converted: '%s'");
      break;

    case INVALID_NUMBER:
      msgid = N_("invalid number: '%s'");
      break;

    case FRACTION_FORBIDDEN_WITHOUT_SCALING:
      msgid = N_("cannot process decimal-point value without scaling: '%s'" \
                 " (consider using --from)");
      break;

    case FRACTION_NO_DIGITS_FOUND:
      msgid = N_("no digits in fraction: '%s'");
      break;

    case FRACTION_OVERFLOW:
      msgid = N_("fraction value too large to be converted: '%s'");
      break;

    case FRACTION_INVALID_NUMBER:
      msgid = N_("invalid fraction number: '%s'");
      break;

    case FRACTION_REQUIRES_SUFFIX:
      msgid = N_("decimal-point values require a suffix (e.g. K/M/G/T): '%s'");
      break;

    case VALID_BUT_FORBIDDEN_SUFFIX:
      msgid = N_("rejecting suffix in input: '%s' (consider using --from)");
      break;

    case INVALID_SUFFIX:
      msgid = N_("invalid suffix in input: '%s'");
      break;

    }

  error (EXIT_FAILURE,0,gettext (msgid), input_str);
}


/* Convert a string of decimal digits, N_STRING, with an optional suffinx
   to an integral value.  Upon successful conversion,
   return that value.  If it cannot be converted, give a diagnostic and exit.
*/
static uintmax_t
string_to_integer (const char *n_string)
{
  strtol_error s_err;
  uintmax_t n;

  s_err = xstrtoumax (n_string, NULL, 10, &n, "bkKmMGTPEZY0");

  if (s_err == LONGINT_OVERFLOW)
    {
      error (EXIT_FAILURE, 0,
             _("%s: unit size is so large that it is not representable"),
                n_string);
    }

  if (s_err != LONGINT_OK)
    {
      error (EXIT_FAILURE, 0, _("%s: invalid unit size"), n_string);
    }
  return n;
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
                  Default is right-aligned. Negative N will left-align.\n\
                  Note: if N is too small, the output will be truncated,\n\
                  and a warning will be printed to stderr.\n\
  --grouping      group digits together (e.g. 1,000,000).\n\
                  Uses the locale-defined grouping (i.e. have no effect\n\
                  in C/POSIX locales).\n\
  --field N       replace the number in input field N (default is 1)\n\
  -d, --delimiter=X  use X instead of whitespace for field delimiter\n\
  --debug         print warning about possible errors.\n\
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
 SI:\n\
      1K = 1000\n\
      1G  = 1000000\n\
      ...\n\
 IEC:\n\
      1K = 1024\n\
      1G = 1048576\n\
      ...\n\
\n\
"), stdout);

      printf (_("\
\n\
Examples:\n\
  %s --to=SI 1000           -> \"1K\"\n\
  echo 1K | %s --from=SI    -> \"1000\"\n\
  echo 1K | %s --from=IEC   -> \"1024\"\n\
"),
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
   Parsesa numeric value from a string.
   Returns the integer value.

   On error, exits with a proper error message.

   The input string must not contain any extra characters
   (except valid suffixes if 'from' scaling is enabled).
*/
static __strtol_t
parse_number (const char* str)
{
  char *ptr=NULL;
  __strtol_t val=0;

  long double dval=0;
  enum human_strtod_error err = human_strtod (str, &ptr, &dval, scale_from);
  if (err != OK)
    human_strtod_fatal (err,str);
  val=(__strtol_t)dval; /* NOTE: Possible data loss here, double->uintmax_t */

  if (dev_debug)
    fprintf (stderr,"Parsing number:\n  input string: '%s'\n  " \
            "remaining characters: '%s'\n  numeric value: %zu\n",
            str,ptr,val);

  if ( ptr && *ptr!='\0' )
       error (EXIT_FAILURE,0,_("invalid suffix in input '%s': '%s'"),
              str, ptr);
  return val;
}


/*
   Prints the given number, using the requested representation.
   The number is printed to STDOUT,
   with padding and alignment.
*/
static void
print_number (const __strtol_t val)
{
  /* Generate Output */
  char buf[LONGEST_HUMAN_READABLE + 1];
  char *s = human_readable (val, buf, human_print_options,
                           from_unit_size,to_unit_size);

  if (dev_debug)
    fprintf (stderr,"Formatting output:\n  value: %zu\n  humanized: '%s'\n",
            val,s);

  if (padding_width)
    {
      size_t w = padding_width;
      mbsalign (s,padding_buffer,padding_width+1,&w,
               padding_alignment,  MBA_UNIBYTE_ONLY );
      if (strlen (padding_buffer)<strlen (s))
        error (0,0,_("value '%s' truncated due to padding, possible data loss"),
              s);

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

  const __strtol_t val = parse_number (text);
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
          padding_buffer = malloc (padding_width+1);
          if (!padding_buffer)
            error (EXIT_FAILURE,0, _("out of memory (requested %ld bytes)"),
                    padding_width+1);
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

  switch (_round)
    {
    case round_ceiling:
      human_print_options |= human_ceiling;
      break;
    case round_floor:
      human_print_options |= human_floor;
      break;
    case round_nearest:
      human_print_options |= human_round_to_nearest;
      break;
    }

  switch (scale_to)
    {
    case scale_SI:
      human_print_options |= human_autoscale | human_SI;
      break;
    case scale_IEC:
      human_print_options |= human_autoscale | human_base_1024 | human_SI ;
      break;

    case scale_auto:
      /* should never happen. assert? */

    case scale_none:
      break;
    }

  if (grouping)
    {
      if (scale_to!=scale_none)
        error (EXIT_FAILURE,0,_("--grouping cannot be combined with --to"));
      human_print_options |= human_group_digits;
      if (debug && (strlen (nl_langinfo (THOUSEP))==0))
            error (0,0,_("--grouping has not effect in this locale"));
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
