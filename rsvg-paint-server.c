/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-paint-server.c: Implement the SVG paint server abstraction.

   Copyright (C) 2000 Eazel, Inc.

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

#include "config.h"
#include "rsvg-private.h"
#include "rsvg-defs.h"
#include "rsvg-paint-server.h"
#include "rsvg-styles.h"
#include "rsvg-image.h"
#include "rsvg-parse-props.h"

#include <glib.h>
#include <string.h>
#include <math.h>

#include "rsvg-css.h"

static void
rsvg_stop_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    const char *klazz = NULL, *id = NULL;
    const char *value, *end;
    RsvgGradientStop *stop;

    stop = (RsvgGradientStop *) self;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "offset"))) {
            /* either a number [0,1] or a percentage */
            stop->offset = _rsvg_parse_number (value, &end, SVG_ATTRIBUTE);
            if (value == end) {
                /* invalid number */
                stop->offset = 0.0; /* TODO: handle error */
            }
            if (*end == '%') {
                stop->offset *= 0.01;
                end++;
            }
            if (*end != '\0') {
                /* the number or percentage should not be followed by anything */
                stop->offset = 0.0; /* TODO: handle error */
            }
            stop->offset = CLAMP (stop->offset, 0., 1.);
        }
        if ((value = rsvg_property_bag_lookup (atts, "class")))
            klazz = value;
        if ((value = rsvg_property_bag_lookup (atts, "id"))) {
            id = value;
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        }

        rsvg_set_presentation_props (ctx, self->state, "stop", klazz, id, atts);
    }
}

RsvgNode *
rsvg_new_stop (void)
{
    RsvgGradientStop *stop = g_new (RsvgGradientStop, 1);
    _rsvg_node_init (&stop->super, RSVG_NODE_TYPE_STOP);
    stop->super.set_atts = rsvg_stop_set_atts;
    stop->offset = 0.0;
    return &stop->super;
}

static void
rsvg_linear_gradient_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    RsvgLinearGradient *grad = (RsvgLinearGradient *) self;
    const char *value;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "id")))
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        if ((value = rsvg_property_bag_lookup (atts, "x1"))) {
            _rsvg_parse_prop_length (value, &grad->x1, SVG_ATTRIBUTE);
            grad->hasx1 = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "y1"))) {
            _rsvg_parse_prop_length (value, &grad->y1, SVG_ATTRIBUTE);
            grad->hasy1 = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "x2"))) {
            _rsvg_parse_prop_length (value, &grad->x2, SVG_ATTRIBUTE);
            grad->hasx2 = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "y2"))) {
            _rsvg_parse_prop_length (value, &grad->y2, SVG_ATTRIBUTE);
            grad->hasy2 = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "spreadMethod"))) {
            if (!strcmp (value, "pad")) {
                grad->spread = CAIRO_EXTEND_PAD;
            } else if (!strcmp (value, "reflect")) {
                grad->spread = CAIRO_EXTEND_REFLECT;
            } else if (!strcmp (value, "repeat")) {
                grad->spread = CAIRO_EXTEND_REPEAT;
            }
            grad->hasspread = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "xlink:href"))) {
            if (self != rsvg_defs_lookup (ctx->priv->defs, value))
                rsvg_defs_add_resolver (ctx->priv->defs, &grad->fallback, value);
        }
        if ((value = rsvg_property_bag_lookup (atts, "gradientTransform"))) {
            if (rsvg_parse_transform (&grad->affine, value))
                grad->hastransform = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "gradientUnits"))) {
            if (!strcmp (value, "userSpaceOnUse"))
                grad->obj_bbox = FALSE;
            else if (!strcmp (value, "objectBoundingBox"))
                grad->obj_bbox = TRUE;
            grad->hasbbox = TRUE;
        }
        rsvg_set_presentation_props (ctx, self->state, "linearGradient", NULL, NULL, atts);
    }
}


RsvgNode *
rsvg_new_linear_gradient (void)
{
    RsvgLinearGradient *grad = NULL;
    grad = g_new (RsvgLinearGradient, 1);
    _rsvg_node_init (&grad->super, RSVG_NODE_TYPE_LINEAR_GRADIENT);
    cairo_matrix_init_identity (&grad->affine);
    grad->x1 = grad->y1 = grad->y2 = (RsvgLength) {0.0, RSVG_UNIT_NUMBER}; /* TODO: should be 0.0% */
    grad->x2 = (RsvgLength) {1.0, RSVG_UNIT_NUMBER}; /* TODO: should be 100.0% */
    grad->fallback = NULL;
    grad->obj_bbox = TRUE;
    grad->spread = CAIRO_EXTEND_PAD;
    grad->super.set_atts = rsvg_linear_gradient_set_atts;
    grad->hasx1 = grad->hasy1 = grad->hasx2 = grad->hasy2 = grad->hasbbox = grad->hasspread =
        grad->hastransform = FALSE;
    return &grad->super;
}

