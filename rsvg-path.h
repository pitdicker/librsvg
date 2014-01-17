/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-path.h: Build RSVGPathData array.

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

#ifndef RSVG_PATH_H
#define RSVG_PATH_H

#include <glib.h>

#include "rsvg-private.h"

G_BEGIN_DECLS 

#define RELTO_COMPARE_RANGE 32
/* This translates to: about RELTO_COMPARE_RANGE * 2 relative path instructions
   (that may cause rounding) followed by an absolute path instruction to the
   same point, will compare as equal. */

typedef struct {
    RSVGPathSegm *path;
    guint i;
    double startdirx, startdiry;
    double enddirx, enddiry;
    gboolean search_path;
} PathSegmDir;

G_GNUC_INTERNAL
void rsvg_path_builder_init (GArray **path,
                             int n_elements);
G_GNUC_INTERNAL
guint rsvg_path_builder_move_to (GArray **path,
                                 double x,
                                 double y,
                                 RsvgPathSegmentType segm_type);
G_GNUC_INTERNAL
void rsvg_path_builder_line_to (GArray **path,
                                double x,
                                double y,
                                RsvgPathSegmentType segm_type);
G_GNUC_INTERNAL
void rsvg_path_builder_cubic_curve_to (GArray **path,
                                       double x,
                                       double y,
                                       double x1,
                                       double y1,
                                       double x2,
                                       double y2,
                                       RsvgPathSegmentType segm_type);
G_GNUC_INTERNAL
void rsvg_path_builder_quadratic_curve_to (GArray **path,
                                           double x,
                                           double y,
                                           double x1,
                                           double y1,
                                           RsvgPathSegmentType segm_type);
G_GNUC_INTERNAL
void rsvg_path_builder_elliptical_arc (GArray **path,
                                       double x,
                                       double y,
                                       double r1,
                                       double r2,
                                       double angle,
                                       guint flags,
                                       RsvgPathSegmentType segm_type);
G_GNUC_INTERNAL
guint rsvg_path_builder_close_path (GArray **path,
                                    guint prev_moveto_index);
G_GNUC_INTERNAL
RSVGPathSegm * rsvg_path_builder_finish (GArray *path);
G_GNUC_INTERNAL
cairo_path_t * rsvg_parse_path (const char *path_str);
G_GNUC_INTERNAL
void rsvg_path_destroy (RSVGPathSegm *path);

G_GNUC_INTERNAL
gboolean rsvg_path_points_not_equal (const double ax,
                                     const double ay,
                                     const double bx,
                                     const double by);
G_GNUC_INTERNAL
gboolean rsvg_path_arc_center_para (RSVGPathSegm arc,
                                    double prevx,
                                    double prevy,
                                    double *cx,
                                    double *cy,
                                    double *rx,
                                    double *ry,
                                    double *th1,
                                    double *th2,
                                    double *delta_theta);
G_END_DECLS

#endif /* RSVG_PATH_H */
