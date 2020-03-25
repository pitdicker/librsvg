/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-shapes.c: Draw SVG shapes

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

   Authors: Raph Levien <raph@artofcode.com>,
            Dom Lachowicz <cinamod@hotmail.com>,
            Caleb Moore <c.moore@student.unsw.edu.au>
            Paul Dicker <pitdicker@gmail.com>
*/

#include "config.h"

#include <glib.h>

#include "rsvg-private.h"
#include "rsvg-styles.h"
#include "rsvg-shapes.h"
#include "rsvg-css.h"
#include "rsvg-defs.h"
#include "rsvg-path.h"
#include "rsvg-marker.h"
#include "rsvg-parse-props.h"

struct _RsvgNodeRect {
    RsvgNode super;
    RsvgLength x, y, w, h, rx, ry;
    gboolean got_rx, got_ry;
};

typedef struct _RsvgNodeRect RsvgNodeRect;

static void
_rsvg_node_rect_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL, *value;
    RsvgNodeRect *rect = (RsvgNodeRect *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "x")))
            _rsvg_parse_prop_length (value, &rect->x, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "y")))
            _rsvg_parse_prop_length (value, &rect->y, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "width")))
            _rsvg_parse_prop_length (value, &rect->w, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "height")))
            _rsvg_parse_prop_length (value, &rect->h, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "rx"))) {
            _rsvg_parse_prop_length (value, &rect->rx, SVG_ATTRIBUTE);
            rect->got_rx = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "ry"))) {
            _rsvg_parse_prop_length (value, &rect->ry, SVG_ATTRIBUTE);
            rect->got_ry = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state, "rect", klazz, id, atts);
    }
}

static void
_rsvg_node_rect_draw (RsvgNode * self, RsvgDrawingCtx * ctx, int dominate)
{
    double x, y, w, h, rx, ry;
    RsvgNodeRect *rect = (RsvgNodeRect *) self;
    GArray *pathdata;
    RSVGPathSegm * path;

    x = rsvg_normalize_length (&rect->x, ctx, HORIZONTAL);
    y = rsvg_normalize_length (&rect->y, ctx, VERTICAL);
    w = rsvg_normalize_length (&rect->w, ctx, HORIZONTAL);
    h = rsvg_normalize_length (&rect->h, ctx, VERTICAL);
    rx = rsvg_normalize_length (&rect->rx, ctx, HORIZONTAL);
    ry = rsvg_normalize_length (&rect->ry, ctx, VERTICAL);

    if (w == 0. || h == 0.)
        return;

    if (rect->got_rx)
        rx = rx;
    else
        rx = ry;
    if (rect->got_ry)
        ry = ry;
    else
        ry = rx;

    if (w < 0. || h < 0. || rx < 0. || ry < 0.)
        return; /* TODO: the whole document should stop rendering at this point */

    if (rx > w / 2.)
        rx = w / 2.;
    if (ry > h / 2.)
        ry = h / 2.;

    if (rx == 0.)
        ry = 0.;
    else if (ry == 0.)
        rx = 0.;

    if (rx == 0.) {
        rsvg_path_builder_init (&pathdata, 5);
        rsvg_path_builder_move_to (&pathdata, x, y,
                                   PATHSEG_MOVETO_ABS);
        rsvg_path_builder_line_to (&pathdata, x + w, y,
                                   PATHSEG_LINETO_HORIZONTAL_REL);
        rsvg_path_builder_line_to (&pathdata, x + w, y + h,
                                   PATHSEG_LINETO_VERTICAL_REL);
        rsvg_path_builder_line_to (&pathdata, x, y + h,
                                   PATHSEG_LINETO_HORIZONTAL_REL);
        rsvg_path_builder_close_path (&pathdata, 0);
    } else {
        rsvg_path_builder_init (&pathdata, 9);
        rsvg_path_builder_move_to (&pathdata, x, y + ry,
                                   PATHSEG_MOVETO_ABS);
        rsvg_path_builder_elliptical_arc (&pathdata, x + rx, y,
                                          rx, ry, 0., RSVG_ARC_FLAG_SWEEP,
                                          PATHSEG_ARC_REL);
        rsvg_path_builder_line_to (&pathdata, x + w - rx, y,
                                   PATHSEG_LINETO_HORIZONTAL_REL);
        rsvg_path_builder_elliptical_arc (&pathdata, x + w, y + ry,
                                          rx, ry, 0., RSVG_ARC_FLAG_SWEEP,
                                          PATHSEG_ARC_REL);
        rsvg_path_builder_line_to (&pathdata, x + w, y + h - ry,
                                   PATHSEG_LINETO_VERTICAL_REL);
        rsvg_path_builder_elliptical_arc (&pathdata, x + w - rx, y + h,
                                          rx, ry, 0., RSVG_ARC_FLAG_SWEEP,
                                          PATHSEG_ARC_REL);
        rsvg_path_builder_line_to (&pathdata, x + rx, y + h,
                                   PATHSEG_LINETO_HORIZONTAL_REL);
        rsvg_path_builder_elliptical_arc (&pathdata, x, y + h - ry,
                                          rx, ry, 0., RSVG_ARC_FLAG_SWEEP,
                                          PATHSEG_ARC_REL);
        rsvg_path_builder_close_path (&pathdata, 0);
    }
    path = rsvg_path_builder_finish (pathdata);

    rsvg_state_reinherit_top (ctx, self->state, dominate);

    rsvg_render_path (ctx, path);
    rsvg_path_destroy (path);
}