static void
rsvg_radial_gradient_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    RsvgRadialGradient *grad = (RsvgRadialGradient *) self;
    const char *value;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "id")))
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        if ((value = rsvg_property_bag_lookup (atts, "cx"))) {
            _rsvg_parse_prop_length (value, &grad->cx, SVG_ATTRIBUTE);
            grad->hascx = TRUE;
            if (!grad->hasfx)
                grad->fx = grad->cx;
        }
        if ((value = rsvg_property_bag_lookup (atts, "cy"))) {
            _rsvg_parse_prop_length (value, &grad->cy, SVG_ATTRIBUTE);
            grad->hascy = TRUE;
            if (!grad->hasfy)
                grad->fy = grad->cy;
        }
        if ((value = rsvg_property_bag_lookup (atts, "r"))) {
            _rsvg_parse_prop_length (value, &grad->r, SVG_ATTRIBUTE);
            grad->hasr = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "fx"))) {
            _rsvg_parse_prop_length (value, &grad->fx, SVG_ATTRIBUTE);
            grad->hasfx = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "fy"))) {
            _rsvg_parse_prop_length (value, &grad->fy, SVG_ATTRIBUTE);
            grad->hasfy = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "xlink:href"))) {
            if (self != rsvg_defs_lookup (ctx->priv->defs, value))
                rsvg_defs_add_resolver (ctx->priv->defs, &grad->fallback, value);
        }
        if ((value = rsvg_property_bag_lookup (atts, "gradientTransform"))) {
            if (rsvg_parse_transform (&grad->affine, value))
                grad->hastransform = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "spreadMethod"))) {
            if (!strcmp (value, "pad"))
                grad->spread = CAIRO_EXTEND_PAD;
            else if (!strcmp (value, "reflect"))
                grad->spread = CAIRO_EXTEND_REFLECT;
            else if (!strcmp (value, "repeat"))
                grad->spread = CAIRO_EXTEND_REPEAT;
            grad->hasspread = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "gradientUnits"))) {
            if (!strcmp (value, "userSpaceOnUse"))
                grad->obj_bbox = FALSE;
            else if (!strcmp (value, "objectBoundingBox"))
                grad->obj_bbox = TRUE;
            grad->hasbbox = TRUE;
        }
        rsvg_set_presentation_props (ctx, self->state, "radialGradient", NULL, NULL, atts);
    }
}

RsvgNode *
rsvg_new_radial_gradient (void)
{

    RsvgRadialGradient *grad = g_new (RsvgRadialGradient, 1);
    _rsvg_node_init (&grad->super, RSVG_NODE_TYPE_RADIAL_GRADIENT);
    cairo_matrix_init_identity (&grad->affine);
    grad->obj_bbox = TRUE;
    grad->spread = CAIRO_EXTEND_PAD;
    grad->fallback = NULL;
    grad->cx = grad->cy = grad->r = grad->fx = grad->fy = (RsvgLength) {0.5, RSVG_UNIT_NUMBER}; /* TODO: should be 50.0% */
    grad->super.set_atts = rsvg_radial_gradient_set_atts;
    grad->hascx = grad->hascy = grad->hasfx = grad->hasfy = grad->hasr = grad->hasbbox =
        grad->hasspread = grad->hastransform = FALSE;
    return &grad->super;
}

