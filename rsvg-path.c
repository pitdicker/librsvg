/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-path.c: Parse SVG path element data into bezier path.

   Copyright (C) 2000 Eazel, Inc.
   Copyright © 2011 Christian Persch
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

   Author: Raph Levien <raph@artofcode.com>
           F. Wang <fred.wang@free.fr> - fix drawing of arc
*/

/* This is adapted from svg-path in Gill. */

#include "config.h"
#include "rsvg-path.h"

#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rsvg-private.h"

/* This module parses an SVG path element into an cairo_path_t.

   At present, there is no support for <marker> or any other contextual
   information from the SVG file. The API will need to change rather
   significantly to support these.

   Reference: SVG working draft 3 March 2000, section 8.
*/

typedef struct _RSVGParsePathCtx RSVGParsePathCtx;

struct _RSVGParsePathCtx {
    RsvgPathBuilder builder;

    cairo_path_data_t cp;       /* current point */
    cairo_path_data_t rp;       /* reflection point (for 's' and 't' commands) */
    char cmd;                   /* current command (lowercase) */
    int param;                  /* parameter number */
    gboolean rel;               /* true if relative coords */
    double params[7];           /* parameters that have been parsed */
};

double rsvg_path_coord_to_abs (RSVGParsePathCtx * ctx, double val);

static inline void
rsvg_path_builder_ensure_capacity (RsvgPathBuilder *builder,
                                   int additional_capacity)
{
}

static inline void
rsvg_path_builder_add_element (RsvgPathBuilder *builder,
                               cairo_path_data_t *data)
{
    g_array_append_val (builder->path_data, *data);
}

void
rsvg_path_builder_init (RsvgPathBuilder *builder,
                        int n_elements)
{
    builder->path_data = g_array_sized_new (FALSE, FALSE, sizeof (cairo_path_data_t), n_elements);
    builder->last_move_to_index = -1;
}

void
rsvg_path_builder_move_to (RsvgPathBuilder *builder,
                           double x,
                           double y)
{
    cairo_path_data_t data;

    rsvg_path_builder_ensure_capacity (builder, 2);

    data.header.type = CAIRO_PATH_MOVE_TO;
    data.header.length = 2;
    rsvg_path_builder_add_element (builder, &data);
    builder->last_move_to_index = builder->path_data->len - 1;

    data.point.x = x;
    data.point.y = y;
    rsvg_path_builder_add_element (builder, &data);
}

void
rsvg_path_builder_line_to (RsvgPathBuilder *builder,
                           double x,
                           double y)
{
    cairo_path_data_t data;

    rsvg_path_builder_ensure_capacity (builder, 2);

    data.header.type = CAIRO_PATH_LINE_TO;
    data.header.length = 2;
    rsvg_path_builder_add_element (builder, &data);
    data.point.x = x;
    data.point.y = y;
    rsvg_path_builder_add_element (builder, &data);
}

void
rsvg_path_builder_curve_to (RsvgPathBuilder *builder,
                            double x1,
                            double y1,
                            double x2,
                            double y2,
                            double x3,
                            double y3)
{
    cairo_path_data_t data;

    rsvg_path_builder_ensure_capacity (builder, 4);

    data.header.type = CAIRO_PATH_CURVE_TO;
    data.header.length = 4;
    rsvg_path_builder_add_element (builder, &data);
    data.point.x = x1;
    data.point.y = y1;
    rsvg_path_builder_add_element (builder, &data);
    data.point.x = x2;
    data.point.y = y2;
    rsvg_path_builder_add_element (builder, &data);
    data.point.x = x3;
    data.point.y = y3;
    rsvg_path_builder_add_element (builder, &data);
}

void
rsvg_path_builder_close_path (RsvgPathBuilder *builder)
{
    cairo_path_data_t data;

    /* If a close_path is directly preceded by a move_to, do not add it.
       This does not affect the rendering, and avoids hitting a bug in cairo. */
    if (builder->last_move_to_index != builder->path_data->len - 2) {
        rsvg_path_builder_ensure_capacity (builder, 1);
        data.header.type = CAIRO_PATH_CLOSE_PATH;
        data.header.length = 1;
        rsvg_path_builder_add_element (builder, &data);
    }

    /* Add a 'move-to' element */
    if (builder->last_move_to_index >= 0) {
        cairo_path_data_t *moveto = &g_array_index (builder->path_data, cairo_path_data_t, builder->last_move_to_index);

        rsvg_path_builder_move_to (builder, moveto[1].point.x, moveto[1].point.y);
    }
}

