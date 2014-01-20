/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-path.c: Build RSVGPathData array.

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

   Author: Paul Dicker <pitdicker@gmail.com>
*/

#include "config.h"
#include "rsvg-path.h"

#include <glib.h>
#include <math.h>
#include <stdlib.h>

#include "rsvg-private.h"

void
rsvg_path_builder_init (GArray **path,
                        int n_elements)
{
    *path = g_array_sized_new (FALSE, FALSE, sizeof (RSVGPathSegm), n_elements);
}

guint
rsvg_path_builder_move_to (GArray **path,
                           double x,
                           double y,
                           RsvgPathSegmentType segm_type)
{
    RSVGPathSegm segm;

    segm.type = segm_type;
    segm.x = x;
    segm.y = y;
    segm.att.subpath.prev_length = 0;
    segm.att.subpath.next_length = 0;
    g_array_append_val (*path, segm);

    return path[0]->len - 1;
}

void
rsvg_path_builder_line_to (GArray **path,
                           double x,
                           double y,
                           RsvgPathSegmentType segm_type)
{
    RSVGPathSegm segm;

    segm.type = segm_type;
    segm.x = x;
    segm.y = y;
    g_array_append_val (*path, segm);
}

void
rsvg_path_builder_cubic_curve_to (GArray **path,
                                  double x,
                                  double y,
                                  double x1,
                                  double y1,
                                  double x2,
                                  double y2,
                                  RsvgPathSegmentType segm_type)
{
    RSVGPathSegm segm;

    segm.type = segm_type;
    segm.x = x;
    segm.y = y;
    segm.att.c.x1 = x1;
    segm.att.c.y1 = y1;
    segm.att.c.x2 = x2;
    segm.att.c.y2 = y2;
    g_array_append_val (*path, segm);
}

void
rsvg_path_builder_quadratic_curve_to (GArray **path,
                                      double x,
                                      double y,
                                      double x1,
                                      double y1,
                                      RsvgPathSegmentType segm_type)
{
    RSVGPathSegm segm;

    segm.type = segm_type;
    segm.x = x;
    segm.y = y;
    segm.att.c.x1 = x1;
    segm.att.c.y1 = y1;
    g_array_append_val (*path, segm);
}

void
rsvg_path_builder_elliptical_arc (GArray **path,
                                  double x,
                                  double y,
                                  double r1,
                                  double r2,
                                  double angle,
                                  guint flags,
                                  RsvgPathSegmentType segm_type)
{
    RSVGPathSegm segm;

    segm.type = segm_type;
    segm.x = x;
    segm.y = y;
    segm.att.a.r1 = r1;
    segm.att.a.r2 = r2;
    segm.att.a.angle = angle * M_PI / 180.;
    segm.att.a.flags = flags;
    g_array_append_val (*path, segm);
}

guint
rsvg_path_builder_close_path (GArray **path,
                              guint subpath_start_index)
{
    RSVGPathSegm segm;
    RSVGPathSegm *prev_moveto;
    guint subpath_length;

    subpath_length = path[0]->len - subpath_start_index;

    /* get x and y coordinates from previous moveto */
    prev_moveto = &g_array_index (*path, RSVGPathSegm, subpath_start_index);
    prev_moveto[0].att.subpath.next_length = subpath_length;

    segm.type = PATHSEG_CLOSEPATH;
    segm.x = prev_moveto[0].x;
    segm.y = prev_moveto[0].y;
    segm.att.subpath.prev_length = subpath_length;
    segm.att.subpath.next_length = 0;
    g_array_append_val (*path, segm);

    return path[0]->len - 1;
}

RSVGPathSegm *
rsvg_path_builder_finish (GArray *path)
{
    RSVGPathSegm *pathdata;

    if (path->len <= 1) {
        /* discard path if it only contains a move_to */
        g_array_free (path, TRUE);
        pathdata = NULL;
    } else {
        /* adopt array segment, and store array length in first segment */
        pathdata = (RSVGPathSegm *) path->data;
        pathdata[0].att.path.number_of_items = path->len;
        g_array_free (path, FALSE);
    }
    return pathdata;
}

void
rsvg_path_destroy (RSVGPathSegm *path)
{
    if (path == NULL)
        return;
    g_free (path);
}

