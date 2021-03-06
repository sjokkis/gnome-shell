/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-table.h: Table layout widget
 *
 * Copyright 2008, 2009 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * Boston, MA 02111-1307, USA.
 *
 * Written by: Thomas Wood  <thomas@linux.intel.com>
 *
 */

#if !defined(ST_H_INSIDE) && !defined(ST_COMPILATION)
#error "Only <st/st.h> can be included directly.h"
#endif

#ifndef __ST_TABLE_H__
#define __ST_TABLE_H__

#include <st/st-types.h>
#include <st/st-container.h>

G_BEGIN_DECLS

/**
 * StTableChildOptions:
 * @ST_KEEP_ASPECT_RATIO: whether to respect the widget's aspect ratio
 * @ST_X_EXPAND: whether to allocate extra space on the widget's x-axis
 * @ST_Y_EXPAND: whether to allocate extra space on the widget's y-axis
 * @ST_X_FILL: whether to stretch the child to fill the cell horizontally
 * @ST_Y_FILL: whether to stretch the child to fill the cell vertically
 *
 * Denotes the child properties an StTable child will have.
 */
typedef enum
{
  ST_KEEP_ASPECT_RATIO = 1 << 0,
  ST_X_EXPAND          = 1 << 1,
  ST_Y_EXPAND          = 1 << 2,
  ST_X_FILL            = 1 << 3,
  ST_Y_FILL            = 1 << 4
} StTableChildOptions;

#define ST_TYPE_TABLE                (st_table_get_type ())
#define ST_TABLE(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), ST_TYPE_TABLE, StTable))
#define ST_IS_TABLE(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), ST_TYPE_TABLE))
#define ST_TABLE_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), ST_TYPE_TABLE, StTableClass))
#define ST_IS_TABLE_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), ST_TYPE_TABLE))
#define ST_TABLE_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), ST_TYPE_TABLE, StTableClass))

typedef struct _StTable              StTable;
typedef struct _StTablePrivate       StTablePrivate;
typedef struct _StTableClass         StTableClass;

/**
 * StTable:
 *
 * The contents of this structure is private and should only be accessed using
 * the provided API.
 */
struct _StTable
{
  /*< private >*/
  StContainer parent_instance;

  StTablePrivate *priv;
};

struct _StTableClass
{
  StContainerClass parent_class;
};

GType st_table_get_type (void) G_GNUC_CONST;

StWidget* st_table_new (void);

gint st_table_get_row_count    (StTable *table);
gint st_table_get_column_count (StTable *table);

G_END_DECLS

#endif /* __ST_TABLE_H__ */