RsvgNode *
rsvg_new_rect (void)
{
    RsvgNodeRect *rect;
    rect = g_new (RsvgNodeRect, 1);
    _rsvg_node_init (&rect->super, RSVG_NODE_TYPE_RECT);
    rect->super.draw = _rsvg_node_rect_draw;
    rect->super.set_atts = _rsvg_node_rect_set_atts;
    rect->x = rect->y = rect->w = rect->h = rect->rx = rect->ry = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    rect->got_rx = rect->got_ry = FALSE;
    return &rect->super;
}

struct _RsvgNodeCircle {
    RsvgNode super;
    RsvgLength cx, cy, r;
};

typedef struct _RsvgNodeCircle RsvgNodeCircle;

static void
_rsvg_node_circle_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL, *value;
    RsvgNodeCircle *circle = (RsvgNodeCircle *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "cx")))
            _rsvg_parse_prop_length (value, &circle->cx, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "cy")))
            _rsvg_parse_prop_length (value, &circle->cy, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "r")))
            _rsvg_parse_prop_length (value, &circle->r, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state, "circle", klazz, id, atts);
    }
}

static void
_rsvg_node_circle_draw (RsvgNode * self, RsvgDrawingCtx * ctx, int dominate)
{
    RsvgNodeCircle *circle = (RsvgNodeCircle *) self;
    GArray *pathdata;
    RSVGPathSegm * path;
    double cx, cy, r;

    cx = rsvg_normalize_length (&circle->cx, ctx, HORIZONTAL);
    cy = rsvg_normalize_length (&circle->cy, ctx, VERTICAL);
    r = rsvg_normalize_length (&circle->r, ctx, NO_DIR);

    if (r == 0)
        return;

    if (r < 0)
        return; /* TODO: the whole document should stop rendering at this point */

    rsvg_path_builder_init (&pathdata, 4);
    rsvg_path_builder_move_to (&pathdata, cx, cy - r,
                               PATHSEG_MOVETO_ABS);
    rsvg_path_builder_elliptical_arc (&pathdata, cx, cy - r,
                                      r, r, 0., RSVG_ARC_FLAG_FULL_ELLIPSE,
                                      PATHSEG_ARC_REL);
    rsvg_path_builder_close_path (&pathdata, 0);
    path = rsvg_path_builder_finish (pathdata);

    rsvg_state_reinherit_top (ctx, self->state, dominate);

    rsvg_render_path (ctx, path);
    rsvg_path_destroy (path);
}

RsvgNode *
rsvg_new_circle (void)
{
    RsvgNodeCircle *circle;
    circle = g_new (RsvgNodeCircle, 1);
    _rsvg_node_init (&circle->super, RSVG_NODE_TYPE_CIRCLE);
    circle->super.draw = _rsvg_node_circle_draw;
    circle->super.set_atts = _rsvg_node_circle_set_atts;
    circle->cx = circle->cy = circle->r = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    return &circle->super;
}