gboolean
rsvg_path_points_not_equal (const double ax, const double ay, const double bx, const double by)
{
    return ((fabs(ax - bx) > ((fabs(ax) < fabs(bx)? fabs(bx) : fabs(ax))
                             * RELTO_COMPARE_RANGE * DBL_EPSILON)) ||
            (fabs(ay - by) > ((fabs(ay) < fabs(by)? fabs(by) : fabs(ay))
                             * RELTO_COMPARE_RANGE * DBL_EPSILON)))
            ? TRUE : FALSE;
}

gboolean
rsvg_path_arc_center_para (const RSVGPathSegm arc,
                           double prevx,
                           double prevy,
                           double *cx,
                           double *cy,
                           double *rx,
                           double *ry,
                           double *th1,
                           double *th2,
                           double *delta_theta)
{
    /* See Appendix F.6 Elliptical arc implementation notes
       http://www.w3.org/TR/SVG/implnote.html#ArcImplementationNotes */

    double x, y, x_axis_rotation;
    gboolean large_arc, sweep;

    double x1_, y1_, cx_, cy_;
    double kx, ky, k1, k2, k3, k4, factor, gamma;
    double sinf, cosf;

    x = arc.x;
    y = arc.y;
    *rx = arc.att.a.r1;
    *ry = arc.att.a.r2;
    x_axis_rotation = arc.att.a.angle;
    large_arc = (arc.att.a.flags & RSVG_ARC_FLAG_LARGEARC)? TRUE : FALSE;
    sweep = (arc.att.a.flags & RSVG_ARC_FLAG_SWEEP)? TRUE : FALSE;

    /* Special case to handle a full circle or ellipse */
    if (arc.att.a.flags & RSVG_ARC_FLAG_FULL_ELLIPSE) {
        *cx = x;
        *cy = y + *ry;
        *th1 = *th2 = 3. / 2. * M_PI;
        *delta_theta = 2. * M_PI;
        return TRUE;
    }

    /* Omit the arc entirely if the endpoints are the same */
    if (!rsvg_path_points_not_equal (prevx, prevy, x, y))
        return FALSE;

    /* If rx = 0 and/or ry = 0 no arc should be drawn.
       Don't do an absolute check against 0.0, but against the precision that
       is available at the start and endpoints. */
    k1 = fabs (prevx);
    if (fabs (prevy) > k1) k1 = fabs (prevy);
    if (fabs (x) > k1)     k1 = fabs (x);
    if (fabs (y) > k1)     k1 = fabs (y);
    if (*rx < k1 * DBL_EPSILON && *ry < k1 * DBL_EPSILON)
        return FALSE;

    /* X-axis */
    sinf = sin (x_axis_rotation);
    cosf = cos (x_axis_rotation);

    /* Step 1: Compute (x1′, y1′) */
    kx = (prevx - x) / 2.;
    ky = (prevy - y) / 2.;
    x1_ = cosf * kx + sinf * ky;
    y1_ = -sinf * kx + cosf * ky;

    /* Step 2: Compute (cx′, cy′) */
    k1 = *rx * y1_;
    k2 = *ry * x1_;
    factor = (*rx * *rx * *ry * *ry) / (k1 * k1 + k2 * k2);
    if (factor < 1.) {
        /* Correct out-of-range radii */
        k3 = x1_ / *rx;
        k4 = y1_ / *ry;
        gamma = k3 * k3 + k4 * k4;
        *rx *= sqrt (gamma);
        *ry *= sqrt (gamma);
        factor = 1.;
    }
    factor = sqrt (factor - 1.);
    if (sweep == large_arc)
        factor = -factor;
    cx_ = factor * k1 / *ry;
    cy_ = -factor * k2 / *rx;

    /* Step 3: Compute (cx, cy) from (cx′, cy′) */
    *cx = cosf * cx_ - sinf * cy_ + (prevx + x) / 2.;
    *cy = sinf * cx_ + cosf * cy_ + (prevy + y) / 2.;

    /* start angle */
    k1 = prevx - *cx;
    k2 = prevy - *cy;
    kx = (cosf * k1 + sinf * k2) / *rx;
    ky = (-sinf * k1 + cosf * k2) / *ry;
    *th1 = atan2 (ky, kx);
    if (*th1 < 0.)
        *th1 += 2. * M_PI;

    /* end angle */
    k1 = x - *cx;
    k2 = y - *cy;
    kx = (cosf * k1 + sinf * k2) / *rx;
    ky = (-sinf * k1 + cosf * k2) / *ry;
    *th2 = atan2 (ky, kx);
    if (*th2 < 0.)
        *th2 += 2. * M_PI;

    /* sweep angle */
    *delta_theta = *th2 - *th1;
    if (sweep) {
        if (*delta_theta <= 0.)
            *delta_theta += 2. * M_PI;
    } else {
        if (*delta_theta >= 0.)
            *delta_theta -= 2. * M_PI;
    }

    return TRUE;
}

