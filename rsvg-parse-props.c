/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-parse-props.c: Parse SVG presentation properties

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
#include "config.h"
#include "rsvg-parse-props.h"

#include <string.h>

#include "rsvg-styles.h"
#include "rsvg-css.h"
#include "rsvg-paint-server.h"

#define SETINHERIT() G_STMT_START {if (inherit != NULL) *inherit = TRUE;} G_STMT_END
#define UNSETINHERIT() G_STMT_START {if (inherit != NULL) *inherit = FALSE;} G_STMT_END

static const char * rsvg_parse_font_family  (const char *str, gboolean * inherit);
static PangoStretch rsvg_parse_font_stretch (const char *str, gboolean * inherit);
static PangoStyle   rsvg_parse_font_style   (const char *str, gboolean * inherit);
static PangoVariant rsvg_parse_font_variant (const char *str, gboolean * inherit);
static PangoWeight  rsvg_parse_font_weight  (const char *str, gboolean * inherit);

static gboolean rsvg_parse_font_size    (const char *str, RsvgLength *result, const RsvgPropSrc prop_src);

static int
rsvg_cmp_keyword (const void *str, const void *b)
{
    struct keywords {
        const char *keyword;
    };
    return strcmp ((const char *) str, ((struct keywords *) b)->keyword);
}

static int
rsvg_casecmp_keyword (const void *str, const void *b)
{
    struct keywords {
        const char *keyword;
    };
    return g_ascii_strcasecmp ((const char *) str, ((struct keywords *) b)->keyword);
}

#define rsvg_match_keyword(str, keywords, prop_src) bsearch (str, keywords, sizeof (keywords) / sizeof (keywords[0]), sizeof (keywords[0]), (prop_src == CSS_VALUE)? rsvg_casecmp_keyword : rsvg_cmp_keyword)

/**
 * _rsvg_parse_number:
 * Parse a number using g_ascii_strtod, but do extra checks to ensure the number
 * matches the stricter requirements of an svg number, css number, or a number
 * in svg path data.
 * If str does not start with a valid number, 0.0 is returned and *end = str.
 */
double
_rsvg_parse_number (const char *str, const char **end, const RsvgNumberFormat format)
{
    double number;

    *end = str;

    if (*str == '.') {
        /* '.' must be followed by a number */
        if (!g_ascii_isdigit(str[1]))
            return 0.;
    } else if (*str == '+' || *str == '-') {
        /* '+' or '-' must be followed by a digit,
           or by a '.' that is followed by a digit */
        if (!(g_ascii_isdigit(str[1]) ||
              (str[1] == '.' && g_ascii_isdigit(str[2]))))
            return 0.;
    } else if (!g_ascii_isdigit(*str)) {
        return 0.;
    }

    number = g_ascii_strtod (str, (gchar **) end);
    /* out-of-range values are clamped by strtod, no need to check errno for ERANGE */

    /* In 'path data' or 'point specification', a number may end with '.', but
       in svg attributes and css properties it may not. */
    if (format != RSVG_NUMBER_FORMAT_PATHDATA && (*end)[-1] == '.')
        (*end)--;

    /* css2 does not allow exponential notation */
    if (format == RSVG_NUMBER_FORMAT_CSS2) {
        while (str < *end) {
            if (*str == 'E' || *str == 'e') {
                /* undo exponent */
                gint64 exponent;
                *end = str;
                str++;
                exponent = g_ascii_strtoll (*end + 1, (gchar **) &str, 10);
                number *= pow (10, -exponent);
                break;
            }
            str++;
        }
    }

    return number;
}

/**
 * _rsvg_parse_length:
 * Parse the basic data type 'length'.
 * If str does not start with a valid length, 0.0 is returned and *end = str.
 */
