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
*/

#include "config.h"

#include "rsvg-marker.h"
#include "rsvg-private.h"
#include "rsvg-styles.h"
#include "rsvg-shapes.h"
#include "rsvg-css.h"
#include "rsvg-defs.h"
#include "rsvg-filter.h"
#include "rsvg-mask.h"
#include "rsvg-image.h"
#include "rsvg-path.h"

#include <string.h>
#include <math.h>
#include <errno.h>

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
            marker->refX = _rsvg_css_parse_length (value);
        if ((value = rsvg_property_bag_lookup (atts, "refY")))
            marker->refY = _rsvg_css_parse_length (value);
        if ((value = rsvg_property_bag_lookup (atts, "markerWidth")))
            marker->width = _rsvg_css_parse_length (value);
        if ((value = rsvg_property_bag_lookup (atts, "markerHeight")))
            marker->height = _rsvg_css_parse_length (value);
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
        rsvg_parse_style_attrs (ctx, self->state, "marker", klazz, id, atts);
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
    marker->refX = marker->refY = _rsvg_css_parse_length ("0");
    marker->width = marker->height = _rsvg_css_parse_length ("3");
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
        w = _rsvg_css_normalize_length (&self->width, ctx, 'h');
        h = _rsvg_css_normalize_length (&self->height, ctx, 'v');

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
                                 -_rsvg_css_normalize_length (&self->refX, ctx, 'h'),
                                 -_rsvg_css_normalize_length (&self->refY, ctx, 'v'));
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
                                    _rsvg_css_normalize_length (&self->width, ctx, 'h'),
                                    _rsvg_css_normalize_length (&self->height, ctx, 'v'));
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

RsvgNode *
rsvg_marker_parse (const RsvgDefs * defs, const char *str)
{
    char *name;

    name = rsvg_get_url_string (str);
    if (name) {
        RsvgNode *val;
        val = rsvg_defs_lookup (defs, name);
        g_free (name);

        if (val && RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_MARKER)
            return val;
    }
    return NULL;
}

