# Makefile.am - for tgpg/tools
# Copyright (C) 2015 g10 Code GmbH
#
# This file is part of TGPG.
#
# TGPG is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# TGPG is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
# MA 02110-1301, USA.

## Process this file with automake to produce Makefile.in

AM_CFLAGS = $(LIBGCRYPT_CFLAGS)

bin_PROGRAMS = tgpg-keystore

tgpg_keystore_SOURCES  = tgpg-keystore.c
tgpg_keystore_CFLAGS = $(LIBGCRYPT_CFLAGS)
tgpg_keystore_LDADD = $(LIBGCRYPT_LIBS)
