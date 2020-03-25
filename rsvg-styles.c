/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-styles.c: Handle SVG styles

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
#include "rsvg-styles.h"

#include <string.h>
#include <math.h>

#include "rsvg.h"
#include "rsvg-private.h"
#include "rsvg-filter.h"
#include "rsvg-css.h"
#include "rsvg-shapes.h"
#include "rsvg-mask.h"
#include "rsvg-marker.h"
#include "rsvg-parse-props.h"

#include <libcroco/libcroco.h>

StyleValueData *
style_value_data_new (const gchar *value, const gboolean important)
{
    StyleValueData *ret;

    ret = g_new (StyleValueData, 1);
    ret->value = g_strdup (value);
    ret->important = important;

    return ret;
}

static void
style_value_data_free (StyleValueData *value)
{
    if (!value)
        return;
    g_free (value->value);
    g_free (value);
}

void
rsvg_state_init (RsvgState *state)
{
    /* TODO: remove memset */
    memset (state, 0, sizeof (RsvgState));

    state->parent = NULL;
    cairo_matrix_init_identity (&state->affine);
    cairo_matrix_init_identity (&state->personal_affine);

    /* presentation attributes */
    state->clip_path         = NULL;
    state->clip_rule         = CAIRO_FILL_RULE_WINDING;
    state->color             = 0x000000; /* black */
    state->direction         = PANGO_DIRECTION_LTR;
    state->enable_background = RSVG_ENABLE_BACKGROUND_ACCUMULATE;
    state->fill              = rsvg_parse_paint_server (NULL, NULL, "#000", 0);
    state->fill_opacity      = 0xff;
    state->fill_rule         = CAIRO_FILL_RULE_WINDING;
    state->filter            = NULL;
    state->flood_color       = 0x000000; /* black */
    state->flood_opacity     = 0xff;
    state->font_family       = g_strdup (RSVG_DEFAULT_FONT);
    state->font_size         = (RsvgLength) {RSVG_DEFAULT_FONT_SIZE, RSVG_UNIT_PX};
    state->font_stretch      = PANGO_STRETCH_NORMAL;
    state->font_style        = PANGO_STYLE_NORMAL;
    state->font_variant      = PANGO_VARIANT_NORMAL;
    state->font_weight       = PANGO_WEIGHT_NORMAL;
    state->letter_spacing    = (RsvgLength) {0.0, RSVG_UNIT_PX};
    state->marker_start      = NULL;
    state->marker_mid        = NULL;
    state->marker_end        = NULL;
    state->mask              = NULL;
    state->opacity           = 0xff;
    state->overflow          = FALSE;
    state->shape_rendering   = SHAPE_RENDERING_AUTO;
    state->stop_color        = 0x000000; /* black */
    state->stop_opacity      = 0xff;
    state->stroke            = NULL;
    state->stroke_dasharray  = (RsvgLengthList) {0, NULL};
    state->stroke_dashoffset = (RsvgLength) {0.0, RSVG_UNIT_NUMBER};
    state->stroke_linecap    = CAIRO_LINE_CAP_BUTT;
    state->stroke_linejoin   = CAIRO_LINE_JOIN_MITER;
    state->stroke_miterlimit = 4.0;
    state->stroke_opacity    = 0xff;
    state->stroke_width      = (RsvgLength) {1.0, RSVG_UNIT_NUMBER};
    state->text_anchor       = TEXT_ANCHOR_START;
    state->text_decoration   = TEXT_DECORATION_NONE;
    state->text_rendering    = TEXT_RENDERING_AUTO;
    state->unicode_bidi      = UNICODE_BIDI_NORMAL;

    /* TODO */
    state->text_gravity      = PANGO_GRAVITY_SOUTH;
    state->visible           = TRUE;

    /* core xml attributes */
    state->lang              = NULL;
    state->space_preserve    = FALSE;

    /* svg 1.2 attribute */
    state->comp_op           = CAIRO_OPERATOR_OVER;

    /* ??? */
    state->cond_true         = TRUE;

    state->styles = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           g_free, (GDestroyNotify) style_value_data_free);
}

void
rsvg_state_reinit (RsvgState *state)
{
    RsvgState *parent = state->parent;
    rsvg_state_finalize (state);
    rsvg_state_init (state);
    state->parent = parent;
}

typedef int (*InheritanceFunction) (const int dst, const int src);

