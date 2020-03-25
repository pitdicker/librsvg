/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-styles.h: Handle SVG styles

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

#ifndef RSVG_STYLES_H
#define RSVG_STYLES_H

#include <cairo.h>
#include "rsvg.h"
#include "rsvg-paint-server.h"

#include <libxml/SAX.h>
#include <pango/pango.h>

#define RSVG_DEFAULT_FONT      "Times New Roman"
#define RSVG_DEFAULT_FONT_SIZE 16.0 /* px */

G_BEGIN_DECLS

typedef int TextDecoration;

enum {
    TEXT_DECORATION_NONE         = 0x00,
    TEXT_DECORATION_UNDERLINE    = 0x01,
    TEXT_DECORATION_OVERLINE     = 0x02,
    TEXT_DECORATION_LINE_THROUGH = 0x04
};

typedef enum {
    TEXT_ANCHOR_START,
    TEXT_ANCHOR_MIDDLE,
    TEXT_ANCHOR_END
} TextAnchor;

typedef enum {
    UNICODE_BIDI_NORMAL = 0,
    UNICODE_BIDI_EMBED = 1,
    UNICODE_BIDI_OVERRIDE = 2
} UnicodeBidi;

typedef enum {
    RSVG_ENABLE_BACKGROUND_ACCUMULATE,
    RSVG_ENABLE_BACKGROUND_NEW
} RsvgEnableBackgroundType;

enum {
    SHAPE_RENDERING_AUTO = CAIRO_ANTIALIAS_DEFAULT,
    SHAPE_RENDERING_OPTIMIZE_SPEED = CAIRO_ANTIALIAS_NONE,
    SHAPE_RENDERING_CRISP_EDGES = CAIRO_ANTIALIAS_NONE,
    SHAPE_RENDERING_GEOMETRIC_PRECISION = CAIRO_ANTIALIAS_DEFAULT
};

enum {
    TEXT_RENDERING_AUTO = CAIRO_ANTIALIAS_DEFAULT,
    TEXT_RENDERING_OPTIMIZE_SPEED = CAIRO_ANTIALIAS_NONE,
    TEXT_RENDERING_OPTIMIZE_LEGIBILITY = CAIRO_ANTIALIAS_DEFAULT,
    TEXT_RENDERING_GEOMETRIC_PRECISION = CAIRO_ANTIALIAS_DEFAULT
};

typedef union {
    struct {
        enum _RsvgPaintType {
            /* TODO: explain */
            NONE = 0xffffffff,
            CURRENT_COLOR = 0xfffffffe,
            LINEAR_GRADIENT = 0xfffffffd,
            RADIAL_GRADIENT = 0xfffffffc,
            PATTERN = 0xfffffffb
        } type;
        RsvgNode *server;
    } paint;
    RsvgColor color;
} RsvgPaint;

enum {
    RSVG_PROP_INITIAL        = 0x00,
    RSVG_PROP_EXPLICITLY_SET = 0x01,
    RSVG_PROP_INHERIT        = 0x02,
    RSVG_PROP_ERROR          = 0x04
};

struct _RsvgState {
    RsvgState         *parent;
    cairo_matrix_t     affine;
    cairo_matrix_t     personal_affine;

    /* presentation attributes */
    RsvgNode          *clip_path;
    cairo_fill_rule_t  clip_rule;
    RsvgColor          color;
    PangoDirection     direction;
    RsvgEnableBackgroundType enable_background;
    RsvgPaint          fill;
    float              fill_opacity;
    cairo_fill_rule_t  fill_rule;
    RsvgNode          *filter;
    RsvgPaint          flood_color;
    float              flood_opacity;
    char              *font_family;
    RsvgLength         font_size;
    PangoStretch       font_stretch;
    PangoStyle         font_style;
    PangoVariant       font_variant;
    PangoWeight        font_weight;
    RsvgLength         letter_spacing;
    RsvgPaint          lighting_color;
    RsvgNode          *marker_start;
    RsvgNode          *marker_mid;
    RsvgNode          *marker_end;
    RsvgNode          *mask;
    float              opacity;
    gboolean           overflow;
    cairo_antialias_t  shape_rendering;
    RsvgPaint          stop_color;
    float              stop_opacity;
    RsvgPaint          stroke;
    RsvgLengthList     stroke_dasharray;
    RsvgLength         stroke_dashoffset;
    cairo_line_cap_t   stroke_linecap;
    cairo_line_join_t  stroke_linejoin;
    double             stroke_miterlimit;
    float              stroke_opacity;
    RsvgLength         stroke_width;
    TextAnchor         text_anchor;
    TextDecoration     text_decoration;
    cairo_antialias_t  text_rendering;
    UnicodeBidi        unicode_bidi;

    /* TODO */
    PangoGravity       text_gravity;
    gboolean           visible;

    /* core xml attributes */
    char              *lang;
    gboolean           space_preserve;

