/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-parse-props.c: Parse SVG presentation properties

   Copyright (C) 2014 Paul Dicker <pitdicker@gmail.com>

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
#include "rsvg-parse-props.h"

#include <string.h>

#include "rsvg-styles.h"
#include "rsvg-css.h"

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

static gboolean
rsvg_keyword_ncmp (const char *s1, const char *s2, const RsvgPropSrc prop_src)
{
    return (((prop_src == CSS_VALUE)? g_ascii_strncasecmp : strncmp) (s1, s2, strlen (s2)) == 0);
}

static int
rsvg_match_keyword_cmp (const void *str, const void *b)
{
    struct keywords {
        const char *keyword;
    };
    return strcmp ((const char *) str, ((struct keywords *) b)->keyword);
}

static int
rsvg_match_keyword_casecmp (const void *str, const void *b)
{
    struct keywords {
        const char *keyword;
    };
    return g_ascii_strcasecmp ((const char *) str, ((struct keywords *) b)->keyword);
}

#define rsvg_match_keyword(str, keywords, prop_src) bsearch (str, keywords, sizeof (keywords) / sizeof (keywords[0]), sizeof (keywords[0]), (prop_src == CSS_VALUE)? rsvg_match_keyword_casecmp : rsvg_match_keyword_cmp)

static gboolean
_rsvg_is_wsp (const char c)
{
    switch (c) {
    case ' ': case '\t': case '\r': case '\n':
        return TRUE;
    default:
        return FALSE;
    }
}

/* ========================================================================== */

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
    } else if (rsvg_keyword_ncmp (*end, "em", prop_src)) {
        out.unit = RSVG_UNIT_EMS;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "ex", prop_src)) {
        out.unit = RSVG_UNIT_EXS;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "px", prop_src)) {
        out.unit = RSVG_UNIT_PX;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "in", prop_src)) {
        out.unit = RSVG_UNIT_IN;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "cm", prop_src)) {
        out.unit = RSVG_UNIT_CM;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "mm", prop_src)) {
        out.unit = RSVG_UNIT_MM;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "pt", prop_src)) {
        out.unit = RSVG_UNIT_PT;
        *end += 2;
    } else if (rsvg_keyword_ncmp (*end, "pc", prop_src)) {
        out.unit = RSVG_UNIT_PC;
        *end += 2;
    } else {
        out.unit = RSVG_UNIT_NUMBER;
    }

    return out;
}

static guint8
_rsvg_parse_rgb_value (const char *str, const char **end, gboolean is_percentage)
{
    gint value;
    double temp;

    while (_rsvg_is_wsp (*str))
        str++;

    if (is_percentage) {
        temp = _rsvg_parse_number (str, end, RSVG_NUMBER_FORMAT_CSS2);
        if (*end == str || **end != '%')
            goto invalid_value;
        (*end)++;
        value = floor (CLAMP (temp, 0., 100.) * 2.55 + 0.5);
    } else {
        value = g_ascii_strtoll (str, (gchar **) end, 10);
        if (*end == str)
            goto invalid_value;
        value = CLAMP (value, 0, 255);
    }
    str = *end;

    while (_rsvg_is_wsp (**end))
        (*end)++;

    return value;

invalid_value:
    *end = str;
    return 0x00;
}