void
rsvg_state_clone (RsvgState *dst, const RsvgState *src)
{
    guint i;
    RsvgState *parent = dst->parent;

    rsvg_state_finalize (dst);

    /* copy properties */
    *dst = *src;

    /* keep old parent */
    dst->parent = parent;

    /* duplicate pointers / increase refs */
    dst->font_family = g_strdup (src->font_family);
    dst->lang = g_strdup (src->lang);
    rsvg_paint_server_ref (dst->fill);
    rsvg_paint_server_ref (dst->stroke);

    if (src->stroke_dasharray.items != NULL) {
        dst->stroke_dasharray.items = g_new (RsvgLength, src->stroke_dasharray.number_of_items);
        for (i = 0; i < src->stroke_dasharray.number_of_items; i++)
            dst->stroke_dasharray.items[i] = src->stroke_dasharray.items[i];
    }

    dst->styles = g_hash_table_ref (src->styles);
}

/*
  This function is where all inheritance takes place. It is given a
  base and a modifier state, as well as a function to determine
  how the base is modified and a flag as to whether things that can
  not be inherited are copied straight over, or ignored.
*/

static void
rsvg_state_inherit_run (RsvgState *dst, const RsvgState *src,
                        const InheritanceFunction function, const gboolean inherituninheritables)
{
    guint i;

    if (function (dst->has_current_color, src->has_current_color))
        dst->color = src->color;
    if (function (dst->has_flood_color, src->has_flood_color))
        dst->flood_color = src->flood_color;
    if (function (dst->has_flood_opacity, src->has_flood_opacity))
        dst->flood_opacity = src->flood_opacity;
    if (function (dst->has_fill_server, src->has_fill_server)) {
        rsvg_paint_server_ref (src->fill);
        if (dst->fill)
            rsvg_paint_server_unref (dst->fill);
        dst->fill = src->fill;
    }
    if (function (dst->has_fill_opacity, src->has_fill_opacity))
        dst->fill_opacity = src->fill_opacity;
    if (function (dst->has_fill_rule, src->has_fill_rule))
        dst->fill_rule = src->fill_rule;
    if (function (dst->has_clip_rule, src->has_clip_rule))
        dst->clip_rule = src->clip_rule;
    if (function (dst->overflow, src->overflow))
        dst->overflow = src->overflow;
    if (function (dst->has_stroke_server, src->has_stroke_server)) {
        rsvg_paint_server_ref (src->stroke);
        if (dst->stroke)
            rsvg_paint_server_unref (dst->stroke);
        dst->stroke = src->stroke;
    }
    if (function (dst->has_stroke_opacity, src->has_stroke_opacity))
        dst->stroke_opacity = src->stroke_opacity;
    if (function (dst->has_stroke_width, src->has_stroke_width))
        dst->stroke_width = src->stroke_width;
    if (function (dst->has_miter_limit, src->has_miter_limit))
        dst->stroke_miterlimit = src->stroke_miterlimit;
    if (function (dst->has_cap, src->has_cap))
        dst->stroke_linecap = src->stroke_linecap;
    if (function (dst->has_join, src->has_join))
        dst->stroke_linejoin = src->stroke_linejoin;
    if (function (dst->has_stop_color, src->has_stop_color))
        dst->stop_color = src->stop_color;
    if (function (dst->has_stop_opacity, src->has_stop_opacity))
        dst->stop_opacity = src->stop_opacity;
    if (function (dst->has_cond, src->has_cond))
        dst->cond_true = src->cond_true;
    if (function (dst->has_font_size, src->has_font_size))
        dst->font_size = src->font_size;
    if (function (dst->has_font_style, src->has_font_style))
        dst->font_style = src->font_style;
    if (function (dst->has_font_variant, src->has_font_variant))
        dst->font_variant = src->font_variant;
    if (function (dst->has_font_weight, src->has_font_weight))
        dst->font_weight = src->font_weight;
    if (function (dst->has_font_stretch, src->has_font_stretch))
        dst->font_stretch = src->font_stretch;
    if (function (dst->has_font_decor, src->has_font_decor))
        dst->text_decoration = src->text_decoration;
    if (function (dst->has_text_dir, src->has_text_dir))
        dst->direction = src->direction;
    if (function (dst->has_text_gravity, src->has_text_gravity))
        dst->text_gravity = src->text_gravity;
    if (function (dst->has_unicode_bidi, src->has_unicode_bidi))
        dst->unicode_bidi = src->unicode_bidi;
    if (function (dst->has_text_anchor, src->has_text_anchor))
        dst->text_anchor = src->text_anchor;
    if (function (dst->has_letter_spacing, src->has_letter_spacing))
        dst->letter_spacing = src->letter_spacing;
    if (function (dst->has_startMarker, src->has_startMarker))
        dst->marker_start = src->marker_start;
    if (function (dst->has_middleMarker, src->has_middleMarker))
        dst->marker_mid = src->marker_mid;
    if (function (dst->has_endMarker, src->has_endMarker))
        dst->marker_end = src->marker_end;
    if (function (dst->has_shape_rendering_type, src->has_shape_rendering_type))
        dst->shape_rendering = src->shape_rendering;
    if (function (dst->has_text_rendering_type, src->has_text_rendering_type))
        dst->text_rendering = src->text_rendering;

    if (function (dst->has_font_family, src->has_font_family)) {
        g_free (dst->font_family);      /* font_family is always set to something */
        dst->font_family = g_strdup (src->font_family);
    }

    if (function (dst->has_space_preserve, src->has_space_preserve))
        dst->space_preserve = src->space_preserve;

    if (function (dst->has_visible, src->has_visible))
        dst->visible = src->visible;

    if (function (dst->has_lang, src->has_lang)) {
        if (dst->has_lang)
            g_free (dst->lang);
        dst->lang = g_strdup (src->lang);
    }

    if ((function (dst->has_dash, src->has_dash))) {
        if (dst->stroke_dasharray.items != NULL)
            g_free (dst->stroke_dasharray.items);

        dst->stroke_dasharray.number_of_items = src->stroke_dasharray.number_of_items;
        dst->stroke_dasharray.items = g_new (RsvgLength, src->stroke_dasharray.number_of_items);
        for (i = 0; i < src->stroke_dasharray.number_of_items; i++)
            dst->stroke_dasharray.items[i] = src->stroke_dasharray.items[i];
    }

    if (function (dst->has_dashoffset, src->has_dashoffset)) {
        dst->stroke_dashoffset = src->stroke_dashoffset;
    }

    if (inherituninheritables) {
        dst->clip_path = src->clip_path;
        dst->mask = src->mask;
        dst->enable_background = src->enable_background;
        dst->opacity = src->opacity;
        dst->filter = src->filter;
        dst->comp_op = src->comp_op;
    }
}