gboolean
rsvg_path_segm_has_dir (const RSVGPathSegm *const segm,
                        const double prevx,
                        const double prevy)
{
    switch (segm->type) {
    case PATHSEG_MOVETO_ABS:
    case PATHSEG_MOVETO_REL:
    case PATHSEG_LINETO_ABS:
    case PATHSEG_LINETO_REL:
    case PATHSEG_LINETO_HORIZONTAL_ABS:
    case PATHSEG_LINETO_HORIZONTAL_REL:
    case PATHSEG_LINETO_VERTICAL_ABS:
    case PATHSEG_LINETO_VERTICAL_REL:
    case PATHSEG_CLOSEPATH:
        if (rsvg_path_points_not_equal (prevx, prevy, segm->x, segm->y))
            return TRUE;
        else
            return FALSE;
        break;
    case PATHSEG_ARC_ABS:
    case PATHSEG_ARC_REL:
        if (segm->att.a.flags & RSVG_ARC_FLAG_FULL_ELLIPSE ||
            rsvg_path_points_not_equal (prevx, prevy, segm->x, segm->y))
            return TRUE;
        else
            return FALSE;
        break;
    case PATHSEG_CURVETO_CUBIC_ABS:
    case PATHSEG_CURVETO_CUBIC_REL:
    case PATHSEG_CURVETO_CUBIC_SMOOTH_ABS:
    case PATHSEG_CURVETO_CUBIC_SMOOTH_REL:
        if (rsvg_path_points_not_equal (prevx, prevy, segm->x, segm->y) ||
            rsvg_path_points_not_equal (prevx, prevy,
                                        segm->att.c.x1, segm->att.c.y1) ||
            rsvg_path_points_not_equal (prevx, prevy,
                                        segm->att.c.x2, segm->att.c.y2) )
            return TRUE;
        else
            return FALSE;
        break;
    case PATHSEG_CURVETO_QUADRATIC_ABS:
    case PATHSEG_CURVETO_QUADRATIC_REL:
    case PATHSEG_CURVETO_QUADRATIC_SMOOTH_ABS:
    case PATHSEG_CURVETO_QUADRATIC_SMOOTH_REL:
        if (rsvg_path_points_not_equal (prevx, prevy, segm->x, segm->y) ||
            rsvg_path_points_not_equal (prevx, prevy,
                                        segm->att.c.x1, segm->att.c.y1) )
            return TRUE;
        else
            return FALSE;
        break;
    case PATHSEG_UNKNOWN:
    default:
        return FALSE; /* should never occur */
    }
}