cairo_path_t *
rsvg_path_builder_finish (RsvgPathBuilder *builder)
{
    cairo_path_t *path;

    path = g_new (cairo_path_t, 1);
    path->status = CAIRO_STATUS_SUCCESS;
    path->data = (cairo_path_data_t *) builder->path_data->data; /* adopt array segment */
    path->num_data = builder->path_data->len;
    g_array_free (builder->path_data, FALSE);

    return path;
}

static void
rsvg_path_arc_segment (RSVGParsePathCtx * ctx,
                       double xc, double yc,
                       double th0, double th1, double rx, double ry,
                       double x_axis_rotation)
{
    double x1, y1, x2, y2, x3, y3;
    double t;
    double th_half;
    double f, sinf, cosf;

    f = x_axis_rotation * M_PI / 180.0;
    sinf = sin (f);
    cosf = cos (f);

    th_half = 0.5 * (th1 - th0);
    t = (8.0 / 3.0) * sin (th_half * 0.5) * sin (th_half * 0.5) / sin (th_half);
    x1 = rx * (cos (th0) - t * sin (th0));
    y1 = ry * (sin (th0) + t * cos (th0));
    x3 = rx * cos (th1);
    y3 = ry * sin (th1);
    x2 = x3 + rx * (t * sin (th1));
    y2 = y3 + ry * (-t * cos (th1));

    rsvg_path_builder_curve_to (&ctx->builder,
                                xc + cosf * x1 - sinf * y1,
                                yc + sinf * x1 + cosf * y1,
                                xc + cosf * x2 - sinf * y2,
                                yc + sinf * x2 + cosf * y2,
                                xc + cosf * x3 - sinf * y3,
                                yc + sinf * x3 + cosf * y3);
}

static void
rsvg_parse_path_moveto (RSVGParsePathCtx * ctx)
{
    rsvg_path_builder_move_to (&ctx->builder, ctx->params[0], ctx->params[1]);
    ctx->cp.point.x = ctx->rp.point.x = ctx->params[0];
    ctx->cp.point.y = ctx->rp.point.y = ctx->params[1];
    ctx->param = 0;
    ctx->cmd = 'L'; /* implicit lineto after a moveto */
}

static void
rsvg_parse_path_closepath (RSVGParsePathCtx * ctx)
{
    rsvg_path_builder_close_path (&ctx->builder);
    ctx->cp = ctx->rp = g_array_index (ctx->builder.path_data, cairo_path_data_t, ctx->builder.path_data->len - 1);
}

static void
rsvg_parse_path_lineto (RSVGParsePathCtx * ctx)
{
    rsvg_path_builder_line_to (&ctx->builder, ctx->params[0], ctx->params[1]);
    ctx->cp.point.x = ctx->rp.point.x = ctx->params[0];
    ctx->cp.point.y = ctx->rp.point.y = ctx->params[1];
    ctx->param = 0;
}

static void
rsvg_parse_path_curveto (RSVGParsePathCtx * ctx)
{
    double x1, y1, x2, y2, x3, y3;
    x1 = ctx->params[0];
    y1 = ctx->params[1];
    x2 = ctx->params[2];
    y2 = ctx->params[3];
    x3 = ctx->params[4];
    y3 = ctx->params[5];
    rsvg_path_builder_curve_to (&ctx->builder, x1, y1, x2, y2, x3, y3);
    ctx->rp.point.x = x2;
    ctx->rp.point.y = y2;
    ctx->cp.point.x = x3;
    ctx->cp.point.y = y3;
    ctx->param = 0;
}

static void
rsvg_parse_path_smooth_curveto (RSVGParsePathCtx * ctx)
{
    double x1, y1, x2, y2, x3, y3;
    x1 = 2 * ctx->cp.point.x - ctx->rp.point.x;
    y1 = 2 * ctx->cp.point.y - ctx->rp.point.y;
    x2 = ctx->params[0];
    y2 = ctx->params[1];
    x3 = ctx->params[2];
    y3 = ctx->params[3];
    rsvg_path_builder_curve_to (&ctx->builder, x1, y1, x2, y2, x3, y3);
    ctx->rp.point.x = x2;
    ctx->rp.point.y = y2;
    ctx->cp.point.x = x3;
    ctx->cp.point.y = y3;
    ctx->param = 0;
}