/*
  reinherit is given dst which is the top of the state stack
  and src which is the layer before in the state stack from
  which it should be inherited from
*/

static int
reinheritfunction (const int dst, const int src)
{
    if (!dst)
        return 1;
    return 0;
}

void
rsvg_state_reinherit (RsvgState *dst, const RsvgState *src)
{
    rsvg_state_inherit_run (dst, src, reinheritfunction, 0);
}

/*
  dominate is given dst which is the top of the state stack
  and src which is the layer before in the state stack from
  which it should be inherited from, however if anything is
  directly specified in src (the second last layer) it will
  override anything on the top layer, this is for overrides
  in use tags
*/

static int
dominatefunction (const int dst, const int src)
{
    if (!dst || src)
        return 1;
    return 0;
}

void
rsvg_state_dominate (RsvgState *dst, const RsvgState *src)
{
    rsvg_state_inherit_run (dst, src, dominatefunction, 0);
}

/* copy everything inheritable from the src to the dst */

static int
clonefunction (const int dst, const int src)
{
    return 1;
}

void
rsvg_state_override (RsvgState *dst, const RsvgState *src)
{
    rsvg_state_inherit_run (dst, src, clonefunction, 0);
}

/*
  put something new on the inheritance stack, dst is the top of the stack,
  src is the state to be integrated, this is essentially the opposite of
  reinherit, because it is being given stuff to be integrated on the top,
  rather than the context underneath.
*/

static int
inheritfunction (const int dst, const int src)
{
    return src;
}

void
rsvg_state_inherit (RsvgState *dst, const RsvgState *src)
{
    rsvg_state_inherit_run (dst, src, inheritfunction, 1);
}

void
rsvg_state_finalize (const RsvgState *state)
{
    g_free (state->font_family);
    g_free (state->lang);
    rsvg_paint_server_unref (state->fill);
    rsvg_paint_server_unref (state->stroke);
    g_free (state->stroke_dasharray.items);
    g_hash_table_unref (state->styles);
}

static void
rsvg_lookup_parse_presentation_attr (const RsvgHandle *ctx, RsvgState *state,
                                     const char *key, const RsvgPropertyBag *atts)
{
    const char *value;

    if ((value = rsvg_property_bag_lookup (atts, key)) != NULL)
        rsvg_parse_prop (ctx, state, key, value, FALSE, SVG_ATTRIBUTE);
}

