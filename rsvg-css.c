/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-css.c: Parse CSS basic data types.

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

   Authors: Dom Lachowicz <cinamod@hotmail.com>
   Raph Levien <raph@artofcode.com>
*/

#include "config.h"
#include "rsvg-css.h"
#include "rsvg-private.h"
#include "rsvg-styles.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <errno.h>
#include <math.h>

#include <libxml/parser.h>

#include <libcroco/libcroco.h>

#define SETINHERIT() G_STMT_START {if (inherit != NULL) *inherit = TRUE;} G_STMT_END
#define UNSETINHERIT() G_STMT_START {if (inherit != NULL) *inherit = FALSE;} G_STMT_END

/**
 * rsvg_css_parse_vbox:
 * @vbox: The CSS viewBox
 * @x : The X output
 * @y: The Y output
 * @w: The Width output
 * @h: The Height output
 *
 * Returns:
 */
RsvgViewBox
rsvg_css_parse_vbox (const char *vbox)
{
    RsvgViewBox vb;
    gdouble *list;
    guint list_len;
    vb.active = FALSE;

    vb.rect.x = vb.rect.y = 0;
    vb.rect.width = vb.rect.height = 0;

    list = rsvg_css_parse_number_list (vbox, &list_len);

    if (!(list && list_len))
        return vb;
    else if (list_len != 4) {
        g_free (list);
        return vb;
    } else {
        vb.rect.x = list[0];
        vb.rect.y = list[1];
        vb.rect.width = list[2];
        vb.rect.height = list[3];
        vb.active = TRUE;

        g_free (list);
        return vb;
    }
}

double
_rsvg_css_normalize_font_size (const RsvgState * state, const RsvgDrawingCtx * ctx)
{
    RsvgState *parent;
    double parent_font_size;
    double font_size = state->font_size.length;

    switch (state->font_size.unit) {
    case RSVG_UNIT_PERCENTAGE:
        font_size *= 0.01;
        break;
    case RSVG_UNIT_EMS:
        break;
    case RSVG_UNIT_EXS:
        font_size *= 0.5; /* TODO: should use real x-height of font */
        break;
    default:
        return rsvg_normalize_length (&state->font_size, ctx, NO_DIR);
    }

    if ((parent = rsvg_state_parent (state)))
        parent_font_size = _rsvg_css_normalize_font_size (parent, ctx);
    else
        parent_font_size = RSVG_DEFAULT_FONT_SIZE;

    return font_size * parent_font_size;
}

double
rsvg_normalize_length (const RsvgLength * in, const RsvgDrawingCtx * ctx,
                       const RsvgLengthDir dir)
{
    switch (in->unit) {
    case RSVG_UNIT_NUMBER:
        return in->length;
    case RSVG_UNIT_PERCENTAGE:
        switch (dir) {
        case HORIZONTAL:
            return in->length * 0.01 * ctx->vb.rect.width;
        case VERTICAL:
            return in->length * 0.01 * ctx->vb.rect.height;
        case NO_DIR:
            return in->length * 0.01 * sqrt ((ctx->vb.rect.width * ctx->vb.rect.width +
                                              ctx->vb.rect.height * ctx->vb.rect.height) / 2.0);
        }
    case RSVG_UNIT_EMS:
        return in->length * _rsvg_css_normalize_font_size (rsvg_current_state (ctx), ctx);
    case RSVG_UNIT_EXS:
        /* TODO: should use real x-height of font */
        return in->length * _rsvg_css_normalize_font_size (rsvg_current_state (ctx), ctx) * 0.5;
    case RSVG_UNIT_PX:
        return in->length;
    case RSVG_UNIT_CM:
        return in->length / 2.54 * ctx->dpi;
    case RSVG_UNIT_MM:
        return in->length / 25.4 * ctx->dpi;
    case RSVG_UNIT_IN:
        return in->length * ctx->dpi;
    case RSVG_UNIT_PT:
        return in->length / 72.0 * ctx->dpi;
    case RSVG_UNIT_PC:
        return in->length / 6.0 * ctx->dpi;
    case RSVG_UNIT_UNKNOWN:
    default:
        g_assert_not_reached();
        return 0.0;
    }
}