RsvgLength
_rsvg_parse_length (const char *str, const char **end, const RsvgPropSrc prop_src)
{
    RsvgLength out;
    RsvgNumberFormat format;

    struct length_units {
        const char *keyword;
        RsvgLengthUnit value;
    };
    const struct length_units units[] = {
        {"%",  RSVG_UNIT_PERCENTAGE},
        {"cm", RSVG_UNIT_CM},
        {"em", RSVG_UNIT_EMS},
        {"ex", RSVG_UNIT_EXS},
        {"in", RSVG_UNIT_IN},
        {"mm", RSVG_UNIT_MM},
        {"pc", RSVG_UNIT_PC},
        {"pt", RSVG_UNIT_PT},
        {"px", RSVG_UNIT_PX}
    };
    struct length_units *unit;

    g_assert (str != NULL);

    switch (prop_src) {
    case SVG_ATTRIBUTE:
        format = RSVG_NUMBER_FORMAT_SVG;
        break;
    case CSS_VALUE:
        format = RSVG_NUMBER_FORMAT_CSS2;
        break;
    }

    out.length = _rsvg_parse_number (str, end, format);
    if (*end == str) /* invalid number */
        return (RsvgLength) {0.0, RSVG_UNIT_UNKNOWN}; /* TODO: will this give problems? */

    if ((unit = rsvg_match_keyword (*end, units, prop_src))) {
        out.unit = unit->value;
        if (out.unit == RSVG_UNIT_PERCENTAGE)
            *end += 1;
        else
            *end += 2;
    } else {
        out.unit = RSVG_UNIT_NUMBER;
    }

    return out;
}

/* ========================================================================== */

gboolean
_rsvg_parse_prop_length (const char *str, RsvgLength *result, const RsvgPropSrc prop_src)
{
    RsvgLength length;
    const char *end;

    g_assert (str != NULL);

    length = _rsvg_parse_length (str, &end, prop_src);
    if (str == end || *end != '\0') {
        printf ("invalid length: '%s'\n", str); /* TODO: report errors properly */
        return FALSE;
    }

    *result = length;
    return TRUE;
}

/* ========================================================================== */

static RsvgNode *
rsvg_parse_clip_path (const RsvgDefs * defs, const char *str)
{
    char *name;

    name = rsvg_get_url_string (str);
    if (name) {
        RsvgNode *val;
        val = rsvg_defs_lookup (defs, name);
        g_free (name);

        if (val && RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_CLIP_PATH)
            return val;
    }
    return NULL;
}

/**
 * rsvg_parse_paint_server:
 * @defs: Defs for looking up gradients.
 * @str: The SVG paint specification string to parse.
 *
 * Parses the paint specification @str, creating a new paint server
 * object.
 *
 * Return value: The newly created paint server, or NULL on error.
 **/
RsvgPaintServer *
rsvg_parse_paint_server (gboolean * inherit, const RsvgDefs * defs, const char *str,
                         guint32 current_color)
{
    char *name;
    guint32 argb;
    if (inherit != NULL)
        *inherit = 1;
    if (str == NULL || !strcmp (str, "none"))
        return NULL;

    name = rsvg_get_url_string (str);
    if (name) {
        RsvgNode *val;
        val = rsvg_defs_lookup (defs, name);
        g_free (name);

        if (val == NULL)
            return NULL;
        if (RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_LINEAR_GRADIENT)
            return rsvg_paint_server_lin_grad ((RsvgLinearGradient *) val);
        else if (RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_RADIAL_GRADIENT)
            return rsvg_paint_server_rad_grad ((RsvgRadialGradient *) val);
        else if (RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_PATTERN)
            return rsvg_paint_server_pattern ((RsvgPattern *) val);
        else
            return NULL;
    } else if (!strcmp (str, "inherit")) {
        if (inherit != NULL)
            *inherit = 0;
        return rsvg_paint_server_solid (0);
    } else if (!strcmp (str, "currentColor")) {
        RsvgPaintServer *ps;
        ps = rsvg_paint_server_solid_current_colour ();
        return ps;
    } else {
        argb = rsvg_css_parse_color (str, inherit);
        return rsvg_paint_server_solid (argb);
    }
}

/**
 * rsvg_parse_filter:
 * @defs: a pointer to the hash of definitions
 * @str: a string with the name of the filter to be looked up
 *
 * Looks up an allready created filter.
 *
 * Returns: a pointer to the filter that the name refers to, or NULL
 * if none was found
 **/
static RsvgFilter *
rsvg_parse_filter (const RsvgDefs * defs, const char *str)
{
    char *name;

    name = rsvg_get_url_string (str);
    if (name) {
        RsvgNode *val;
        val = rsvg_defs_lookup (defs, name);
        g_free (name);

        if (val && RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_FILTER)
            return (RsvgFilter *) val;
    }
    return NULL;
}

static const char *
rsvg_parse_font_family (const char *str, gboolean * inherit)
{
    SETINHERIT ();

    if (str == NULL)
        return NULL;
    else if (!strcmp (str, "inherit")) {
        UNSETINHERIT ();
        return NULL;
    } else
        return str;
}

