/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
#ifndef __ST_THEME_PRIVATE_H__
#define __ST_THEME_PRIVATE_H__

#include <libcroco/libcroco.h>
#include "st-theme.h"

G_BEGIN_DECLS

GPtrArray *_st_theme_get_matched_properties (StTheme       *theme,
                                             StThemeNode   *node);

/* Resolve an URL from the stylesheet to a filename */
char *_st_theme_resolve_url (StTheme      *theme,
                             CRStyleSheet *base_stylesheet,
                             const char   *url);

CRDeclaration *_st_theme_parse_declaration_list (const char *str);

G_END_DECLS

#endif /* __ST_THEME_PRIVATE_H__ */