    /* svg 1.2 attribute */
    cairo_operator_t   comp_op;

    /* ??? */
    gboolean           cond_true;

    gboolean has_fill_server;
    gboolean has_fill_opacity;
    gboolean has_fill_rule;
    gboolean has_clip_rule;
    gboolean has_overflow;
    gboolean has_stroke_server;
    gboolean has_stroke_opacity;
    gboolean has_stroke_width;
    gboolean has_miter_limit;
    gboolean has_cap;
    gboolean has_join;
    gboolean has_font_size;
    gboolean has_font_family;
    gboolean has_lang;
    gboolean has_font_style;
    gboolean has_font_variant;
    gboolean has_font_weight;
    gboolean has_font_stretch;
    gboolean has_font_decor;
    gboolean has_text_dir;
    gboolean has_text_gravity;
    gboolean has_unicode_bidi;
    gboolean has_text_anchor;
    gboolean has_letter_spacing;
    gboolean has_stop_color;
    gboolean has_stop_opacity;
    gboolean has_visible;
    gboolean has_space_preserve;
    gboolean has_cond;
    gboolean has_dash;
    gboolean has_dashoffset;
    gboolean has_current_color;
    gboolean has_flood_color;
    gboolean has_flood_opacity;
    gboolean has_startMarker;
    gboolean has_middleMarker;
    gboolean has_endMarker;
    gboolean has_shape_rendering_type;
    gboolean has_text_rendering_type;
};

typedef struct _StyleValueData {
    gchar *value;
    gboolean important;
} StyleValueData;

typedef enum {
    HORIZONTAL,
    VERTICAL,
    NO_DIR
} RsvgLengthDir;

G_GNUC_INTERNAL
RsvgState *rsvg_state_new (void);

G_GNUC_INTERNAL
StyleValueData *style_value_data_new (const gchar *value, const gboolean important);
G_GNUC_INTERNAL
void rsvg_state_init        (RsvgState *state);
G_GNUC_INTERNAL
void rsvg_state_reinit      (RsvgState *state);
G_GNUC_INTERNAL
void rsvg_state_clone       (RsvgState *dst, const RsvgState *src);
G_GNUC_INTERNAL
void rsvg_state_inherit     (RsvgState *dst, const RsvgState *src);
G_GNUC_INTERNAL
void rsvg_state_reinherit   (RsvgState *dst, const RsvgState *src);
G_GNUC_INTERNAL
void rsvg_state_dominate    (RsvgState *dst, const RsvgState *src);
G_GNUC_INTERNAL
void rsvg_state_override    (RsvgState *dst, const RsvgState *src);
G_GNUC_INTERNAL
void rsvg_state_finalize    (const RsvgState *state);
G_GNUC_INTERNAL
void rsvg_state_free_all    (RsvgState *state);

G_GNUC_INTERNAL
void rsvg_parse_presentation_attr (const RsvgHandle *ctx, RsvgState *state, const RsvgPropertyBag *atts);
G_GNUC_INTERNAL
void rsvg_parse_style             (const RsvgHandle *ctx, RsvgState *state, const char *str);
G_GNUC_INTERNAL
void rsvg_parse_cssbuffer         (RsvgHandle *ctx, const char *buff, size_t buflen);
G_GNUC_INTERNAL
void rsvg_set_presentation_props  (const RsvgHandle *ctx, RsvgState *state,
                                   const char *tag, const char *klazz, const char *id,
                                   const RsvgPropertyBag *atts);

G_GNUC_INTERNAL
gboolean rsvg_parse_transform   (cairo_matrix_t *matrix, const char *src);

G_GNUC_INTERNAL
RsvgState *rsvg_state_parent    (const RsvgState *state);

G_GNUC_INTERNAL
void       rsvg_state_pop       (RsvgDrawingCtx *ctx);
G_GNUC_INTERNAL
void       rsvg_state_push      (RsvgDrawingCtx *ctx);
G_GNUC_INTERNAL
RsvgState *rsvg_current_state   (const RsvgDrawingCtx *ctx);

G_GNUC_INTERNAL
void rsvg_state_reinherit_top   (const RsvgDrawingCtx *ctx, RsvgState *state, const int dominate);

G_GNUC_INTERNAL
void rsvg_state_reconstruct     (RsvgState *state, RsvgNode *current);

G_GNUC_INTERNAL
double rsvg_normalize_length           (const RsvgLength in, const RsvgDrawingCtx *ctx,
                                        const RsvgLengthDir dir);
G_GNUC_INTERNAL
double rsvg_normalize_font_size        (const RsvgState *state, const RsvgDrawingCtx *ctx);
G_GNUC_INTERNAL
guint  rsvg_normalize_stroke_dasharray (const RsvgLengthList src, double **dst, const RsvgDrawingCtx *ctx);

G_END_DECLS

#endif                          /* RSVG_STYLES_H */