static gboolean
rsvg_parse_font_size (const char *str, RsvgLength *result, const RsvgPropSrc prop_src)
{
    RsvgLength length;
    const char *end;

    struct font_size_keywords {
        const char *keyword;
        RsvgLength value;
    };
    const struct font_size_keywords keywords[] = {
        {"large",    {RSVG_DEFAULT_FONT_SIZE * 1.2, RSVG_UNIT_PX}},
        {"larger",   {1.2, RSVG_UNIT_EMS}},
        {"medium",   {RSVG_DEFAULT_FONT_SIZE, RSVG_UNIT_PX}},
        {"small",    {RSVG_DEFAULT_FONT_SIZE / 1.2, RSVG_UNIT_PX}},
        {"smaller",  {1.0 / 1.2, RSVG_UNIT_EMS}},
        {"x-large",  {RSVG_DEFAULT_FONT_SIZE * (1.2 * 1.2), RSVG_UNIT_PX}},
        {"x-small",  {RSVG_DEFAULT_FONT_SIZE / (1.2 * 1.2), RSVG_UNIT_PX}},
        {"xx-large", {RSVG_DEFAULT_FONT_SIZE * (1.2 * 1.2 * 1.2), RSVG_UNIT_PX}},
        {"xx-small", {RSVG_DEFAULT_FONT_SIZE / (1.2 * 1.2 * 1.2), RSVG_UNIT_PX}}
    };
    struct font_size_keywords *keyword;

    g_assert (str != NULL);

    if ((keyword = rsvg_match_keyword (str, keywords, prop_src))) {
        *result = keyword->value;
    } else {
        /* try normal length values */
        length = _rsvg_parse_length (str, &end, prop_src);
        if (str == end || *end != '\0' ||
            (prop_src == CSS_VALUE && length.unit == RSVG_UNIT_NUMBER && length.length != 0.0) ) {
            printf ("invalid font size: '%s'\n", str); /* TODO: report errors properly */
            return FALSE;
        }
        *result = length;
    }
    return TRUE;
}

static PangoStretch
rsvg_parse_font_stretch (const char *str, gboolean * inherit)
{
    SETINHERIT ();

    if (str) {
        if (!strcmp (str, "ultra-condensed"))
            return PANGO_STRETCH_ULTRA_CONDENSED;
        else if (!strcmp (str, "extra-condensed"))
            return PANGO_STRETCH_EXTRA_CONDENSED;
        else if (!strcmp (str, "condensed") || !strcmp (str, "narrower"))       /* narrower not quite correct */
            return PANGO_STRETCH_CONDENSED;
        else if (!strcmp (str, "semi-condensed"))
            return PANGO_STRETCH_SEMI_CONDENSED;
        else if (!strcmp (str, "semi-expanded"))
            return PANGO_STRETCH_SEMI_EXPANDED;
        else if (!strcmp (str, "expanded") || !strcmp (str, "wider"))   /* wider not quite correct */
            return PANGO_STRETCH_EXPANDED;
        else if (!strcmp (str, "extra-expanded"))
            return PANGO_STRETCH_EXTRA_EXPANDED;
        else if (!strcmp (str, "ultra-expanded"))
            return PANGO_STRETCH_ULTRA_EXPANDED;
        else if (!strcmp (str, "inherit")) {
            UNSETINHERIT ();
            return PANGO_STRETCH_NORMAL;
        }
    }
    UNSETINHERIT ();
    return PANGO_STRETCH_NORMAL;
}

static PangoStyle
rsvg_parse_font_style (const char *str, gboolean * inherit)
{
    SETINHERIT ();

    if (str) {
        if (!strcmp (str, "oblique"))
            return PANGO_STYLE_OBLIQUE;
        if (!strcmp (str, "italic"))
            return PANGO_STYLE_ITALIC;
        else if (!strcmp (str, "inherit")) {
            UNSETINHERIT ();
            return PANGO_STYLE_NORMAL;
        }
    }
    UNSETINHERIT ();
    return PANGO_STYLE_NORMAL;
}

static PangoVariant
rsvg_parse_font_variant (const char *str, gboolean * inherit)
{
    SETINHERIT ();

    if (str) {
        if (!strcmp (str, "small-caps"))
            return PANGO_VARIANT_SMALL_CAPS;
        else if (!strcmp (str, "inherit")) {
            UNSETINHERIT ();
            return PANGO_VARIANT_NORMAL;
        }
    }
    UNSETINHERIT ();
    return PANGO_VARIANT_NORMAL;
}

