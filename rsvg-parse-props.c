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

/**
 * rsvg_keyword_cmp:
 * Compare 2 strings, returns TRUE if equal.
 * If the last argument is 'CSS_VALUE', the comparison is case-insensitive.
 */
static gboolean
rsvg_keyword_cmp (const char *s1, const char *s2, const RsvgPropSrc prop_src)
{
    return (((prop_src == CSS_VALUE)? g_ascii_strcasecmp : strcmp) (s1, s2) == 0);
}

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

    g_assert (str != NULL);

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

    g_assert (str != NULL);

    out.length = _rsvg_parse_number (str, end, prop_src);
    if (*end == str) /* invalid number */
        return (RsvgLength) {0.0, RSVG_UNIT_UNKNOWN}; /* TODO: will this give problems? */

    if (**end == '%') {
        out.unit = RSVG_UNIT_PERCENTAGE;
        *end += 1;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "em", 2) == 0)) {
        out.unit = RSVG_UNIT_EMS;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "ex", 2) == 0)) {
        out.unit = RSVG_UNIT_EXS;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "px", 2) == 0)) {
        out.unit = RSVG_UNIT_PX;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "in", 2) == 0)) {
        out.unit = RSVG_UNIT_IN;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "cm", 2) == 0)) {
        out.unit = RSVG_UNIT_CM;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "mm", 2) == 0)) {
        out.unit = RSVG_UNIT_MM;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "pt", 2) == 0)) {
        out.unit = RSVG_UNIT_PT;
        *end += 2;
    } else if ((((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (*end, "pc", 2) == 0)) {
        out.unit = RSVG_UNIT_PC;
        *end += 2;
    } else {
        out.unit = RSVG_UNIT_NUMBER;
    }

    return out;
}

/**
 * _rsvg_skip_comma_wsp:
 * *end is set to the first character that follows the comma-whitespace
 * Returns TRUE is a comma is found (which implies something has to follow)
 */
gboolean
_rsvg_skip_comma_wsp (const char *str, const char **end)
{
    g_assert (str != NULL);

    /* skip whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')
        str++;

    if (*str == ',') {
        /* skip optional comma */
        str++;

        /* skip whitespace */
        while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')
            str++;

        *end = str;
        return TRUE;
    } else {
        *end = str;
        return FALSE;
    }
}

static char *
rsvg_get_url_string (const char *str)
//_rsvg_parse_funciri (const char *str)
{
    /* TODO */
    if (!strncmp (str, "url(", 4)) {
        const char *p = str + 4;
        int ix;

        while (g_ascii_isspace (*p))
            p++;

        for (ix = 0; p[ix]; ix++)
            if (p[ix] == ')')
                return g_strndup (p, ix);
    }
    return NULL;
}

/* ========================================================================== */

gboolean
_rsvg_parse_prop_length (const char *str, RsvgLength *result, const RsvgPropSrc prop_src)
{
    RsvgLength length;
    const char *end;

    g_assert (str != NULL);

    length = _rsvg_parse_length (str, &end, prop_src);
    if (str == end || *end != '\0')
        return FALSE;

    *result = length;
    return TRUE;
}

gboolean
_rsvg_parse_number_list (const char *str, RsvgNumberList *result, const RsvgPropSrc prop_src)
{
    RsvgNumberList list;
    const char *tmp;
    guint i;

    g_assert (str != NULL);

    if (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n' || *str == ',' || *str == '\0') {
        /* str starts with comma-wsp */
        return FALSE;
    }

    /* count number of items */
    list.number_of_items = 0;
    tmp = str;
    while (*tmp != '\0') {
        while (*tmp != ' ' && *tmp != '\t' && *tmp != '\r' && *tmp != '\n' && *tmp != ',' && *tmp != '\0')
            tmp++;

        list.number_of_items++;
        _rsvg_skip_comma_wsp (tmp, &tmp);
    }

    list.items = g_new (RsvgNumberList, list.number_of_items);

    /* parse the numbers */
    for (i = 0; i < list.number_of_items; i++) {
        list.items[i] = _rsvg_parse_number (str, &tmp, prop_src);
        if (str == tmp)
            goto invalid_list;
        str = tmp;

        /* if this is not the last item skip comma-whitespace */
        if (i + 1 != list.number_of_items) {
            _rsvg_skip_comma_wsp (str, &tmp);
            if (str == tmp)
                goto invalid_list;
            str = tmp;
        }
    }

    if (*str != '\0')
        goto invalid_list;

    *result = list;
    return TRUE;

invalid_list:
    g_free (list.items);
    return FALSE;
}

