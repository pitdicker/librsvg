/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 sts=4 ts=4 expandtab: */
/*
   rsvg-parse-props.h: Parse SVG presentation properties

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
*/

#ifndef RSVG_PARSE_PROPS_H
#define RSVG_PARSE_PROPS_H

#include <cairo.h>
#include "rsvg.h"
#include "rsvg-styles.h"

G_BEGIN_DECLS

typedef enum {
    SVG_ATTRIBUTE,
    CSS_VALUE
} RsvgPropSrc;

typedef enum {
    RSVG_NUMBER_FORMAT_SVG,
    RSVG_NUMBER_FORMAT_CSS2,
    RSVG_NUMBER_FORMAT_PATHDATA
} RsvgNumberFormat;

/* functions for parsing basic data types */
G_GNUC_INTERNAL
double     _rsvg_parse_number (const char *str,
                               const char **end,
                               const RsvgNumberFormat format);
G_GNUC_INTERNAL
RsvgLength _rsvg_parse_length (const char *str,
                               const char **end,
                               const RsvgPropSrc prop_src);

/* functions for parsing properties that can only contain a basic data type */
gboolean _rsvg_parse_prop_length (const char *str, RsvgLength *result, const RsvgPropSrc prop_src);

/* TODO: this one should not be nessesary in a header */
G_GNUC_INTERNAL
RsvgPaintServer * rsvg_parse_paint_server (gboolean * inherit,
                                           const RsvgDefs * defs,
                                           const char *str,
                                           guint32 current_color);
G_GNUC_INTERNAL
void rsvg_parse_prop (RsvgHandle * ctx,
                      RsvgState * state,
                      const gchar * name,
                      const gchar * value,
                      gboolean important,
                      const RsvgPropSrc prop_src);

G_END_DECLS

#endif                          /* RSVG_PARSE_PROPS_H */