static PangoWeight
rsvg_parse_font_weight (const char *str, gboolean * inherit)
{
    SETINHERIT ();
    if (str) {
        if (!strcmp (str, "lighter"))
            return PANGO_WEIGHT_LIGHT;
        else if (!strcmp (str, "bold"))
            return PANGO_WEIGHT_BOLD;
        else if (!strcmp (str, "bolder"))
            return PANGO_WEIGHT_ULTRABOLD;
        else if (!strcmp (str, "100"))
            return (PangoWeight) 100;
        else if (!strcmp (str, "200"))
            return (PangoWeight) 200;
        else if (!strcmp (str, "300"))
            return (PangoWeight) 300;
        else if (!strcmp (str, "400"))
            return (PangoWeight) 400;
        else if (!strcmp (str, "500"))
            return (PangoWeight) 500;
        else if (!strcmp (str, "600"))
            return (PangoWeight) 600;
        else if (!strcmp (str, "700"))
            return (PangoWeight) 700;
        else if (!strcmp (str, "800"))
            return (PangoWeight) 800;
        else if (!strcmp (str, "900"))
            return (PangoWeight) 900;
        else if (!strcmp (str, "inherit")) {
            UNSETINHERIT ();
            return PANGO_WEIGHT_NORMAL;
        }
    }

    UNSETINHERIT ();
    return PANGO_WEIGHT_NORMAL;
}

static RsvgNode *
rsvg_parse_marker (const RsvgDefs * defs, const char *str)
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

static RsvgNode *
rsvg_parse_mask (const RsvgDefs * defs, const char *str)
{
    char *name;

    name = rsvg_get_url_string (str);
    if (name) {
        RsvgNode *val;
        val = rsvg_defs_lookup (defs, name);
        g_free (name);

        if (val && RSVG_NODE_TYPE (val) == RSVG_NODE_TYPE_MASK)
            return val;
    }
    return NULL;
}

static gboolean
rsvg_parse_overflow (const char *str, gboolean * inherit)
{
    SETINHERIT ();
    if (!strcmp (str, "visible") || !strcmp (str, "auto"))
        return 1;
    if (!strcmp (str, "hidden") || !strcmp (str, "scroll"))
        return 0;
    UNSETINHERIT ();
    return 0;
}

