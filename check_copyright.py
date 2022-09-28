#!/usr/bin/env python3

# Copyright (c) 2022 Arm Limited.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

'''
This script will check the files passed as arguments for a valid copyright
header. It will run against all changed files as part of the pre-commit hook
after running `pre-commit install` in the root of the repository. It can also
be run manually e.g. `./check_copyright.py file1 dir1/file2`

A valid copyright header includes the word 'Copyright' followed by the
relevant copyright years. It also requires an SPDX licence id string identifying
the license as MIT, as follows: 'SPDX-License-Identifier: MIT'
'''

import datetime
import re
import sys
from typing import List

# Set the limit on number of lines at top of file to search
MAX_SEARCH_LINES = 20
CURRENT_YEAR = datetime.datetime.now().year

# Regex for positive copyright string
COPYRIGHT_YEAR_REGEX = re.compile(
    r".*\bCOPYRIGHT.*%s.*" % str(CURRENT_YEAR), re.IGNORECASE
)

# Regex for positive SPDX id string
SPDX_REGEX = re.compile(r".*SPDX-License-Identifier: MIT.*", re.IGNORECASE)

# To match eg: "Copyright (C) 2014-2021"
PATTERN_STRING = r"\bCopyright\b.*[0-9,)]"
PATTERN_COPYRIGHT = re.compile(PATTERN_STRING, re.IGNORECASE)


def generate_years_string(years: List[int]) -> str:
    """
    Create a compacted string representation of a list of years.

    E.g. [1991, 2001, 2002, 2003, 2006, 2007] becomes "1991, 2001-2003,
    2006-2007"
    """

    generated_years_string = ""
    if len(years) > 0:
        y_mod_strings = ["%d" % years[0]]

        last_element_was_incremental = False
        for i in range(1, len(years)):
            # Are we in an incremental sequence?
            if years[i] == years[i - 1] + 1:

                last_element_was_incremental = True

                # Are we at the last element?
                if i == len(years) - 1:
                    y_mod_strings.append("-%d" % years[i])
                else:
                    continue

            else:
                # End of a sequence?
                if last_element_was_incremental:
                    y_mod_strings.append("-%d, " % years[i - 1])
                else:
                    y_mod_strings.append(", ")

                y_mod_strings.append("%d" % years[i])
                last_element_was_incremental = False

        generated_years_string = "".join(y_mod_strings)

    return generated_years_string


def parse_years_string(s: str) -> List[int]:
    """
    Given the string "1999, 2001-2005" this function returns the list:
    [1999, 2001, 2002, 2003, 2004, 2005]
    """
    singles = re.findall(r"(?<![-\d])\d+(?![-\d])", s)
    years = [int(x) for x in singles]

    ranges = re.findall(r"\d+-\d+", s)
    for r in ranges:
        limits = re.findall(r"\d+", r)
        years.extend(range(int(limits[0]), int(limits[1]) + 1))

    years = list(set(y for y in years if 1900 < y <= CURRENT_YEAR))
    years.sort()

    return years


def update_header(filename: str) -> None:
    """
    Updates the Copyright header in 'filename' to hold the correct years.
    """

    with open(filename, "r+", encoding="utf-8") as file_handle:
        file_data = file_handle.read()

        copyright_match = re.search(PATTERN_COPYRIGHT, file_data)
        if copyright_match:

            notice_years = parse_years_string(copyright_match.group(0))

            if not notice_years or notice_years[-1] != CURRENT_YEAR:
                notice_years.append(CURRENT_YEAR)

            years_string = generate_years_string(notice_years)

            file_data = re.sub(
                PATTERN_COPYRIGHT, "Copyright (c) %s" % years_string, file_data, 1
            )
            file_handle.seek(0)
            file_handle.write(file_data)
            file_handle.truncate()


bad_copyright_files = []
bad_spdx_files = []

for changed_file in sys.argv[1:]:
    copyright_found = False
    spdx_found = False

    with open(changed_file, encoding="utf-8") as f:
        for line_num, line in enumerate(f):
            if line_num > MAX_SEARCH_LINES:
                break
            if COPYRIGHT_YEAR_REGEX.match(line):
                copyright_found = True
            if SPDX_REGEX.match(line):
                spdx_found = True

    if not copyright_found:
        bad_copyright_files.append(changed_file)
        update_header(changed_file)
    if not spdx_found:
        bad_spdx_files.append(changed_file)

if bad_copyright_files:
    print(
        "The following files did not have a valid copyright header: "
        + str(bad_copyright_files)
        + "\nAn attempted fix may have been made please check the files and re-commit",
        file=sys.stderr,
    )

if bad_spdx_files:
    print(
        "The following files do not have a valid SPDX licence identifier: "
        + str(bad_spdx_files)
        + "\nPlease add the identifier as follows 'SPDX-License-Identifier: MIT'",
        file=sys.stderr,
    )

if bad_copyright_files or bad_spdx_files:
    sys.exit(1)