static guint32
_rsvg_parse_raw_color (const char *str, const char **end)
{
    guint32 value;
    guint i;
    gboolean has_alpha = FALSE;
    gboolean is_percentage = FALSE;
    guint8 r, g, b, a = 255;
    double temp;
    const char *str_start;

    struct keywords {
        const char *keyword;
        guint32 value;
    };
    const struct keywords svg_color_keywords[] = {
        {"aliceblue",            0xfff0f8ff},
        {"antiquewhite",         0xfffaebd7},
        {"aqua",                 0xff00ffff},
        {"aquamarine",           0xff7fffd4},
        {"azure",                0xfff0ffff},
        {"beige",                0xfff5f5dc},
        {"bisque",               0xffffe4c4},
        {"black",                0xff000000},
        {"blanchedalmond",       0xffffebcd},
        {"blue",                 0xff0000ff},
        {"blueviolet",           0xff8a2be2},
        {"brown",                0xffa52a2a},
        {"burlywood",            0xffdeb887},
        {"cadetblue",            0xff5f9ea0},
        {"chartreuse",           0xff7fff00},
        {"chocolate",            0xffd2691e},
        {"coral",                0xffff7f50},
        {"cornflowerblue",       0xff6495ed},
        {"cornsilk",             0xfffff8dc},
        {"crimson",              0xffdc143c},
        {"cyan",                 0xff00ffff},
        {"darkblue",             0xff00008b},
        {"darkcyan",             0xff008b8b},
        {"darkgoldenrod",        0xffb8860b},
        {"darkgray",             0xffa9a9a9},
        {"darkgreen",            0xff006400},
        {"darkgrey",             0xffa9a9a9},
        {"darkkhaki",            0xffbdb76b},
        {"darkmagenta",          0xff8b008b},
        {"darkolivegreen",       0xff556b2f},
        {"darkorange",           0xffff8c00},
        {"darkorchid",           0xff9932cc},
        {"darkred",              0xff8b0000},
        {"darksalmon",           0xffe9967a},
        {"darkseagreen",         0xff8fbc8f},
        {"darkslateblue",        0xff483d8b},
        {"darkslategray",        0xff2f4f4f},
        {"darkslategrey",        0xff2f4f4f},
        {"darkturquoise",        0xff00ced1},
        {"darkviolet",           0xff9400d3},
        {"deeppink",             0xffff1493},
        {"deepskyblue",          0xff00bfff},
        {"dimgray",              0xff696969},
        {"dimgrey",              0xff696969},
        {"dodgerblue",           0xff1e90ff},
        {"firebrick",            0xffb22222},
        {"floralwhite",          0xfffffaf0},
        {"forestgreen",          0xff228b22},
        {"fuchsia",              0xffff00ff},
        {"gainsboro",            0xffdcdcdc},
        {"ghostwhite",           0xfff8f8ff},
        {"gold",                 0xffffd700},
        {"goldenrod",            0xffdaa520},
        {"gray",                 0xff808080},
        {"grey",                 0xff808080},
        {"green",                0xff008000},
        {"greenyellow",          0xffadff2f},
        {"honeydew",             0xfff0fff0},
        {"hotpink",              0xffff69b4},
        {"indianred",            0xffcd5c5c},
        {"indigo",               0xff4b0082},
        {"ivory",                0xfffffff0},
        {"khaki",                0xfff0e68c},
        {"lavender",             0xffe6e6fa},
        {"lavenderblush",        0xfffff0f5},
        {"lawngreen",            0xff7cfc00},
        {"lemonchiffon",         0xfffffacd},
        {"lightblue",            0xffadd8e6},
        {"lightcoral",           0xfff08080},
        {"lightcyan",            0xffe0ffff},
        {"lightgoldenrodyellow", 0xfffafad2},
        {"lightgray",            0xffd3d3d3},
        {"lightgreen",           0xff90ee90},
        {"lightgrey",            0xffd3d3d3},
        {"lightpink",            0xffffb6c1},
        {"lightsalmon",          0xffffa07a},
        {"lightseagreen",        0xff20b2aa},
        {"lightskyblue",         0xff87cefa},
        {"lightslategray",       0xff778899},
        {"lightslategrey",       0xff778899},
        {"lightsteelblue",       0xffb0c4de},
        {"lightyellow",          0xffffffe0},
        {"lime",                 0xff00ff00},
        {"limegreen",            0xff32cd32},
        {"linen",                0xfffaf0e6},
        {"magenta",              0xffff00ff},
        {"maroon",               0xff800000},
        {"mediumaquamarine",     0xff66cdaa},
        {"mediumblue",           0xff0000cd},
        {"mediumorchid",         0xffba55d3},
        {"mediumpurple",         0xff9370db},
        {"mediumseagreen",       0xff3cb371},
        {"mediumslateblue",      0xff7b68ee},
        {"mediumspringgreen",    0xff00fa9a},
        {"mediumturquoise",      0xff48d1cc},
        {"mediumvioletred",      0xffc71585},
        {"midnightblue",         0xff191970},
        {"mintcream",            0xfff5fffa},
        {"mistyrose",            0xffffe4e1},
        {"moccasin",             0xffffe4b5},
        {"navajowhite",          0xffffdead},
        {"navy",                 0xff000080},
        {"oldlace",              0xfffdf5e6},
        {"olive",                0xff808000},
        {"olivedrab",            0xff6b8e23},
        {"orange",               0xffffa500},
        {"orangered",            0xffff4500},
        {"orchid",               0xffda70d6},
        {"palegoldenrod",        0xffeee8aa},
        {"palegreen",            0xff98fb98},
        {"paleturquoise",        0xffafeeee},
        {"palevioletred",        0xffdb7093},
        {"papayawhip",           0xffffefd5},
        {"peachpuff",            0xffffdab9},
        {"peru",                 0xffcd853f},
        {"pink",                 0xffffc0cb},
        {"plum",                 0xffdda0dd},
        {"powderblue",           0xffb0e0e6},
        {"purple",               0xff800080},
        {"red",                  0xffff0000},
        {"rosybrown",            0xffbc8f8f},
        {"royalblue",            0xff4169e1},
        {"saddlebrown",          0xff8b4513},
        {"salmon",               0xfffa8072},
        {"sandybrown",           0xfff4a460},
        {"seagreen",             0xff2e8b57},
        {"seashell",             0xfffff5ee},
        {"sienna",               0xffa0522d},
        {"silver",               0xffc0c0c0},
        {"skyblue",              0xff87ceeb},
        {"slateblue",            0xff6a5acd},
        {"slategray",            0xff708090},
        {"slategrey",            0xff708090},
        {"snow",                 0xfffffafa},
        {"springgreen",          0xff00ff7f},
        {"steelblue",            0xff4682b4},
        {"tan",                  0xffd2b48c},
        {"teal",                 0xff008080},
        {"thistle",              0xffd8bfd8},
        {"tomato",               0xffff6347},
        {"turquoise",            0xff40e0d0},
        {"violet",               0xffee82ee},
        {"wheat",                0xfff5deb3},
        {"white",                0xffffffff},
        {"whitesmoke",           0xfff5f5f5},
        {"yellow",               0xffffff00},
        {"yellowgreen",          0xff9acd32}
    };
    const struct keywords system_color_keywords[] = {
        /* emulate using default system colors of Windows 98 */
        {"ActiveBorder",         0xffc0c0c0},
        {"ActiveCaption",        0xff000084},
        {"AppWorkspace",         0xff808080},
        {"Background",           0xff008081},
        {"ButtonFace",           0xffc0c0c0},
        {"ButtonHighlight",      0xffdfdfdf},
        {"ButtonShadow",         0xff808080},
        {"ButtonText",           0xff000000},
        {"CaptionText",          0xffffffff},
        {"GrayText",             0xff808080},
        {"Highlight",            0xff08246b},
        {"HighlightText",        0xffffffff},
        {"InactiveBorder",       0xffc0c0c0},
        {"InactiveCaption",      0xff808080},
        {"InactiveCaptionText",  0xffc0c0c0},
        {"InfoBackground",       0xffffffe1},
        {"InfoText",             0xff000000},
        {"Menu",                 0xffc0c0c0},
        {"MenuText",             0xff000000},
        {"Scrollbar",            0xffc0c0c0},
        {"ThreeDDarkShadow",     0xff000000},
        {"ThreeDFace",           0xffc0c0c0},
        {"ThreeDHighlight",      0xffdfdfdf},
        {"ThreeDLightShadow",    0xffffffff},
        {"ThreeDShadow",         0xff808080},
        {"Window",               0xffffffff},
        {"WindowFrame",          0xff000000},
        {"WindowText",           0xff000000}
    };
    struct keywords *keyword;

    g_assert (str != NULL);
    str_start = str;

    if (*str == '#') {
        str++;
        value = 0;
        for (i = 0; i < 6; i++) {
            if (*str >= '0' && *str <= '9') {
                value = (value << 4) + (*str - '0');
            } else if (*str >= 'A' && *str <= 'F') {
                value = (value << 4) + (*str - 'A' + 10);
            } else if (*str >= 'a' && *str <= 'f') {
                value = (value << 4) + (*str - 'a' + 10);
            } else {
                break;
            }
            str++;
        }
        if (i == 3) {
            /* handle #nnn case */
            value = ((value & 0xf00) << 8) | ((value & 0x0f0) << 4) | (value & 0x00f);
            value |= value << 4;
        } else if (i != 6) {
            goto invalid_color;
        }
        value |= 0xff000000; /* opaque */
        *end = str;
        return value;
    } else if (g_ascii_strncasecmp(str, "rgb", 3) == 0) {
        str += 3;
        if (*str == 'a') { /* rgba */
            has_alpha = TRUE;
            str++;
        }

        if (*str != '(')
            goto invalid_color;
        str++;

        for (i = 0; str[i] != ',' && str[i] != '\0'; i++) {
            if (str[i] == '%') {
                is_percentage = TRUE;
                break;
            }
        }

        r = _rsvg_parse_rgb_value (str, end, is_percentage);
        if (*end == str)
            goto invalid_color;
        str = *end;

        if (*str != ',')
            goto invalid_color;
        str++;

        g = _rsvg_parse_rgb_value (str, end, is_percentage);
        if (*end == str)
            goto invalid_color;
        str = *end;

        if (*str != ',')
            goto invalid_color;
        str++;

        b = _rsvg_parse_rgb_value (str, end, is_percentage);
        if (*end == str)
            goto invalid_color;
        str = *end;

        if (has_alpha) {
            if (*str != ',')
                goto invalid_color;
            str++;

            while (_rsvg_is_wsp (*str))
                str++;

            temp = _rsvg_parse_number (str, end, RSVG_NUMBER_FORMAT_CSS2);
            if (*end == str)
                goto invalid_color;
            str = *end;
            a = floor (CLAMP (temp, 0., 1.) * 255. + 0.5);

            while (_rsvg_is_wsp (*str))
                str++;
        }

        if (*str != ')')
            goto invalid_color;
        str++;

        *end = str;
        return (a << 24) | (r << 16) | (g << 8) | b;
    } else if ((keyword = rsvg_match_keyword (str, svg_color_keywords, CSS_VALUE))) {
        /* TODO: does not work if the keyword is followed by something */
        *end = str + strlen(keyword->keyword);
        return keyword->value;
    } else if ((keyword = rsvg_match_keyword (str, system_color_keywords, CSS_VALUE))) {
        /* TODO: does not work if the keyword is followed by something */
        *end = str + strlen(keyword->keyword);
        return keyword->value;
    }

invalid_color:
    *end = str_start;
    return 0xff000000; /* black */
}

