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

my @Tests =
    (
     ['1', '1234',             {OUT => "1234"}],
     ['2', '--from=si 1K',     {OUT => "1000"}],
     ['3', '--from=iec 1K',    {OUT => "1024"}],
     ['4', '--from=auto 1K',   {OUT => "1000"}],
     ['5', '--from=auto 1Ki',  {OUT => "1024"}],

     ['6', {IN_PIPE => "1234"},            {OUT => "1234"}],
     ['7', '--from=si', {IN_PIPE => "2K"}, {OUT => "2000"}],

     #NOTE: "SI" outputs lower "k"?
     ['8',  '--to=si 2000',                   {OUT => "2.0k"}],
     ['9',  '--to=si 2001',                   {OUT => "2.1k"}],
     ['10', '--to=si 1999',                   {OUT => "2.0k"}],
     ['11', '--to=si --round=floor   2001',   {OUT => "2.0k"}],
     ['12', '--to=si --round=floor   1999',   {OUT => "1.9k"}],
     ['13', '--to=si --round=ceiling 1901',   {OUT => "2.0k"}],
     ['14', '--to=si --round=floor   1901',   {OUT => "1.9k"}],
     ['15', '--to=si --round=nearest 1901',   {OUT => "1.9k"}],
     ['16', '--to=si --round=nearest 1945',   {OUT => "1.9k"}],
     ['17', '--to=si --round=nearest 1955',   {OUT => "2.0k"}],

     ['18',  '--to=iec 2048',                  {OUT => "2.0K"}],
     ['19',  '--to=iec 2049',                  {OUT => "2.1K"}],
     ['20', '--to=iec 2047',                   {OUT => "2.0K"}],
     ['21', '--to=iec --round=floor   2049',   {OUT => "2.0K"}],
     ['22', '--to=iec --round=floor   2047',   {OUT => "1.9K"}],
     ['23', '--to=iec --round=ceiling 2040',   {OUT => "2.0K"}],
     ['24', '--to=iec --round=floor   2040',   {OUT => "1.9K"}],
     ['25', '--to=iec --round=nearest 1996',   {OUT => "1.9K"}],
     ['26', '--to=iec --round=nearest 1997',   {OUT => "2.0K"}],

     ['27', '--from-unit=512 4',   {OUT => "2048"}],
     ['28', '--to-unit=512 2048',   {OUT => "4"}],

     ['29', '--from-unit=512 --from=si 4M',   {OUT => "2048000000"}],
     ['30', '--from-unit=512 --from=iec --to=iec 4M',   {OUT => "2.0G"}],

     # Test Suffix logic
     ['suf-1', '4000',    {OUT=>'4000'}],
     ['suf-2', '4Q',
             {ERR => "$prog: invalid suffix in input '4Q': 'Q'\n"},
             {EXIT => '1'}],
     ['suf-2.1', '4M',
             {ERR => "$prog: not accepting suffix 'M' " .
             "in input '4M' (consider using --from)\n"},
             {EXIT => '1'}],
     ['suf-3', '--from=si 4M',  {OUT=>'4000000'}],
     ['suf-4', '--from=si 4Q',
             {ERR => "$prog: invalid suffix in input '4Q': 'Q'\n"},
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
             {ERR => "$prog: invalid suffix in input '4QiQ': 'QiQ'\n"},
             {EXIT => '1'}],

     # characters after a white space are OK - printed as-is
     ['suf-11', '"4 M"',     {OUT=>'4 M'}],

     # Custom suffix
     ['suf-12', '--suffix=Foo 70Foo',               {OUT=>'70Foo'}],
     ['suf-13', '--suffix=Foo 70',                  {OUT=>'70Foo'}],
     ['suf-14', '--suffix=Foo --from=si 70K',       {OUT=>'70000Foo'}],
     ['suf-15', '--suffix=Foo --from=si 70KFoo',    {OUT=>'70000Foo'}],
     ['suf-16', '--suffix=Foo --to=si   7000Foo',    {OUT=>'7.0kFoo'}],
     ['suf-17', '--suffix=Foo --to=si   7000Bar',
              {ERR => "$prog: invalid suffix in input '7000Bar': 'Bar'\n"},
              {EXIT => '1'}],
     ['suf-18', '--suffix=Foo --to=si   7000FooF',
              {ERR => "$prog: invalid suffix in input '7000FooF': 'FooF'\n"},
              {EXIT => '1'}],

     ## GROUPING

     # "C" locale - no grouping.
     ['grp-1', '--from=si --grouping 7M',   {OUT=>'7000000'}],
     ['grp-2', '--from=si --to=si --grouping 7M',
              {ERR => "$prog: --grouping cannot be combined with --to\n"},
              {EXIT => '1'}],

     ## TODO: check under a different locale (one that supports grouping)?
     #['grp-3', '--from=si --grouping 7M',   {OUT=>'7,000,000'}],


     ## Padding
     ['pad-1', '--padding=10 5',             {OUT=>'         5'}],
     ['pad-2', '--padding=-10 5',            {OUT=>'5         '}],
     ['pad-3', '--padding=A 5',
             {ERR => "$prog: invalid padding value 'A'\n"},
             {EXIT => '1'}],
     ['pad-4', '--padding=10 --to=si 50000',             {OUT=>'       50k'}],
     ['pad-5', '--padding=-10 --to=si 50000',            {OUT=>'50k       '}],

     # padding causing truncation, data loss
     ['pad-6', '--padding=2 --to=si 1000', {OUT=>'1.'},
             {ERR => "$prog: value '1.0k' truncated due to padding, " .
                "possible data loss\n"}],


     # Padding + suffix
     ['pad-7', '--padding=10 --suffix=foo --to=si 50000',
             {OUT=>'       50kfoo'}],
     ['pad-8', '--padding=-10 --suffix=foo --to=si 50000',
             {OUT=>'50k       foo'}],


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
             {ERR=>"$prog: invalid input number 'World'\n"},
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
             {OUT=>"4.0kM5000M9000"}],
     # 'M' is the delimiter, so the second input field is '5000'
     ['mix-14', '--delimiter=M --field 2 --from=auto --to=si 4000M5000M9000',
             {OUT=>"4000M5.0kM9000"}],

    );

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