gboolean
_rsvg_parse_length_list (const char *str, RsvgLengthList *result, const RsvgPropSrc prop_src)
{
    RsvgLengthList list;
    const char *tmp;
    guint i;

    g_assert (str != NULL);

    if (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n' || *str == ',' || *str == '\0') {
        /* str starts with comma-wsp */
        return FALSE;
    }

    /* count number of items */
    list.number_of_items = 0;
    tmp = str;
    while (*tmp != '\0') {
        while (*tmp != ' ' && *tmp != '\t' && *tmp != '\r' && *tmp != '\n' && *tmp != ',' && *tmp != '\0')
            tmp++;

        list.number_of_items++;
        _rsvg_skip_comma_wsp (tmp, &tmp);
    }

    list.items = g_new (RsvgLength, list.number_of_items);

    /* parse the lengths */
    for (i = 0; i < list.number_of_items; i++) {
        list.items[i] = _rsvg_parse_length (str, &tmp, prop_src);
        if (str == tmp)
            goto invalid_list;
        str = tmp;

        /* if this is not the last item skip comma-whitespace */
        if (i + 1 != list.number_of_items) {
            _rsvg_skip_comma_wsp (str, &tmp);
            if (str == tmp)
                goto invalid_list;
            str = tmp;
        }
    }

    if (*str != '\0')
        goto invalid_list;

    *result = list;
    return TRUE;

invalid_list:
    g_free (list.items);
    return FALSE;
}


static gboolean
_rsvg_parse_opacity (const char *str, guint8 *result, const RsvgPropSrc prop_src)
{
    double opacity;
    const char *end;

    opacity = _rsvg_parse_number (str, &end, prop_src);
    if (str == end || *end != '\0')
        return FALSE;

    opacity = CLAMP (opacity, 0., 1.);
    *result = floor (opacity * 255. + 0.5);

    return TRUE;
}

/* ========================================================================== */