static void
rsvg_parse_path_horizontal_lineto (RSVGParsePathCtx * ctx)
{
    rsvg_path_builder_line_to (&ctx->builder, ctx->params[0], ctx->cp.point.y);
    ctx->cp.point.x = ctx->rp.point.x = ctx->params[0];
    ctx->rp.point.y = ctx->cp.point.y;
    ctx->param = 0;
}

static void
rsvg_parse_path_vertical_lineto (RSVGParsePathCtx * ctx)
{
    rsvg_path_builder_line_to (&ctx->builder, ctx->cp.point.x, ctx->params[0]);
    ctx->cp.point.y = ctx->rp.point.y = ctx->params[0];
    ctx->rp.point.x = ctx->cp.point.x;
    ctx->param = 0;
}

static void
rsvg_parse_path_quadratic_bezier_curveto (RSVGParsePathCtx * ctx)
{
    double x1, y1, x2, y2, x3, y3;
    /* raise quadratic bezier to cubic */
    x1 = (ctx->cp.point.x + 2 * ctx->params[0]) * (1.0 / 3.0);
    y1 = (ctx->cp.point.y + 2 * ctx->params[1]) * (1.0 / 3.0);
    x3 = ctx->params[2];
    y3 = ctx->params[3];
    x2 = (x3 + 2 * ctx->params[0]) * (1.0 / 3.0);
    y2 = (y3 + 2 * ctx->params[1]) * (1.0 / 3.0);
    rsvg_path_builder_curve_to (&ctx->builder, x1, y1, x2, y2, x3, y3);
    ctx->rp.point.x = ctx->params[0];
    ctx->rp.point.y = ctx->params[1];
    ctx->cp.point.x = x3;
    ctx->cp.point.y = y3;
    ctx->param = 0;
}

static void
rsvg_parse_path_smooth_quadratic_bezier_curveto (RSVGParsePathCtx * ctx)
{
    double x1, y1, x2, y2, x3, y3;
    double xc, yc; /* quadratic control point */
    xc = 2 * ctx->cp.point.x - ctx->rp.point.x;
    yc = 2 * ctx->cp.point.y - ctx->rp.point.y;
    /* generate a quadratic bezier with control point = xc, yc */
    x1 = (ctx->cp.point.x + 2 * xc) * (1.0 / 3.0);
    y1 = (ctx->cp.point.y + 2 * yc) * (1.0 / 3.0);
    x3 = ctx->params[0];
    y3 = ctx->params[1];
    x2 = (x3 + 2 * xc) * (1.0 / 3.0);
    y2 = (y3 + 2 * yc) * (1.0 / 3.0);
    rsvg_path_builder_curve_to (&ctx->builder, x1, y1, x2, y2, x3, y3);
    ctx->rp.point.x = xc;
    ctx->rp.point.y = yc;
    ctx->cp.point.x = x3;
    ctx->cp.point.y = y3;
    ctx->param = 0;
}