/* take a pair of the form (fill="#ff00ff") and parse it as a style */
void
rsvg_parse_presentation_attr (const RsvgHandle * ctx,
                              RsvgState * state,
                              const RsvgPropertyBag * atts)
{
    rsvg_lookup_parse_presentation_attr (ctx, state, "clip-path", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "clip-rule", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "color", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "direction", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "display", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "enable-background", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "comp-op", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "fill", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "fill-opacity", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "fill-rule", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "filter", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "flood-color", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "flood-opacity", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "font-family", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "font-size", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "font-stretch", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "font-style", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "font-variant", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "font-weight", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "marker-end", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "mask", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "marker-mid", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "marker-start", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "opacity", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "overflow", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "shape-rendering", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stop-color", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stop-opacity", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke-dasharray", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke-dashoffset", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke_linecap", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke_linejoin", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke-miterlimit", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke-opacity", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "stroke-width", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "text-anchor", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "text-decoration", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "unicode-bidi", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "letter-spacing", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "visibility", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "writing-mode", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "xml:lang", atts);
    rsvg_lookup_parse_presentation_attr (ctx, state, "xml:space", atts);

    {
        /* TODO: this conditional behavior isn't quite correct, and i'm not sure it should reside here */
        gboolean cond_true, has_cond;

        cond_true = rsvg_eval_switch_attributes (atts, &has_cond);

        if (has_cond) {
            state->cond_true = cond_true;
            state->has_cond = TRUE;
        }
    }
}

static gboolean
parse_style_value (const gchar *string, gchar **value, gboolean *important)
{
    gchar **strings;

    strings = g_strsplit (string, "!", 2);

    if (strings == NULL || strings[0] == NULL) {
        g_strfreev (strings);
        return FALSE;
    }

    if (strings[1] != NULL && strings[2] == NULL &&
        g_str_equal (g_strstrip (strings[1]), "important")) {
        *important = TRUE;
    } else {
        *important = FALSE;
    }

    *value = g_strdup (g_strstrip (strings[0]));

    g_strfreev (strings);

    return TRUE;
}

/* Split a CSS2 style into individual style arguments, setting attributes
   in the SVG context.

   It's known that this is _way_ out of spec. A more complete CSS2
   implementation will happen later.
*/
void
rsvg_parse_style (const RsvgHandle *ctx, RsvgState *state, const char *str)
{
    gchar **styles;
    guint i;

    styles = g_strsplit (str, ";", -1);
    for (i = 0; i < g_strv_length (styles); i++) {
        gchar **values;
        values = g_strsplit (styles[i], ":", 2);
        if (!values)
            continue;

        if (g_strv_length (values) == 2) {
            gboolean important;
            gchar *style_value = NULL;
            if (parse_style_value (values[1], &style_value, &important))
                rsvg_parse_prop (ctx, state, g_strstrip (values[0]),
                                 style_value, important, CSS_VALUE);
            g_free (style_value);
        }
        g_strfreev (values);
    }
    g_strfreev (styles);
}

static void
rsvg_css_define_style (RsvgHandle *ctx,
                       const gchar *selector,
                       const gchar *style_name,
                       const gchar *style_value,
                       const gboolean important)
{
    GHashTable *styles;
    gboolean need_insert = FALSE;

    /* push name/style pair into HT */
    styles = g_hash_table_lookup (ctx->priv->css_props, selector);
    if (styles == NULL) {
        styles = g_hash_table_new_full (g_str_hash, g_str_equal,
                                        g_free, (GDestroyNotify) style_value_data_free);
        g_hash_table_insert (ctx->priv->css_props, (gpointer) g_strdup (selector), styles);
        need_insert = TRUE;
    } else {
        StyleValueData *current_value;
        current_value = g_hash_table_lookup (styles, style_name);
        if (current_value == NULL || !current_value->important)
            need_insert = TRUE;
    }
    if (need_insert) {
        g_hash_table_insert (styles,
                             (gpointer) g_strdup (style_name),
                             (gpointer) style_value_data_new (style_value, important));
    }
}

typedef struct _CSSUserData {
    RsvgHandle *ctx;
    CRSelector *selector;
} CSSUserData;

static void
css_user_data_init (CSSUserData *user_data, RsvgHandle *ctx)
{
    user_data->ctx = ctx;
    user_data->selector = NULL;
}

static void
ccss_start_selector (CRDocHandler *a_handler, CRSelector *a_selector_list)
{
    CSSUserData *user_data;

    g_return_if_fail (a_handler);

    user_data = (CSSUserData *) a_handler->app_data;
    cr_selector_ref (a_selector_list);
    user_data->selector = a_selector_list;
}

static void
ccss_end_selector (CRDocHandler *a_handler, CRSelector *a_selector_list)
{
    CSSUserData *user_data;

    g_return_if_fail (a_handler);

    user_data = (CSSUserData *) a_handler->app_data;

    cr_selector_unref (user_data->selector);
    user_data->selector = NULL;
}