/**
 * _rsvg_parse_list_next_item:
 * *end is set to the first character that follows the comma-whitespace
 * Returns TRUE if a comma is found (which implies something has to follow)
 */
gboolean
_rsvg_parse_list_next_item (const char *str, const char **end)
{
    g_assert (str != NULL);

    while (_rsvg_is_wsp (*str))
        str++;

    if (*str == ',') {
        str++;
        while (_rsvg_is_wsp (*str))
            str++;

        *end = str;
        return TRUE;
    } else {
        *end = str;
        return FALSE;
    }
}

static guint
_rsvg_parse_list_count_items (const char *str)
{
    guint i = 0;

    g_assert (str != NULL);

    /* ensure str does not start with wsp */
    if (_rsvg_is_wsp (*str) || *str == '\0')
        return 0;

    /* count number of items (assuming an item can not contain comma-wsp) */
    while (*str != '\0') {
        if (*str == ',')
            return 0;

        while (!_rsvg_is_wsp (*str) && *str != ',' && *str != '\0')
            str++;
        i++;

        _rsvg_parse_list_next_item (str, &str);
    }
    return i;
}

static gboolean
_rsvg_parse_funciri (const char *str, const char **end, RsvgNode **ref,
                     const RsvgPropSrc prop_src, const RsvgDefs *defs)
{
    char *name;
    guint len;

    if (!rsvg_keyword_ncmp (str, "url(", prop_src))
        return FALSE;

    for (len = 4; str[len] != ')'; len++) {
        if (str[len] == '\0')
            return FALSE;
    }
    name = g_strndup (str + 4, len - 4);
    *end = str + len + 1;

    *ref = rsvg_defs_lookup (defs, name);
    g_free (name);
    return TRUE;
}