void
rsvg_path_get_segm_dir (const RSVGPathSegm *const path,
                        guint i,
                        double *startdirx,
                        double *startdiry,
                        double *enddirx,
                        double *enddiry)
{
    double x1, y1, x2, y2; /* curve_to control points */
    double rx, ry, cx, cy, th1, th2, delta_theta; /* arc parameters */

    double sinf, cosf; /* elliptical arc helper variables */

    /* for the direction searching algorithm */
    gboolean has_dir, wrapped_subpath;
    double tempdirx, tempdiry;
    guint j, number_of_items;

    double tot;

    /* TODO: assert (i != 0) */

    switch (path[i].type) {
    case PATHSEG_MOVETO_ABS:
    case PATHSEG_MOVETO_REL:
    case PATHSEG_LINETO_ABS:
    case PATHSEG_LINETO_REL:
    case PATHSEG_LINETO_HORIZONTAL_ABS:
    case PATHSEG_LINETO_HORIZONTAL_REL:
    case PATHSEG_LINETO_VERTICAL_ABS:
    case PATHSEG_LINETO_VERTICAL_REL:
    case PATHSEG_CLOSEPATH:
        if (!rsvg_path_points_not_equal (path[i].x, path[i].y,
                                         path[i - 1].x, path[i - 1].y)) {
            has_dir = FALSE;
        } else {
            *startdirx = *enddirx = path[i].x - path[i - 1].x;
            *startdiry = *enddiry = path[i].y - path[i - 1].y;
            has_dir = TRUE;
        }
        break;
    case PATHSEG_CURVETO_CUBIC_ABS:
    case PATHSEG_CURVETO_CUBIC_REL:
    case PATHSEG_CURVETO_CUBIC_SMOOTH_ABS:
    case PATHSEG_CURVETO_CUBIC_SMOOTH_REL:
        x1 = path[i].att.c.x1;
        y1 = path[i].att.c.y1;
        x2 = path[i].att.c.x2;
        y2 = path[i].att.c.y2;

        has_dir = TRUE;
        if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                        x1, y1)) {
            *startdirx = x1 - path[i - 1].x;
            *startdiry = y1 - path[i - 1].y;
        } else if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                               x2, y2)) {
            *startdirx = x2 - path[i - 1].x;
            *startdiry = y2 - path[i - 1].y;
        } else if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                               path[i].x, path[i].y)) {
            *startdirx = path[i].x - path[i - 1].x;
            *startdiry = path[i].y - path[i - 1].y;
        } else {
            has_dir = FALSE;
            break;
        }

        if (rsvg_path_points_not_equal (x2, y2,
                                        path[i].x, path[i].y)) {
            *enddirx = path[i].x - x2;
            *enddiry = path[i].y - y2;
        } else if (rsvg_path_points_not_equal (x1, y1,
                                               path[i].x, path[i].y)) {
            *enddirx = path[i].x - x1;
            *enddiry = path[i].y - y1;
        } else if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                               path[i].x, path[i].y)) {
            *enddirx = path[i].x - path[i - 1].x;
            *enddiry = path[i].y - path[i - 1].y;
        }
        break;
    case PATHSEG_CURVETO_QUADRATIC_ABS:
    case PATHSEG_CURVETO_QUADRATIC_REL:
    case PATHSEG_CURVETO_QUADRATIC_SMOOTH_ABS:
    case PATHSEG_CURVETO_QUADRATIC_SMOOTH_REL:
        x1 = path[i].att.c.x1;
        y1 = path[i].att.c.y1;

        has_dir = TRUE;
        if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                        x1, y1)) {
            *startdirx = x1 - path[i - 1].x;
            *startdiry = y1 - path[i - 1].y;
        } else if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                               path[i].x, path[i].y)) {
            *startdirx = path[i].x - path[i - 1].x;
            *startdiry = path[i].y - path[i - 1].y;
        } else {
            has_dir = FALSE;
            break;
        }

        if (rsvg_path_points_not_equal (x1, y1,
                                        path[i].x, path[i].y)) {
            *enddirx = path[i].x - x1;
            *enddiry = path[i].y - y1;
        } else if (rsvg_path_points_not_equal (path[i - 1].x, path[i - 1].y,
                                               path[i].x, path[i].y)) {
            *enddirx = path[i].x - path[i - 1].x;
            *enddiry = path[i].y - path[i - 1].y;
        }
        break;
    case PATHSEG_ARC_ABS:
    case PATHSEG_ARC_REL:
        if (rsvg_path_arc_center_para (path[i], path[i - 1].x, path[i - 1].y,
                                       &cx, &cy, &rx, &ry,
                                       &th1, &th2, &delta_theta)) {
            x1 = rx * sin (th1);
            y1 = ry * -cos (th1);
            x2 = rx * sin (th2);
            y2 = ry * -cos (th2);
            if (path[i].att.a.flags & RSVG_ARC_FLAG_SWEEP) {
                x1 = -x1;
                y1 = -y1;
                x2 = -x2;
                y2 = -y2;
            }

            sinf = sin (path[i].att.a.angle);
            cosf = cos (path[i].att.a.angle);
            *startdirx = cosf * x1 - sinf * y1;
            *startdiry = sinf * x1 + cosf * y1;
            *enddirx = cosf * x2 - sinf * y2;
            *enddiry = sinf * x2 + cosf * y2;
            has_dir = TRUE;
        } else {
            *startdirx = *enddirx = path[i].x - path[i - 1].x;
            *startdiry = *enddiry = path[i].y - path[i - 1].y;
            has_dir = TRUE;
        }
        break;
    case PATHSEG_UNKNOWN:
    default:
        ; /* should never occur */
    }

    if (has_dir == FALSE) {
        /* Algorithm to establish directionality for zero-length path segments,
           see http://www.w3.org/TR/SVG11/implnote.html#PathElementImplementationNotes */
        *startdirx = *startdiry = 0.;
        *enddirx = *enddiry = 0.;
        number_of_items = path[0].att.path.number_of_items;

        /* starting direction is the ending direction of the previous segment
           with non-zero length (within this subpath, if any) */
        j = i;
        wrapped_subpath = FALSE;
        do {
            j--;
            if (path[j].type == PATHSEG_MOVETO_ABS ||
                path[j].type == PATHSEG_MOVETO_REL ||
                path[j].type == PATHSEG_CLOSEPATH) {
                /* Reached start of the current subpath. If it is closed,
                   continue from the end of the subpath. If it is open, try to
                   take the direction from the incoming moveto and stop. */
                /* TODO: handle special case of no moveto before close? */
                if (path[j].att.subpath.next_length != 0) {
                    if (path[j].att.subpath.next_length == 1 || wrapped_subpath)
                        break;
                    j += path[j].att.subpath.next_length;
                    wrapped_subpath = TRUE;
                } else {
                    if (j > 0 && rsvg_path_segm_has_dir (&path[j],
                                                         path[j - 1].x,
                                                         path[j - 1].y) ) {
                        rsvg_path_get_segm_dir (path, j, &tempdirx, &tempdiry,
                                                startdirx, startdiry);
                    }
                    break;
                }
            }

            if (rsvg_path_segm_has_dir (&path[j], path[j - 1].x, path[j - 1].y)) {
                rsvg_path_get_segm_dir (path, j, &tempdirx, &tempdiry,
                                        startdirx, startdiry);
                break;
            }
        } while (TRUE);

        /* ending direction is the starting direction of the next segment
           with non-zero length (within this subpath, if any) */
        j = i;
        wrapped_subpath = FALSE;
        do {
            if (path[j].type == PATHSEG_CLOSEPATH) {
                /* continue from the start of the subpath */
                if (path[j].att.subpath.prev_length == 1 || wrapped_subpath)
                    break;
                j -= path[j].att.subpath.prev_length - 1;
                wrapped_subpath = TRUE;
            } else {
                j++;
                if (j == number_of_items)
                    break;
            }

            if (rsvg_path_segm_has_dir (&path[j], path[j - 1].x, path[j - 1].y)) {
                rsvg_path_get_segm_dir (path, j, enddirx, enddiry,
                                        &tempdirx, &tempdiry);
                break;
            }
        } while (path[j].type != PATHSEG_MOVETO_ABS &&
                 path[j].type != PATHSEG_MOVETO_REL);

        if (*enddirx == 0. && *enddiry == 0.) {
            if (*startdirx == 0. && *startdiry == 0.) {
                *enddirx = *startdirx = 1.;
                *enddiry = *startdiry = 0.;
            } else {
                *enddirx = *startdirx;
                *enddiry = *startdiry;
            }
        }
        if (*startdirx == 0. && *startdiry == 0.) {
            *startdirx = *enddirx;
            *startdiry = *enddiry;
        }
    } else {
        tot = sqrt (*startdirx * *startdirx + *startdiry * *startdiry);
        *startdirx /= tot;
        *startdiry /= tot;

        tot = sqrt (*enddirx * *enddirx + *enddiry * *enddiry);
        *enddirx /= tot;
        *enddiry /= tot;
    }
}