void
rsvg_render_markers (RsvgDrawingCtx * ctx,
                     const cairo_path_t *path)
{
    double x, y;
    double linewidth;
    cairo_path_data_type_t intype, outtype;
    double xdifin, ydifin, xdifout, ydifout, xdifstart, ydifstart, tot;
    int in, out, num_data;
    gboolean closed;
    int renderstart, rendermiddle;

    RsvgState *state;
    RsvgMarker *startmarker;
    RsvgMarker *middlemarker;
    RsvgMarker *endmarker;
    cairo_path_data_t *data;
    cairo_path_data_t prevp, nextp;

    state = rsvg_current_state (ctx);

    linewidth = _rsvg_css_normalize_length (&state->stroke_width, ctx, 'o');
    startmarker = (RsvgMarker *) state->startMarker;
    middlemarker = (RsvgMarker *) state->middleMarker;
    endmarker = (RsvgMarker *) state->endMarker;

    /* this function makes the assumptions about the path data:
       - the first element is always a MOVE_TO
       - every CLOSE_TO is followed by a MOVE_TO, containing the start point of
         the current subpath */

    if (linewidth == 0)
        return;

    if (!startmarker && !middlemarker && !endmarker)
        return;

    if (path->num_data <= 2) /* the path only contains a MOVE_TO */
        return;

    data = &path->data[0];
    num_data = path->num_data;

    renderstart = 1;
    rendermiddle = 0;
    closed = FALSE;
    prevp = data[1];
    xdifin = ydifin = 0;

    for (in = 0; in < num_data; in += data[in].header.length) {
        intype = data[in].header.type;
        if (intype == CAIRO_PATH_CLOSE_PATH)
            in++; /* get position from next MOVE_TO */
        x = data[in + data[in].header.length - 1].point.x;
        y = data[in + data[in].header.length - 1].point.y;

        rendermiddle++;

        /* if this is a new subpath: determine if this subpath is closed */
        if (intype == CAIRO_PATH_MOVE_TO) {
            closed = FALSE;
            for (out = in + data[in].header.length; out < num_data; out += data[out].header.length) {
                 if (closed) {
                    /* a path only really counts as closed if CLOSE_PATH is
                     * directly followed by a MOVE_TO or nothing */
                    if (data[out].header.type != CAIRO_PATH_MOVE_TO)
                        closed = FALSE;
                } else {
                    if (data[out].header.type == CAIRO_PATH_CLOSE_PATH) {
                        closed = TRUE;
                        out++; /* prepare to skip next MOVE_TO */
                    } else if (data[out].header.type == CAIRO_PATH_MOVE_TO) {
                        break;
                    }
                }
            }
            if (closed) {
                /* render one marker less, because this will be done at the end
                 * of the CLOSE_PATH segment */
                rendermiddle--;
                /* direction will be set once the outgoing direction is known */
                xdifstart = ydifstart = 0;
            }
        }

        out = in + data[in].header.length;
        if (out < num_data) {
            /* determine position of next vertex */
            outtype = data[out].header.type;
            if (outtype == CAIRO_PATH_CLOSE_PATH)
                out++;/* get position from next MOVE_TO */
            nextp = data[out + data[out].header.length - 1];

            /* determine outgoing angle */
            if (intype == CAIRO_PATH_CLOSE_PATH && outtype == CAIRO_PATH_MOVE_TO) {
                xdifout = xdifstart;
                ydifout = ydifstart;
            } else if (outtype == CAIRO_PATH_CURVE_TO &&
                (x != data[out + 1].point.x || y != data[out + 1].point.y)) {
                xdifout = data[out + 1].point.x - x;
                ydifout = data[out + 1].point.y - y;
            } else if (outtype == CAIRO_PATH_CURVE_TO &&
                (x != data[out + 2].point.x || y != data[out + 2].point.y)) {
                xdifout = data[out + 2].point.x - x;
                ydifout = data[out + 2].point.y - y;
            } else if (x != nextp.point.x || y != nextp.point.y) {
                xdifout = nextp.point.x - x;
                ydifout = nextp.point.y - y;
            } else {
                continue;
            }
        } else {
            /* the end of the path is reached */
            if (intype == CAIRO_PATH_CLOSE_PATH) {
                xdifout = xdifstart;
                ydifout = ydifstart;
            } else {
                xdifout = xdifin;
                ydifout = ydifin;
                rendermiddle--;
            }
        }

        /* if from a closed subpath this is the first segment that has a direction,
         * remember this direction as the direction of the start of this subpath */
        if (closed && (xdifstart == 0 && ydifstart == 0) &&
            (xdifout != 0 || ydifout != 0)) {
            xdifstart = xdifout;
            ydifstart = ydifout;
        }

        /* determine incoming angle */
        if (intype == CAIRO_PATH_CURVE_TO &&
            (x != data[in + 2].point.x || y != data[in + 2].point.y)) {
            xdifin = x - data[in + 2].point.x;
            ydifin = y - data[in + 2].point.y;
        } else if (intype == CAIRO_PATH_CURVE_TO &&
                   (x != data[in + 1].point.x || y != data[in + 1].point.y)) {
            xdifin = x - data[in + 1].point.x;
            ydifin = y - data[in + 1].point.y;
        } else if (x != prevp.point.x || y != prevp.point.y) {
            xdifin = x - prevp.point.x;
            ydifin = y - prevp.point.y;
        } else if ((xdifin == 0 && ydifin == 0) &&
                   (xdifout != 0 || ydifout != 0)) {
            /* there has not yet been a segment with an incoming direction,
               but an outgoing direction is known */
            xdifin = xdifout;
            ydifin = ydifout;
        } else {
            /* just keep the last known incoming angle */
        }

        /* the actual rendering */
        if (renderstart == 1) {
            if (startmarker && (closed == FALSE || !middlemarker)) {
                rsvg_marker_render (startmarker, x, y,
                                    atan2 (ydifout, xdifout),
                                    linewidth, ctx);
            }
            renderstart = 0;
            rendermiddle--;
        }

        if (middlemarker && rendermiddle >= 1) {
            tot = sqrt (xdifin * xdifin + ydifin * ydifin);
            if (tot) {
                xdifin /= tot;
                ydifin /= tot;
            }
            tot = sqrt (xdifout * xdifout + ydifout * ydifout);
            if (tot) {
                xdifout /= tot;
                ydifout /= tot;
            }
            for (; rendermiddle > 0; rendermiddle--) {
                rsvg_marker_render (middlemarker, x, y,
                                    atan2 (ydifin + ydifout, xdifin + xdifout),
                                    linewidth, ctx);
             }
        }
        rendermiddle = 0;

        prevp.point.x = x;
        prevp.point.y = y;
    }

    if (endmarker && (closed == FALSE || !middlemarker)) {
        rsvg_marker_render (endmarker, x, y,
                            atan2 (ydifin, xdifin),
                            linewidth, ctx);
    }
}
