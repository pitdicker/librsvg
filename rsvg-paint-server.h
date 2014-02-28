/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-paint-server.h : RSVG colors

   Copyright (C) 2000 Eazel, Inc.
   Copyright (C) 2002 Dom Lachowicz <cinamod@hotmail.com>

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

   Author: Raph Levien <raph@artofcode.com>
*/

#ifndef RSVG_PAINT_SERVER_H
#define RSVG_PAINT_SERVER_H

#include <glib.h>
#include <cairo.h>
#include "rsvg-defs.h"

G_BEGIN_DECLS 

typedef struct _RsvgGradientStop RsvgGradientStop;
typedef struct _RsvgGradientStops RsvgGradientStops;
typedef struct _RsvgLinearGradient RsvgLinearGradient;
typedef struct _RsvgRadialGradient RsvgRadialGradient;
typedef struct _RsvgPattern RsvgPattern;

typedef struct _RsvgPaintServer RsvgPaintServer;

typedef struct _RsvgPSCtx RsvgPSCtx;

struct _RsvgGradientStop {
    RsvgNode super;
    double offset;
};

struct _RsvgLinearGradient {
    RsvgNode super;
    gboolean obj_bbox;
    cairo_matrix_t affine; /* user space to actual at time of gradient def */
    cairo_extend_t spread;
    RsvgLength x1, y1, x2, y2;
    int hasx1:1;
    int hasy1:1;
    int hasx2:1;
    int hasy2:1;
    int hasbbox:1;
    int hasspread:1;
    int hastransform:1;
    RsvgNode *fallback;
};

struct _RsvgRadialGradient {
    RsvgNode super;
    gboolean obj_bbox;
    cairo_matrix_t affine; /* user space to actual at time of gradient def */
    cairo_extend_t spread;
    RsvgLength cx, cy, r, fx, fy;
    int hascx:1;
    int hascy:1;
    int hasfx:1;
    int hasfy:1;
    int hasr:1;
    int hasspread:1;
    int hasbbox:1;
    int hastransform:1;
    RsvgNode *fallback;
};

struct _RsvgPattern {
    RsvgNode super;
    gboolean obj_cbbox;
    gboolean obj_bbox;
    cairo_matrix_t affine; /* user space to actual at time of gradient def */
    RsvgLength x, y, width, height;
    RsvgViewBox vbox;
    unsigned int preserve_aspect_ratio;
    int hasx:1;
    int hasy:1;
    int hasvbox:1;
    int haswidth:1;
    int hasheight:1;
    int hasaspect:1;
    int hascbox:1;
    int hasbbox:1;
    int hastransform:1;
    RsvgPattern *fallback;
};

struct _RsvgPaintServer {
    enum _RsvgPaintServerType {
        RSVG_PAINT_SERVER_NONE,
        RSVG_PAINT_SERVER_SOLID,
        RSVG_PAINT_SERVER_CURRENT_COLOR,
        RSVG_PAINT_SERVER_RAD_GRAD,
        RSVG_PAINT_SERVER_LIN_GRAD,
        RSVG_PAINT_SERVER_PATTERN
    } type;
    union _RsvgPaintServerCore {
        guint32             color;
        RsvgLinearGradient *lingrad;
        RsvgRadialGradient *radgrad;
        RsvgPattern        *pattern;
    } core;
};

G_GNUC_INTERNAL
RsvgRadialGradient  *rsvg_clone_radial_gradient (const RsvgRadialGradient * grad,
                                                 gboolean * shallow_cloned);
G_GNUC_INTERNAL
RsvgLinearGradient  *rsvg_clone_linear_gradient (const RsvgLinearGradient * grad,
                                                 gboolean * shallow_cloned);
G_GNUC_INTERNAL
RsvgNode *rsvg_new_linear_gradient  (void);
G_GNUC_INTERNAL
RsvgNode *rsvg_new_radial_gradient  (void);
G_GNUC_INTERNAL
RsvgNode *rsvg_new_stop         (void);
G_GNUC_INTERNAL
RsvgNode *rsvg_new_pattern      (void);

G_GNUC_INTERNAL
void rsvg_linear_gradient_fix_fallback  (RsvgLinearGradient * grad);
G_GNUC_INTERNAL
void rsvg_radial_gradient_fix_fallback  (RsvgRadialGradient * grad);
G_GNUC_INTERNAL
void rsvg_pattern_fix_fallback          (RsvgPattern * pattern);

G_END_DECLS

#endif