struct _RsvgNodeEllipse {
    RsvgNode super;
    RsvgLength cx, cy, rx, ry;
};

typedef struct _RsvgNodeEllipse RsvgNodeEllipse;

static void
_rsvg_node_ellipse_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL, *value;
    RsvgNodeEllipse *ellipse = (RsvgNodeEllipse *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "cx")))
            _rsvg_parse_prop_length (value, &ellipse->cx, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "cy")))
            _rsvg_parse_prop_length (value, &ellipse->cy, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "rx")))
            _rsvg_parse_prop_length (value, &ellipse->rx, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "ry")))
            _rsvg_parse_prop_length (value, &ellipse->ry, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state, "ellipse", klazz, id, atts);
    }
}

static void
_rsvg_node_ellipse_draw (RsvgNode * self, RsvgDrawingCtx * ctx, int dominate)
{
    RsvgNodeEllipse *ellipse = (RsvgNodeEllipse *) self;
    GArray *pathdata;
    RSVGPathSegm * path;
    double cx, cy, rx, ry;

    cx = rsvg_normalize_length (&ellipse->cx, ctx, HORIZONTAL);
    cy = rsvg_normalize_length (&ellipse->cy, ctx, VERTICAL);
    rx = rsvg_normalize_length (&ellipse->rx, ctx, HORIZONTAL);
    ry = rsvg_normalize_length (&ellipse->ry, ctx, VERTICAL);

    if (rx == 0 || ry == 0)
        return;

    if (rx < 0 || ry < 0)
        return; /* TODO: the whole document should stop rendering at this point */

    rsvg_path_builder_init (&pathdata, 4);
    rsvg_path_builder_move_to (&pathdata, cx, cy - ry,
                               PATHSEG_MOVETO_ABS);
    rsvg_path_builder_elliptical_arc (&pathdata, cx, cy - ry,
                                      rx, ry, 0., RSVG_ARC_FLAG_FULL_ELLIPSE,
                                      PATHSEG_ARC_REL);
    rsvg_path_builder_close_path (&pathdata, 0);
    path = rsvg_path_builder_finish (pathdata);

    rsvg_state_reinherit_top (ctx, self->state, dominate);

    rsvg_render_path (ctx, path);
    rsvg_path_destroy (path);
}

RsvgNode *
rsvg_new_ellipse (void)
{
    RsvgNodeEllipse *ellipse;
    ellipse = g_new (RsvgNodeEllipse, 1);
    _rsvg_node_init (&ellipse->super, RSVG_NODE_TYPE_ELLIPSE);
    ellipse->super.draw = _rsvg_node_ellipse_draw;
    ellipse->super.set_atts = _rsvg_node_ellipse_set_atts;
    ellipse->cx = ellipse->cy = ellipse->rx = ellipse->ry = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    return &ellipse->super;
}

struct _RsvgNodeLine {
    RsvgNode super;
    RsvgLength x1, x2, y1, y2;
};

typedef struct _RsvgNodeLine RsvgNodeLine;

static void
_rsvg_node_line_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL, *value;
    RsvgNodeLine *line = (RsvgNodeLine *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "x1")))
            _rsvg_parse_prop_length (value, &line->x1, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "y1")))
            _rsvg_parse_prop_length (value, &line->y1, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "x2")))
            _rsvg_parse_prop_length (value, &line->x2, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "y2")))
            _rsvg_parse_prop_length (value, &line->y2, SVG_ATTRIBUTE);
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state, "line", klazz, id, atts);
    }
}

static void
_rsvg_node_line_draw (RsvgNode * overself, RsvgDrawingCtx * ctx, int dominate)
{
    double x1, y1, x2, y2;
    RsvgNodeLine *self = (RsvgNodeLine *) overself;
    GArray *pathdata;
    RSVGPathSegm * path;

    x1 = rsvg_normalize_length (&self->x1, ctx, HORIZONTAL);
    y1 = rsvg_normalize_length (&self->y1, ctx, VERTICAL);
    x2 = rsvg_normalize_length (&self->x2, ctx, HORIZONTAL);
    y2 = rsvg_normalize_length (&self->y2, ctx, VERTICAL);

    rsvg_path_builder_init (&pathdata, 2);
    rsvg_path_builder_move_to (&pathdata, x1, y1, PATHSEG_MOVETO_ABS);
    rsvg_path_builder_line_to (&pathdata, x2, y2, PATHSEG_LINETO_ABS);
    path = rsvg_path_builder_finish (pathdata);

    rsvg_state_reinherit_top (ctx, overself->state, dominate);

    rsvg_render_path (ctx, path);
    rsvg_render_markers (ctx, path);
    rsvg_path_destroy (path);
}

