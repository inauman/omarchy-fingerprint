# Licensing

This repository contains work aimed at two upstream projects, and each part
carries the licence of the project it is meant for:

- `tools/elanpress/` (the libfprint driver, matcher and evaluation tool):
  **LGPL-2.1-or-later**, the licence of libfprint.
- `tools/omarchy-fingerprint-enroll/` (the Omarchy shell plugin, commands and
  polkit files): **MIT**, the licence of Omarchy.
- Everything else (documentation, scripts, logs): **MIT**.

The driver derives from Filip Spanne's `elanpress` driver for the Elan
04f3:0c6e (LGPL-2.1-or-later), and its matcher design was informed by
dragosol/fpmatch (Apache-2.0 OR LGPL-2.1-or-later).

## MIT

Copyright (c) 2026 Mohammad Nauman

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## LGPL-2.1-or-later

The full text is at https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