static void
ccss_property (CRDocHandler * a_handler, CRString * a_name, CRTerm * a_expr, gboolean a_important)
{
    CSSUserData *user_data;
    gchar *name = NULL;
    size_t len = 0;

    g_return_if_fail (a_handler);

    user_data = (CSSUserData *) a_handler->app_data;

    if (a_name && a_expr && user_data->selector) {
        CRSelector *cur;
        for (cur = user_data->selector; cur; cur = cur->next) {
            if (cur->simple_sel) {
                gchar *selector = (gchar *) cr_simple_sel_to_string (cur->simple_sel);
                if (selector) {
                    gchar *style_name, *style_value;
                    name = (gchar *) cr_string_peek_raw_str (a_name);
                    len = cr_string_peek_raw_str_len (a_name);
                    style_name = g_strndup (name, len);
                    style_value = (gchar *)cr_term_to_string (a_expr);
                    rsvg_css_define_style (user_data->ctx,
                                           selector,
                                           style_name,
                                           style_value,
                                           a_important);
                    g_free (selector);
                    g_free (style_name);
                    g_free (style_value);
                }
            }
        }
    }
}

static void
ccss_error (CRDocHandler * a_handler)
{
    /* yup, like i care about CSS parsing errors ;-)
       ignore, chug along */
    g_warning (_("CSS parsing error\n"));
}

static void
ccss_unrecoverable_error (CRDocHandler * a_handler)
{
    /* yup, like i care about CSS parsing errors ;-)
       ignore, chug along */
    g_warning (_("CSS unrecoverable error\n"));
}

static void
 ccss_import_style (CRDocHandler * a_this,
                    GList * a_media_list,
                    CRString * a_uri, CRString * a_uri_default_ns, CRParsingLocation * a_location);

static void
init_sac_handler (CRDocHandler * a_handler)
{
    a_handler->start_document = NULL;
    a_handler->end_document = NULL;
    a_handler->import_style = ccss_import_style;
    a_handler->namespace_declaration = NULL;
    a_handler->comment = NULL;
    a_handler->start_selector = ccss_start_selector;
    a_handler->end_selector = ccss_end_selector;
    a_handler->property = ccss_property;
    a_handler->start_font_face = NULL;
    a_handler->end_font_face = NULL;
    a_handler->start_media = NULL;
    a_handler->end_media = NULL;
    a_handler->start_page = NULL;
    a_handler->end_page = NULL;
    a_handler->ignorable_at_rule = NULL;
    a_handler->error = ccss_error;
    a_handler->unrecoverable_error = ccss_unrecoverable_error;
}

void
rsvg_parse_cssbuffer (RsvgHandle *ctx, const char *buff, size_t buflen)
{
    CRParser *parser = NULL;
    CRDocHandler *css_handler = NULL;
    CSSUserData user_data;

    if (buff == NULL || buflen == 0)
        return;

    css_handler = cr_doc_handler_new ();
    init_sac_handler (css_handler);

    css_user_data_init (&user_data, ctx);
    css_handler->app_data = &user_data;

    /* TODO: fix libcroco to take in const strings */
    parser = cr_parser_new_from_buf ((guchar *) buff, (gulong) buflen, CR_UTF_8, FALSE);
    if (parser == NULL) {
        cr_doc_handler_unref (css_handler);
        return;
    }

    cr_parser_set_sac_handler (parser, css_handler);
    cr_doc_handler_unref (css_handler);

    cr_parser_set_use_core_grammar (parser, FALSE);
    cr_parser_parse (parser);

    cr_parser_destroy (parser);
}

static void
ccss_import_style (CRDocHandler * a_this,
                   GList * a_media_list,
                   CRString * a_uri, CRString * a_uri_default_ns, CRParsingLocation * a_location)
{
    CSSUserData *user_data = (CSSUserData *) a_this->app_data;
    guint8 *stylesheet_data;
    gsize stylesheet_data_len;
    char *mime_type = NULL;

    if (a_uri == NULL)
        return;

    stylesheet_data = _rsvg_handle_acquire_data (user_data->ctx,
                                                 (gchar *) cr_string_peek_raw_str (a_uri),
                                                 &mime_type,
                                                 &stylesheet_data_len,
                                                 NULL);
    if (stylesheet_data == NULL ||
        mime_type == NULL ||
        strcmp (mime_type, "text/css") != 0) {
        g_free (stylesheet_data);
        g_free (mime_type);
        return;
    }

    rsvg_parse_cssbuffer (user_data->ctx, (const char *) stylesheet_data,
                          stylesheet_data_len);
    g_free (stylesheet_data);
    g_free (mime_type);
}

/* Parse an SVG transform string into an affine matrix. Reference: SVG 1.1
   (Second Edition), section 7.6. Returns TRUE on success. */
