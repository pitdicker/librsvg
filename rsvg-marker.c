/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-marker.c: Marker loading and rendering

   Copyright (C) 2004, 2005 Caleb Moore <c.moore@student.unsw.edu.au>
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

   Authors: Caleb Moore <c.moore@student.unsw.edu.au>
            Paul Dicker <pitdicker@gmail.com>
*/

#include "config.h"

#include "rsvg-marker.h"
#include "rsvg-private.h"
#include "rsvg-styles.h"
#include "rsvg-css.h"
#include "rsvg-defs.h"
#include "rsvg-image.h"
#include "rsvg-path.h"
#include "rsvg-parse-props.h"

#include <string.h>
#include <math.h>

static void
rsvg_node_marker_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL, *value;
    RsvgMarker *marker;
    marker = (RsvgMarker *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, id, &marker->super);
        }
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "viewBox")))
            marker->vbox = rsvg_css_parse_vbox (value);
        if ((value = rsvg_property_bag_lookup (atts, "refX")))
            _rsvg_parse_prop_length (value, &marker->refX, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "refY")))
            _rsvg_parse_prop_length (value, &marker->refY, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "markerWidth")))
            _rsvg_parse_prop_length (value, &marker->width, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "markerHeight")))
            _rsvg_parse_prop_length (value, &marker->height, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "orient"))) {
            if (!strcmp (value, "auto"))
                marker->orientAuto = TRUE;
            else
                marker->orient = rsvg_css_parse_angle (value);
        }
        if ((value = rsvg_property_bag_lookup (atts, "markerUnits"))) {
            if (!strcmp (value, "userSpaceOnUse"))
                marker->bbox = FALSE;
            if (!strcmp (value, "strokeWidth"))
                marker->bbox = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "preserveAspectRatio")))
            marker->preserve_aspect_ratio = rsvg_css_parse_aspect_ratio (value);
        rsvg_set_presentation_props (ctx, self->state, "marker", klazz, id, atts);
    }
}

RsvgNode *
rsvg_new_marker (void)
{
    RsvgMarker *marker;
    marker = g_new (RsvgMarker, 1);
    _rsvg_node_init (&marker->super, RSVG_NODE_TYPE_MARKER);
    marker->orient = 0;
    marker->orientAuto = FALSE;
    marker->preserve_aspect_ratio = RSVG_ASPECT_RATIO_XMID_YMID;
    marker->refX = marker->refY = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    marker->width = marker->height = (RsvgLength) {3.0, RSVG_UNIT_NUMBER};
    marker->bbox = TRUE;
    marker->vbox.active = FALSE;
    marker->super.set_atts = rsvg_node_marker_set_atts;
    return &marker->super;
}

void
rsvg_marker_render (RsvgMarker * self, gdouble x, gdouble y, gdouble orient, gdouble linewidth,
                    RsvgDrawingCtx * ctx)
{
    cairo_matrix_t affine, taffine;
    unsigned int i;
    gdouble rotation;
    RsvgState *state = rsvg_current_state (ctx);

    cairo_matrix_init_translate (&taffine, x, y);
    cairo_matrix_multiply (&affine, &taffine, &state->affine);

    if (self->orientAuto)
        rotation = orient;
    else
        rotation = self->orient * M_PI / 180.;

    cairo_matrix_init_rotate (&taffine, rotation);
    cairo_matrix_multiply (&affine, &taffine, &affine);

    if (self->bbox) {
        cairo_matrix_init_scale (&taffine, linewidth, linewidth);
        cairo_matrix_multiply (&affine, &taffine, &affine);
    }

    if (self->vbox.active) {
        double w, h;
        w = rsvg_normalize_length (&self->width, ctx, HORIZONTAL);
        h = rsvg_normalize_length (&self->height, ctx, VERTICAL);

        rsvg_preserve_aspect_ratio (self->preserve_aspect_ratio,
                                    self->vbox.rect.width,
                                    self->vbox.rect.height,
                                    &w, &h, &x, &y);

        cairo_matrix_init_scale (&taffine,
                                 w / self->vbox.rect.width,
                                 h / self->vbox.rect.height);
        cairo_matrix_multiply (&affine, &taffine, &affine);

        _rsvg_push_view_box (ctx, self->vbox.rect.width, self->vbox.rect.height);
    }

    cairo_matrix_init_translate (&taffine,
                                 -rsvg_normalize_length (&self->refX, ctx, HORIZONTAL),
                                 -rsvg_normalize_length (&self->refY, ctx, VERTICAL));
    cairo_matrix_multiply (&affine, &taffine, &affine);

    rsvg_state_push (ctx);
    state = rsvg_current_state (ctx);

    rsvg_state_reinit (state);

    rsvg_state_reconstruct (state, &self->super);

    state->affine = affine;

    rsvg_push_discrete_layer (ctx);

    state = rsvg_current_state (ctx);

    if (!state->overflow) {
        if (self->vbox.active)
            rsvg_add_clipping_rect (ctx, self->vbox.rect.x, self->vbox.rect.y,
                                    self->vbox.rect.width, self->vbox.rect.height);
        else
            rsvg_add_clipping_rect (ctx, 0, 0,
                                    rsvg_normalize_length (&self->width, ctx, HORIZONTAL),
                                    rsvg_normalize_length (&self->height, ctx, VERTICAL));
    }

    for (i = 0; i < self->super.children->len; i++) {
        rsvg_state_push (ctx);

        rsvg_node_draw (g_ptr_array_index (self->super.children, i), ctx, 0);

        rsvg_state_pop (ctx);
    }
    rsvg_pop_discrete_layer (ctx);

    rsvg_state_pop (ctx);
    if (self->vbox.active)
        _rsvg_pop_view_box (ctx);
}