RsvgNode *
rsvg_new_line (void)
{
    RsvgNodeLine *line;
    line = g_new (RsvgNodeLine, 1);
    _rsvg_node_init (&line->super, RSVG_NODE_TYPE_LINE);
    line->super.draw = _rsvg_node_line_draw;
    line->super.set_atts = _rsvg_node_line_set_atts;
    line->x1 = line->x2 = line->y1 = line->y2 = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    return &line->super;
}

struct _RsvgNodePoly {
    RsvgNode super;
    RSVGPathSegm *path;
};

typedef struct _RsvgNodePoly RsvgNodePoly;

static RSVGPathSegm *
_rsvg_node_poly_build_path (const char *value,
                            gboolean close_path);

static void
_rsvg_node_poly_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    RsvgNodePoly *poly = (RsvgNodePoly *) self;
    const char *klazz = NULL, *id = NULL, *value;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "points"))) {
            if (poly->path)
                rsvg_path_destroy (poly->path);
            poly->path = _rsvg_node_poly_build_path (value,
                                                     RSVG_NODE_TYPE (self) == RSVG_NODE_TYPE_POLYGON);
        }
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state,
                                     RSVG_NODE_TYPE (self) == RSVG_NODE_TYPE_POLYLINE ? "polyline" : "polygon",
                                     klazz, id, atts);
    }

}

static RSVGPathSegm *
_rsvg_node_poly_build_path (const char *data,
                            gboolean close_path)
{
    int param;
    double params[2];
    gboolean first_cmd = TRUE;

    GArray *path;

    param = 0;

    rsvg_path_builder_init (&path, 16);

    while (*data != 0) {
        switch (*data) {
        case '.':
            /* '.' must be followed by a number */
            if (!(data[1] >= 0 && data[1] <= '9'))
                goto exitloop;
            /* fallthrough */
        case '+': case '-':
            /* '+' or '-' must be followed by a digit,
               or by a '.' that is followed by a digit */
            if (!((data[1] >= 0 && data[1] <= '9') ||
                  (data[1] == '.' && !(data[2] >= 0 && data[2] <= '9'))))
                goto exitloop;
            /* fallthrough */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            params[param] = g_ascii_strtod(data, (gchar **) &data);
            param++;

            /* strtod also parses infinity and nan, which are not valid */
            if (!isfinite (params[param]))
                goto exitloop;

            if (param == 2) {
                if (first_cmd) {
                    rsvg_path_builder_move_to (&path, params[0], params[1],
                                               PATHSEG_MOVETO_ABS);
                    first_cmd = FALSE;
                } else {
                    rsvg_path_builder_line_to (&path, params[0], params[1],
                                               PATHSEG_LINETO_ABS);
                }
                param = 0;
            }

            /* skip trailing whitespaces and comma */
            while (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n')
                data++;
            if (*data != ',')
                data--;
            break;
        case ' ': case '\t': case '\r': case '\n':
            break;
        default: /* invalid character */
            goto exitloop;
        }
        data++;
    }
exitloop:

    if (close_path)
        rsvg_path_builder_close_path (&path, 0);

    return rsvg_path_builder_finish (path);
}

static void
_rsvg_node_poly_draw (RsvgNode * self, RsvgDrawingCtx * ctx, int dominate)
{
    RsvgNodePoly *poly = (RsvgNodePoly *) self;

    if (poly->path == NULL)
        return;

    rsvg_state_reinherit_top (ctx, self->state, dominate);

    rsvg_render_path (ctx, poly->path);
    rsvg_render_markers (ctx, poly->path);
}

static void
_rsvg_node_poly_free (RsvgNode * self)
{
    RsvgNodePoly *poly = (RsvgNodePoly *) self;
    if (poly->path)
        rsvg_path_destroy (poly->path);
    _rsvg_node_finalize (&poly->super);
    g_free (poly);
}

static RsvgNode *
rsvg_new_any_poly (RsvgNodeType type)
{
    RsvgNodePoly *poly;
    poly = g_new (RsvgNodePoly, 1);
    _rsvg_node_init (&poly->super, type);
    poly->super.free = _rsvg_node_poly_free;
    poly->super.draw = _rsvg_node_poly_draw;
    poly->super.set_atts = _rsvg_node_poly_set_atts;
    poly->path = NULL;
    return &poly->super;
}

RsvgNode *
rsvg_new_polyline (void)
{
    return rsvg_new_any_poly (RSVG_NODE_TYPE_POLYLINE);
}

RsvgNode *
rsvg_new_polygon (void)
{
    return rsvg_new_any_poly (RSVG_NODE_TYPE_POLYGON);
}

typedef struct _RsvgNodePath RsvgNodePath;

struct _RsvgNodePath {
    RsvgNode super;
    RSVGPathSegm *path;
};

typedef struct _RSVGParsePathCtx RSVGParsePathCtx;

struct _RSVGParsePathCtx {
    GArray *path;
    double x, y;               /* current point */
    double rpx, rpy;           /* reflection point (for 'S' and 'T' commands) */
    char lastcmd;              /* previous command (uppercase) */
    gboolean rel;              /* true if relative coords */
    int param;                 /* parameter number */
    double params[7];          /* parameters that have been parsed */
    guint subpath_start_index; /* array index of previous moveto or closepath */
};

static RSVGPathSegm *
_rsvg_node_path_build_path (const char *data);

static void
rsvg_node_path_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL, *value;
    RsvgNodePath *path = (RsvgNodePath *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "d"))) {
            if (path->path)
                rsvg_path_destroy (path->path);
            path->path = _rsvg_node_path_build_path (value);
        }
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state, "path", klazz, id, atts);
    }
}