static gboolean
rsvg_parse_direction (const char *str, PangoDirection *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        PangoDirection value;
    };
    const struct keywords keywords[] = {
        {"ltr", PANGO_DIRECTION_LTR},
        {"rtl", PANGO_DIRECTION_RTL}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

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

static gboolean
rsvg_parse_fill_rule (const char *str, cairo_fill_rule_t *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        cairo_fill_rule_t value;
    };
    const struct keywords keywords[] = {
        {"evenodd", CAIRO_FILL_RULE_EVEN_ODD},
        {"nonzero", CAIRO_FILL_RULE_WINDING}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_font_family (const char *str, char **result, const RsvgPropSrc prop_src)
{
    if (*result != NULL)
        g_free (*result);
    /* TODO: who does the parsing and error-checking of font-family */
    *result = g_strdup (str);
    return TRUE;
}

static gboolean
rsvg_parse_font_size (const char *str, RsvgLength *result, const RsvgPropSrc prop_src)
{
    RsvgLength font_size;
    const char *end;

    struct keywords {
        const char *keyword;
        RsvgLength value;
    };
    const struct keywords keywords[] = {
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
    struct keywords *keyword;

    g_assert (str != NULL);

    if ((keyword = rsvg_match_keyword (str, keywords, prop_src))) {
        *result = keyword->value;
    } else {
        /* try normal length values */
        font_size = _rsvg_parse_length (str, &end, prop_src);
        if (str == end || *end != '\0' ||
            (prop_src == CSS_VALUE && font_size.unit == RSVG_UNIT_NUMBER && font_size.length != 0.0) ||
            font_size.length < 0.0 ) {
            return FALSE;
        }
        *result = font_size;
    }
    return TRUE;
}

static gboolean
rsvg_parse_font_stretch (const char *str, PangoStretch *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        PangoStretch value;
    };
    const struct keywords keywords[] = {
        {"condensed",       PANGO_STRETCH_CONDENSED},
        {"expanded",        PANGO_STRETCH_EXPANDED},
        {"extra-condensed", PANGO_STRETCH_EXTRA_CONDENSED},
        {"extra-expanded",  PANGO_STRETCH_EXTRA_EXPANDED},
        {"narrower",        PANGO_STRETCH_CONDENSED},
        {"normal",          PANGO_STRETCH_NORMAL},
        {"semi-condensed",  PANGO_STRETCH_SEMI_CONDENSED},
        {"semi-expanded",   PANGO_STRETCH_SEMI_EXPANDED},
        {"ultra-condensed", PANGO_STRETCH_ULTRA_CONDENSED},
        {"ultra-expanded",  PANGO_STRETCH_ULTRA_EXPANDED},
        {"wider",           PANGO_STRETCH_EXPANDED}
    };
    struct keywords *keyword;

    /* TODO: 'narrower' and 'wider' should be relative to the font-stretch of the parent node */

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_font_style (const char *str, PangoStyle *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        PangoStyle value;
    };
    const struct keywords keywords[] = {
        {"italic",  PANGO_STRETCH_CONDENSED},
        {"normal",  PANGO_STRETCH_EXPANDED},
        {"oblique", PANGO_STRETCH_EXPANDED}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_font_variant (const char *str, PangoVariant *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        PangoVariant value;
    };
    const struct keywords keywords[] = {
        {"normal",     PANGO_VARIANT_SMALL_CAPS},
        {"small-caps", PANGO_VARIANT_NORMAL}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_font_weight (const char *str, PangoWeight *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        PangoWeight value;
    };
    const struct keywords keywords[] = {
        {"100",     100},
        {"200",     200},
        {"300",     300},
        {"400",     400},
        {"500",     500},
        {"600",     600},
        {"700",     700},
        {"800",     800},
        {"900",     900},
        {"bold",    PANGO_WEIGHT_LIGHT},
        {"bolder",  PANGO_WEIGHT_BOLD},
        {"lighter", PANGO_WEIGHT_ULTRABOLD},
        {"normal",  PANGO_WEIGHT_NORMAL},
    };
    struct keywords *keyword;

    /* TODO: 'bolder' and 'lighter' should be relative to the font-weight of the parent node */

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
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
rsvg_parse_overflow (const char *str, gboolean *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        gboolean value;
    };
    const struct keywords keywords[] = {
        {"auto",    TRUE},
        {"hidden",  FALSE},
        {"scroll",  FALSE},
        {"visible", TRUE}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_shape_rendering (const char *str, cairo_antialias_t *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        cairo_antialias_t value;
    };
    const struct keywords keywords[] = {
        {"auto",               SHAPE_RENDERING_AUTO},
        {"crispEdges",         SHAPE_RENDERING_CRISP_EDGES},
        {"geometricPrecision", SHAPE_RENDERING_GEOMETRIC_PRECISION},
        {"optimizeSpeed",      SHAPE_RENDERING_OPTIMIZE_SPEED}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_stroke_dasharray (const char *str, RsvgLengthList *result, const RsvgPropSrc prop_src)
{
    RsvgLengthList list;
    guint i;
    gboolean nonzero_length = FALSE;

    if (rsvg_keyword_cmp (str, "none", prop_src)) {
        result->number_of_items = 0;
        result->items = NULL;
        return TRUE;
    }

    if (_rsvg_parse_length_list (str, &list, prop_src) == FALSE)
        return FALSE;

    /* make sure there are no values with a negative value and the sum of
       values is not zero */
    for (i = 0; i < list.number_of_items; i++) {
        if (list.items[i].length > 0.0) {
            nonzero_length = TRUE;
        } else if (list.items[i].length < 0.0) {
            g_free (list.items);
            return FALSE;
        }
    }
    if (nonzero_length == FALSE) {
        /* handle as if a value of none were specified */
        list.number_of_items = 0;
        list.items = NULL;
    }

    if (result->items != NULL)
        g_free (result->items);
    *result = list;
    return TRUE;
}

static gboolean
rsvg_parse_stroke_linecap (const char *str, cairo_line_cap_t *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        cairo_line_cap_t value;
    };
    const struct keywords keywords[] = {
        {"butt",   CAIRO_LINE_CAP_BUTT},
        {"round",  CAIRO_LINE_CAP_ROUND},
        {"square", CAIRO_LINE_CAP_SQUARE}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_stroke_linejoin (const char *str, cairo_line_join_t *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        cairo_line_join_t value;
    };
    const struct keywords keywords[] = {
        {"bevel", CAIRO_LINE_JOIN_BEVEL},
        {"miter", CAIRO_LINE_JOIN_MITER},
        {"round", CAIRO_LINE_JOIN_ROUND}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_stroke_miterlimit (const char *str, double *result, const RsvgPropSrc prop_src)
{
    double length;
    const char *end;

    g_assert (str != NULL);

    length = _rsvg_parse_number (str, &end, prop_src);
    if (str == end || *end != '\0' || length < 1.0)
        return FALSE;

    *result = length;
    return TRUE;
}

static gboolean
rsvg_parse_stroke_width (const char *str, RsvgLength *result, const RsvgPropSrc prop_src)
{
    RsvgLength length;
    const char *end;

    length = _rsvg_parse_length (str, &end, prop_src);
    if (str == end || *end != '\0' || length.length < 0.0)
        return FALSE;

    *result = length;
    return TRUE;
}

static gboolean
rsvg_parse_text_anchor (const char *str, TextAnchor *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        TextAnchor value;
    };
    const struct keywords keywords[] = {
        {"end",    TEXT_ANCHOR_END},
        {"middle", TEXT_ANCHOR_MIDDLE},
        {"start",  TEXT_ANCHOR_START}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_text_decoration (const char *str, TextDecoration *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        TextDecoration value;
    };
    const struct keywords keywords[] = {
        {"line-through", TEXT_DECORATION_LINE_THROUGH},
        {"none",         TEXT_DECORATION_NONE},
        {"overline",     TEXT_DECORATION_OVERLINE},
        {"underline",    TEXT_DECORATION_UNDERLINE}
        /* 'blink' is not supported */
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    if (keyword->value == TEXT_DECORATION_NONE)
        *result = TEXT_DECORATION_NONE;
    else
        *result |= keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_text_rendering (const char *str, cairo_antialias_t *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        cairo_antialias_t value;
    };
    const struct keywords keywords[] = {
        {"auto",               TEXT_RENDERING_AUTO},
        {"geometricPrecision", TEXT_RENDERING_GEOMETRIC_PRECISION},
        {"optimizeLegibility", TEXT_RENDERING_OPTIMIZE_LEGIBILITY},
        {"optimizeSpeed",      TEXT_RENDERING_OPTIMIZE_SPEED}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_unicode_bidi (const char *str, UnicodeBidi *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        UnicodeBidi value;
    };
    const struct keywords keywords[] = {
        {"bidi-override", UNICODE_BIDI_OVERRIDE},
        {"embed",         UNICODE_BIDI_EMBED},
        {"normal",        UNICODE_BIDI_NORMAL}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

static gboolean
rsvg_parse_comp_op (const char *str, cairo_operator_t *result, const RsvgPropSrc prop_src)
{
    struct keywords {
        const char *keyword;
        cairo_operator_t value;
    };
    const struct keywords keywords[] = {
        {"clear",       CAIRO_OPERATOR_CLEAR},
        {"color-burn",  CAIRO_OPERATOR_COLOR_BURN},
        {"color-dodge", CAIRO_OPERATOR_COLOR_DODGE},
        {"darken",      CAIRO_OPERATOR_DARKEN},
        {"difference",  CAIRO_OPERATOR_DIFFERENCE},
        {"dst",         CAIRO_OPERATOR_DEST},
        {"dst-atop",    CAIRO_OPERATOR_DEST_ATOP},
        {"dst-in",      CAIRO_OPERATOR_DEST_IN},
        {"dst-out",     CAIRO_OPERATOR_DEST_OUT},
        {"dst-over",    CAIRO_OPERATOR_DEST_OVER},
        {"exclusion",   CAIRO_OPERATOR_EXCLUSION},
        {"hard-light",  CAIRO_OPERATOR_HARD_LIGHT},
        {"lighten",     CAIRO_OPERATOR_LIGHTEN},
        {"multiply",    CAIRO_OPERATOR_MULTIPLY},
        {"overlay",     CAIRO_OPERATOR_OVERLAY},
        {"plus",        CAIRO_OPERATOR_ADD},
        {"screen",      CAIRO_OPERATOR_SCREEN},
        {"soft-light",  CAIRO_OPERATOR_SOFT_LIGHT},
        {"src",         CAIRO_OPERATOR_SOURCE},
        {"src-atop",    CAIRO_OPERATOR_ATOP},
        {"src-in",      CAIRO_OPERATOR_IN},
        {"src-out",     CAIRO_OPERATOR_OUT},
        {"src-over",    CAIRO_OPERATOR_OVER},
        {"xor",         CAIRO_OPERATOR_XOR}
    };
    struct keywords *keyword;

    keyword = rsvg_match_keyword (str, keywords, prop_src);
    if (keyword == NULL)
        return FALSE;

    *result = keyword->value;
    return TRUE;
}

/* Parse a CSS2 style argument, setting the SVG context attributes. */
void
rsvg_parse_prop (const RsvgHandle *ctx,
                 RsvgState *state,
                 const gchar *name,
                 const gchar *value,
                 const gboolean important,
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
        /* TODO */
        state->clip_path = rsvg_parse_clip_path (ctx->priv->defs, value);
    } else if (g_str_equal (name, "clip-rule")) {
        if (rsvg_parse_fill_rule (value, &state->clip_rule, prop_src))
            state->has_clip_rule = TRUE;
    } else if (g_str_equal (name, "color")) {
        /* TODO */
        state->color = rsvg_css_parse_color (value, &state->has_current_color);
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
        if (rsvg_parse_direction (value, &state->direction, prop_src))
            state->has_text_dir = TRUE;
    } else if (g_str_equal (name, "display")) {
        /* TODO */
        state->has_visible = TRUE;
        if (g_str_equal (value, "none"))
            state->visible = FALSE;
        else
            state->visible = TRUE;
    } else if (g_str_equal (name, "dominant-baseline")) {
        /* TODO */
    } else if (g_str_equal (name, "enable-background")) {
        /* TODO */
        if (g_str_equal (value, "new"))
            state->enable_background = RSVG_ENABLE_BACKGROUND_NEW;
        else
            state->enable_background = RSVG_ENABLE_BACKGROUND_ACCUMULATE;
    } else if (g_str_equal (name, "fill")) {
        /* TODO */
        RsvgPaintServer *fill = state->fill;
        state->fill =
            rsvg_parse_paint_server (&state->has_fill_server, ctx->priv->defs, value, 0);
        rsvg_paint_server_unref (fill);
    } else if (g_str_equal (name, "fill-opacity")) {
        if (_rsvg_parse_opacity (value, &state->fill_opacity, prop_src))
            state->has_fill_opacity = TRUE;
    } else if (g_str_equal (name, "fill-rule")) {
        if (rsvg_parse_fill_rule (value, &state->fill_rule, prop_src))
            state->has_fill_rule = TRUE;
    } else if (g_str_equal (name, "filter")) {
        /* TODO */
        state->filter = rsvg_parse_filter (ctx->priv->defs, value);
    } else if (g_str_equal (name, "flood-color")) {
        /* TODO */
        state->flood_color = rsvg_css_parse_color (value, &state->has_flood_color);
    } else if (g_str_equal (name, "flood-opacity")) {
        if (_rsvg_parse_opacity (value, &state->flood_opacity, prop_src))
            state->has_flood_opacity = TRUE;
    } else if (g_str_equal (name, "font")) {
        /* TODO */
    } else if (g_str_equal (name, "font-family")) {
        if (rsvg_parse_font_family (value, &state->font_family, prop_src))
            state->has_font_family = TRUE;
    } else if (g_str_equal (name, "font-size")) {
        if (rsvg_parse_font_size (value, &state->font_size, prop_src))
            state->has_font_size = TRUE;
    } else if (g_str_equal (name, "font-size-adjust")) {
        /* TODO */
    } else if (g_str_equal (name, "font-stretch")) {
        if (rsvg_parse_font_stretch (value, &state->font_stretch, prop_src))
            state->has_font_stretch = TRUE;
    } else if (g_str_equal (name, "font-style")) {
        if (rsvg_parse_font_style (value, &state->font_style, prop_src))
            state->has_font_style = TRUE;
    } else if (g_str_equal (name, "font-variant")) {
        if (rsvg_parse_font_variant (value, &state->font_variant, prop_src))
            state->has_font_variant = TRUE;
    } else if (g_str_equal (name, "font-weight")) {
        if (rsvg_parse_font_weight (value, &state->font_weight, prop_src))
            state->has_font_weight = TRUE;
    } else if (g_str_equal (name, "glyph-orientation-horizontal")) {
        /* TODO */
    } else if (g_str_equal (name, "glyph-orientation-vertical")) {
        /* TODO */
    } else if (g_str_equal (name, "image-rendering")) {
        /* TODO */
    } else if (g_str_equal (name, "kerning")) {
        /* TODO */
    } else if (g_str_equal (name, "letter-spacing")) {
        if (_rsvg_parse_prop_length (value, &state->letter_spacing, prop_src))
            state->has_letter_spacing = TRUE;
    } else if (g_str_equal (name, "lighting-color")) {
        /* TODO */
    } else if (g_str_equal (name, "marker")) {
        /* TODO */
    } else if (g_str_equal (name, "marker-start")) {
        /* TODO */
        state->marker_start = rsvg_parse_marker (ctx->priv->defs, value);
        state->has_startMarker = TRUE;
    } else if (g_str_equal (name, "marker-mid")) {
        /* TODO */
        state->marker_mid = rsvg_parse_marker (ctx->priv->defs, value);
        state->has_middleMarker = TRUE;
    } else if (g_str_equal (name, "marker-end")) {
        /* TODO */
        state->marker_end = rsvg_parse_marker (ctx->priv->defs, value);
        state->has_endMarker = TRUE;
    } else if (g_str_equal (name, "mask")) {
        /* TODO */
        state->mask = rsvg_parse_mask (ctx->priv->defs, value);
    } else if (g_str_equal (name, "opacity")) {
        if (_rsvg_parse_opacity (value, &state->opacity, prop_src))
            ; /* there is no has_opacity */
    } else if (g_str_equal (name, "overflow")) {
        if (rsvg_parse_overflow (value, &state->overflow, prop_src))
            state->has_overflow = TRUE;
    } else if (g_str_equal (name, "pointer-events")) {
        /* TODO */
    } else if (g_str_equal (name, "shape-rendering")) {
        if (rsvg_parse_shape_rendering (value, &state->shape_rendering, prop_src))
            state->has_shape_rendering_type = TRUE;
    } else if (g_str_equal (name, "stop-color")) {
        /* TODO */
        state->stop_color = rsvg_css_parse_color (value, &state->has_stop_color);
    } else if (g_str_equal (name, "stop-opacity")) {
        if (_rsvg_parse_opacity (value, &state->stop_opacity, prop_src))
            state->has_stop_opacity = TRUE;
    } else if (g_str_equal (name, "stroke")) {
        /* TODO */
        RsvgPaintServer *stroke = state->stroke;
        state->stroke =
            rsvg_parse_paint_server (&state->has_stroke_server, ctx->priv->defs, value, 0);
        rsvg_paint_server_unref (stroke);
    } else if (g_str_equal (name, "stroke-dasharray")) {
        if (rsvg_parse_stroke_dasharray (value, &state->stroke_dasharray, prop_src))
            state->has_dash = TRUE;
    } else if (g_str_equal (name, "stroke-dashoffset")) {
        if (_rsvg_parse_prop_length (value, &state->stroke_dashoffset, prop_src))
            state->has_dashoffset = TRUE;
    } else if (g_str_equal (name, "stroke-linecap")) {
       if (rsvg_parse_stroke_linecap (value, &state->stroke_linecap, prop_src))
            state->has_cap = TRUE;
    } else if (g_str_equal (name, "stroke-linejoin")) {
        if (rsvg_parse_stroke_linejoin (value, &state->stroke_linejoin, prop_src))
            state->has_join = TRUE;
    } else if (g_str_equal (name, "stroke-miterlimit")) {
        if (rsvg_parse_stroke_miterlimit (value, &state->stroke_miterlimit, prop_src))
            state->has_miter_limit = TRUE;
    } else if (g_str_equal (name, "stroke-opacity")) {
        if (_rsvg_parse_opacity (value, &state->stroke_opacity, prop_src))
            state->has_stroke_opacity = TRUE;
    } else if (g_str_equal (name, "stroke-width")) {
        if (rsvg_parse_stroke_width (value, &state->stroke_width, prop_src))
            state->has_stroke_width = TRUE;
    } else if (g_str_equal (name, "text-anchor")) {
        if (rsvg_parse_text_anchor (value, &state->text_anchor, prop_src))
            state->has_text_anchor = TRUE;
    } else if (g_str_equal (name, "text-decoration")) {
        if (rsvg_parse_text_decoration (value, &state->text_decoration, prop_src))
            state->has_font_decor = TRUE;
    } else if (g_str_equal (name, "text-rendering")) {
        if (rsvg_parse_text_rendering (value, &state->text_rendering, prop_src))
            state->has_text_rendering_type = TRUE;
    } else if (g_str_equal (name, "unicode-bidi")) {
        if (rsvg_parse_unicode_bidi (value, &state->unicode_bidi, prop_src))
            state->has_unicode_bidi = TRUE;
    } else if (g_str_equal (name, "visibility")) {
        /* TODO */
        state->has_visible = TRUE;
        if (g_str_equal (value, "visible"))
            state->visible = TRUE;
        else
            state->visible = FALSE; /* collapse or hidden */
    } else if (g_str_equal (name, "pointer-events")) {
        /* TODO */
    } else if (g_str_equal (name, "writing-mode")) {
        /* TODO: these aren't quite right... */

        state->has_text_dir = TRUE;
        state->has_text_gravity = TRUE;
        if (g_str_equal (value, "lr-tb") || g_str_equal (value, "lr")) {
            state->direction = PANGO_DIRECTION_LTR;
            state->text_gravity = PANGO_GRAVITY_SOUTH;
        } else if (g_str_equal (value, "rl-tb") || g_str_equal (value, "rl")) {
            state->direction = PANGO_DIRECTION_RTL;
            state->text_gravity = PANGO_GRAVITY_SOUTH;
        } else if (g_str_equal (value, "tb-rl") || g_str_equal (value, "tb")) {
            state->direction = PANGO_DIRECTION_LTR;
            state->text_gravity = PANGO_GRAVITY_EAST;
        }
    } else if (g_str_equal (name, "xml:lang")) {
        /* TODO */
        char *save = g_strdup (value);
        g_free (state->lang);
        state->lang = save;
        state->has_lang = TRUE;
    } else if (g_str_equal (name, "xml:space")) {
        /* TODO */
        state->has_space_preserve = TRUE;
        if (g_str_equal (value, "default"))
            state->space_preserve = FALSE;
        else if (!g_str_equal (value, "preserve") == 0)
            state->space_preserve = TRUE;
        else
            state->space_preserve = FALSE;
    } else if (g_str_equal (name, "comp-op")) {
        if (rsvg_parse_comp_op (value, &state->comp_op, prop_src))
            ; /* there is no has_comp_op */
    }
}