static void
rsvg_pattern_set_atts (RsvgNode * self, RsvgHandle * ctx, RsvgPropertyBag * atts)
{
    RsvgPattern *pattern = (RsvgPattern *) self;
    const char *value;

    if (rsvg_property_bag_size (atts)) {
        if ((value = rsvg_property_bag_lookup (atts, "id")))
            rsvg_defs_register_name (ctx->priv->defs, value, self);
        if ((value = rsvg_property_bag_lookup (atts, "viewBox"))) {
            rsvg_parse_viewbox (value, &pattern->vbox);
            pattern->hasvbox = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "x"))) {
            _rsvg_parse_prop_length (value, &pattern->x, SVG_ATTRIBUTE);
            pattern->hasx = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "y"))) {
            _rsvg_parse_prop_length (value, &pattern->y, SVG_ATTRIBUTE);
            pattern->hasy = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "width"))) {
            _rsvg_parse_prop_length (value, &pattern->width, SVG_ATTRIBUTE);
            pattern->haswidth = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "height"))) {
            _rsvg_parse_prop_length (value, &pattern->height, SVG_ATTRIBUTE);
            pattern->hasheight = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "xlink:href"))) {
            if (self != rsvg_defs_lookup (ctx->priv->defs, value)) {
                /* The (void *) cast is to avoid a GCC warning like:
                 * "warning: dereferencing type-punned pointer will break strict-aliasing rules"
                 * which is wrong for this code. (void *) introduces a compatible
                 * intermediate type in the cast list. */
                rsvg_defs_add_resolver (ctx->priv->defs, (RsvgNode **) (void *) &pattern->fallback,
                                        value);
            }
        }
        if ((value = rsvg_property_bag_lookup (atts, "patternTransform"))) {
            if (rsvg_parse_transform (&pattern->affine, value))
                pattern->hastransform = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "patternUnits"))) {
            if (!strcmp (value, "userSpaceOnUse"))
                pattern->obj_bbox = FALSE;
            else if (!strcmp (value, "objectBoundingBox"))
                pattern->obj_bbox = TRUE;
            pattern->hasbbox = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "patternContentUnits"))) {
            if (!strcmp (value, "userSpaceOnUse"))
                pattern->obj_cbbox = FALSE;
            else if (!strcmp (value, "objectBoundingBox"))
                pattern->obj_cbbox = TRUE;
            pattern->hascbox = TRUE;
        }
        if ((value = rsvg_property_bag_lookup (atts, "preserveAspectRatio"))) {
            pattern->preserve_aspect_ratio = rsvg_css_parse_aspect_ratio (value);
            pattern->hasaspect = TRUE;
        }
    }
}


RsvgNode *
rsvg_new_pattern (void)
{
    RsvgPattern *pattern = g_new (RsvgPattern, 1);
    _rsvg_node_init (&pattern->super, RSVG_NODE_TYPE_PATTERN);
    cairo_matrix_init_identity (&pattern->affine);
    pattern->obj_bbox = TRUE;
    pattern->obj_cbbox = FALSE;
    pattern->x = pattern->y = pattern->width = pattern->height = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    pattern->fallback = NULL;
    pattern->preserve_aspect_ratio = RSVG_ASPECT_RATIO_XMID_YMID;
    pattern->vbox.active = FALSE;
    pattern->super.set_atts = rsvg_pattern_set_atts;
    pattern->hasx = pattern->hasy = pattern->haswidth = pattern->hasheight = pattern->hasbbox =
        pattern->hascbox = pattern->hasvbox = pattern->hasaspect = pattern->hastransform = FALSE;
    return &pattern->super;
}

static int
hasstop (GPtrArray * lookin)
{
    unsigned int i;
    for (i = 0; i < lookin->len; i++) {
        RsvgNode *node = g_ptr_array_index (lookin, i);
        if (RSVG_NODE_TYPE (node) == RSVG_NODE_TYPE_STOP)
            return 1;
    }
    return 0;
}

void
rsvg_linear_gradient_fix_fallback (RsvgLinearGradient * grad)
{
    RsvgNode *ufallback;
    ufallback = grad->fallback;
    while (ufallback != NULL) {
        if (RSVG_NODE_TYPE (ufallback) == RSVG_NODE_TYPE_LINEAR_GRADIENT) {
            RsvgLinearGradient *fallback = (RsvgLinearGradient *) ufallback;
            if (!grad->hasx1 && fallback->hasx1) {
                grad->hasx1 = TRUE;
                grad->x1 = fallback->x1;
            }
            if (!grad->hasy1 && fallback->hasy1) {
                grad->hasy1 = TRUE;
                grad->y1 = fallback->y1;
            }
            if (!grad->hasx2 && fallback->hasx2) {
                grad->hasx2 = TRUE;
                grad->x2 = fallback->x2;
            }
            if (!grad->hasy2 && fallback->hasy2) {
                grad->hasy2 = TRUE;
                grad->y2 = fallback->y2;
            }
            if (!grad->hastransform && fallback->hastransform) {
                grad->hastransform = TRUE;
                grad->affine = fallback->affine;
            }
            if (!grad->hasspread && fallback->hasspread) {
                grad->hasspread = TRUE;
                grad->spread = fallback->spread;
            }
            if (!grad->hasbbox && fallback->hasbbox) {
                grad->hasbbox = TRUE;
                grad->obj_bbox = fallback->obj_bbox;
            }
            if (!hasstop (grad->super.children) && hasstop (fallback->super.children)) {
                grad->super.children = fallback->super.children;
            }
            ufallback = fallback->fallback;
        } else if (RSVG_NODE_TYPE (ufallback) == RSVG_NODE_TYPE_RADIAL_GRADIENT) {
            RsvgRadialGradient *fallback = (RsvgRadialGradient *) ufallback;
            if (!grad->hastransform && fallback->hastransform) {
                grad->hastransform = TRUE;
                grad->affine = fallback->affine;
            }
            if (!grad->hasspread && fallback->hasspread) {
                grad->hasspread = TRUE;
                grad->spread = fallback->spread;
            }
            if (!grad->hasbbox && fallback->hasbbox) {
                grad->hasbbox = TRUE;
                grad->obj_bbox = fallback->obj_bbox;
            }
            if (!hasstop (grad->super.children) && hasstop (fallback->super.children)) {
                grad->super.children = fallback->super.children;
            }
            ufallback = fallback->fallback;
        }
    }
}

