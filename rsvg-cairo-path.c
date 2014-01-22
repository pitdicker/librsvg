/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-cairo-path.c: convert a RSVGPathData array to a cairo_path_t.

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

   Authors: Raph Levien <raph@artofcode.com>
            F. Wang <fred.wang@free.fr> - fix drawing of arc
            Paul Dicker <pitdicker@gmail.com>
*/

#include "config.h"
#include "rsvg-cairo-path.h"

#include <glib.h>
#include <math.h>

#include "rsvg-private.h"
#include "rsvg-path.h"

/* fraction of pixels to which the approximation of an arc by bezier curves
   should be accurate */
#define ARC_MAX_ERROR .25

static inline void
rsvg_cairo_path_builder_ensure_capacity (GArray **cairopath,
                                         int additional_capacity)
{
}

static inline void
rsvg_cairo_path_builder_add_element (GArray **cairopath,
                                     cairo_path_data_t *data)
{
    g_array_append_val (*cairopath, *data);
}

static void
rsvg_cairo_path_builder_init (GArray **cairopath,
                              int n_elements)
{
    *cairopath = g_array_sized_new (FALSE, FALSE, sizeof (cairo_path_data_t), n_elements);
}

static void
rsvg_cairo_path_builder_move_to (GArray **cairopath,
                                 double x,
                                 double y)
{
    cairo_path_data_t data;

    rsvg_cairo_path_builder_ensure_capacity (cairopath, 2);

    data.header.type = CAIRO_PATH_MOVE_TO;
    data.header.length = 2;
    rsvg_cairo_path_builder_add_element (cairopath, &data);

    data.point.x = x;
    data.point.y = y;
    rsvg_cairo_path_builder_add_element (cairopath, &data);
}

static void
rsvg_cairo_path_builder_line_to (GArray **cairopath,
                                 double x,
                                 double y)
{
    cairo_path_data_t data;

    rsvg_cairo_path_builder_ensure_capacity (cairopath, 2);

    data.header.type = CAIRO_PATH_LINE_TO;
    data.header.length = 2;
    rsvg_cairo_path_builder_add_element (cairopath, &data);
    data.point.x = x;
    data.point.y = y;
    rsvg_cairo_path_builder_add_element (cairopath, &data);
}

static void
rsvg_cairo_path_builder_curve_to (GArray **cairopath,
                                  double x1,
                                  double y1,
                                  double x2,
                                  double y2,
                                  double x,
                                  double y)
{
    cairo_path_data_t data;

    rsvg_cairo_path_builder_ensure_capacity (cairopath, 4);

    data.header.type = CAIRO_PATH_CURVE_TO;
    data.header.length = 4;
    rsvg_cairo_path_builder_add_element (cairopath, &data);

    data.point.x = x1;
    data.point.y = y1;
    rsvg_cairo_path_builder_add_element (cairopath, &data);

    data.point.x = x2;
    data.point.y = y2;
    rsvg_cairo_path_builder_add_element (cairopath, &data);

    data.point.x = x;
    data.point.y = y;
    rsvg_cairo_path_builder_add_element (cairopath, &data);
}

static void
rsvg_cairo_path_builder_close_path (GArray **cairopath)
{
  cairo_path_data_t data;

  rsvg_cairo_path_builder_ensure_capacity (cairopath, 1);

  data.header.type = CAIRO_PATH_CLOSE_PATH;
  data.header.length = 1;
  rsvg_cairo_path_builder_add_element (cairopath, &data);
}

static cairo_path_t *
rsvg_cairo_path_builder_finish (GArray *cairopath)
{
    cairo_path_t *path;

    path = g_new (cairo_path_t, 1);
    path->status = CAIRO_STATUS_SUCCESS;
    path->data = (cairo_path_data_t *) cairopath->data; /* adopt array segment */
    path->num_data = cairopath->len;
    g_array_free (cairopath, FALSE);

    return path;
}