static void
rsvg_parse_path_elliptical_arc (RSVGParsePathCtx * ctx)
{
    /* See Appendix F.6 Elliptical arc implementation notes
       http://www.w3.org/TR/SVG/implnote.html#ArcImplementationNotes */

    double rx; /* Radius in x direction (before rotation) */
    double ry; /* Radius in y direction (before rotation) */
    double x_axis_rotation; /* Rotation angle for axes */
    int large_arc_flag; /* 0 for arc length <= 180, 1 for arc >= 180 */
    int sweep_flag; /* 0 for "negative angle", 1 for "positive angle" */
    double x; /* New x coordinate */
    double y; /* New y coordinate */
    
    double f, sinf, cosf;
    double x1, y1, x2, y2;
    double x1_, y1_;
    double cx_, cy_, cx, cy;
    double gamma;
    double theta1, delta_theta;
    double k1, k2, k3, k4, k5;

    int i, n_segs;

    rx = ctx->params[0];
    ry = ctx->params[1];
    x_axis_rotation = ctx->params[2];
    large_arc_flag = ctx->params[3];
    sweep_flag = ctx->params[4];
    x = ctx->params[5];
    y = ctx->params[6];

    /* Start and end of path segment */
    x1 = ctx->cp.point.x;
    y1 = ctx->cp.point.y;

    x2 = x;
    y2 = y;

    if (x1 == x2 && y1 == y2)
        goto error_in_arc_calculation; /* beginpoint == endpoint */

    /* X-axis */
    f = x_axis_rotation * M_PI / 180.0;
    sinf = sin (f);
    cosf = cos (f);

    /* Check the radius against floating point underflow.
       See http://bugs.debian.org/508443 */
    if ((fabs(rx) < DBL_EPSILON) || (fabs(ry) < DBL_EPSILON))
        goto error_in_arc_calculation; /* radius is (almost) 0 */

    if (rx < 0)
        rx = -rx;
    if (ry < 0)
        ry = -ry;

    /* Correction of out-of-range radii */

    k1 = (x1 - x2) / 2;
    k2 = (y1 - y2) / 2;

    x1_ = cosf * k1 + sinf * k2;
    y1_ = -sinf * k1 + cosf * k2;

    gamma = (x1_ * x1_) / (rx * rx) + (y1_ * y1_) / (ry * ry);
    if (gamma > 1) {
        rx *= sqrt (gamma);
        ry *= sqrt (gamma);
    }

    /* Compute the center */

    k1 = rx * rx * y1_ * y1_ + ry * ry * x1_ * x1_;
    if (k1 == 0)
        goto error_in_arc_calculation; /* about to divide by zero */

    k1 = sqrt (fabs ((rx * rx * ry * ry) / k1 - 1));
    if (sweep_flag == large_arc_flag)
        k1 = -k1;

    cx_ = k1 * rx * y1_ / ry;
    cy_ = -k1 * ry * x1_ / rx;

    cx = cosf * cx_ - sinf * cy_ + (x1 + x2) / 2;
    cy = sinf * cx_ + cosf * cy_ + (y1 + y2) / 2;

    /* Compute start angle */

    k1 = (x1_ - cx_) / rx;
    k2 = (y1_ - cy_) / ry;
    k3 = (-x1_ - cx_) / rx;
    k4 = (-y1_ - cy_) / ry;

    k5 = sqrt (fabs (k1 * k1 + k2 * k2));
    if (k5 == 0)
        goto error_in_arc_calculation; /* about to divide by zero */

    k5 = k1 / k5;
    if (k5 < -1)
        k5 = -1;
    else if (k5 > 1)
        k5 = 1;
    theta1 = acos (k5);
    if (k2 < 0)
        theta1 = -theta1;

    /* Compute delta_theta */

    k5 = sqrt (fabs ((k1 * k1 + k2 * k2) * (k3 * k3 + k4 * k4)));
    if (k5 == 0)
        goto error_in_arc_calculation; /* about to divide by zero */

    k5 = (k1 * k3 + k2 * k4) / k5;
    if (k5 < -1)
        k5 = -1;
    else if (k5 > 1)
        k5 = 1;
    delta_theta = acos (k5);
    if (k1 * k4 - k3 * k2 < 0)
        delta_theta = -delta_theta;

    if (sweep_flag && delta_theta < 0)
        delta_theta += M_PI*2;
    else if (!sweep_flag && delta_theta > 0)
        delta_theta -= M_PI*2;

    /* Now draw the arc */

    n_segs = ceil (fabs (delta_theta / (M_PI * 0.5 + 0.001)));

    for (i = 0; i < n_segs; i++) {
        rsvg_path_arc_segment (ctx, cx, cy,
                               theta1 + i * delta_theta / n_segs,
                               theta1 + (i + 1) * delta_theta / n_segs,
                               rx, ry, x_axis_rotation);
    }

    ctx->cp.point.x = ctx->rp.point.x = x;
    ctx->cp.point.y = ctx->rp.point.y = y;
    ctx->param = 0;
    return;

error_in_arc_calculation:
    ctx->params[0] = ctx->params[5];
    ctx->params[1] = ctx->params[6];
    rsvg_parse_path_lineto (ctx);
}