double
_rsvg_css_hand_normalize_length (const RsvgLength * in, gdouble pixels_per_inch,
                                 gdouble width_or_height, gdouble font_size)
{
    switch (in->unit) {
    case RSVG_UNIT_NUMBER:
        return in->length;
    case RSVG_UNIT_PERCENTAGE:
        return in->length * 0.01 * width_or_height;
    case RSVG_UNIT_EMS:
        return in->length * font_size;
    case RSVG_UNIT_EXS:
        return in->length * font_size * 0.5; /* TODO: should use real x-height of font */
    case RSVG_UNIT_PX:
        return in->length;
    case RSVG_UNIT_CM:
        return in->length / 2.54 * pixels_per_inch;
    case RSVG_UNIT_MM:
        return in->length / 25.4 * pixels_per_inch;
    case RSVG_UNIT_IN:
        return in->length * pixels_per_inch;
    case RSVG_UNIT_PT:
        return in->length / 72.0 * pixels_per_inch;
    case RSVG_UNIT_PC:
        return in->length / 6.0 * pixels_per_inch;
    case RSVG_UNIT_UNKNOWN:
    default:
        g_assert_not_reached();
        return 0.0;
    }
}

guint
rsvg_normalize_stroke_dasharray (const RsvgLengthList src,
                                 double **dst,
                                 const RsvgDrawingCtx *ctx)
{
    double *result;
    gboolean is_even = FALSE;
    guint i, n_dashes;

    g_assert (src.number_of_items != 0 && src.items != NULL);

    is_even = (src.number_of_items % 2 == 0);
    n_dashes = (is_even? 1 : 2) * src.number_of_items;

    result = g_new (double, n_dashes);

    for (i = 0; i < src.number_of_items; i++)
        result[i] = rsvg_normalize_length (&src.items[i], ctx, NO_DIR);

    /* an odd number of dashes gets repeated */
    if (!is_even) {
        for (; i < n_dashes; i++)
            result[i] = result[i - src.number_of_items];
    }

    *dst = result;
    return n_dashes;
}

static gint
rsvg_css_clip_rgb_percent (const char *s, double max)
{
    double value;
    char *end;

    value = g_ascii_strtod (s, &end);

    if (*end == '%') {
        value = CLAMP (value, 0, 100) / 100.0;
    }
    else {
        value = CLAMP (value, 0, max) / max;
    }

    return (gint) floor (value * 255 + 0.5);
}

/* pack 3 [0,255] ints into one 32 bit one */
#define PACK_RGBA(r,g,b,a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define PACK_RGB(r,g,b) PACK_RGBA(r, g, b, 255)

/**
 * rsvg_css_parse_color:
 * @str: string to parse
 * @inherit: whether to inherit
 *
 * Parse a CSS2 color specifier, return RGB value
 *
 * Returns: and RGB value
 */
guint32
rsvg_css_parse_color (const char *str, gboolean * inherit)
{
    gint val = 0;

    SETINHERIT ();

    if (str[0] == '#') {
        int i;
        for (i = 1; str[i]; i++) {
            int hexval;
            if (str[i] >= '0' && str[i] <= '9')
                hexval = str[i] - '0';
            else if (str[i] >= 'A' && str[i] <= 'F')
                hexval = str[i] - 'A' + 10;
            else if (str[i] >= 'a' && str[i] <= 'f')
                hexval = str[i] - 'a' + 10;
            else
                break;
            val = (val << 4) + hexval;
        }
        /* handle #rgb case */
        if (i == 4) {
            val = ((val & 0xf00) << 8) | ((val & 0x0f0) << 4) | (val & 0x00f);
            val |= val << 4;
        }

        val |= 0xff000000; /* opaque */
    }
    else if (g_str_has_prefix (str, "rgb")) {
        gint r, g, b, a;
        gboolean has_alpha;
        guint nb_toks;
        char **toks;

        r = g = b = 0;
        a = 255;

        if (str[3] == 'a') {
            /* "rgba" */
            has_alpha = TRUE;
            str += 4;
        }
        else {
            /* "rgb" */
            has_alpha = FALSE;
            str += 3;
        }

        str = strchr (str, '(');
        if (str == NULL)
          return val;

        toks = rsvg_css_parse_list (str + 1, &nb_toks);

        if (toks) {
            if (nb_toks == (has_alpha ? 4 : 3)) {
                r = rsvg_css_clip_rgb_percent (toks[0], 255.0);
                g = rsvg_css_clip_rgb_percent (toks[1], 255.0);
                b = rsvg_css_clip_rgb_percent (toks[2], 255.0);
                if (has_alpha)
                    a = rsvg_css_clip_rgb_percent (toks[3], 1.0);
                else
                    a = 255;
            }

            g_strfreev (toks);
        }

        val = PACK_RGBA (r, g, b, a);
    } else if (!strcmp (str, "inherit"))
        UNSETINHERIT ();
    else {
        CRRgb rgb;

        if (cr_rgb_set_from_name (&rgb, (const guchar *) str) == CR_OK) {
            val = PACK_RGB (rgb.red, rgb.green, rgb.blue);
        } else {
            /* default to opaque black on failed lookup */
            UNSETINHERIT ();
            val = PACK_RGB (0, 0, 0);
        }
    }

    return val;
}