static void
rsvg_parse_path_do_cmd (RSVGParsePathCtx * ctx, char cmd)
{
    double curx, cury; /* current start point */
    double x, y; /* end point of command */
    double x1, y1, x2, y2; /* Bezier curve control points */
    double rx, ry, x_axis_rotation; /* elliptical arc parameters */
    guint flags;
    RSVGPathSegm *prev_moveto;

    curx = ctx->x;
    cury = ctx->y;

    if (ctx->rel) {
        x = curx;
        y = cury;
    } else {
        x = 0.;
        y = 0.;
    }

    switch (cmd) {
    case 'M':
        x += ctx->params[0];
        y += ctx->params[1];
        ctx->subpath_start_index = rsvg_path_builder_move_to (&ctx->path, x, y,
                                                              ctx->rel? PATHSEG_MOVETO_REL
                                                                      : PATHSEG_MOVETO_ABS);
        break;
    case 'L':
        x += ctx->params[0];
        y += ctx->params[1];
        rsvg_path_builder_line_to (&ctx->path, x, y,
                                   ctx->rel? PATHSEG_LINETO_REL
                                           : PATHSEG_LINETO_ABS);
        break;
    case 'H':
        x += ctx->params[0];
        y = cury;
        rsvg_path_builder_line_to (&ctx->path, x, y,
                                   ctx->rel? PATHSEG_LINETO_HORIZONTAL_REL
                                           : PATHSEG_LINETO_HORIZONTAL_ABS);
        break;
    case 'V':
        x = curx;
        y += ctx->params[0];
        rsvg_path_builder_line_to (&ctx->path, x, y,
                                   ctx->rel? PATHSEG_LINETO_VERTICAL_REL
                                           : PATHSEG_LINETO_VERTICAL_ABS);
        break;
    case 'C':
        x1 = x + ctx->params[0];
        y1 = y + ctx->params[1];
        x2 = ctx->rpx = x + ctx->params[2];
        y2 = ctx->rpy = y + ctx->params[3];
        x += ctx->params[4];
        y += ctx->params[5];
        rsvg_path_builder_cubic_curve_to (&ctx->path, x, y, x1, y1, x2, y2,
                                          ctx->rel? PATHSEG_CURVETO_CUBIC_REL
                                                  : PATHSEG_CURVETO_CUBIC_ABS);
        break;
    case 'S':
        if (ctx->lastcmd == 'C' || ctx->lastcmd == 'S') {
            x1 = 2 * curx - ctx->rpx;
            y1 = 2 * cury - ctx->rpy;
        } else {
            x1 = curx;
            y1 = cury;
        }
        x2 = ctx->rpx = x + ctx->params[0];
        y2 = ctx->rpy = y + ctx->params[1];
        x += ctx->params[2];
        y += ctx->params[3];
        rsvg_path_builder_cubic_curve_to (&ctx->path, x, y, x1, y1, x2, y2,
                                          ctx->rel? PATHSEG_CURVETO_CUBIC_SMOOTH_REL
                                                  : PATHSEG_CURVETO_CUBIC_SMOOTH_ABS);
        break;
    case 'Q':
        x1 = ctx->rpx = x + ctx->params[0];
        y1 = ctx->rpy = y + ctx->params[1];
        x += ctx->params[2];
        y += ctx->params[3];
        rsvg_path_builder_quadratic_curve_to (&ctx->path, x, y, x1, y1,
                                              ctx->rel? PATHSEG_CURVETO_QUADRATIC_REL
                                                      : PATHSEG_CURVETO_QUADRATIC_ABS);
        break;
    case 'T':
        if (ctx->lastcmd == 'Q' || ctx->lastcmd == 'T') {
            x1 = ctx->rpx = 2 * curx - ctx->rpx;
            y1 = ctx->rpy = 2 * cury - ctx->rpy;
        } else {
            x1 = ctx->rpx = curx;
            y1 = ctx->rpy = cury;
        }
        x += ctx->params[0];
        y += ctx->params[1];
        rsvg_path_builder_quadratic_curve_to (&ctx->path, x, y, x1, y1,
                                              ctx->rel? PATHSEG_CURVETO_QUADRATIC_SMOOTH_REL
                                                      : PATHSEG_CURVETO_QUADRATIC_SMOOTH_ABS);
        break;
    case 'A':
        rx = ctx->params[0];
        ry = ctx->params[1];
        x_axis_rotation = ctx->params[2];
        x += ctx->params[5];
        y += ctx->params[6];

        flags = 0;
        if (ctx->params[3])
            flags |= RSVG_ARC_FLAG_LARGEARC;
        if (ctx->params[4])
            flags |= RSVG_ARC_FLAG_SWEEP;

        rsvg_path_builder_elliptical_arc (&ctx->path, x, y, rx, ry,
                                          x_axis_rotation, flags,
                                          ctx->rel? PATHSEG_ARC_REL
                                                  : PATHSEG_ARC_ABS);
        break;
    case 'Z':
        /* set x and y to that of the previous moveto */
        prev_moveto = &g_array_index (ctx->path, RSVGPathSegm, ctx->subpath_start_index);
        x = prev_moveto->x;
        y = prev_moveto->y;

        ctx->subpath_start_index = rsvg_path_builder_close_path (&ctx->path, ctx->subpath_start_index);
    }

    ctx->x = x;
    ctx->y = y;
    ctx->param = 0;
    ctx->lastcmd = cmd;
}