double
rsvg_path_coord_to_abs (RSVGParsePathCtx * ctx, double val)
{
    if (ctx->rel) {
        /* Handle relative coordinates. This switch statement determines what
           the coords are relative to. */
        switch (ctx->cmd) {
        case 'L':
        case 'M':
        case 'C':
        case 'S':
        case 'Q':
        case 'T':
            /* rule: even-numbered params are x-relative, odd-numbered
               are y-relative */
            if ((ctx->param & 1) == 0)
                val += ctx->cp.point.x;
            else
                val += ctx->cp.point.y;
            break;
        case 'A':
            /* rule: sixth and seventh are x and y, rest are not
               relative */
            if (ctx->param == 5)
                val += ctx->cp.point.x;
            else if (ctx->param == 6)
                val += ctx->cp.point.y;
            break;
        case 'H':
            /* rule: x-relative */
            val += ctx->cp.point.x;
            break;
        case 'V':
            /* rule: y-relative */
            val += ctx->cp.point.y;
            break;
        }
    }
    return val;
}

cairo_path_t *
rsvg_parse_path (const char *path_str)
{
    RSVGParsePathCtx ctx;
    double val;
    gchar *valend;

    rsvg_path_builder_init (&ctx.builder, 32);

    ctx.cp.point.x = 0.0;
    ctx.cp.point.y = 0.0;
    ctx.rp = ctx.cp;
    ctx.cmd = 0;
    ctx.param = 0;
    ctx.rel = FALSE;

    while (*path_str != 0) {
        switch (*path_str) {
        case '.':
            /* '.' must be followed by a number */
            if ((end[1] >= 0 && end[1] <= '9'))
                goto exitloop;
        case '+': case '-':
            /* '+' of '-' must be followed by a number, or
               a '.' that is followed by a number */
            if (!((end[1] >= 0 && end[1] <= '9') ||
                  end[1] == '.' && !(end[2] >= 0 && end[2] <= '9')))
                goto exitloop;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            val = g_ascii_strtod(path_str, (gchar **) &valend);
            if (!isfinite (val))
                goto exitloop;

            ctx.params[ctx.param] = rsvg_path_coord_to_abs (&ctx, val);
            ctx.param++;
            path_str = valend - 1;
            
            switch (ctx.cmd) {
            case 'M':
                if (ctx.param == 2)
                    rsvg_parse_path_moveto (&ctx);
                break;
            case 'L':
                if (ctx.param == 2)
                    rsvg_parse_path_lineto (&ctx);
                break;
            case 'C':
                if (ctx.param == 6)
                    rsvg_parse_path_curveto (&ctx);
                break;
            case 'S':
                if (ctx.param == 4)
                    rsvg_parse_path_smooth_curveto (&ctx);
                break;
            case 'H':
                if (ctx.param == 1)
                    rsvg_parse_path_horizontal_lineto (&ctx);
                break;
            case 'V':
                if (ctx.param == 1)
                    rsvg_parse_path_vertical_lineto (&ctx);
                break;
            case 'Q':
                if (ctx.param == 4)
                    rsvg_parse_path_quadratic_bezier_curveto (&ctx);
                break;
            case 'T':
                if (ctx.param == 2)
                    rsvg_parse_path_smooth_quadratic_bezier_curveto (&ctx);
                break;
            case 'A':
                if (ctx.param == 7) {
                    rsvg_parse_path_elliptical_arc (&ctx);
                }
                break;
            default:
                goto exitloop;
            }
            break;
        case 'L': case 'C': case 'S': case 'H': case 'V': case 'Q': case 'T': case 'A':
            if (ctx.cmd == 0) /* only a moveto is accepted as the first command of path data */
                goto exitloop;
        case 'M':
            if (ctx.param != 0)
                goto exitloop;
            ctx.cmd = *path_str;
            ctx.rel = FALSE;
            break;
        case 'l': case 'c': case 's': case 'h': case 'v': case 'q': case 't': case 'a':
            if (ctx.cmd == 0) /* only a moveto is accepted as the first command of path data */
                goto exitloop;
        case 'm':
            if (ctx.param != 0)
                goto exitloop;
            ctx.cmd = *path_str - 'a' + 'A';
            ctx.rel = TRUE;
            break;
        case 'Z': case 'z':
            if (ctx.param != 0) /* only a moveto is accepted as the first command of path data */
                goto exitloop;
            rsvg_parse_path_closepath (&ctx);
            ctx.cmd = 'Z';
            break;
        case ' ': case '\t': case '\r': case '\n': case ',':
            break;
        default:
            goto exitloop;
        }
        path_str++;
    }
exitloop:
    return rsvg_path_builder_finish (&ctx.builder);
}

void
rsvg_cairo_path_destroy (cairo_path_t *path)
{
    if (path == NULL)
        return;

    g_free (path->data);
    g_free (path);
}