#undef PACK_RGB
#undef PACK_RGBA

/*
  <angle>: An angle value is a <number> optionally followed immediately with
  an angle unit identifier. Angle unit identifiers are:

    * deg: degrees
    * grad: grads
    * rad: radians

    For properties defined in [CSS2], an angle unit identifier must be provided.
    For SVG-specific attributes and properties, the angle unit identifier is
    optional. If not provided, the angle value is assumed to be in degrees.
*/
double
rsvg_css_parse_angle (const char *str)
{
    double degrees;
    char *end_ptr;

    degrees = g_ascii_strtod (str, &end_ptr);

    /* todo: error condition - figure out how to best represent it */
    if ((degrees == -HUGE_VAL || degrees == HUGE_VAL) && (ERANGE == errno))
        return 0.0;

    if (end_ptr) {
        if (!strcmp (end_ptr, "rad"))
            return degrees * 180. / G_PI;
        else if (!strcmp (end_ptr, "grad"))
            return degrees * 360. / 400.;
    }

    return degrees;
}

#if !defined(HAVE_STRTOK_R)

static char *
strtok_r (char *s, const char *delim, char **last)
{
    char *p;

    if (s == NULL)
        s = *last;

    if (s == NULL)
        return NULL;

    while (*s && strchr (delim, *s))
        s++;

    if (*s == '\0') {
        *last = NULL;
        return NULL;
    }

    p = s;
    while (*p && !strchr (delim, *p))
        p++;

    if (*p == '\0')
        *last = NULL;
    else {
        *p = '\0';
        p++;
        *last = p;
    }

    return s;
}

#endif                          /* !HAVE_STRTOK_R */

gchar **
rsvg_css_parse_list (const char *in_str, guint * out_list_len)
{
    char *ptr, *tok;
    char *str;

    guint n = 0;
    GSList *string_list = NULL;
    gchar **string_array = NULL;

    str = g_strdup (in_str);
    tok = strtok_r (str, ", \t", &ptr);
    if (tok != NULL) {
        if (strcmp (tok, " ") != 0) {
            string_list = g_slist_prepend (string_list, g_strdup (tok));
            n++;
        }

        while ((tok = strtok_r (NULL, ", \t", &ptr)) != NULL) {
            if (strcmp (tok, " ") != 0) {
                string_list = g_slist_prepend (string_list, g_strdup (tok));
                n++;
            }
        }
    }
    g_free (str);

    if (out_list_len)
        *out_list_len = n;

    if (string_list) {
        GSList *slist;

        string_array = g_new (gchar *, n + 1);

        string_array[n--] = NULL;
        for (slist = string_list; slist; slist = slist->next)
            string_array[n--] = (gchar *) slist->data;

        g_slist_free (string_list);
    }

    return string_array;
}

gdouble *
rsvg_css_parse_number_list (const char *in_str, guint * out_list_len)
{
    gchar **string_array;
    gdouble *output;
    guint len, i;

    if (out_list_len)
        *out_list_len = 0;

    string_array = rsvg_css_parse_list (in_str, &len);

    if (!(string_array && len))
        return NULL;

    output = g_new (gdouble, len);

    /* TODO: some error checking */
    for (i = 0; i < len; i++)
        output[i] = g_ascii_strtod (string_array[i], NULL);

    g_strfreev (string_array);

    if (out_list_len != NULL)
        *out_list_len = len;

    return output;
}