static RSVGPathSegm *
_rsvg_node_path_build_path (const char *data)
{
    RSVGParsePathCtx ctx;
    char cmd;
    gboolean in_cmd;

    rsvg_path_builder_init (&ctx.path, 16);

    cmd = ctx.lastcmd = 0;
    in_cmd = FALSE;
    ctx.param = 0;
    ctx.x = ctx.y = 0.;

    while (*data != 0) {
        switch (*data) {
        case '.':
            /* '.' must be followed by a number */
            if (!(data[1] >= 0 && data[1] <= '9'))
                goto exitloop;
            /* fallthrough */
        case '+': case '-':
            /* '+' or '-' must be followed by a digit,
               or by a '.' that is followed by a digit */
            if (!((data[1] >= 0 && data[1] <= '9') ||
                  (data[1] == '.' && !(data[2] >= 0 && data[2] <= '9'))))
                goto exitloop;
            /* fallthrough */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            if (cmd == 'A' && (ctx.param == 3 || ctx.param == 4)) {
                if (*data != '0' && *data != '1')
                    goto exitloop;
                ctx.params[ctx.param] = *data - '0';
            } else {
                ctx.params[ctx.param] = g_ascii_strtod (data, (gchar **) &data);
                data -= 1;
                /* strtod also parses infinity and nan, which are not valid */
                if (!isfinite (ctx.params[ctx.param]))
                    goto exitloop;
            }
            ctx.param++;
            in_cmd = TRUE;

            switch (cmd) {
            case 'M':
                if (ctx.param == 2) {
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                    cmd = 'L'; /* implicit lineto after a moveto */
                }
                break;
            case 'L':
                if (ctx.param == 2)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'H':
                if (ctx.param == 1)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'V':
                if (ctx.param == 1)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'C':
                if (ctx.param == 6)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'S':
                if (ctx.param == 4)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'Q':
                if (ctx.param == 4)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'T':
                if (ctx.param == 2)
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                break;
            case 'A':
                if (ctx.param == 7) {
                    /* arc is invalid if rx < 0 or ry < 0 */
                    if (ctx.params[0] < 0. || ctx.params[1] < 0.)
                        goto exitloop;
                    rsvg_parse_path_do_cmd (&ctx, cmd);
                }
                break;
            default:
                goto exitloop;
            }

            if (ctx.param == 0) { /* just finished parsing a command */
                in_cmd = FALSE;
            } else {
                /* skip trailing whitespaces and comma */
                data++;
                while (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n')
                    data++;
                if (*data != ',')
                    data--;
            }
            break;
        case 'L': case 'C': case 'S': case 'H': case 'V': case 'Q': case 'T': case 'A':
            if (cmd == 0) /* only a moveto is accepted as the first command of path data */
                goto exitloop;
            /* fallthrough */
        case 'M':
            if (in_cmd)
                goto exitloop;
            cmd = *data;
            ctx.rel = FALSE;
            in_cmd = TRUE;
            break;
        case 'l': case 'c': case 's': case 'h': case 'v': case 'q': case 't': case 'a':
            if (cmd == 0) /* only a moveto is accepted as the first command of path data */
                goto exitloop;
            /* fallthrough */
        case 'm':
            if (in_cmd)
                goto exitloop;
            cmd = *data - 'a' + 'A';
            ctx.rel = TRUE;
            in_cmd = TRUE;
            break;
        case 'Z': case 'z':
            if (cmd == 0)
                goto exitloop;
            if (in_cmd)
                goto exitloop;
            cmd = 'Z';
            rsvg_parse_path_do_cmd (&ctx, cmd);
            break;
        case ',':
            /* can only occure between multiple argument sequences of the last command */
            if (cmd == 0)
                goto exitloop;
            if (in_cmd)
                goto exitloop;
            in_cmd = TRUE;
            break;
        case ' ': case '\t': case '\r': case '\n':
            break;
        default: /* invalid character */
            goto exitloop;
        }
        data++;
    }
exitloop:

    return rsvg_path_builder_finish (ctx.path);
}

static void
rsvg_node_path_draw (RsvgNode * self, RsvgDrawingCtx * ctx, int dominate)
{
    RsvgNodePath *path = (RsvgNodePath *) self;

    if (path->path == NULL)
        return;

    rsvg_state_reinherit_top (ctx, self->state, dominate);

    rsvg_render_path (ctx, path->path);
    rsvg_render_markers (ctx, path->path);
}

static void
rsvg_node_path_free (RsvgNode * self)
{
    RsvgNodePath *path = (RsvgNodePath *) self;
    if (path->path)
        rsvg_path_destroy (path->path);
    _rsvg_node_finalize (&path->super);
    g_free (path);
}

RsvgNode *
rsvg_new_path (void)
{
    RsvgNodePath *path;
    path = g_new (RsvgNodePath, 1);
    _rsvg_node_init (&path->super, RSVG_NODE_TYPE_PATH);
    path->path = NULL;
    path->super.free = rsvg_node_path_free;
    path->super.draw = rsvg_node_path_draw;
    path->super.set_atts = rsvg_node_path_set_atts;

    return &path->super;
}