gboolean
rsvg_parse_transform (cairo_matrix_t *dst, const char *src)
{
    double args[6];
    int n_args;
    cairo_matrix_t affine;
    gboolean expect_next;

    enum {
        MATRIX,
        TRANSLATE,
        SCALE,
        ROTATE,
        SKEWX,
        SKEWY
    } keyword;
    const int max_args[6] = {6, 2, 2, 3, 1, 1};

    cairo_matrix_init_identity (dst);

    while (*src) {
        /* skip initial whitespace */
        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
            src++;

        /* parse keyword */
        if (!strncmp (src, "matrix", 6)) {
            keyword = MATRIX;
            src += 6;
        } else if (!strncmp (src, "translate", 9)) {
            keyword = TRANSLATE;
            src += 9;
        } else if (!strncmp (src, "scale", 5)) {
            keyword = SCALE;
            src += 5;
        } else if (!strncmp (src, "rotate", 6)) {
            keyword = ROTATE;
            src += 6;
        } else if (!strncmp (src, "skewX", 5)) {
            keyword = SKEWX;
            src += 5;
        } else if (!strncmp (src, "skewY", 5)) {
            keyword = SKEWY;
            src += 5;
        } else {
            goto invalid_transform;
        }

        /* skip whitespace */
        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
            src++;

        if (*src != '(')
            goto invalid_transform;
        src++;

        n_args = 0;
        do {
            if (n_args == max_args[keyword])
                goto invalid_transform;

            /* skip whitespace */
            while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
                src++;

            /* parse number */
            switch (*src) {
            case '.':
                /* '.' must be followed by a number */
                if (!(src[1] >= 0 && src[1] <= '9'))
                    goto invalid_transform;
                /* fallthrough */
            case '+': case '-':
                /* '+' or '-' must be followed by a digit,
                   or by a '.' that is followed by a digit */
                if (!((src[1] >= 0 && src[1] <= '9') ||
                      (src[1] == '.' && !(src[2] >= 0 && src[2] <= '9'))))
                    goto invalid_transform;
                /* fallthrough */
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                args[n_args] = g_ascii_strtod(src, (gchar **) &src);
                /* strtod also parses infinity and nan, which are not valid */
                if (!isfinite (args[n_args]))
                    goto invalid_transform;
                n_args++;
                break;
            default:
                goto invalid_transform;
            }

            /* skip whitespace */
            while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
                src++;

            /* skip optional comma */
            expect_next = FALSE;
            if (*src == ',') {
                src++;
                expect_next = TRUE;
            }
        } while (*src != ')');
        src++;

        if (expect_next)
            goto invalid_transform;

        /* ok, have parsed keyword and args, now modify the transform */
        switch (keyword) {
        case MATRIX:
            if (n_args != 6)
                goto invalid_transform;
            cairo_matrix_init (&affine, args[0], args[1], args[2], args[3], args[4], args[5]);
            cairo_matrix_multiply (dst, &affine, dst);
            break;
        case TRANSLATE:
            if (n_args == 1)
                args[1] = 0.;
            else if (n_args != 2)
                goto invalid_transform;
            cairo_matrix_init_translate (&affine, args[0], args[1]);
            cairo_matrix_multiply (dst, &affine, dst);
            break;
        case SCALE:
            if (n_args == 1)
                args[1] = args[0];
            else if (n_args != 2)
                goto invalid_transform;
            cairo_matrix_init_scale (&affine, args[0], args[1]);
            cairo_matrix_multiply (dst, &affine, dst);
            break;
        case ROTATE:
            if (n_args == 1) {
                cairo_matrix_init_rotate (&affine, args[0] * M_PI / 180.);
                cairo_matrix_multiply (dst, &affine, dst);
            } else if (n_args == 3) {
                cairo_matrix_init_translate (&affine, args[1], args[2]);
                cairo_matrix_multiply (dst, &affine, dst);

                cairo_matrix_init_rotate (&affine, args[0] * M_PI / 180.);
                cairo_matrix_multiply (dst, &affine, dst);

                cairo_matrix_init_translate (&affine, -args[1], -args[2]);
                cairo_matrix_multiply (dst, &affine, dst);
            } else {
                goto invalid_transform;
            }
            break;
        case SKEWX:
            if (n_args != 1)
                goto invalid_transform;
            cairo_matrix_init (&affine, 1., 0., tan (args[0] * M_PI / 180.0), 1., 0., 0.);
            cairo_matrix_multiply (dst, &affine, dst);
            break;
        case SKEWY:
            if (n_args != 1)
                goto invalid_transform;
            cairo_matrix_init (&affine, 1., tan (args[0] * M_PI / 180.0), 0., 1., 0., 0.);
            cairo_matrix_multiply (dst, &affine, dst);
            break;
        }

        /* skip whitespace */
        while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
            src++;

        /* skip optional comma */
        expect_next = FALSE;
        if (*src == ',') {
            src++;
            expect_next = TRUE;
        }
    }

    if (expect_next)
        goto invalid_transform;

    return TRUE;

invalid_transform:
    cairo_matrix_init_identity (dst);
    return FALSE;
}

/**
 * rsvg_parse_transform_attr:
 * @ctx: Rsvg context.
 * @state: State in which to apply the transform.
 * @str: String containing transform.
 *
 * Parses the transform attribute in @str and applies it to @state.
 **/