void
rsvg_css_parse_number_optional_number (const char *str, double *x, double *y)
{
    char *endptr;

    /* TODO: some error checking */

    *x = g_ascii_strtod (str, &endptr);

    if (endptr && *endptr != '\0')
        while (g_ascii_isspace (*endptr) && *endptr)
            endptr++;

    if (endptr && *endptr)
        *y = g_ascii_strtod (endptr, NULL);
    else
        *y = *x;
}

int
rsvg_css_parse_aspect_ratio (const char *str)
{
    char **elems;
    guint nb_elems;

    int ratio = RSVG_ASPECT_RATIO_NONE;

    elems = rsvg_css_parse_list (str, &nb_elems);

    if (elems && nb_elems) {
        guint i;

        for (i = 0; i < nb_elems; i++) {
            if (!strcmp (elems[i], "xMinYMin"))
                ratio = RSVG_ASPECT_RATIO_XMIN_YMIN;
            else if (!strcmp (elems[i], "xMidYMin"))
                ratio = RSVG_ASPECT_RATIO_XMID_YMIN;
            else if (!strcmp (elems[i], "xMaxYMin"))
                ratio = RSVG_ASPECT_RATIO_XMAX_YMIN;
            else if (!strcmp (elems[i], "xMinYMid"))
                ratio = RSVG_ASPECT_RATIO_XMIN_YMID;
            else if (!strcmp (elems[i], "xMidYMid"))
                ratio = RSVG_ASPECT_RATIO_XMID_YMID;
            else if (!strcmp (elems[i], "xMaxYMid"))
                ratio = RSVG_ASPECT_RATIO_XMAX_YMID;
            else if (!strcmp (elems[i], "xMinYMax"))
                ratio = RSVG_ASPECT_RATIO_XMIN_YMAX;
            else if (!strcmp (elems[i], "xMidYMax"))
                ratio = RSVG_ASPECT_RATIO_XMID_YMAX;
            else if (!strcmp (elems[i], "xMaxYMax"))
                ratio = RSVG_ASPECT_RATIO_XMAX_YMAX;
            else if (!strcmp (elems[i], "slice"))
                ratio |= RSVG_ASPECT_RATIO_SLICE;
        }

        g_strfreev (elems);
    }

    return ratio;
}

static void
rsvg_xml_noerror (void *data, xmlErrorPtr error)
{
}

/* This is quite hacky and not entirely correct, but apparently
 * libxml2 has NO support for parsing pseudo attributes as defined
 * by the xml-styleheet spec.
 */
char **
rsvg_css_parse_xml_attribute_string (const char *attribute_string)
{
    xmlSAXHandler handler;
    xmlParserCtxtPtr parser;
    xmlDocPtr doc;
    xmlNodePtr node;
    xmlAttrPtr attr;
    char *tag;
    GPtrArray *attributes;
    char **retval = NULL;

    tag = g_strdup_printf ("<rsvg-hack %s />\n", attribute_string);

    memset (&handler, 0, sizeof (handler));
    xmlSAX2InitDefaultSAXHandler (&handler, 0);
    handler.serror = rsvg_xml_noerror;
    parser = xmlCreatePushParserCtxt (&handler, NULL, tag, strlen (tag) + 1, NULL);
    parser->options |= XML_PARSE_NONET;

    if (xmlParseDocument (parser) != 0)
        goto done;

    if ((doc = parser->myDoc) == NULL ||
        (node = doc->children) == NULL ||
        strcmp (node->name, "rsvg-hack") != 0 ||
        node->next != NULL ||
        node->properties == NULL)
          goto done;

    attributes = g_ptr_array_new ();
    for (attr = node->properties; attr; attr = attr->next) {
        xmlNodePtr content = attr->children;

        g_ptr_array_add (attributes, g_strdup ((char *) attr->name));
        if (content)
          g_ptr_array_add (attributes, g_strdup ((char *) content->content));
        else
          g_ptr_array_add (attributes, g_strdup (""));
    }

    g_ptr_array_add (attributes, NULL);
    retval = (char **) g_ptr_array_free (attributes, FALSE);

  done:
    if (parser->myDoc)
      xmlFreeDoc (parser->myDoc);
    xmlFreeParserCtxt (parser);
    g_free (tag);

    return retval;
}