/* ========================================================================== */

/* Parsers for basic datatypes or generic attributes */

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

    list.n_items = _rsvg_parse_list_count_items (str);
    if (list.n_items == 0)
        return FALSE;
    list.items = g_new (double, list.n_items);

    /* parse the numbers */
    for (i = 0; i < list.n_items; i++) {
        list.items[i] = _rsvg_parse_number (str, &tmp, prop_src);
        if (str == tmp)
            goto invalid_list;
        str = tmp;

        /* if this is not the last item skip comma-whitespace */
        if (i + 1 != list.n_items) {
            _rsvg_parse_list_next_item (str, &tmp);
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

    list.n_items = _rsvg_parse_list_count_items (str);
    if (list.n_items == 0)
        return FALSE;
    list.items = g_new (RsvgLength, list.n_items);

    /* parse the lengths */
    for (i = 0; i < list.n_items; i++) {
        list.items[i] = _rsvg_parse_length (str, &tmp, prop_src);
        if (str == tmp)
            goto invalid_list;
        str = tmp;

        /* if this is not the last item skip comma-whitespace */
        if (i + 1 != list.n_items) {
            _rsvg_parse_list_next_item (str, &tmp);
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
rsvg_parse_viewbox (const char *str, RsvgViewBox *result)
{
    RsvgViewBox vb;
    const char *tmp;
    vb.active = TRUE;

    g_assert (str != NULL);

    vb.rect.x = _rsvg_parse_number (str, &tmp, SVG_ATTRIBUTE);
    if (str == tmp)
        return FALSE;
    str = tmp;
    _rsvg_parse_list_next_item (str, &tmp);
    if (str == tmp)
        return FALSE;
    str = tmp;

    vb.rect.y = _rsvg_parse_number (str, &tmp, SVG_ATTRIBUTE);
    if (str == tmp)
        return FALSE;
    str = tmp;
    _rsvg_parse_list_next_item (str, &tmp);
    if (str == tmp)
        return FALSE;
    str = tmp;

    vb.rect.width = _rsvg_parse_number (str, &tmp, SVG_ATTRIBUTE);
    if (str == tmp || vb.rect.width < 0.0)
        return FALSE;
    str = tmp;
    _rsvg_parse_list_next_item (str, &tmp);
    if (str == tmp)
        return FALSE;
    str = tmp;

    vb.rect.height = _rsvg_parse_number (str, &tmp, SVG_ATTRIBUTE);
    if (str == tmp || vb.rect.height < 0.0)
        return FALSE;
    str = tmp;
    if (*str != '\0')
        return FALSE;

    *result = vb;
    return TRUE;
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

static gboolean
_rsvg_parse_node_ref (const char *str, RsvgNode **result, const RsvgPropSrc prop_src,
                      const RsvgDefs *defs, const RsvgNodeType node_type)
{
    RsvgNode *ref;

    if (rsvg_keyword_cmp (str, "none", prop_src)) {
        *result = NULL;
        return TRUE;
    }

    if (!_rsvg_parse_funciri (str, &str, &ref, prop_src, defs))
        return FALSE;

    if (ref != NULL && RSVG_NODE_TYPE (ref) != node_type)
        ref = NULL;

    *result = ref;
    return TRUE;
}

/* ========================================================================== */

/* Parsers for presentation attributes */

static gboolean
rsvg_parse_color (const char *str, guint32 *result, const RsvgPropSrc prop_src)
{
    double value;
    const char *end;

    value = _rsvg_parse_raw_color (str, &end);
    if (str == end || *end != '\0')
        return FALSE;

    *result = value;
    return TRUE;
}

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

static gboolean
rsvg_parse_paint (const char *str, RsvgPaintServer *result,
                  const RsvgPropSrc prop_src, const RsvgDefs *defs)
{
    RsvgPaintServer ps, ps_ref;
    RsvgNode *ref;
    gboolean expect_color = TRUE;
    gboolean has_ref = FALSE;

    if (_rsvg_parse_funciri (str, &str, &ref, prop_src, defs)) {
        expect_color = FALSE;
        if (*str != '\0') {
            expect_color = TRUE;
            while (_rsvg_is_wsp (*str))
                str++;
        }

        has_ref = TRUE;
        if (ref == NULL) {
            has_ref = FALSE;
        } else if (RSVG_NODE_TYPE (ref) == RSVG_NODE_TYPE_LINEAR_GRADIENT) {
            ps_ref.type = RSVG_PAINT_SERVER_LIN_GRAD;
            ps_ref.core.lingrad = (RsvgLinearGradient *) ref;
        } else if (RSVG_NODE_TYPE (ref) == RSVG_NODE_TYPE_RADIAL_GRADIENT) {
            ps_ref.type = RSVG_PAINT_SERVER_RAD_GRAD;
            ps_ref.core.radgrad = (RsvgRadialGradient *) ref;
        } else if (RSVG_NODE_TYPE (ref) == RSVG_NODE_TYPE_PATTERN) {
            ps_ref.type = RSVG_PAINT_SERVER_PATTERN;
            ps_ref.core.pattern = (RsvgPattern *) ref;
        } else {
            has_ref = FALSE;
        }
    }

    if (expect_color) {
        if (rsvg_keyword_cmp (str, "none", prop_src)) {
            ps.type = RSVG_PAINT_SERVER_NONE;
        } else if (rsvg_keyword_cmp (str, "currentColor", prop_src)) {
            ps.type = RSVG_PAINT_SERVER_CURRENT_COLOR;
        } else if (rsvg_parse_color (str, &ps.core.color, CSS_VALUE)) {
            ps.type = RSVG_PAINT_SERVER_SOLID;
        } else {
            return FALSE;
        }
    }

    if (has_ref)
        *result = ps_ref;
    else if (expect_color)
        *result = ps;
    else
        return FALSE;
    return TRUE;
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
        result->n_items = 0;
        result->items = NULL;
        return TRUE;
    }

    if (_rsvg_parse_length_list (str, &list, prop_src) == FALSE)
        return FALSE;

    /* make sure there are no values with a negative value and the sum of
       values is not zero */
    for (i = 0; i < list.n_items; i++) {
        if (list.items[i].length > 0.0) {
            nonzero_length = TRUE;
        } else if (list.items[i].length < 0.0) {
            g_free (list.items);
            return FALSE;
        }
    }
    if (nonzero_length == FALSE) {
        /* handle as if a value of none were specified */
        list.n_items = 0;
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
        if (_rsvg_parse_node_ref (value, &state->clip_path, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_CLIP_PATH))
            ; /* there is no has_clip_path */
    } else if (g_str_equal (name, "clip-rule")) {
        if (rsvg_parse_fill_rule (value, &state->clip_rule, prop_src))
            state->has_clip_rule = TRUE;
    } else if (g_str_equal (name, "color")) {
        if (rsvg_parse_color (value, &state->color, prop_src))
            state->has_current_color = TRUE;
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
        if (rsvg_parse_paint (value, &state->fill, prop_src, ctx->priv->defs))
            state->has_fill_server = TRUE;
    } else if (g_str_equal (name, "fill-opacity")) {
        if (_rsvg_parse_opacity (value, &state->fill_opacity, prop_src))
            state->has_fill_opacity = TRUE;
    } else if (g_str_equal (name, "fill-rule")) {
        if (rsvg_parse_fill_rule (value, &state->fill_rule, prop_src))
            state->has_fill_rule = TRUE;
    } else if (g_str_equal (name, "filter")) {
        if (_rsvg_parse_node_ref (value, &state->clip_path, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_FILTER))
            ; /* there is no has_filter */
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
        if (_rsvg_parse_node_ref (value, &state->clip_path, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MARKER))
            state->has_startMarker = TRUE;
    } else if (g_str_equal (name, "marker-mid")) {
        if (_rsvg_parse_node_ref (value, &state->clip_path, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MARKER))
            state->has_middleMarker = TRUE;
    } else if (g_str_equal (name, "marker-end")) {
        if (_rsvg_parse_node_ref (value, &state->clip_path, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MARKER))
            state->has_endMarker = TRUE;
    } else if (g_str_equal (name, "mask")) {
        if (_rsvg_parse_node_ref (value, &state->clip_path, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MASK))
            ; /* there is no has_mask */
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
        if (rsvg_parse_paint (value, &state->stroke, prop_src, ctx->priv->defs))
            state->has_stroke_server = TRUE;
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