static void
rsvg_parse_transform_attr (RsvgState * state, const char *str)
{
    cairo_matrix_t affine;

    if (rsvg_parse_transform (&affine, str)) {
        cairo_matrix_multiply (&state->personal_affine, &affine, &state->personal_affine);
        cairo_matrix_multiply (&state->affine, &affine, &state->affine);
    }
}

typedef struct _StylesData {
    const RsvgHandle *ctx;
    RsvgState *state;
} StylesData;

static void
rsvg_apply_css_props (const gchar *key, StyleValueData *value, gpointer user_data)
{
    StylesData *data = (StylesData *) user_data;
    rsvg_parse_prop (data->ctx, data->state, key, value->value, value->important, CSS_VALUE);
}

static gboolean
rsvg_lookup_apply_css_style (const RsvgHandle * ctx, const char *target, RsvgState * state)
{
    GHashTable *styles;

    styles = g_hash_table_lookup (ctx->priv->css_props, target);

    if (styles != NULL) {
        StylesData *data = g_new (StylesData, 1);
        data->ctx = ctx;
        data->state = state;
        g_hash_table_foreach (styles, (GHFunc) rsvg_apply_css_props, data);
        g_free (data);
        return TRUE;
    }
    return FALSE;
}

/**
 * rsvg_set_presentation_props:
 * @ctx: Rsvg context.
 * @state: Rsvg state
 * @tag: The SVG tag we're processing (eg: circle, ellipse), optionally %NULL
 * @klazz: The space delimited class list, optionally %NULL
 * @atts: Attributes in SAX style.
 *
 * Parses style and transform attributes and modifies state at top of
 * stack.
 **/
void
rsvg_set_presentation_props (const RsvgHandle *ctx, RsvgState *state,
                             const char *tag, const char *klazz, const char *id,
                             const RsvgPropertyBag *atts)
{
    int i = 0, j = 0;
    char *target = NULL;
    gboolean found = FALSE;
    GString *klazz_list = NULL;

    if (rsvg_property_bag_size (atts) > 0)
        rsvg_parse_presentation_attr (ctx, state, atts);

    /* Try to properly support all of the following, including inheritance:
     * *
     * #id
     * tag
     * tag#id
     * tag.class
     * tag.class#id
     *
     * This is basically a semi-compliant CSS2 selection engine
     */

    /* * */
    rsvg_lookup_apply_css_style (ctx, "*", state);

    /* tag */
    if (tag != NULL) {
        rsvg_lookup_apply_css_style (ctx, tag, state);
    }

    if (klazz != NULL) {
        i = strlen (klazz);
        while (j < i) {
            found = FALSE;
            klazz_list = g_string_new (".");

            while (j < i && g_ascii_isspace (klazz[j]))
                j++;

            while (j < i && !g_ascii_isspace (klazz[j]))
                g_string_append_c (klazz_list, klazz[j++]);

            /* tag.class#id */
            if (tag != NULL && klazz_list->len != 1 && id != NULL) {
                target = g_strdup_printf ("%s%s#%s", tag, klazz_list->str, id);
                found = found || rsvg_lookup_apply_css_style (ctx, target, state);
                g_free (target);
            }

            /* class#id */
            if (klazz_list->len != 1 && id != NULL) {
                target = g_strdup_printf ("%s#%s", klazz_list->str, id);
                found = found || rsvg_lookup_apply_css_style (ctx, target, state);
                g_free (target);
            }

            /* tag.class */
            if (tag != NULL && klazz_list->len != 1) {
                target = g_strdup_printf ("%s%s", tag, klazz_list->str);
                found = found || rsvg_lookup_apply_css_style (ctx, target, state);
                g_free (target);
            }

            /* didn't find anything more specific, just apply the class style */
            if (!found) {
                found = found || rsvg_lookup_apply_css_style (ctx, klazz_list->str, state);
            }
            g_string_free (klazz_list, TRUE);
        }
    }

    /* #id */
    if (id != NULL) {
        target = g_strdup_printf ("#%s", id);
        rsvg_lookup_apply_css_style (ctx, target, state);
        g_free (target);
    }

    /* tag#id */
    if (tag != NULL && id != NULL) {
        target = g_strdup_printf ("%s#%s", tag, id);
        rsvg_lookup_apply_css_style (ctx, target, state);
        g_free (target);
    }

    if (rsvg_property_bag_size (atts) > 0) {
        const char *value;

        if ((value = rsvg_property_bag_lookup (atts, "style")) != NULL)
            rsvg_parse_style (ctx, state, value);
        if ((value = rsvg_property_bag_lookup (atts, "transform")) != NULL)
            rsvg_parse_transform_attr (state, value);
    }
}

