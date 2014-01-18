/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-cairo-path.c: convert a RSVGPathData array to a cairo_path_t.

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2002 Dom Lachowicz <cinamod@hotmail.com>
   Copyright (C) 2013 Paul Dicker <pitdicker@gmail.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Authors: Raph Levien <raph@artofcode.com>
            Paul Dicker <pitdicker@gmail.com>
*/

#ifndef RSVG_CAIRO_PATH_H
#define RSVG_CAIRO_PATH_H

#include <cairo.h>
#include "rsvg-private.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL
cairo_path_t * rsvg_cairo_build_path (const RSVGPathSegm *const path);
G_GNUC_INTERNAL
void rsvg_cairo_path_destroy (cairo_path_t *path);

G_END_DECLS

#endif /* RSVG_CAIRO_PATH_H */