void
rsvg_radial_gradient_fix_fallback (RsvgRadialGradient * grad)
{
    RsvgNode *ufallback;
    ufallback = grad->fallback;
    while (ufallback != NULL) {
        if (RSVG_NODE_TYPE (ufallback) == RSVG_NODE_TYPE_RADIAL_GRADIENT) {
            RsvgRadialGradient *fallback = (RsvgRadialGradient *) ufallback;
            if (!grad->hascx && fallback->hascx) {
                grad->hascx = TRUE;
                grad->cx = fallback->cx;
            }
            if (!grad->hascy && fallback->hascy) {
                grad->hascy = TRUE;
                grad->cy = fallback->cy;
            }
            if (!grad->hasfx && fallback->hasfx) {
                grad->hasfx = TRUE;
                grad->fx = fallback->fx;
            }
            if (!grad->hasfy && fallback->hasfy) {
                grad->hasfy = TRUE;
                grad->fy = fallback->fy;
            }
            if (!grad->hasr && fallback->hasr) {
                grad->hasr = TRUE;
                grad->r = fallback->r;
            }
            if (!grad->hastransform && fallback->hastransform) {
                grad->hastransform = TRUE;
                grad->affine = fallback->affine;
            }
            if (!grad->hasspread && fallback->hasspread) {
                grad->hasspread = TRUE;
                grad->spread = fallback->spread;
            }
            if (!grad->hasbbox && fallback->hasbbox) {
                grad->hasbbox = TRUE;
                grad->obj_bbox = fallback->obj_bbox;
            }
            if (!hasstop (grad->super.children) && hasstop (fallback->super.children)) {
                grad->super.children = fallback->super.children;
            }
            ufallback = fallback->fallback;
        } else if (RSVG_NODE_TYPE (ufallback) == RSVG_NODE_TYPE_LINEAR_GRADIENT) {
            RsvgLinearGradient *fallback = (RsvgLinearGradient *) ufallback;
            if (!grad->hastransform && fallback->hastransform) {
                grad->hastransform = TRUE;
                grad->affine = fallback->affine;
            }
            if (!grad->hasspread && fallback->hasspread) {
                grad->hasspread = TRUE;
                grad->spread = fallback->spread;
            }
            if (!grad->hasbbox && fallback->hasbbox) {
                grad->hasbbox = TRUE;
                grad->obj_bbox = fallback->obj_bbox;
            }
            if (!hasstop (grad->super.children) && hasstop (fallback->super.children)) {
                grad->super.children = fallback->super.children;
            }
            ufallback = fallback->fallback;
        }
    }
}

void
rsvg_pattern_fix_fallback (RsvgPattern * pattern)
{
    RsvgPattern *fallback;
    for (fallback = pattern->fallback; fallback != NULL; fallback = fallback->fallback) {
        if (!pattern->hasx && fallback->hasx) {
            pattern->hasx = TRUE;
            pattern->x = fallback->x;
        }
        if (!pattern->hasy && fallback->hasy) {
            pattern->hasy = TRUE;
            pattern->y = fallback->y;
        }
        if (!pattern->haswidth && fallback->haswidth) {
            pattern->haswidth = TRUE;
            pattern->width = fallback->width;
        }
        if (!pattern->hasheight && fallback->hasheight) {
            pattern->hasheight = TRUE;
            pattern->height = fallback->height;
        }
        if (!pattern->hastransform && fallback->hastransform) {
            pattern->hastransform = TRUE;
            pattern->affine = fallback->affine;
        }
        if (!pattern->hasvbox && fallback->hasvbox) {
            pattern->vbox = fallback->vbox;
        }
        if (!pattern->hasaspect && fallback->hasaspect) {
            pattern->hasaspect = TRUE;
            pattern->preserve_aspect_ratio = fallback->preserve_aspect_ratio;
        }
        if (!pattern->hasbbox && fallback->hasbbox) {
            pattern->hasbbox = TRUE;
            pattern->obj_bbox = fallback->obj_bbox;
        }
        if (!pattern->hascbox && fallback->hascbox) {
            pattern->hascbox = TRUE;
            pattern->obj_cbbox = fallback->obj_cbbox;
        }
        if (!pattern->super.children->len && fallback->super.children->len) {
            pattern->super.children = fallback->super.children;
        }
    }
}