static double
rsvg_marker_calc_angle (double indirx, double indiry,
                        double outdirx, double outdiry) {
    if (fabs (indirx + outdirx) < DBL_EPSILON &&
        fabs (indiry + outdiry) < DBL_EPSILON) {
        return atan2 (indiry, indirx);
    }

    return atan2 (indiry + outdiry, indirx + outdirx);
}

void
rsvg_render_markers (RsvgDrawingCtx * ctx, const RSVGPathSegm *path)
{
    RsvgState *state;
    RsvgMarker *marker_start, *marker_mid, *marker_end;
    double linewidth;

    double indirx, indiry, outdirx, outdiry;
    double tempdirx, tempdiry;
    double angle;
    guint i, number_of_items;

    state = rsvg_current_state (ctx);
    linewidth = rsvg_normalize_length (&state->stroke_width, ctx, NO_DIR);

    marker_start = (RsvgMarker *) state->marker_start;
    marker_mid = (RsvgMarker *) state->marker_mid;
    marker_end = (RsvgMarker *) state->marker_end;

    if (linewidth == 0.) {
        /* If a marker is scaled to the current lineweight, do not render it if
           the lineweight is 0.0 */
        if (marker_start && marker_start->bbox)
            marker_start = NULL;
        if (marker_mid && marker_mid->bbox)
            marker_mid = NULL;
        if (marker_end && marker_end->bbox)
            marker_end = NULL;
    }

    if (path == NULL)
        return;

    number_of_items = path[0].att.path.number_of_items;

    if (marker_start) {
        angle = 0.;
        if (marker_start->orientAuto) {
            rsvg_path_get_segm_dir (path, 1, &outdirx, &outdiry,
                                    &tempdirx, &tempdiry);

            if (path[0].att.subpath.next_length != 0) {
                rsvg_path_get_segm_dir (path, path[0].att.subpath.next_length,
                                        &tempdirx, &tempdiry,
                                        &indirx, &indiry);
                angle = rsvg_marker_calc_angle (indirx, indiry, outdirx, outdiry);
            } else {
                angle = atan2 (outdiry, outdirx);
            }
        }
        rsvg_marker_render (marker_start, path[0].x, path[0].y,
                            angle, linewidth, ctx);
    }

    if (marker_mid) {
        angle = 0.;
        for (i = 1; i < number_of_items - 1; i++) {
            if (marker_mid->orientAuto) {
                if ((path[i].type == PATHSEG_MOVETO_ABS ||
                      path[i].type == PATHSEG_MOVETO_REL) &&
                      path[i].att.subpath.next_length != 0) {
                    rsvg_path_get_segm_dir (path, i + path[i].att.subpath.next_length,
                                            &tempdirx, &tempdiry,
                                            &indirx, &indiry);
                } else {
                    rsvg_path_get_segm_dir (path, i, &tempdirx, &tempdiry,
                                            &indirx, &indiry);
                }

                if (path[i].type == PATHSEG_CLOSEPATH &&
                    (path[i + 1].type == PATHSEG_MOVETO_ABS ||
                     path[i + 1].type == PATHSEG_MOVETO_REL) ) {
                     rsvg_path_get_segm_dir (path, i - path[i].att.subpath.prev_length + 1,
                                            &outdirx, &outdiry,
                                            &tempdirx, &tempdiry);
                } else {
                    rsvg_path_get_segm_dir (path, i + 1, &outdirx, &outdiry,
                                            &tempdirx, &tempdiry);
                }

                angle = rsvg_marker_calc_angle (indirx, indiry, outdirx, outdiry);
                /* TODO: cache previous outdir */
            }
            rsvg_marker_render (marker_mid, path[i].x, path[i].y,
                                angle, linewidth, ctx);
        }
    }

    if (marker_end) {
        i = number_of_items - 1;
        angle = 0.;
        if (marker_end->orientAuto) {
            rsvg_path_get_segm_dir (path, i, &tempdirx, &tempdiry,
                                    &indirx, &indiry);

            if (path[i].type == PATHSEG_CLOSEPATH) {
                rsvg_path_get_segm_dir (path, i - path[i].att.subpath.prev_length + 1,
                                        &outdirx, &outdiry,
                                        &tempdirx, &tempdiry);
                angle = rsvg_marker_calc_angle (indirx, indiry, outdirx, outdiry);
            } else {
                angle = atan2 (indiry, indirx);
            }
        }
        rsvg_marker_render (marker_end, path[i].x, path[i].y,
                            angle, linewidth, ctx);

    }
}
