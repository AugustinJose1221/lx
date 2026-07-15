# This file is part of lx, a terminal log viewer and analyzer.
# Copyright (C) 2026 Augustin Jose
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version. This program is distributed in
# the hope that it will be useful, but WITHOUT ANY WARRANTY. See the
# LICENSE file in the repository root for the full license text.
# Convenience wrapper; the real build lives in source/Makefile.
all:
	$(MAKE) -C source

test:
	$(MAKE) -C source test

install:
	$(MAKE) -C source install

clean:
	$(MAKE) -C source clean

.PHONY: all test install clean