RsvgState *
rsvg_current_state (const RsvgDrawingCtx *ctx)
{
    return ctx->state;
}

RsvgState *
rsvg_state_parent (const RsvgState *state)
{
    return state->parent;
}

void
rsvg_state_free_all (RsvgState *state)
{
    while (state) {
        RsvgState *parent = state->parent;
        rsvg_state_finalize (state);
        g_slice_free (RsvgState, state);
        state = parent;
    }
}

/**
 * rsvg_property_bag_new:
 * @atts:
 *
 * The property bag will NOT copy the attributes and values. If you need
 * to store them for later, use rsvg_property_bag_dup().
 *
 * Returns: (transfer full): a new property bag
 */
RsvgPropertyBag *
rsvg_property_bag_new (const char **atts)
{
    RsvgPropertyBag *bag;
    int i;

    bag = g_hash_table_new (g_str_hash, g_str_equal);

    if (atts != NULL) {
        for (i = 0; atts[i] != NULL; i += 2)
            g_hash_table_insert (bag, (gpointer) atts[i], (gpointer) atts[i + 1]);
    }

    return bag;
}

/**
 * rsvg_property_bag_dup:
 * @bag:
 *
 * Returns a copy of @bag that owns the attributes and values.
 *
 * Returns: (transfer full): a new property bag
 */
RsvgPropertyBag *
rsvg_property_bag_dup (RsvgPropertyBag * bag)
{
    RsvgPropertyBag *dup;
    GHashTableIter iter;
    gpointer key, value;

    dup = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    g_hash_table_iter_init (&iter, bag);
    while (g_hash_table_iter_next (&iter, &key, &value))
      g_hash_table_insert (dup,
                           (gpointer) g_strdup ((char *) key),
                           (gpointer) g_strdup ((char *) value));

    return dup;
}

void
rsvg_property_bag_free (RsvgPropertyBag * bag)
{
    g_hash_table_unref (bag);
}

const char *
rsvg_property_bag_lookup (const RsvgPropertyBag *bag, const char *key)
{
    return (const char *) g_hash_table_lookup ((RsvgPropertyBag *) bag, (gconstpointer) key);
}

guint
rsvg_property_bag_size (const RsvgPropertyBag * bag)
{
    return g_hash_table_size ((RsvgPropertyBag *) bag);
}

void
rsvg_property_bag_enumerate (RsvgPropertyBag * bag, RsvgPropertyBagEnumFunc func,
                             gpointer user_data)
{
    g_hash_table_foreach (bag, (GHFunc) func, user_data);
}

void
rsvg_state_push (RsvgDrawingCtx * ctx)
{
    RsvgState *data;
    RsvgState *baseon;

    baseon = ctx->state;
    data = g_slice_new (RsvgState);
    rsvg_state_init (data);

    if (baseon) {
        rsvg_state_reinherit (data, baseon);
        data->affine = baseon->affine;
        data->parent = baseon;
    }

    ctx->state = data;
}

void
rsvg_state_pop (RsvgDrawingCtx * ctx)
{
    RsvgState *dead_state = ctx->state;
    ctx->state = dead_state->parent;
    rsvg_state_finalize (dead_state);
    g_slice_free (RsvgState, dead_state);
}

/*
  A function for modifying the top of the state stack depending on a
  flag given. If that flag is 0, style and transform will inherit
  normally. If that flag is 1, style will inherit normally with the
  exception that any value explicity set on the second last level
  will have a higher precedence than values set on the last level.
  If the flag equals two then the style will be overridden totally
  however the transform will be left as is. This is because of
  patterns which are not based on the context of their use and are
  rather based wholly on their own loading context. Other things
  may want to have this totally disabled, and a value of three will
  achieve this.
*/

void
rsvg_state_reinherit_top (const RsvgDrawingCtx * ctx, RsvgState * state, const int dominate)
{
    RsvgState *current;

    if (dominate == 3)
        return;

    current = rsvg_current_state (ctx);
    /* This is a special domination mode for patterns, the transform
       is simply left as is, wheras the style is totally overridden */
    if (dominate == 2) {
        rsvg_state_override (current, state);
    } else {
        RsvgState *parent= rsvg_state_parent (current);
        rsvg_state_clone (current, state);
        if (parent) {
            if (dominate)
                rsvg_state_dominate (current, parent);
            else
                rsvg_state_reinherit (current, parent);
            cairo_matrix_multiply (&current->affine,
                                   &current->affine,
                                   &parent->affine);
        }
    }
}

void
rsvg_state_reconstruct (RsvgState * state, RsvgNode * current)
{
    if (current == NULL)
        return;
    rsvg_state_reconstruct (state, current->parent);
    rsvg_state_inherit (state, current->state);
}
