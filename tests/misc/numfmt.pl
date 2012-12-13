#!/usr/bin/perl
# Basic tests for "numfmt".

# Copyright (C) 2012 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

use strict;

(my $program_name = $0) =~ s|.*/||;
my $prog = 'numfmt';

# TODO: add localization tests with "grouping"
# Turn off localization of executable's output.
@ENV{qw(LANGUAGE LANG LC_ALL)} = ('C') x 3;

my $locale = $ENV{LOCALE_FR_UTF8};
! defined $locale || $locale eq 'none'
  and $locale = 'C';

my @Tests =
    (
     ['1', '1234',             {OUT => "1234"}],
     ['2', '--from=si 1K',     {OUT => "1000"}],
     ['3', '--from=iec 1K',    {OUT => "1024"}],
     ['4', '--from=auto 1K',   {OUT => "1000"}],
     ['5', '--from=auto 1Ki',  {OUT => "1024"}],

     ['6', {IN_PIPE => "1234"},            {OUT => "1234"}],
     ['7', '--from=si', {IN_PIPE => "2K"}, {OUT => "2000"}],

     ['8',  '--to=si 2000',                   {OUT => "2.0K"}],
     ['9',  '--to=si 2001',                   {OUT => "2.1K"}],
     ['10', '--to=si 1999',                   {OUT => "2.0K"}],
     ['11', '--to=si --round=floor   2001',   {OUT => "2.0K"}],
     ['12', '--to=si --round=floor   1999',   {OUT => "1.9K"}],
     ['13', '--to=si --round=ceiling 1901',   {OUT => "2.0K"}],
     ['14', '--to=si --round=floor   1901',   {OUT => "1.9K"}],
     ['15', '--to=si --round=nearest 1901',   {OUT => "1.9K"}],
     ['16', '--to=si --round=nearest 1945',   {OUT => "1.9K"}],
     ['17', '--to=si --round=nearest 1955',   {OUT => "2.0K"}],

     ['18',  '--to=iec 2048',                  {OUT => "2.0K"}],
     ['19',  '--to=iec 2049',                  {OUT => "2.1K"}],
     ['20', '--to=iec 2047',                   {OUT => "2.0K"}],
     ['21', '--to=iec --round=floor   2049',   {OUT => "2.0K"}],
     ['22', '--to=iec --round=floor   2047',   {OUT => "1.9K"}],
     ['23', '--to=iec --round=ceiling 2040',   {OUT => "2.0K"}],
     ['24', '--to=iec --round=floor   2040',   {OUT => "1.9K"}],
     ['25', '--to=iec --round=nearest 1996',   {OUT => "1.9K"}],
     ['26', '--to=iec --round=nearest 1997',   {OUT => "2.0K"}],

     ['unit-1', '--from-unit=512 4',   {OUT => "2048"}],
     ['unit-2', '--to-unit=512 2048',   {OUT => "4"}],

     ['unit-3', '--from-unit=512 --from=si 4M',   {OUT => "2048000000"}],
     ['unit-4', '--from-unit=512 --from=iec --to=iec 4M',   {OUT => "2.0G"}],

     # Test Suffix logic
     ['suf-1', '4000',    {OUT=>'4000'}],
     ['suf-2', '4Q',
             {ERR => "$prog: invalid suffix in input: '4Q'\n"},
             {EXIT => '1'}],
     ['suf-2.1', '4M',
             {ERR => "$prog: rejecting suffix " .
             "in input: '4M' (consider using --from)\n"},
             {EXIT => '1'}],
     ['suf-3', '--from=si 4M',  {OUT=>'4000000'}],
     ['suf-4', '--from=si 4Q',
             {ERR => "$prog: invalid suffix in input: '4Q'\n"},
             {EXIT => '1'}],
     ['suf-5', '--from=si 4MQ',
             {ERR => "$prog: invalid suffix in input '4MQ': 'Q'\n"},
             {EXIT => '1'}],

     ['suf-6', '--from=iec 4M',  {OUT=>'4194304'}],
     ['suf-7', '--from=auto 4M',  {OUT=>'4000000'}],
     ['suf-8', '--from=auto 4Mi',  {OUT=>'4194304'}],
     ['suf-9', '--from=auto 4MiQ',
             {ERR => "$prog: invalid suffix in input '4MiQ': 'Q'\n"},
             {EXIT => '1'}],
     ['suf-10', '--from=auto 4QiQ',
             {ERR => "$prog: invalid suffix in input: '4QiQ'\n"},
             {EXIT => '1'}],

     # characters after a white space are OK - printed as-is
     ['suf-11', '"4 M"',     {OUT=>'4 M'}],

     # Custom suffix
     ['suf-12', '--suffix=Foo 70Foo',               {OUT=>'70Foo'}],
     ['suf-13', '--suffix=Foo 70',                  {OUT=>'70Foo'}],
     ['suf-14', '--suffix=Foo --from=si 70K',       {OUT=>'70000Foo'}],
     ['suf-15', '--suffix=Foo --from=si 70KFoo',    {OUT=>'70000Foo'}],
     ['suf-16', '--suffix=Foo --to=si   7000Foo',    {OUT=>'7.0KFoo'}],
     ['suf-17', '--suffix=Foo --to=si   7000Bar',
              {ERR => "$prog: invalid suffix in input: '7000Bar'\n"},
              {EXIT => '1'}],
     ['suf-18', '--suffix=Foo --to=si   7000FooF',
              {ERR => "$prog: invalid suffix in input: '7000FooF'\n"},
              {EXIT => '1'}],

     ## GROUPING

     # "C" locale - no grouping (locale-specific tests, below)
     ['grp-1', '--from=si --grouping 7M',   {OUT=>'7000000'}],
     ['grp-2', '--from=si --to=si --grouping 7M',
              {ERR => "$prog: --grouping cannot be combined with --to\n"},
              {EXIT => '1'}],


     ## Padding
     ['pad-1', '--padding=10 5',             {OUT=>'         5'}],
     ['pad-2', '--padding=-10 5',            {OUT=>'5         '}],
     ['pad-3', '--padding=A 5',
             {ERR => "$prog: invalid padding value 'A'\n"},
             {EXIT => '1'}],
     ['pad-4', '--padding=10 --to=si 50000',             {OUT=>'       50K'}],
     ['pad-5', '--padding=-10 --to=si 50000',            {OUT=>'50K       '}],

     # padding causing truncation, data loss
     ['pad-6', '--padding=2 --to=si 1000', {OUT=>'1.'},
             {ERR => "$prog: value '1.0K' truncated due to padding, " .
                "possible data loss\n"}],


     # Padding + suffix
     ['pad-7', '--padding=10 --suffix=foo --to=si 50000',
             {OUT=>'       50Kfoo'}],
     ['pad-8', '--padding=-10 --suffix=foo --to=si 50000',
             {OUT=>'50K       foo'}],


     # Delimiters
     ['delim-1', '--delimiter=: --from=auto 40M:',   {OUT=>'40000000:'}],
     ['delim-2', '--delimiter="" --from=auto 40M:',
             {ERR => "$prog: delimiter must be exactly one character\n"},
             {EXIT => '1'}],
     ['delim-3', '--delimiter=" " --from=auto "40M Foo"',{OUT=>'40000000 Foo'}],
     ['delim-4', '--delimiter=: --from=auto 40M:60M',  {OUT=>'40000000:60M'}],

     #Fields
     ['field-1', '--field A',
             {ERR => "$prog: invalid field value 'A'\n"},
             {EXIT => '1'}],
     ['field-2', '--field 2 --from=auto "Hello 40M World 90G"',
             {OUT=>'Hello 40000000 World 90G'}],
     ['field-3', '--field 3 --from=auto "Hello 40M World 90G"',
             {OUT=>"Hello 40M "},
             {ERR=>"$prog: invalid number: 'World'\n"},
             {EXIT => 1},],
     # Last field - no text after number
     ['field-4', '--field 4 --from=auto "Hello 40M World 90G"',
             {OUT=>"Hello 40M World 90000000000"}],
     # Last field - a delimiter after the number
     ['field-5', '--field 4 --from=auto "Hello 40M World 90G "',
             {OUT=>"Hello 40M World 90000000000 "}],

     # Mix Fields + Delimiters
     ['field-6', '--delimiter=: --field 2 --from=auto "Hello:40M:World:90G"',
             {OUT=>"Hello:40000000:World:90G"}],

     # not enough fields - silently ignored
     ['field-7', '--field 3 --to=si "Hello World"', {OUT=>"Hello World"}],
     ['field-8', '--field 3 --debug --to=si "Hello World"',
             {OUT=>"Hello World"},
             {ERR=>"$prog: input line is too short, no numbers found " .
                   "to convert in field 3\n"}],


     # Corner-cases:
     # weird mix of identical suffix,delimiters
     # The priority is:
     #   1. delimiters (and fields) are parsed (in process_line()
     #   2. optional custom suffix is removed (in process_suffixed_number())
     #   3. Remaining suffixes must be valid SI/IEC (in human_xstrtol())

     # custom suffix comes BEFORE SI/IEC suffix,
     #   so these are 40 of "M", not 40,000,000.
     ['mix-1', '--suffix=M --from=si 40M',     {OUT=>"40M"}],

     # These are fourty-million Ms .
     ['mix-2', '--suffix=M --from=si 40MM',     {OUT=>"40000000M"}],

     ['mix-3', '--suffix=M --from=auto 40MM',     {OUT=>"40000000M"}],
     ['mix-4', '--suffix=M --from=auto 40MiM',     {OUT=>"41943040M"}],
     ['mix-5', '--suffix=M --to=si --from=si 4MM',     {OUT=>"4.0MM"}],

     # This might be confusing to the user, but it's legit:
     # The M in the output is the custom suffix, not Mega.
     ['mix-6', '--suffix=M 40',     {OUT=>"40M"}],
     ['mix-7', '--suffix=M 4000000',     {OUT=>"4000000M"}],
     ['mix-8', '--suffix=M --to=si 4000000',     {OUT=>"4.0MM"}],

     # The output 'M' is the custom suffix.
     ['mix-10', '--delimiter=M --suffix=M 40',     {OUT=>"40M"}],

     # The INPUT 'M' is a delimiter (delimiters are top priority)
     # The output contains one M for custom suffix, and one 'M' delimiter.
     ['mix-11', '--delimiter=M --suffix=M 40M',     {OUT=>"40MM"}],

     # Same as above, the "M" is NOT treated as a mega SI prefix,
     ['mix-12', '--delimiter=M --from=si --suffix=M 40M',     {OUT=>"40MM"}],

     # The 'M' is treated as a delimiter, and so the input value is '4000'
     ['mix-13', '--delimiter=M --to=si --from=auto 4000M5000M9000',
             {OUT=>"4.0KM5000M9000"}],
     # 'M' is the delimiter, so the second input field is '5000'
     ['mix-14', '--delimiter=M --field 2 --from=auto --to=si 4000M5000M9000',
             {OUT=>"4000M5.0KM9000"}],



     ## Header testing

     # header - silently ignored with command line parameters
     ['header-1', '--header --to=iec 4096', {OUT=>"4.0K"}],

     # header warning with --debug
     ['header-2', '--debug --header --to=iec 4096', {OUT=>"4.0K"},
             {ERR=>"$prog: --header ignored with command-line input\n"}],

     ['header-3', '--header=A',
             {ERR=>"$prog: invalid header value 'A'\n"},
             {EXIT => 1},],
     ['header-4', '--header=0',
             {ERR=>"$prog: invalid header value '0'\n"},
             {EXIT => 1},],
     ['header-5', '--header=-6',
             {ERR=>"$prog: invalid header value '-6'\n"},
             {EXIT => 1},],
     ['header-6', '--debug --header --to=iec',
             {IN_PIPE=>"size\n5000\n90000\n"},
             {OUT=>"size\n4.9K\n88K"}],
     ['header-7', '--debug --header=3 --to=iec',
             {IN_PIPE=>"hello\nworld\nsize\n5000\n90000\n"},
             {OUT=>"hello\nworld\nsize\n4.9K\n88K"}],


     ## human_strtod testing

     # NO_DIGITS_FOUND
     ['strtod-1', '--from=si "foo"',
             {ERR=>"$prog: invalid number: 'foo'\n"},
             {EXIT=> 1}],
     ['strtod-2', '--from=si ""',
             {ERR=>"$prog: invalid number: ''\n"},
             {EXIT=> 1}],

     # INTEGRAL_OVERFLOW
     ['strtod-3', '--from=si "1234567890123456789012345678901234567890'.
                  '1234567890123456789012345678901234567890"',
             {ERR=>"$prog: value too large to be converted: '" .
                     "1234567890123456789012345678901234567890" .
                     "1234567890123456789012345678901234567890'\n",
                     },
             {EXIT=> 1}],

     #FRACTION_FORBIDDEN_WITHOUT_SCALING
     ['strtod-4', '12.2',
             {ERR=>"$prog: cannot process decimal-point value without " .
                     "scaling: '12.2' (consider using --from)\n"},
             {EXIT=>1}],

     # FRACTION_NO_DIGITS_FOUND
     ['strtod-5', '--from=si 12.',
             {ERR=>"$prog: invalid number: '12.'\n"},
             {EXIT=>1}],
     ['strtod-6', '--from=si 12.K',
             {ERR=>"$prog: invalid number: '12.K'\n"},
             {EXIT=>1}],

     # whitespace is not allowed after decimal-point
     ['strtod-6.1', '--from=si --delimiter=, "12.  2"',
             {ERR=>"$prog: invalid number: '12.  2'\n"},
             {EXIT=>1}],

     # FRACTION_OVERFLOW
     ['strtod-7', '--from=si "12.1234567890123456789012345678901234567890'.
                  '1234567890123456789012345678901234567890"',
             {ERR=>"$prog: value too large to be converted: '" .
                     "12.1234567890123456789012345678901234567890" .
                     "1234567890123456789012345678901234567890'\n",
                     },
             {EXIT=> 1}],

     # FRACTION_REQUIRES_SUFFIX
     ['strtod-8', '--from=si 12.2',
             {ERR=>"$prog: decimal-point values require a suffix" .
                    " (e.g. K/M/G/T): '12.2'\n"},
             {EXIT=>1}],

     # INVALID_SUFFIX
     ['strtod-9', '--from=si 12.2Q',
             {ERR=>"$prog: invalid suffix in input: '12.2Q'\n"},
             {EXIT=>1}],

     # VALID_BUT_FORBIDDEN_SUFFIX
     ['strtod-10', '12M',
             {ERR => "$prog: rejecting suffix " .
                     "in input: '12M' (consider using --from)\n"},
             {EXIT=>1}],

     #
     # Test double_to_human()
     #

     # 1K and smaller
     ['dbl-to-human-1','--to=si 800',  {OUT=>"800"}],
     ['dbl-to-human-2','--to=si 0',  {OUT=>"0"}],
     ['dbl-to-human-2.1','--to=si 999',  {OUT=>"999"}],
     ['dbl-to-human-2.2','--to=si 1000',  {OUT=>"1.0K"}],
     #NOTE: the following are consistent with "ls -lh" output
     ['dbl-to-human-2.3','--to=iec 999',  {OUT=>"999"}],
     ['dbl-to-human-2.4','--to=iec 1023',  {OUT=>"1023"}],
     ['dbl-to-human-2.5','--to=iec 1024',  {OUT=>"1.0K"}],
     ['dbl-to-human-2.6','--to=iec 1025',  {OUT=>"1.1K"}],

     # values resulting in "N.Nx" output
     ['dbl-to-human-3','--to=si 8000', {OUT=>"8.0K"}],
     ['dbl-to-human-3.1','--to=si 8001', {OUT=>"8.1K"}],
     ['dbl-to-human-4','--to=si --round=floor 8001', {OUT=>"8.0K"}],

     ['dbl-to-human-5','--to=si --round=floor 3500', {OUT=>"3.5K"}],
     ['dbl-to-human-6','--to=si --round=nearest 3500', {OUT=>"3.5K"}],
     ['dbl-to-human-7','--to=si --round=ceiling 3500', {OUT=>"3.5K"}],

     ['dbl-to-human-8','--to=si --round=floor    3501', {OUT=>"3.5K"}],
     ['dbl-to-human-9','--to=si --round=nearest  3501', {OUT=>"3.5K"}],
     ['dbl-to-human-10','--to=si --round=ceiling 3501', {OUT=>"3.6K"}],

     ['dbl-to-human-11','--to=si --round=nearest  3550', {OUT=>"3.6K"}],
     ['dbl-to-human-12','--to=si --from=si 999.89K', {OUT=>"1.0M"}],
     ['dbl-to-human-13','--to=si --from=si 9.9K', {OUT=>"9.9K"}],
     ['dbl-to-human-14','--to=si 9900', {OUT=>"9.9K"}],
     ['dbl-to-human-15','--to=iec --from=si 3.3K', {OUT=>"3.3K"}],
     ['dbl-to-human-16','--to=iec --round=floor --from=si 3.3K', {OUT=>"3.2K"}],

     # values resulting in 'NNx' output
     ['dbl-to-human-17','--to=si 9999', {OUT=>"10K"}],
     ['dbl-to-human-18','--to=si --round=floor 35000', {OUT=>"35K"}],
     ['dbl-to-human-19','--to=iec 35000', {OUT=>"35K"}],
     ['dbl-to-human-20','--to=iec --round=floor 35000', {OUT=>"34K"}],
     ['dbl-to-human-21','--to=iec 35000000', {OUT=>"34M"}],
     ['dbl-to-human-22','--to=iec --round=floor 35000000', {OUT=>"33M"}],
     ['dbl-to-human-23','--to=si  35000001', {OUT=>"36M"}],
     ['dbl-to-human-24','--to=si --from=si  9.99M', {OUT=>"10M"}],
     ['dbl-to-human-25','--to=si --from=iec 9.99M', {OUT=>"11M"}],
     ['dbl-to-human-25.1','--to=iec 99999', {OUT=>"98K"}],

     # values resulting in 'NNNx' output
     ['dbl-to-human-26','--to=si 999000000000', {OUT=>"999G"}],
     ['dbl-to-human-27','--to=iec 999000000000', {OUT=>"931G"}],
     ['dbl-to-human-28','--to=si 123600000000000', {OUT=>"124T"}],
     ['dbl-to-human-29','--to=si 998123', {OUT=>"999K"}],
     ['dbl-to-human-30','--to=si --round=nearest 998123', {OUT=>"998K"}],
     ['dbl-to-human-31','--to=si 99999', {OUT=>"100K"}],
     ['dbl-to-human-32','--to=iec 102399', {OUT=>"100K"}],


     # Large Values
     ['large-1','1000000000000000', {OUT=>"1000000000000000"}],
     ['large-2','1000000000000000000', {OUT=>"1000000000000000000"}],

     #FIX ME: can't read numbers so big - strtoumax fails
#     ['large-3','1000000000000000000000', {OUT=>"1000000000000000000000"}],

     ['large-4','--to=si 1000000000000000000', {OUT=>"1.0E"}],
     ['large-5','--from=si --to=si 2E', {OUT=>"2.0E"}],
     ['large-6','--from=si --to=si 3.4Z', {OUT=>"3.4Z"}],
     ['large-7','--from=si --to=si 80Y', {OUT=>"80Y"}],
     ['large-8','--from=si --to=si 9000Z', {OUT=>"9.0Y"}],
     ['large-9','--from=si 9.7Y', {OUT=>"9700000000000000000000000"}],

     #FIXME: can't handle such big numbres
#     ['large-10','--from=si 999Y', {OUT=>"9700000000000000000000000"}],

    );