/* Parse a CSS2 style argument, setting the SVG context attributes. */
void
rsvg_parse_prop (RsvgHandle * ctx,
                 RsvgState * state,
                 const gchar * name,
                 const gchar * value,
                 gboolean important,
                 const RsvgPropSrc prop_src)
{
    StyleValueData *data;

    data = g_hash_table_lookup (state->styles, name);
    if (data && data->important && !important)
        return;

    if (name == NULL || value == NULL)
        return;

    g_hash_table_insert (state->styles,
                         (gpointer) g_strdup (name),
                         (gpointer) style_value_data_new (value, important));

    if (g_str_equal (name, "alignment-baseline")) {
        /* TODO */
    } else if (g_str_equal (name, "baseline-shift")) {
        /* TODO */
    } else if (g_str_equal (name, "clip")) {
        /* TODO */
    } else if (g_str_equal (name, "clip-path")) {
        state->clip_path_ref = rsvg_parse_clip_path (ctx->priv->defs, value);
    } else if (g_str_equal (name, "clip-rule")) {
        state->has_clip_rule = TRUE;
        if (g_str_equal (value, "nonzero"))
            state->clip_rule = CAIRO_FILL_RULE_WINDING;
        else if (g_str_equal (value, "evenodd"))
            state->clip_rule = CAIRO_FILL_RULE_EVEN_ODD;
        else
            state->has_clip_rule = FALSE;
    } else if (g_str_equal (name, "color")) {
        state->current_color = rsvg_css_parse_color (value, &state->has_current_color);
    } else if (g_str_equal (name, "color-interpolation")) {
        /* TODO */
    } else if (g_str_equal (name, "color-interpolation-filters")) {
        /* TODO */
    } else if (g_str_equal (name, "color-profile")) {
        /* TODO */
    } else if (g_str_equal (name, "color-rendering")) {
        /* TODO */
    } else if (g_str_equal (name, "cursor")) {
        /* TODO */
    } else if (g_str_equal (name, "direction")) {
        state->has_text_dir = TRUE;
        if (g_str_equal (value, "inherit")) {
            state->text_dir = PANGO_DIRECTION_LTR;
            state->has_text_dir = FALSE;
        } else if (g_str_equal (value, "rtl"))
            state->text_dir = PANGO_DIRECTION_RTL;
        else                    /* ltr */
            state->text_dir = PANGO_DIRECTION_LTR;
    } else if (g_str_equal (name, "display")) {
        state->has_visible = TRUE;
        if (g_str_equal (value, "none"))
            state->visible = FALSE;
        else if (!g_str_equal (value, "inherit") != 0)
            state->visible = TRUE;
        else
            state->has_visible = FALSE;
    } else if (g_str_equal (name, "dominant-baseline")) {
        /* TODO */
    } else if (g_str_equal (name, "enable-background")) {
        if (g_str_equal (value, "new"))
            state->enable_background = RSVG_ENABLE_BACKGROUND_NEW;
        else
            state->enable_background = RSVG_ENABLE_BACKGROUND_ACCUMULATE;
    } else if (g_str_equal (name, "fill")) {
        RsvgPaintServer *fill = state->fill;
        state->fill =
            rsvg_parse_paint_server (&state->has_fill_server, ctx->priv->defs, value, 0);
        rsvg_paint_server_unref (fill);
    } else if (g_str_equal (name, "fill-opacity")) {
        state->fill_opacity = rsvg_css_parse_opacity (value);
        state->has_fill_opacity = TRUE;
    } else if (g_str_equal (name, "fill-rule")) {
        state->has_fill_rule = TRUE;
        if (g_str_equal (value, "nonzero"))
            state->fill_rule = CAIRO_FILL_RULE_WINDING;
        else if (g_str_equal (value, "evenodd"))
            state->fill_rule = CAIRO_FILL_RULE_EVEN_ODD;
        else
            state->has_fill_rule = FALSE;
    } else if (g_str_equal (name, "filter")) {
        state->filter = rsvg_parse_filter (ctx->priv->defs, value);
    } else if (g_str_equal (name, "flood-color")) {
        state->flood_color = rsvg_css_parse_color (value, &state->has_flood_color);
    } else if (g_str_equal (name, "flood-opacity")) {
        state->flood_opacity = rsvg_css_parse_opacity (value);
        state->has_flood_opacity = TRUE;
    } else if (g_str_equal (name, "font")) {
        /* TODO */
    } else if (g_str_equal (name, "font-family")) {
        char *save = g_strdup (rsvg_parse_font_family (value, &state->has_font_family));
        g_free (state->font_family);
        state->font_family = save;
    } else if (g_str_equal (name, "font-size")) {
        rsvg_parse_font_size (value, &state->font_size, prop_src);
        state->has_font_size = TRUE;
    } else if (g_str_equal (name, "font-size-adjust")) {
        /* TODO */
    } else if (g_str_equal (name, "font-stretch")) {
        state->font_stretch = rsvg_parse_font_stretch (value, &state->has_font_stretch);
    } else if (g_str_equal (name, "font-style")) {
        state->font_style = rsvg_parse_font_style (value, &state->has_font_style);
    } else if (g_str_equal (name, "font-variant")) {
        state->font_variant = rsvg_parse_font_variant (value, &state->has_font_variant);
    } else if (g_str_equal (name, "font-weight")) {
        state->font_weight = rsvg_parse_font_weight (value, &state->has_font_weight);
    } else if (g_str_equal (name, "glyph-orientation-horizontal")) {
        /* TODO */
    } else if (g_str_equal (name, "glyph-orientation-vertical")) {
        /* TODO */
    } else if (g_str_equal (name, "image-rendering")) {
        /* TODO */
    } else if (g_str_equal (name, "kerning")) {
        /* TODO */
    } else if (g_str_equal (name, "letter-spacing")) {
        state->has_letter_spacing = TRUE;
        _rsvg_parse_prop_length (value, &state->letter_spacing, prop_src);
    } else if (g_str_equal (name, "lighting-color")) {
        /* TODO */
    } else if (g_str_equal (name, "marker")) {
        /* TODO */
    } else if (g_str_equal (name, "marker-start")) {
        state->startMarker = rsvg_parse_marker (ctx->priv->defs, value);
        state->has_startMarker = TRUE;
    } else if (g_str_equal (name, "marker-mid")) {
        state->middleMarker = rsvg_parse_marker (ctx->priv->defs, value);
        state->has_middleMarker = TRUE;
    } else if (g_str_equal (name, "marker-end")) {
        state->endMarker = rsvg_parse_marker (ctx->priv->defs, value);
        state->has_endMarker = TRUE;
    } else if (g_str_equal (name, "mask")) {
        state->mask = rsvg_parse_mask (ctx->priv->defs, value);
    } else if (g_str_equal (name, "opacity")) {
        state->opacity = rsvg_css_parse_opacity (value);
    } else if (g_str_equal (name, "overflow")) {
        if (!g_str_equal (value, "inherit")) {
            state->overflow = rsvg_parse_overflow (value, &state->has_overflow);
        }
    } else if (g_str_equal (name, "pointer-events")) {
        /* TODO */
    } else if (g_str_equal (name, "shape-rendering")) {
        state->has_shape_rendering_type = TRUE;
        if (g_str_equal (value, "auto") || g_str_equal (value, "default"))
            state->shape_rendering_type = SHAPE_RENDERING_AUTO;
        else if (g_str_equal (value, "optimizeSpeed"))
            state->shape_rendering_type = SHAPE_RENDERING_OPTIMIZE_SPEED;
        else if (g_str_equal (value, "crispEdges"))
            state->shape_rendering_type = SHAPE_RENDERING_CRISP_EDGES;
        else if (g_str_equal (value, "geometricPrecision"))
            state->shape_rendering_type = SHAPE_RENDERING_GEOMETRIC_PRECISION;
    } else if (g_str_equal (name, "stop-color")) {
        if (!g_str_equal (value, "inherit")) {
            state->stop_color = rsvg_css_parse_color (value, &state->has_stop_color);
        }
    } else if (g_str_equal (name, "stop-opacity")) {
        if (!g_str_equal (value, "inherit")) {
            state->has_stop_opacity = TRUE;
            state->stop_opacity = rsvg_css_parse_opacity (value);
        }
    } else if (g_str_equal (name, "stroke")) {
        RsvgPaintServer *stroke = state->stroke;
        state->stroke =
            rsvg_parse_paint_server (&state->has_stroke_server, ctx->priv->defs, value, 0);
        rsvg_paint_server_unref (stroke);
    } else if (g_str_equal (name, "stroke-dasharray")) {
        state->has_dash = TRUE;
        if (g_str_equal (value, "none")) {
            if (state->dash.n_dash != 0) {
                /* free any cloned dash data */
                g_free (state->dash.dash);
                state->dash.n_dash = 0;
            }
        } else {
            gchar **dashes = g_strsplit_set (value, ", ", -1);
            if (NULL != dashes) {
                gint n_dashes, i, j;
                gboolean is_even = FALSE;
                gdouble total = 0;

                /* count the #dashes */
                n_dashes = 0;
                for (i = 0; dashes[i] != NULL; i++) {
                    if (*dashes[i] != '\0')
                        n_dashes++;
                }

                is_even = (n_dashes % 2 == 0);
                state->dash.n_dash = (is_even ? n_dashes : n_dashes * 2);
                state->dash.dash = g_new (double, state->dash.n_dash);

                /* TODO: handle negative value == error case */

                /* the even and base case */
                i = j = 0;
                while (dashes[i] != NULL) {
                    if (*dashes[i] != '\0') {
                        /* TODO: use _rsvg_parse_length */
                        state->dash.dash[j] = g_ascii_strtod (dashes[i], NULL);
                        total += state->dash.dash[j];
                        j++;
                    }
                    i++;
                }
                /* if an odd number of dashes is found, it gets repeated */
                if (!is_even)
                    for (; j < state->dash.n_dash; j++)
                        state->dash.dash[j] = state->dash.dash[j - n_dashes];

                g_strfreev (dashes);
                /* If the dashes add up to 0, then it should
                   be ignored */
                if (total == 0) {
                    g_free (state->dash.dash);
                    state->dash.n_dash = 0;
                }
            }
        }
    } else if (g_str_equal (name, "stroke-dashoffset")) {
        if (_rsvg_parse_prop_length (value, &state->dash.offset, prop_src)) {
            state->has_dashoffset = TRUE;
            if (state->dash.offset.length < 0.)
                state->dash.offset.length = 0.;
        }
    } else if (g_str_equal (name, "stroke-linecap")) {
        state->has_cap = TRUE;
        if (g_str_equal (value, "butt"))
            state->cap = CAIRO_LINE_CAP_BUTT;
        else if (g_str_equal (value, "round"))
            state->cap = CAIRO_LINE_CAP_ROUND;
        else if (g_str_equal (value, "square"))
            state->cap = CAIRO_LINE_CAP_SQUARE;
        else
            g_warning (_("unknown line cap style %s\n"), value);
    } else if (g_str_equal (name, "stroke-miterlimit")) {
        state->has_miter_limit = TRUE;
        state->miter_limit = g_ascii_strtod (value, NULL);
    } else if (g_str_equal (name, "stroke-opacity")) {
        state->stroke_opacity = rsvg_css_parse_opacity (value);
        state->has_stroke_opacity = TRUE;
    } else if (g_str_equal (name, "stroke-linejoin")) {
        state->has_join = TRUE;
        if (g_str_equal (value, "miter"))
            state->join = CAIRO_LINE_JOIN_MITER;
        else if (g_str_equal (value, "round"))
            state->join = CAIRO_LINE_JOIN_ROUND;
        else if (g_str_equal (value, "bevel"))
            state->join = CAIRO_LINE_JOIN_BEVEL;
        else
            g_warning (_("unknown line join style %s\n"), value);
    } else if (g_str_equal (name, "stroke-width")) {
        _rsvg_parse_prop_length (value, &state->stroke_width, prop_src);
        state->has_stroke_width = TRUE;
    } else if (g_str_equal (name, "text-anchor")) {
        state->has_text_anchor = TRUE;
        if (g_str_equal (value, "inherit")) {
            state->text_anchor = TEXT_ANCHOR_START;
            state->has_text_anchor = FALSE;
        } else {
            if (strstr (value, "start"))
                state->text_anchor = TEXT_ANCHOR_START;
            else if (strstr (value, "middle"))
                state->text_anchor = TEXT_ANCHOR_MIDDLE;
            else if (strstr (value, "end"))
                state->text_anchor = TEXT_ANCHOR_END;
        }
    } else if (g_str_equal (name, "text-decoration")) {
        if (g_str_equal (value, "inherit")) {
            state->has_font_decor = FALSE;
            state->font_decor = TEXT_NORMAL;
        } else {
            if (strstr (value, "underline"))
                state->font_decor |= TEXT_UNDERLINE;
            if (strstr (value, "overline"))
                state->font_decor |= TEXT_OVERLINE;
            if (strstr (value, "strike") || strstr (value, "line-through"))     /* strike though or line-through */
                state->font_decor |= TEXT_STRIKE;
            state->has_font_decor = TRUE;
        }
    } else if (g_str_equal (name, "text-rendering")) {
        state->has_text_rendering_type = TRUE;

        if (g_str_equal (value, "auto") || g_str_equal (value, "default"))
            state->text_rendering_type = TEXT_RENDERING_AUTO;
        else if (g_str_equal (value, "optimizeSpeed"))
            state->text_rendering_type = TEXT_RENDERING_OPTIMIZE_SPEED;
        else if (g_str_equal (value, "optimizeLegibility"))
            state->text_rendering_type = TEXT_RENDERING_OPTIMIZE_LEGIBILITY;
        else if (g_str_equal (value, "geometricPrecision"))
            state->text_rendering_type = TEXT_RENDERING_GEOMETRIC_PRECISION;
    } else if (g_str_equal (name, "unicode-bidi")) {
        state->has_unicode_bidi = TRUE;
        if (g_str_equal (value, "inherit")) {
            state->unicode_bidi = UNICODE_BIDI_NORMAL;
            state->has_unicode_bidi = FALSE;
        } else if (g_str_equal (value, "embed"))
            state->unicode_bidi = UNICODE_BIDI_EMBED;
        else if (g_str_equal (value, "bidi-override"))
            state->unicode_bidi = UNICODE_BIDI_OVERRIDE;
        else                    /* normal */
            state->unicode_bidi = UNICODE_BIDI_NORMAL;
    } else if (g_str_equal (name, "visibility")) {
        state->has_visible = TRUE;
        if (g_str_equal (value, "visible"))
            state->visible = TRUE;
        else if (!g_str_equal (value, "inherit") != 0)
            state->visible = FALSE;     /* collapse or hidden */
        else
            state->has_visible = FALSE;
    } else if (g_str_equal (name, "pointer-events")) {
        /* TODO */
    } else if (g_str_equal (name, "writing-mode")) {
        /* TODO: these aren't quite right... */

        state->has_text_dir = TRUE;
        state->has_text_gravity = TRUE;
        if (g_str_equal (value, "inherit")) {
            state->text_dir = PANGO_DIRECTION_LTR;
            state->has_text_dir = FALSE;
            state->text_gravity = PANGO_GRAVITY_SOUTH;
            state->has_text_gravity = FALSE;
        } else if (g_str_equal (value, "lr-tb") || g_str_equal (value, "lr")) {
            state->text_dir = PANGO_DIRECTION_LTR;
            state->text_gravity = PANGO_GRAVITY_SOUTH;
        } else if (g_str_equal (value, "rl-tb") || g_str_equal (value, "rl")) {
            state->text_dir = PANGO_DIRECTION_RTL;
            state->text_gravity = PANGO_GRAVITY_SOUTH;
        } else if (g_str_equal (value, "tb-rl") || g_str_equal (value, "tb")) {
            state->text_dir = PANGO_DIRECTION_LTR;
            state->text_gravity = PANGO_GRAVITY_EAST;
        }
    } else if (g_str_equal (name, "xml:lang")) {
        char *save = g_strdup (value);
        g_free (state->lang);
        state->lang = save;
        state->has_lang = TRUE;
    } else if (g_str_equal (name, "xml:space")) {
        state->has_space_preserve = TRUE;
        if (g_str_equal (value, "default"))
            state->space_preserve = FALSE;
        else if (!g_str_equal (value, "preserve") == 0)
            state->space_preserve = TRUE;
        else
            state->space_preserve = FALSE;
    } else if (g_str_equal (name, "comp-op")) {
        if (g_str_equal (value, "clear"))
            state->comp_op = CAIRO_OPERATOR_CLEAR;
        else if (g_str_equal (value, "src"))
            state->comp_op = CAIRO_OPERATOR_SOURCE;
        else if (g_str_equal (value, "dst"))
            state->comp_op = CAIRO_OPERATOR_DEST;
        else if (g_str_equal (value, "src-over"))
            state->comp_op = CAIRO_OPERATOR_OVER;
        else if (g_str_equal (value, "dst-over"))
            state->comp_op = CAIRO_OPERATOR_DEST_OVER;
        else if (g_str_equal (value, "src-in"))
            state->comp_op = CAIRO_OPERATOR_IN;
        else if (g_str_equal (value, "dst-in"))
            state->comp_op = CAIRO_OPERATOR_DEST_IN;
        else if (g_str_equal (value, "src-out"))
            state->comp_op = CAIRO_OPERATOR_OUT;
        else if (g_str_equal (value, "dst-out"))
            state->comp_op = CAIRO_OPERATOR_DEST_OUT;
        else if (g_str_equal (value, "src-atop"))
            state->comp_op = CAIRO_OPERATOR_ATOP;
        else if (g_str_equal (value, "dst-atop"))
            state->comp_op = CAIRO_OPERATOR_DEST_ATOP;
        else if (g_str_equal (value, "xor"))
            state->comp_op = CAIRO_OPERATOR_XOR;
        else if (g_str_equal (value, "plus"))
            state->comp_op = CAIRO_OPERATOR_ADD;
        else if (g_str_equal (value, "multiply"))
            state->comp_op = CAIRO_OPERATOR_MULTIPLY;
        else if (g_str_equal (value, "screen"))
            state->comp_op = CAIRO_OPERATOR_SCREEN;
        else if (g_str_equal (value, "overlay"))
            state->comp_op = CAIRO_OPERATOR_OVERLAY;
        else if (g_str_equal (value, "darken"))
            state->comp_op = CAIRO_OPERATOR_DARKEN;
        else if (g_str_equal (value, "lighten"))
            state->comp_op = CAIRO_OPERATOR_LIGHTEN;
        else if (g_str_equal (value, "color-dodge"))
            state->comp_op = CAIRO_OPERATOR_COLOR_DODGE;
        else if (g_str_equal (value, "color-burn"))
            state->comp_op = CAIRO_OPERATOR_COLOR_BURN;
        else if (g_str_equal (value, "hard-light"))
            state->comp_op = CAIRO_OPERATOR_HARD_LIGHT;
        else if (g_str_equal (value, "soft-light"))
            state->comp_op = CAIRO_OPERATOR_SOFT_LIGHT;
        else if (g_str_equal (value, "difference"))
            state->comp_op = CAIRO_OPERATOR_DIFFERENCE;
        else if (g_str_equal (value, "exclusion"))
            state->comp_op = CAIRO_OPERATOR_EXCLUSION;
        else
            state->comp_op = CAIRO_OPERATOR_OVER;
    }
}
