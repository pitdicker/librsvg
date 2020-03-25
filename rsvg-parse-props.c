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

static float
_rsvg_parse_rgb_value (const char *str, const char **end, gboolean is_percentage)
{
    float value;

    while (_rsvg_is_wsp (*str))
        str++;

    if (is_percentage) {
        value = _rsvg_parse_number (str, end, RSVG_NUMBER_FORMAT_CSS2) * 2.55;
        if (*end == str || **end != '%')
            goto invalid_value;
        (*end)++;
    } else {
        value = g_ascii_strtoll (str, (gchar **) end, 10);
        if (*end == str)
            goto invalid_value;
    }
    str = *end;

    while (_rsvg_is_wsp (**end))
        (*end)++;

    return value;

invalid_value:
    *end = str;
    return 0.0;
}

RsvgColor
_rsvg_parse_raw_color (const char *str, const char **end)
{
    RsvgColor color;
    guint i;
    gboolean has_alpha = FALSE;
    gboolean is_percentage = FALSE;
    const char *str_start;
    guint32 hex;

    struct keywords {
        const char *keyword;
        RsvgColor value;
    };
    const struct keywords svg_color_keywords[] = {
        {"aliceblue",            (RsvgColor) {240, 248, 255, 1.0}},
        {"antiquewhite",         (RsvgColor) {250, 235, 215, 1.0}},
        {"aqua",                 (RsvgColor) {  0, 255, 255, 1.0}},
        {"aquamarine",           (RsvgColor) {127, 255, 212, 1.0}},
        {"azure",                (RsvgColor) {240, 255, 255, 1.0}},
        {"beige",                (RsvgColor) {245, 245, 220, 1.0}},
        {"bisque",               (RsvgColor) {255, 228, 196, 1.0}},
        {"black",                (RsvgColor) {  0,   0,   0, 1.0}},
        {"blanchedalmond",       (RsvgColor) {255, 235, 205, 1.0}},
        {"blue",                 (RsvgColor) {  0,   0, 255, 1.0}},
        {"blueviolet",           (RsvgColor) {138,  43, 226, 1.0}},
        {"brown",                (RsvgColor) {165,  42,  42, 1.0}},
        {"burlywood",            (RsvgColor) {222, 184, 135, 1.0}},
        {"cadetblue",            (RsvgColor) { 95, 158, 160, 1.0}},
        {"chartreuse",           (RsvgColor) {127, 255,   0, 1.0}},
        {"chocolate",            (RsvgColor) {210, 105,  30, 1.0}},
        {"coral",                (RsvgColor) {255, 127,  80, 1.0}},
        {"cornflowerblue",       (RsvgColor) {100, 149, 237, 1.0}},
        {"cornsilk",             (RsvgColor) {255, 248, 220, 1.0}},
        {"crimson",              (RsvgColor) {220,  20,  60, 1.0}},
        {"cyan",                 (RsvgColor) {  0, 255, 255, 1.0}},
        {"darkblue",             (RsvgColor) {  0,   0, 139, 1.0}},
        {"darkcyan",             (RsvgColor) {  0, 139, 139, 1.0}},
        {"darkgoldenrod",        (RsvgColor) {184, 134,  11, 1.0}},
        {"darkgray",             (RsvgColor) {169, 169, 169, 1.0}},
        {"darkgreen",            (RsvgColor) {  0, 100,   0, 1.0}},
        {"darkgrey",             (RsvgColor) {169, 169, 169, 1.0}},
        {"darkkhaki",            (RsvgColor) {189, 183, 107, 1.0}},
        {"darkmagenta",          (RsvgColor) {139,   0, 139, 1.0}},
        {"darkolivegreen",       (RsvgColor) { 85, 107,  47, 1.0}},
        {"darkorange",           (RsvgColor) {255, 140,   0, 1.0}},
        {"darkorchid",           (RsvgColor) {153,  50, 204, 1.0}},
        {"darkred",              (RsvgColor) {139,   0,   0, 1.0}},
        {"darksalmon",           (RsvgColor) {233, 150, 122, 1.0}},
        {"darkseagreen",         (RsvgColor) {143, 188, 143, 1.0}},
        {"darkslateblue",        (RsvgColor) { 72,  61, 139, 1.0}},
        {"darkslategray",        (RsvgColor) { 47,  79,  79, 1.0}},
        {"darkslategrey",        (RsvgColor) { 47,  79,  79, 1.0}},
        {"darkturquoise",        (RsvgColor) {  0, 206, 209, 1.0}},
        {"darkviolet",           (RsvgColor) {148,   0, 211, 1.0}},
        {"deeppink",             (RsvgColor) {255,  20, 147, 1.0}},
        {"deepskyblue",          (RsvgColor) {  0, 191, 255, 1.0}},
        {"dimgray",              (RsvgColor) {105, 105, 105, 1.0}},
        {"dimgrey",              (RsvgColor) {105, 105, 105, 1.0}},
        {"dodgerblue",           (RsvgColor) { 30, 144, 255, 1.0}},
        {"firebrick",            (RsvgColor) {178,  34,  34, 1.0}},
        {"floralwhite",          (RsvgColor) {255, 250, 240, 1.0}},
        {"forestgreen",          (RsvgColor) { 34, 139,  34, 1.0}},
        {"fuchsia",              (RsvgColor) {255,   0, 255, 1.0}},
        {"gainsboro",            (RsvgColor) {220, 220, 220, 1.0}},
        {"ghostwhite",           (RsvgColor) {248, 248, 255, 1.0}},
        {"gold",                 (RsvgColor) {255, 215,   0, 1.0}},
        {"goldenrod",            (RsvgColor) {218, 165,  32, 1.0}},
        {"gray",                 (RsvgColor) {128, 128, 128, 1.0}},
        {"grey",                 (RsvgColor) {128, 128, 128, 1.0}},
        {"green",                (RsvgColor) {  0, 128,   0, 1.0}},
        {"greenyellow",          (RsvgColor) {173, 255,  47, 1.0}},
        {"honeydew",             (RsvgColor) {240, 255, 240, 1.0}},
        {"hotpink",              (RsvgColor) {255, 105, 180, 1.0}},
        {"indianred",            (RsvgColor) {205,  92,  92, 1.0}},
        {"indigo",               (RsvgColor) { 75,   0, 130, 1.0}},
        {"ivory",                (RsvgColor) {255, 255, 240, 1.0}},
        {"khaki",                (RsvgColor) {240, 230, 140, 1.0}},
        {"lavender",             (RsvgColor) {230, 230, 250, 1.0}},
        {"lavenderblush",        (RsvgColor) {255, 240, 245, 1.0}},
        {"lawngreen",            (RsvgColor) {124, 252,   0, 1.0}},
        {"lemonchiffon",         (RsvgColor) {255, 250, 205, 1.0}},
        {"lightblue",            (RsvgColor) {173, 216, 230, 1.0}},
        {"lightcoral",           (RsvgColor) {240, 128, 128, 1.0}},
        {"lightcyan",            (RsvgColor) {224, 255, 255, 1.0}},
        {"lightgoldenrodyellow", (RsvgColor) {250, 250, 210, 1.0}},
        {"lightgray",            (RsvgColor) {211, 211, 211, 1.0}},
        {"lightgreen",           (RsvgColor) {144, 238, 144, 1.0}},
        {"lightgrey",            (RsvgColor) {211, 211, 211, 1.0}},
        {"lightpink",            (RsvgColor) {255, 182, 193, 1.0}},
        {"lightsalmon",          (RsvgColor) {255, 160, 122, 1.0}},
        {"lightseagreen",        (RsvgColor) { 32, 178, 170, 1.0}},
        {"lightskyblue",         (RsvgColor) {135, 206, 250, 1.0}},
        {"lightslategray",       (RsvgColor) {119, 136, 153, 1.0}},
        {"lightslategrey",       (RsvgColor) {119, 136, 153, 1.0}},
        {"lightsteelblue",       (RsvgColor) {176, 196, 222, 1.0}},
        {"lightyellow",          (RsvgColor) {255, 255, 224, 1.0}},
        {"lime",                 (RsvgColor) {  0, 255,   0, 1.0}},
        {"limegreen",            (RsvgColor) { 50, 205,  50, 1.0}},
        {"linen",                (RsvgColor) {250, 240, 230, 1.0}},
        {"magenta",              (RsvgColor) {255,   0, 255, 1.0}},
        {"maroon",               (RsvgColor) {128,   0,   0, 1.0}},
        {"mediumaquamarine",     (RsvgColor) {102, 205, 170, 1.0}},
        {"mediumblue",           (RsvgColor) {  0,   0, 205, 1.0}},
        {"mediumorchid",         (RsvgColor) {186,  85, 211, 1.0}},
        {"mediumpurple",         (RsvgColor) {147, 112, 219, 1.0}},
        {"mediumseagreen",       (RsvgColor) { 60, 179, 113, 1.0}},
        {"mediumslateblue",      (RsvgColor) {123, 104, 238, 1.0}},
        {"mediumspringgreen",    (RsvgColor) {  0, 250, 154, 1.0}},
        {"mediumturquoise",      (RsvgColor) { 72, 209, 204, 1.0}},
        {"mediumvioletred",      (RsvgColor) {199,  21, 133, 1.0}},
        {"midnightblue",         (RsvgColor) { 25,  25, 112, 1.0}},
        {"mintcream",            (RsvgColor) {245, 255, 250, 1.0}},
        {"mistyrose",            (RsvgColor) {255, 228, 225, 1.0}},
        {"moccasin",             (RsvgColor) {255, 228, 181, 1.0}},
        {"navajowhite",          (RsvgColor) {255, 222, 173, 1.0}},
        {"navy",                 (RsvgColor) {  0,   0, 128, 1.0}},
        {"oldlace",              (RsvgColor) {253, 245, 230, 1.0}},
        {"olive",                (RsvgColor) {128, 128,   0, 1.0}},
        {"olivedrab",            (RsvgColor) {107, 142,  35, 1.0}},
        {"orange",               (RsvgColor) {255, 165,   0, 1.0}},
        {"orangered",            (RsvgColor) {255,  69,   0, 1.0}},
        {"orchid",               (RsvgColor) {218, 112, 214, 1.0}},
        {"palegoldenrod",        (RsvgColor) {238, 232, 170, 1.0}},
        {"palegreen",            (RsvgColor) {152, 251, 152, 1.0}},
        {"paleturquoise",        (RsvgColor) {175, 238, 238, 1.0}},
        {"palevioletred",        (RsvgColor) {219, 112, 147, 1.0}},
        {"papayawhip",           (RsvgColor) {255, 239, 213, 1.0}},
        {"peachpuff",            (RsvgColor) {255, 218, 185, 1.0}},
        {"peru",                 (RsvgColor) {205, 133,  63, 1.0}},
        {"pink",                 (RsvgColor) {255, 192, 203, 1.0}},
        {"plum",                 (RsvgColor) {221, 160, 221, 1.0}},
        {"powderblue",           (RsvgColor) {176, 224, 230, 1.0}},
        {"purple",               (RsvgColor) {128,   0, 128, 1.0}},
        {"red",                  (RsvgColor) {255,   0,   0, 1.0}},
        {"rosybrown",            (RsvgColor) {188, 143, 143, 1.0}},
        {"royalblue",            (RsvgColor) { 65, 105, 225, 1.0}},
        {"saddlebrown",          (RsvgColor) {139,  69,  19, 1.0}},
        {"salmon",               (RsvgColor) {250, 128, 114, 1.0}},
        {"sandybrown",           (RsvgColor) {244, 164,  96, 1.0}},
        {"seagreen",             (RsvgColor) { 46, 139,  87, 1.0}},
        {"seashell",             (RsvgColor) {255, 245, 238, 1.0}},
        {"sienna",               (RsvgColor) {160,  82,  45, 1.0}},
        {"silver",               (RsvgColor) {192, 192, 192, 1.0}},
        {"skyblue",              (RsvgColor) {135, 206, 235, 1.0}},
        {"slateblue",            (RsvgColor) {106,  90, 205, 1.0}},
        {"slategray",            (RsvgColor) {112, 128, 144, 1.0}},
        {"slategrey",            (RsvgColor) {112, 128, 144, 1.0}},
        {"snow",                 (RsvgColor) {255, 250, 250, 1.0}},
        {"springgreen",          (RsvgColor) {  0, 255, 127, 1.0}},
        {"steelblue",            (RsvgColor) { 70, 130, 180, 1.0}},
        {"tan",                  (RsvgColor) {210, 180, 140, 1.0}},
        {"teal",                 (RsvgColor) {  0, 128, 128, 1.0}},
        {"thistle",              (RsvgColor) {216, 191, 216, 1.0}},
        {"tomato",               (RsvgColor) {255,  99,  71, 1.0}},
        {"turquoise",            (RsvgColor) { 64, 224, 208, 1.0}},
        {"violet",               (RsvgColor) {238, 130, 238, 1.0}},
        {"wheat",                (RsvgColor) {245, 222, 179, 1.0}},
        {"white",                (RsvgColor) {255, 255, 255, 1.0}},
        {"whitesmoke",           (RsvgColor) {245, 245, 245, 1.0}},
        {"yellow",               (RsvgColor) {255, 255,   0, 1.0}},
        {"yellowgreen",          (RsvgColor) {154, 205,  50, 1.0}},
    };
    const struct keywords system_color_keywords[] = {
        /* emulate using default system colors of Windows 98 */
        {"ActiveBorder",         (RsvgColor) {192, 192, 192, 1.0}},
        {"ActiveCaption",        (RsvgColor) {  0,   0, 132, 1.0}},
        {"AppWorkspace",         (RsvgColor) {128, 128, 128, 1.0}},
        {"Background",           (RsvgColor) {  0, 128, 129, 1.0}},
        {"ButtonFace",           (RsvgColor) {192, 192, 192, 1.0}},
        {"ButtonHighlight",      (RsvgColor) {223, 223, 223, 1.0}},
        {"ButtonShadow",         (RsvgColor) {128, 128, 128, 1.0}},
        {"ButtonText",           (RsvgColor) {  0,   0,   0, 1.0}},
        {"CaptionText",          (RsvgColor) {255, 255, 255, 1.0}},
        {"GrayText",             (RsvgColor) {128, 128, 128, 1.0}},
        {"Highlight",            (RsvgColor) {  8,  36, 107, 1.0}},
        {"HighlightText",        (RsvgColor) {255, 255, 255, 1.0}},
        {"InactiveBorder",       (RsvgColor) {192, 192, 192, 1.0}},
        {"InactiveCaption",      (RsvgColor) {128, 128, 128, 1.0}},
        {"InactiveCaptionText",  (RsvgColor) {192, 192, 192, 1.0}},
        {"InfoBackground",       (RsvgColor) {255, 255, 225, 1.0}},
        {"InfoText",             (RsvgColor) {  0,   0,   0, 1.0}},
        {"Menu",                 (RsvgColor) {192, 192, 192, 1.0}},
        {"MenuText",             (RsvgColor) {  0,   0,   0, 1.0}},
        {"Scrollbar",            (RsvgColor) {192, 192, 192, 1.0}},
        {"ThreeDDarkShadow",     (RsvgColor) {  0,   0,   0, 1.0}},
        {"ThreeDFace",           (RsvgColor) {192, 192, 192, 1.0}},
        {"ThreeDHighlight",      (RsvgColor) {223, 223, 223, 1.0}},
        {"ThreeDLightShadow",    (RsvgColor) {255, 255, 255, 1.0}},
        {"ThreeDShadow",         (RsvgColor) {128, 128, 128, 1.0}},
        {"Window",               (RsvgColor) {255, 255, 255, 1.0}},
        {"WindowFrame",          (RsvgColor) {  0,   0,   0, 1.0}},
        {"WindowText",           (RsvgColor) {  0,   0,   0, 1.0}}
    };
    struct keywords *keyword;

    g_assert (str != NULL);
    str_start = str;

    if (*str == '#') {
        str++;
        hex = 0;
        for (i = 0; i < 6; i++) {
            if (*str >= '0' && *str <= '9') {
                hex = (hex << 4) + (*str - '0');
            } else if (*str >= 'A' && *str <= 'F') {
                hex = (hex << 4) + (*str - 'A' + 10);
            } else if (*str >= 'a' && *str <= 'f') {
                hex = (hex << 4) + (*str - 'a' + 10);
            } else {
                break;
            }
            str++;
        }
        *end = str;

        if (i == 3) {
            /* convert #rgb to #rrggbb */
            hex = ((hex & 0xf00) << 8) | ((hex & 0x0f0) << 4) | (hex & 0x00f);
            hex |= hex << 4;
        } else if (i != 6) {
            goto invalid_color;
        }

        color.r = (hex >> 16) & 0xff;
        color.g = (hex >>  8) & 0xff;
        color.b = hex & 0xff;
        color.a = 1.0;
        return color;
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

        color.r = _rsvg_parse_rgb_value (str, end, is_percentage);
        if (*end == str)
            goto invalid_color;
        str = *end;

        if (*str != ',')
            goto invalid_color;
        str++;

        color.g = _rsvg_parse_rgb_value (str, end, is_percentage);
        if (*end == str)
            goto invalid_color;
        str = *end;

        if (*str != ',')
            goto invalid_color;
        str++;

        color.b = _rsvg_parse_rgb_value (str, end, is_percentage);
        if (*end == str)
            goto invalid_color;
        str = *end;

        if (has_alpha) {
            if (*str != ',')
                goto invalid_color;
            str++;

            while (_rsvg_is_wsp (*str))
                str++;

            color.a = _rsvg_parse_number (str, end, RSVG_NUMBER_FORMAT_CSS2);
            if (*end == str)
                goto invalid_color;
            str = *end;

            while (_rsvg_is_wsp (*str))
                str++;
        }

        if (*str != ')')
            goto invalid_color;
        str++;

        *end = str;
        return color;
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
    return (RsvgColor) {0, 0, 0, 1.0}; /* black */
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

static RsvgNode *
_rsvg_parse_funciri (const char *str, const char **end,
                     const RsvgPropSrc prop_src, const RsvgDefs *defs)
{
    char *name;
    guint len;
    RsvgNode *ref;

    *end = str;

    if (!rsvg_keyword_ncmp (str, "url(", prop_src))
        return NULL;

    for (len = 4; str[len] != ')'; len++) {
        if (str[len] == '\0')
            return NULL;
    }
    name = g_strndup (str + 4, len - 4);
    *end = str + len + 1;

    ref = rsvg_defs_lookup (defs, name);
    g_free (name);
    return ref;
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
_rsvg_parse_opacity (const char *str, float *result, const RsvgPropSrc prop_src)
{
    float opacity;
    const char *end;

    opacity = _rsvg_parse_number (str, &end, prop_src);
    if (str == end || *end != '\0')
        return FALSE;

    *result = opacity;

    return TRUE;
}

static gboolean
_rsvg_parse_color (const char *str, RsvgPaint *result, const RsvgPropSrc prop_src)
{
    RsvgColor color;
    const char *end;

    if (rsvg_keyword_cmp (str, "currentColor", prop_src)) {
        result->paint.type = CURRENT_COLOR;
        return TRUE;
    }

    color = _rsvg_parse_raw_color (str, &end);
    if (str == end || *end != '\0')
        return FALSE;

    /* TODO: parse icccolor */

    result->color = color;
    return TRUE;
}

static gboolean
_rsvg_parse_node_ref (const char *str, RsvgNode **result, const RsvgPropSrc prop_src,
                      const RsvgDefs *defs, const RsvgNodeType node_type)
{
    RsvgNode *ref;
    const char *end;

    if (rsvg_keyword_cmp (str, "none", prop_src)) {
        *result = NULL;
        return TRUE;
    }

    ref = _rsvg_parse_funciri (str, &end, prop_src, defs);

    if (ref == NULL) {
        if (end == str)
            return FALSE;
    } else if (RSVG_NODE_TYPE (ref) != node_type) {
        ref = NULL;
    }

    *result = ref;
    return TRUE;
}

/* ========================================================================== */

/* Parsers for presentation attributes */

static gboolean
rsvg_parse_current_color (const char *str, RsvgColor *result, const RsvgPropSrc prop_src)
{
    RsvgColor color;
    const char *end;

    color = _rsvg_parse_raw_color (str, &end);
    if (str == end || *end != '\0')
        return FALSE;

    *result = color;
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
rsvg_parse_paint (const char *str, RsvgPaint *result,
                  const RsvgPropSrc prop_src, const RsvgDefs *defs)
{
    RsvgPaint paint, paint_server;
    RsvgNode *ref;
    const char *end;
    gboolean expect_color = TRUE;
    gboolean has_ref = FALSE;

    ref = _rsvg_parse_funciri (str, &end, prop_src, defs);
    if (end != str) {
        has_ref = TRUE;
        if (ref == NULL) {
            has_ref = FALSE;
        } else if (RSVG_NODE_TYPE (ref) == RSVG_NODE_TYPE_LINEAR_GRADIENT) {
            paint_server.paint.type = LINEAR_GRADIENT;
            paint_server.paint.server = ref;
        } else if (RSVG_NODE_TYPE (ref) == RSVG_NODE_TYPE_RADIAL_GRADIENT) {
            paint_server.paint.type = RADIAL_GRADIENT;
            paint_server.paint.server = ref;
        } else if (RSVG_NODE_TYPE (ref) == RSVG_NODE_TYPE_PATTERN) {
            paint_server.paint.type = PATTERN;
            paint_server.paint.server = ref;
        } else {
            has_ref = FALSE;
        }

        /* prepare for a fallback color */
        expect_color = FALSE;
        if (*end != '\0') {
            str = end;
            expect_color = TRUE;
            while (_rsvg_is_wsp (*str))
                str++;
        }
    }

    if (expect_color) {
        if (rsvg_keyword_cmp (str, "none", prop_src)) {
            paint.paint.type = NONE;
        } else if (rsvg_keyword_cmp (str, "currentColor", prop_src)) {
            paint.paint.type = CURRENT_COLOR;
        } else if (!rsvg_parse_current_color (str, &paint.color, CSS_VALUE)) {
            return FALSE;
        }
    }

    if (has_ref)
        *result = paint_server;
    else if (expect_color)
        *result = paint;
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
    if (name == NULL || value == NULL)
        return;

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
        if (rsvg_parse_current_color (value, &state->color, prop_src))
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
        if (_rsvg_parse_node_ref (value, &state->filter, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_FILTER))
            ; /* there is no has_filter */
    } else if (g_str_equal (name, "flood-color")) {
        if (_rsvg_parse_color (value, &state->flood_color, prop_src))
            state->has_flood_color = TRUE;
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
        if (_rsvg_parse_color (value, &state->lighting_color, prop_src))
            ; /* there is no has_lighting_color */
    } else if (g_str_equal (name, "marker")) {
        /* TODO */
    } else if (g_str_equal (name, "marker-start")) {
        if (_rsvg_parse_node_ref (value, &state->marker_start, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MARKER))
            state->has_startMarker = TRUE;
    } else if (g_str_equal (name, "marker-mid")) {
        if (_rsvg_parse_node_ref (value, &state->marker_mid, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MARKER))
            state->has_middleMarker = TRUE;
    } else if (g_str_equal (name, "marker-end")) {
        if (_rsvg_parse_node_ref (value, &state->marker_end, prop_src,
                                  ctx->priv->defs, RSVG_NODE_TYPE_MARKER))
            state->has_endMarker = TRUE;
    } else if (g_str_equal (name, "mask")) {
        if (_rsvg_parse_node_ref (value, &state->mask, prop_src,
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
        if (_rsvg_parse_color (value, &state->stop_color, prop_src))
            state->has_stop_color = TRUE;
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