my @Locale_Tests =
  (
     # Locale that supports grouping, but without '--grouping' parameter
     ['lcl-grp-1', '--from=si 7M',   {OUT=>"7000000"},
             {ENV=>"LC_ALL=$locale"}],

     # Locale with grouping
     ['lcl-grp-2', '--from=si --grouping 7M',   {OUT=>"7 000 000"},
             {ENV=>"LC_ALL=$locale"}],

     # Input with locale'd decimal-point
     ['lcl-stdtod-1', '--from=si 12,2K', {OUT=>"12200"},
             {ENV=>"LC_ALL=$locale"}],

     ['lcl-dbl-to-human-1', '--to=si 1100', {OUT=>"1,1K"},
             {ENV=>"LC_ALL=$locale"}],
  );
push @Tests, @Locale_Tests if $locale ne "C";

# Prepend the command line argument and append a newline to end
# of each expected 'OUT' string.
my $t;

Test:
foreach $t (@Tests)
  {
    # Don't fiddle with expected OUT string if there's a nonzero exit status.
    foreach my $e (@$t)
      {
        ref $e eq 'HASH' && exists $e->{EXIT} && $e->{EXIT}
          and next Test;
      }

    foreach my $e (@$t)
      {
        ref $e eq 'HASH' && exists $e->{OUT}
          and $e->{OUT} .= "\n"
      }
  }

my $save_temps = $ENV{SAVE_TEMPS};
my $verbose = $ENV{VERBOSE};

my $fail = run_tests ($program_name, $prog, \@Tests, $save_temps, $verbose);
exit $fail;