cairo_path_t *
rsvg_cairo_build_path (const RSVGPathSegm *const path, cairo_matrix_t affine)
{
    GArray *cairopath;

    guint i, j, number_of_items;
    double x, y, prevx, prevy;
    double x1, y1, x2, y2, x3, y3, xc, yc; /* curve control points */
    double rx, ry, cx, cy, th1, th2, delta_theta; /* elliptical arc parameters */

    /* elliptical arc helper variables */
    double sinf, cosf, th, t;
    guint n, n_segs;
    cairo_matrix_t raffine;

    double startdirx, startdiry, enddirx, enddiry;
    double min_prec_x;
    gboolean zero_length_subpath;

    /* Length of a very short line in cairo device pixels that is used for
       zero-length segments. */
    min_prec_x = 1. / ((affine.xx + affine.xy) * 256.);

    rsvg_cairo_path_builder_init (&cairopath, 32);

    x = y = 0.;
    number_of_items = path[0].att.path.number_of_items;

    for (i = 0; i < number_of_items; i++) {
        prevx = x;
        prevy = y;
        x = path[i].x;
        y = path[i].y;

        switch (path[i].type) {
        case PATHSEG_MOVETO_ABS:
        case PATHSEG_MOVETO_REL:
            /* handle zero-length subpaths */
            zero_length_subpath = TRUE;
            j = i + 1;
            while (j < number_of_items &&
                   path[j].type != PATHSEG_MOVETO_ABS &&
                   path[j].type != PATHSEG_MOVETO_REL) {
                if (rsvg_path_segm_has_dir (&path[j], path[j - 1].x, path[j - 1].y)) {
                    zero_length_subpath = FALSE;
                    break;
                }
                j++;
            }

            if (zero_length_subpath && j > i + 1) {
                rsvg_cairo_path_builder_move_to (&cairopath, x - min_prec_x, y);
                rsvg_cairo_path_builder_line_to (&cairopath, x + min_prec_x, y);
                i = j - 1;
            } else {
                rsvg_cairo_path_builder_move_to (&cairopath, x, y);
            }
            break;
        case PATHSEG_LINETO_ABS:
        case PATHSEG_LINETO_REL:
        case PATHSEG_LINETO_HORIZONTAL_ABS:
        case PATHSEG_LINETO_HORIZONTAL_REL:
        case PATHSEG_LINETO_VERTICAL_ABS:
        case PATHSEG_LINETO_VERTICAL_REL:
            rsvg_cairo_path_builder_line_to (&cairopath, x, y);
            break;
        case PATHSEG_CURVETO_CUBIC_ABS:
        case PATHSEG_CURVETO_CUBIC_REL:
        case PATHSEG_CURVETO_CUBIC_SMOOTH_ABS:
        case PATHSEG_CURVETO_CUBIC_SMOOTH_REL:
            rsvg_cairo_path_builder_curve_to (&cairopath,
                                              path[i].att.c.x1, path[i].att.c.y1,
                                              path[i].att.c.x2, path[i].att.c.y2,
                                              x, y);
            break;
        case PATHSEG_CURVETO_QUADRATIC_ABS:
        case PATHSEG_CURVETO_QUADRATIC_REL:
        case PATHSEG_CURVETO_QUADRATIC_SMOOTH_ABS:
        case PATHSEG_CURVETO_QUADRATIC_SMOOTH_REL:
            xc = path[i].att.c.x1;
            yc = path[i].att.c.y1;

            /* raise quadratic bezier to cubic */
            x1 = (prevx + 2. * xc) * (1. / 3.);
            y1 = (prevy + 2. * yc) * (1. / 3.);
            x2 = (x + 2. * xc) * (1. / 3.);
            y2 = (y + 2. * yc) * (1. / 3.);

            rsvg_cairo_path_builder_curve_to (&cairopath, x1, y1, x2, y2, x, y);
            break;
        case PATHSEG_ARC_ABS:
        case PATHSEG_ARC_REL:
            if (!rsvg_path_arc_center_para (path[i], prevx, prevy, &cx, &cy,
                                            &rx, &ry, &th1, &th2, &delta_theta)) {
                rsvg_cairo_path_builder_line_to (&cairopath, x, y);
                break;
            }

            /* X-axis */
            sinf = sin (path[i].att.a.angle);
            cosf = cos (path[i].att.a.angle);

            /* calculate the number of bezier curves neccesary to approximate
               the arc, depending on it's average radius (including
               transformations) and included angle */
            raffine = affine;
            cairo_matrix_rotate (&raffine, path[i].att.a.angle);
            x1 = raffine.xx * rx + raffine.xy * ry;
            y1 = raffine.yx * rx + raffine.yy * ry;
            n_segs = ceil (sqrt (x1 * x1 + y1 * y1)
                           * 8 * M_PI / fabs (delta_theta)
                           * .001231984794614557 / ARC_MAX_ERROR);
            if (n_segs < ceil (fabs (delta_theta) / (M_PI * 2. / 3.) - 0.001))
                n_segs = ceil (fabs (delta_theta) / (M_PI * 2. / 3.) - 0.001);

            /* Calculate control points of cubic bezier curves */
            th = delta_theta / n_segs;
            t = 1.332440737409712 * (1. - cos (th * 0.5)) / sin (th * 0.5);
            for (n = 0; n < n_segs; n++) {
                th2 = th1 + th;
                x1 = rx * (cos (th1) - t * sin (th1));
                y1 = ry * (sin (th1) + t * cos (th1));
                x3 = rx * cos (th2);
                y3 = ry * sin (th2);
                x2 = x3 + rx * (t * sin (th2));
                y2 = y3 + ry * (-t * cos (th2));
                rsvg_cairo_path_builder_curve_to (&cairopath,
                                                   cx + cosf * x1 - sinf * y1,
                                                   cy + sinf * x1 + cosf * y1,
                                                   cx + cosf * x2 - sinf * y2,
                                                   cy + sinf * x2 + cosf * y2,
                                                   cx + cosf * x3 - sinf * y3,
                                                   cy + sinf * x3 + cosf * y3);
                th1 = th2; /* set start angle for next bezier */
            }
            break;
        case PATHSEG_CLOSEPATH:
            rsvg_cairo_path_builder_close_path (&cairopath);
            break;
        case PATHSEG_UNKNOWN:
            ;
        }
    }

    return rsvg_cairo_path_builder_finish (cairopath);
}

void
rsvg_cairo_path_destroy (cairo_path_t *path)
{
    if (path == NULL)
        return;

    g_free (path->data);
    g_free (path);
}
