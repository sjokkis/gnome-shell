/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * st-widget.c: Base class for St actors
 *
 * Copyright 2007 OpenedHand
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
 * Written by: Emmanuele Bassi <ebassi@openedhand.com>
 *             Thomas Wood <thomas@linux.intel.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <clutter/clutter.h>

#include "st-widget.h"

#include "st-marshal.h"
#include "st-private.h"
#include "st-texture-cache.h"
#include "st-theme-context.h"
#include "st-tooltip.h"

/*
 * Forward declaration for sake of StWidgetChild
 */
struct _StWidgetPrivate
{
  StTheme      *theme;
  StThemeNode  *theme_node;
  gchar        *pseudo_class;
  gchar        *style_class;
  gchar        *inline_style;

  gboolean      is_stylable : 1;
  gboolean      has_tooltip : 1;
  gboolean      is_style_dirty : 1;
  gboolean      draw_bg_color : 1;
  gboolean      draw_border_internal : 1;
  gboolean      track_hover : 1;
  gboolean      hover : 1;

  StTooltip    *tooltip;

  StTextDirection   direction;
};

/**
 * SECTION:st-widget
 * @short_description: Base class for stylable actors
 *
 * #StWidget is a simple abstract class on top of #ClutterActor. It
 * provides basic themeing properties.
 *
 * Actors in the St library should subclass #StWidget if they plan
 * to obey to a certain #StStyle.
 */

enum
{
  PROP_0,

  PROP_THEME,
  PROP_PSEUDO_CLASS,
  PROP_STYLE_CLASS,
  PROP_STYLE,
  PROP_STYLABLE,
  PROP_HAS_TOOLTIP,
  PROP_TOOLTIP_TEXT,
  PROP_TRACK_HOVER,
  PROP_HOVER
};

enum
{
  STYLE_CHANGED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_ABSTRACT_TYPE (StWidget, st_widget, CLUTTER_TYPE_ACTOR);

#define ST_WIDGET_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), ST_TYPE_WIDGET, StWidgetPrivate))

static void st_widget_recompute_style (StWidget    *widget,
                                       StThemeNode *old_theme_node);

static void
st_widget_set_property (GObject      *gobject,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  StWidget *actor = ST_WIDGET (gobject);

  switch (prop_id)
    {
    case PROP_THEME:
      st_widget_set_theme (actor, g_value_get_object (value));
      break;

    case PROP_PSEUDO_CLASS:
      st_widget_set_style_pseudo_class (actor, g_value_get_string (value));
      break;

    case PROP_STYLE_CLASS:
      st_widget_set_style_class_name (actor, g_value_get_string (value));
      break;

    case PROP_STYLE:
      st_widget_set_style (actor, g_value_get_string (value));
      break;

    case PROP_STYLABLE:
      if (actor->priv->is_stylable != g_value_get_boolean (value))
        {
          actor->priv->is_stylable = g_value_get_boolean (value);
          clutter_actor_queue_relayout ((ClutterActor *) gobject);
        }
      break;

    case PROP_HAS_TOOLTIP:
      st_widget_set_has_tooltip (actor, g_value_get_boolean (value));
      break;

    case PROP_TOOLTIP_TEXT:
      st_widget_set_tooltip_text (actor, g_value_get_string (value));
      break;

    case PROP_TRACK_HOVER:
      st_widget_set_track_hover (actor, g_value_get_boolean (value));
      break;

    case PROP_HOVER:
      st_widget_set_hover (actor, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_widget_get_property (GObject    *gobject,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  StWidget *actor = ST_WIDGET (gobject);
  StWidgetPrivate *priv = actor->priv;

  switch (prop_id)
    {
    case PROP_THEME:
      g_value_set_object (value, priv->theme);
      break;

    case PROP_PSEUDO_CLASS:
      g_value_set_string (value, priv->pseudo_class);
      break;

    case PROP_STYLE_CLASS:
      g_value_set_string (value, priv->style_class);
      break;

    case PROP_STYLE:
      g_value_set_string (value, priv->inline_style);
      break;

    case PROP_STYLABLE:
      g_value_set_boolean (value, priv->is_stylable);
      break;

    case PROP_HAS_TOOLTIP:
      g_value_set_boolean (value, priv->has_tooltip);
      break;

    case PROP_TOOLTIP_TEXT:
      g_value_set_string (value, st_widget_get_tooltip_text (actor));
      break;

    case PROP_TRACK_HOVER:
      g_value_set_boolean (value, priv->track_hover);
      break;

    case PROP_HOVER:
      g_value_set_boolean (value, priv->hover);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
    }
}

static void
st_widget_dispose (GObject *gobject)
{
  StWidget *actor = ST_WIDGET (gobject);
  StWidgetPrivate *priv = ST_WIDGET (actor)->priv;

  if (priv->theme)
    {
      g_object_unref (priv->theme);
      priv->theme = NULL;
    }

  if (priv->theme_node)
    {
      g_object_run_dispose (G_OBJECT (priv->theme_node));
      g_object_unref (priv->theme_node);
      priv->theme_node = NULL;
    }

  if (priv->tooltip)
    {
      ClutterContainer *parent;
      ClutterActor *tooltip = CLUTTER_ACTOR (priv->tooltip);

      /* this is just a little bit awkward because the tooltip is parented
       * on the stage, but we still want to "own" it */
      parent = CLUTTER_CONTAINER (clutter_actor_get_parent (tooltip));

      if (parent)
        clutter_container_remove_actor (parent, tooltip);

      priv->tooltip = NULL;
    }

  G_OBJECT_CLASS (st_widget_parent_class)->dispose (gobject);
}

static void
st_widget_finalize (GObject *gobject)
{
  StWidgetPrivate *priv = ST_WIDGET (gobject)->priv;

  g_free (priv->style_class);
  g_free (priv->pseudo_class);

  G_OBJECT_CLASS (st_widget_parent_class)->finalize (gobject);
}


static void
st_widget_get_preferred_width (ClutterActor *self,
                               gfloat        for_height,
                               gfloat       *min_width_p,
                               gfloat       *natural_width_p)
{
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (self));

  /* Most subclasses will override this and not chain down. However,
   * if they do not, then we need to override ClutterActor's default
   * behavior (request 0x0) to take CSS-specified minimums into
   * account (which st_theme_node_adjust_preferred_width() will do.)
   */

  if (min_width_p)
    *min_width_p = 0;

  if (natural_width_p)
    *natural_width_p = 0;

  st_theme_node_adjust_preferred_width (theme_node, min_width_p, natural_width_p);
}

static void
st_widget_get_preferred_height (ClutterActor *self,
                                gfloat        for_width,
                                gfloat       *min_height_p,
                                gfloat       *natural_height_p)
{
  StThemeNode *theme_node = st_widget_get_theme_node (ST_WIDGET (self));

  /* See st_widget_get_preferred_width() */

  if (min_height_p)
    *min_height_p = 0;

  if (natural_height_p)
    *natural_height_p = 0;

  st_theme_node_adjust_preferred_height (theme_node, min_height_p, natural_height_p);
}

static void
st_widget_allocate (ClutterActor          *actor,
                    const ClutterActorBox *box,
                    ClutterAllocationFlags flags)
{
  StWidget *self = ST_WIDGET (actor);
  StWidgetPrivate *priv = self->priv;
  StThemeNode *theme_node;
  ClutterActorClass *klass;
  ClutterGeometry area;
  ClutterVertex in_v, out_v;

  theme_node = st_widget_get_theme_node (self);

  klass = CLUTTER_ACTOR_CLASS (st_widget_parent_class);
  klass->allocate (actor, box, flags);

  /* update tooltip position */
  if (priv->tooltip)
    {
      in_v.x = in_v.y = in_v.z = 0;
      clutter_actor_apply_transform_to_point (actor, &in_v, &out_v);
      area.x = out_v.x;
      area.y = out_v.y;

      in_v.x = box->x2 - box->x1;
      in_v.y = box->y2 - box->y1;
      clutter_actor_apply_transform_to_point (actor, &in_v, &out_v);
      area.width = out_v.x - area.x;
      area.height = out_v.y - area.y;

      st_tooltip_set_tip_area (priv->tooltip, &area);
    }
}

static void
st_widget_paint (ClutterActor *actor)
{
  StWidget *self = ST_WIDGET (actor);
  StThemeNode *theme_node;
  ClutterActorBox allocation;

  theme_node = st_widget_get_theme_node (self);

  clutter_actor_get_allocation_box (actor, &allocation);

  st_theme_node_paint (theme_node, &allocation, clutter_actor_get_paint_opacity (actor));
}

static void
st_widget_parent_set (ClutterActor *widget,
                      ClutterActor *old_parent)
{
  ClutterActorClass *parent_class;
  ClutterActor *new_parent;

  parent_class = CLUTTER_ACTOR_CLASS (st_widget_parent_class);
  if (parent_class->parent_set)
    parent_class->parent_set (widget, old_parent);

  new_parent = clutter_actor_get_parent (widget);

  /* don't send the style changed signal if we no longer have a parent actor */
  if (new_parent)
    st_widget_style_changed (ST_WIDGET (widget));
}

static void
st_widget_map (ClutterActor *actor)
{
  StWidgetPrivate *priv = ST_WIDGET (actor)->priv;

  CLUTTER_ACTOR_CLASS (st_widget_parent_class)->map (actor);

  st_widget_ensure_style ((StWidget*) actor);

  if (priv->tooltip)
    clutter_actor_map ((ClutterActor *) priv->tooltip);
}

static void
st_widget_unmap (ClutterActor *actor)
{
  StWidgetPrivate *priv = ST_WIDGET (actor)->priv;

  CLUTTER_ACTOR_CLASS (st_widget_parent_class)->unmap (actor);

  if (priv->tooltip)
    clutter_actor_unmap ((ClutterActor *) priv->tooltip);
}

static void notify_children_of_style_change (ClutterContainer *container);

static void
notify_children_of_style_change_foreach (ClutterActor *actor,
                                         gpointer      user_data)
{
  if (ST_IS_WIDGET (actor))
    st_widget_style_changed (ST_WIDGET (actor));
  else if (CLUTTER_IS_CONTAINER (actor))
    notify_children_of_style_change ((ClutterContainer *)actor);
}

static void
notify_children_of_style_change (ClutterContainer *container)
{
  /* notify our children that their parent stylable has changed */
  clutter_container_foreach (container,
                             notify_children_of_style_change_foreach,
                             NULL);
}

static void
st_widget_real_style_changed (StWidget *self)
{
  StWidgetPrivate *priv = ST_WIDGET (self)->priv;

  /* application has request this widget is not stylable */
  if (!priv->is_stylable)
    return;

  clutter_actor_queue_redraw ((ClutterActor *) self);

  if (CLUTTER_IS_CONTAINER (self))
    notify_children_of_style_change ((ClutterContainer *)self);
}

void
st_widget_style_changed (StWidget *widget)
{
  StThemeNode *old_theme_node = NULL;

  widget->priv->is_style_dirty = TRUE;
  if (widget->priv->theme_node)
    {
      old_theme_node = widget->priv->theme_node;
      widget->priv->theme_node = NULL;
    }

  /* update the style only if we are mapped */
  if (CLUTTER_ACTOR_IS_MAPPED (CLUTTER_ACTOR (widget)))
    st_widget_recompute_style (widget, old_theme_node);

  if (old_theme_node)
    g_object_unref (old_theme_node);
}

static void
on_theme_context_changed (StThemeContext *context,
                          ClutterStage      *stage)
{
  notify_children_of_style_change (CLUTTER_CONTAINER (stage));
}

static StThemeNode *
get_root_theme_node (ClutterStage *stage)
{
  StThemeContext *context = st_theme_context_get_for_stage (stage);

  if (!g_object_get_data (G_OBJECT (context), "st-theme-initialized"))
    {
      g_object_set_data (G_OBJECT (context), "st-theme-initialized", GUINT_TO_POINTER (1));
      g_signal_connect (G_OBJECT (context), "changed",
                        G_CALLBACK (on_theme_context_changed), stage);
    }

  return st_theme_context_get_root_node (context);
}

/**
 * st_widget_get_theme_node:
 * @widget: a #StWidget
 *
 * Gets the theme node holding style information for the widget.
 * The theme node is used to access standard and custom CSS
 * properties of the widget.
 *
 * Return value: (transfer none): the theme node for the widget.
 *   This is owned by the widget. When attributes of the widget
 *   or the environment that affect the styling change (for example
 *   the style_class property of the widget), it will be recreated,
 *   and the ::style-changed signal will be emitted on the widget.
 */
StThemeNode *
st_widget_get_theme_node (StWidget *widget)
{
  StWidgetPrivate *priv = widget->priv;

  if (priv->theme_node == NULL)
    {
      StThemeNode *parent_node = NULL;
      ClutterStage *stage = NULL;
      ClutterActor *parent;

      parent = clutter_actor_get_parent (CLUTTER_ACTOR (widget));
      while (parent != NULL)
        {
          if (parent_node == NULL && ST_IS_WIDGET (parent))
            parent_node = st_widget_get_theme_node (ST_WIDGET (parent));
          else if (CLUTTER_IS_STAGE (parent))
            stage = CLUTTER_STAGE (parent);

          parent = clutter_actor_get_parent (parent);
        }

      if (stage == NULL)
        {
          g_error ("st_widget_get_theme_node called on a widget not in a stage");
        }

      if (parent_node == NULL)
        parent_node = get_root_theme_node (CLUTTER_STAGE (stage));

      priv->theme_node = st_theme_node_new (st_theme_context_get_for_stage (stage),
                                            parent_node, priv->theme,
                                            G_OBJECT_TYPE (widget),
                                            clutter_actor_get_name (CLUTTER_ACTOR (widget)),
                                            priv->style_class,
                                            priv->pseudo_class,
                                            priv->inline_style);
    }

  return priv->theme_node;
}

static gboolean
actor_contains (ClutterActor *widget,
                ClutterActor *other)
{
  while (other != NULL && other != widget)
    other = clutter_actor_get_parent (other);
  return other != NULL;
}

static gboolean
st_widget_enter (ClutterActor         *actor,
                 ClutterCrossingEvent *event)
{
  StWidgetPrivate *priv = ST_WIDGET (actor)->priv;

  if (priv->track_hover)
    {
      if (actor_contains (actor, event->source))
        st_widget_set_hover (ST_WIDGET (actor), TRUE);
      else
        {
          /* The widget has a grab and is being told about an
           * enter-event outside its hierarchy. Hopefully we already
           * got a leave-event, but if not, handle it now.
           */
          st_widget_set_hover (ST_WIDGET (actor), FALSE);
        }
    }

  if (CLUTTER_ACTOR_CLASS (st_widget_parent_class)->enter_event)
    return CLUTTER_ACTOR_CLASS (st_widget_parent_class)->enter_event (actor, event);
  else
    return FALSE;
}

static gboolean
st_widget_leave (ClutterActor         *actor,
                 ClutterCrossingEvent *event)
{
  StWidgetPrivate *priv = ST_WIDGET (actor)->priv;

  if (priv->track_hover)
    {
      if (!actor_contains (actor, event->related))
        st_widget_set_hover (ST_WIDGET (actor), FALSE);
    }

  if (CLUTTER_ACTOR_CLASS (st_widget_parent_class)->leave_event)
    return CLUTTER_ACTOR_CLASS (st_widget_parent_class)->leave_event (actor, event);
  else
    return FALSE;
}

static void
st_widget_hide (ClutterActor *actor)
{
  StWidget *widget = (StWidget *) actor;

  /* hide the tooltip, if there is one */
  if (widget->priv->tooltip)
    st_tooltip_hide (ST_TOOLTIP (widget->priv->tooltip));

  CLUTTER_ACTOR_CLASS (st_widget_parent_class)->hide (actor);
}



static void
st_widget_class_init (StWidgetClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (StWidgetPrivate));

  gobject_class->set_property = st_widget_set_property;
  gobject_class->get_property = st_widget_get_property;
  gobject_class->dispose = st_widget_dispose;
  gobject_class->finalize = st_widget_finalize;

  actor_class->get_preferred_width = st_widget_get_preferred_width;
  actor_class->get_preferred_height = st_widget_get_preferred_height;
  actor_class->allocate = st_widget_allocate;
  actor_class->paint = st_widget_paint;
  actor_class->parent_set = st_widget_parent_set;
  actor_class->map = st_widget_map;
  actor_class->unmap = st_widget_unmap;

  actor_class->enter_event = st_widget_enter;
  actor_class->leave_event = st_widget_leave;
  actor_class->hide = st_widget_hide;

  klass->style_changed = st_widget_real_style_changed;

  /**
   * StWidget:pseudo-class:
   *
   * The pseudo-class of the actor. Typical values include "hover", "active",
   * "focus".
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PSEUDO_CLASS,
                                   g_param_spec_string ("pseudo-class",
                                                        "Pseudo Class",
                                                        "Pseudo class for styling",
                                                        "",
                                                        ST_PARAM_READWRITE));
  /**
   * StWidget:style-class:
   *
   * The style-class of the actor for use in styling.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_STYLE_CLASS,
                                   g_param_spec_string ("style-class",
                                                        "Style Class",
                                                        "Style class for styling",
                                                        "",
                                                        ST_PARAM_READWRITE));

  /**
   * StWidget:style:
   *
   * Inline style information for the actor as a ';'-separated list of
   * CSS properties.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_STYLE,
                                   g_param_spec_string ("style",
                                                        "Style",
                                                        "Inline style string",
                                                        "",
                                                        ST_PARAM_READWRITE));

  /**
   * StWidget:theme
   *
   * A theme set on this actor overriding the global theming for this actor
   * and its descendants
   */
  g_object_class_install_property (gobject_class,
                                   PROP_THEME,
                                   g_param_spec_object ("theme",
                                                        "Theme",
                                                        "Theme override",
                                                        ST_TYPE_THEME,
                                                        ST_PARAM_READWRITE));

  /**
   * StWidget:stylable:
   *
   * Enable or disable styling of the widget
   */
  pspec = g_param_spec_boolean ("stylable",
                                "Stylable",
                                "Whether the table should be styled",
                                TRUE,
                                ST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class,
                                   PROP_STYLABLE,
                                   pspec);

  /**
   * StWidget:has-tooltip:
   *
   * Determines whether the widget has a tooltip. If set to %TRUE, causes the
   * widget to monitor hover state (i.e. sets #ClutterActor:reactive and
   * #StWidget:track-hover).
   */
  pspec = g_param_spec_boolean ("has-tooltip",
                                "Has Tooltip",
                                "Determines whether the widget has a tooltip",
                                FALSE,
                                ST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class,
                                   PROP_HAS_TOOLTIP,
                                   pspec);


  /**
   * StWidget:tooltip-text:
   *
   * text displayed on the tooltip
   */
  pspec = g_param_spec_string ("tooltip-text",
                               "Tooltip Text",
                               "Text displayed on the tooltip",
                               "",
                               ST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_TOOLTIP_TEXT, pspec);

  /**
   * StWidget:track-hover:
   *
   * Determines whether the widget tracks pointer hover state. If
   * %TRUE (and the widget is visible and reactive), the
   * #StWidget:hover property and "hover" style pseudo class will be
   * adjusted automatically as the pointer moves in and out of the
   * widget.
   */
  pspec = g_param_spec_boolean ("track-hover",
                                "Track hover",
                                "Determines whether the widget tracks hover state",
                                FALSE,
                                ST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class,
                                   PROP_TRACK_HOVER,
                                   pspec);

  /**
   * StWidget:hover:
   *
   * Whether or not the pointer is currently hovering over the widget. This is
   * only tracked automatically if #StWidget:track-hover is %TRUE, but you can
   * adjust it manually in any case.
   */
  pspec = g_param_spec_boolean ("hover",
                                "Hover",
                                "Whether the pointer is hovering over the widget",
                                FALSE,
                                ST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class,
                                   PROP_HOVER,
                                   pspec);

  /**
   * StWidget::style-changed:
   *
   * Emitted when the style information that the widget derives from the
   * theme changes
   */
  signals[STYLE_CHANGED] =
    g_signal_new ("style-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (StWidgetClass, style_changed),
                  NULL, NULL,
                  _st_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

/**
 * st_widget_set_theme:
 * @actor: a #StWidget
 * @theme: a new style class string
 *
 * Overrides the theme that would be inherited from the actor's parent
 * or the stage with an entirely new theme (set of stylesheets).
 */
void
st_widget_set_theme (StWidget  *actor,
                      StTheme  *theme)
{
  StWidgetPrivate *priv = actor->priv;

  g_return_if_fail (ST_IS_WIDGET (actor));

  priv = actor->priv;

  if (theme !=priv->theme)
    {
      if (priv->theme)
        g_object_unref (priv->theme);
      priv->theme = g_object_ref (priv->theme);

      st_widget_style_changed (actor);

      g_object_notify (G_OBJECT (actor), "theme");
    }
}

/**
 * st_widget_get_theme:
 * @actor: a #StWidget
 *
 * Gets the overriding theme set on the actor. See st_widget_set_theme()
 *
 * Return value: (transfer none): the overriding theme, or %NULL
 */
StTheme *
st_widget_get_theme (StWidget *actor)
{
  g_return_val_if_fail (ST_IS_WIDGET (actor), NULL);

  return actor->priv->theme;
}

static const gchar *
find_class_name (const gchar *class_list,
                 const gchar *class_name)
{
  gint len = strlen (class_name);
  const gchar *match;

  if (!class_list)
    return NULL;

  for (match = strstr (class_list, class_name); match; match = strstr (match + 1, class_name))
    {
      if ((match == class_list || g_ascii_isspace (match[-1])) &&
          (match[len] == '\0' || g_ascii_isspace (match[len])))
        return match;
    }

  return NULL;
}

static gboolean
set_class_list (gchar       **class_list,
                const gchar  *new_class_list)
{
  if (g_strcmp0 (*class_list, new_class_list) != 0)
    {
      g_free (*class_list);
      *class_list = g_strdup (new_class_list);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
add_class_name (gchar       **class_list,
                const gchar  *class_name)
{
  gchar *new_class_list;

  if (*class_list)
    {
      if (find_class_name (*class_list, class_name))
        return FALSE;

      new_class_list = g_strdup_printf ("%s %s", *class_list, class_name);
      g_free (*class_list);
      *class_list = new_class_list;
    }
  else
    *class_list = g_strdup (class_name);

  return TRUE;
}

static gboolean
remove_class_name (gchar       **class_list,
                   const gchar  *class_name)
{
  const gchar *match, *end;
  gchar *new_class_list;

  if (!*class_list)
    return FALSE;

  if (strcmp (*class_list, class_name) == 0)
    {
      g_free (*class_list);
      *class_list = NULL;
      return TRUE;
    }

  match = find_class_name (*class_list, class_name);
  if (!match)
    return FALSE;
  end = match + strlen (class_name);

  /* Adjust either match or end to include a space as well.
   * (One or the other must be possible at this point.)
   */
  if (match != *class_list)
    match--;
  else
    end++;

  new_class_list = g_strdup_printf ("%.*s%s", (int)(match - *class_list),
                                    *class_list, end);
  g_free (*class_list);
  *class_list = new_class_list;

  return TRUE;
}

/**
 * st_widget_set_style_class_name:
 * @actor: a #StWidget
 * @style_class_list: (allow-none): a new style class list string
 *
 * Set the style class name list. @style_class_list can either be
 * %NULL, for no classes, or a space-separated list of style class
 * names. See also st_widget_add_style_class_name() and
 * st_widget_remove_style_class_name().
 */
void
st_widget_set_style_class_name (StWidget    *actor,
                                const gchar *style_class_list)
{
  g_return_if_fail (ST_IS_WIDGET (actor));

  if (set_class_list (&actor->priv->style_class, style_class_list))
    {
      st_widget_style_changed (actor);
      g_object_notify (G_OBJECT (actor), "style-class");
    }
}

/**
 * st_widget_add_style_class_name:
 * @actor: a #StWidget
 * @style_class: a style class name string
 *
 * Adds @style_class to @actor's style class name list, if it is not
 * already present.
 */
void
st_widget_add_style_class_name (StWidget    *actor,
                                const gchar *style_class)
{
  g_return_if_fail (ST_IS_WIDGET (actor));
  g_return_if_fail (style_class != NULL);

  if (add_class_name (&actor->priv->style_class, style_class))
    {
      st_widget_style_changed (actor);
      g_object_notify (G_OBJECT (actor), "style-class");
    }
}

/**
 * st_widget_remove_style_class_name:
 * @actor: a #StWidget
 * @style_class: a style class name string
 *
 * Removes @style_class from @actor's style class name, if it is
 * present.
 */
void
st_widget_remove_style_class_name (StWidget    *actor,
                                   const gchar *style_class)
{
  g_return_if_fail (ST_IS_WIDGET (actor));
  g_return_if_fail (style_class != NULL);

  if (remove_class_name (&actor->priv->style_class, style_class))
    {
      st_widget_style_changed (actor);
      g_object_notify (G_OBJECT (actor), "style-class");
    }
}

/**
 * st_widget_get_style_class_name:
 * @actor: a #StWidget
 *
 * Get the current style class name
 *
 * Returns: the class name string. The string is owned by the #StWidget and
 * should not be modified or freed.
 */
const gchar*
st_widget_get_style_class_name (StWidget *actor)
{
  g_return_val_if_fail (ST_IS_WIDGET (actor), NULL);

  return actor->priv->style_class;
}

/**
 * st_widget_has_style_class_name:
 * @actor: a #StWidget
 * @style_class: a style class string
 *
 * Tests if @actor's style class list includes @style_class.
 *
 * Returns: whether or not @actor's style class list includes
 * @style_class.
 */
gboolean
st_widget_has_style_class_name (StWidget    *actor,
                                const gchar *style_class)
{
  g_return_val_if_fail (ST_IS_WIDGET (actor), FALSE);

  return find_class_name (actor->priv->style_class, style_class) != NULL;
}

/**
 * st_widget_get_style_pseudo_class:
 * @actor: a #StWidget
 *
 * Get the current style pseudo class list.
 *
 * Note that an actor can have multiple pseudo classes; if you just
 * want to test for the presence of a specific pseudo class, use
 * st_widget_has_style_pseudo_class().
 *
 * Returns: the pseudo class list string. The string is owned by the
 * #StWidget and should not be modified or freed.
 */
const gchar*
st_widget_get_style_pseudo_class (StWidget *actor)
{
  g_return_val_if_fail (ST_IS_WIDGET (actor), NULL);

  return actor->priv->pseudo_class;
}

/**
 * st_widget_has_style_pseudo_class:
 * @actor: a #StWidget
 * @pseudo_class: a pseudo class string
 *
 * Tests if @actor's pseudo class list includes @pseudo_class.
 *
 * Returns: whether or not @actor's pseudo class list includes
 * @pseudo_class.
 */
gboolean
st_widget_has_style_pseudo_class (StWidget    *actor,
                                  const gchar *pseudo_class)
{
  g_return_val_if_fail (ST_IS_WIDGET (actor), FALSE);

  return find_class_name (actor->priv->pseudo_class, pseudo_class) != NULL;
}

/**
 * st_widget_set_style_pseudo_class:
 * @actor: a #StWidget
 * @pseudo_class_list: (allow-none): a new pseudo class list string
 *
 * Set the style pseudo class list. @pseudo_class_list can either be
 * %NULL, for no classes, or a space-separated list of pseudo class
 * names. See also st_widget_add_style_pseudo_class() and
 * st_widget_remove_style_pseudo_class().
 */
void
st_widget_set_style_pseudo_class (StWidget    *actor,
                                  const gchar *pseudo_class_list)
{
  g_return_if_fail (ST_IS_WIDGET (actor));

  if (set_class_list (&actor->priv->pseudo_class, pseudo_class_list))
    {
      st_widget_style_changed (actor);
      g_object_notify (G_OBJECT (actor), "pseudo-class");
    }
}

/**
 * st_widget_add_style_pseudo_class:
 * @actor: a #StWidget
 * @pseudo_class: a pseudo class string
 *
 * Adds @pseudo_class to @actor's pseudo class list, if it is not
 * already present.
 */
void
st_widget_add_style_pseudo_class (StWidget    *actor,
                                  const gchar *pseudo_class)
{
  g_return_if_fail (ST_IS_WIDGET (actor));
  g_return_if_fail (pseudo_class != NULL);

  if (add_class_name (&actor->priv->pseudo_class, pseudo_class))
    {
      st_widget_style_changed (actor);
      g_object_notify (G_OBJECT (actor), "pseudo-class");
    }
}

/**
 * st_widget_remove_style_pseudo_class:
 * @actor: a #StWidget
 * @pseudo_class: a pseudo class string
 *
 * Removes @pseudo_class from @actor's pseudo class, if it is present.
 */
void
st_widget_remove_style_pseudo_class (StWidget    *actor,
                                     const gchar *pseudo_class)
{
  g_return_if_fail (ST_IS_WIDGET (actor));
  g_return_if_fail (pseudo_class != NULL);

  if (remove_class_name (&actor->priv->pseudo_class, pseudo_class))
    {
      st_widget_style_changed (actor);
      g_object_notify (G_OBJECT (actor), "pseudo-class");
    }
}

/**
 * st_widget_set_style:
 * @actor: a #StWidget
 * @style_class: (allow-none): a inline style string, or %NULL
 *
 * Set the inline style string for this widget. The inline style string is an
 * optional ';'-separated list of CSS properties that override the style as
 * determined from the stylesheets of the current theme.
 */
void
st_widget_set_style (StWidget  *actor,
                       const gchar *style)
{
  StWidgetPrivate *priv = actor->priv;

  g_return_if_fail (ST_IS_WIDGET (actor));

  priv = actor->priv;

  if (g_strcmp0 (style, priv->inline_style))
    {
      g_free (priv->inline_style);
      priv->inline_style = g_strdup (style);

      st_widget_style_changed (actor);

      g_object_notify (G_OBJECT (actor), "style");
    }
}

/**
 * st_widget_get_style:
 * @actor: a #StWidget
 *
 * Get the current inline style string. See st_widget_set_style().
 *
 * Returns: The inline style string, or %NULL. The string is owned by the
 * #StWidget and should not be modified or freed.
 */
const gchar*
st_widget_get_style (StWidget *actor)
{
  g_return_val_if_fail (ST_IS_WIDGET (actor), NULL);

  return actor->priv->inline_style;
}

static void
st_widget_name_notify (StWidget   *widget,
                       GParamSpec *pspec,
                       gpointer    data)
{
  st_widget_style_changed (widget);
}

static void
st_widget_init (StWidget *actor)
{
  StWidgetPrivate *priv;

  actor->priv = priv = ST_WIDGET_GET_PRIVATE (actor);
  priv->is_stylable = TRUE;

  /* connect style changed */
  g_signal_connect (actor, "notify::name", G_CALLBACK (st_widget_name_notify), NULL);
}

static void
st_widget_recompute_style (StWidget    *widget,
                           StThemeNode *old_theme_node)
{
  StThemeNode *new_theme_node = st_widget_get_theme_node (widget);

  if (!old_theme_node ||
      !st_theme_node_geometry_equal (old_theme_node, new_theme_node))
    clutter_actor_queue_relayout ((ClutterActor *) widget);

  g_signal_emit (widget, signals[STYLE_CHANGED], 0);
  widget->priv->is_style_dirty = FALSE;
}

/**
 * st_widget_ensure_style:
 * @widget: A #StWidget
 *
 * Ensures that @widget has read its style information.
 *
 */
void
st_widget_ensure_style (StWidget *widget)
{
  g_return_if_fail (ST_IS_WIDGET (widget));

  if (widget->priv->is_style_dirty)
    st_widget_recompute_style (widget, NULL);
}

static StTextDirection default_direction = ST_TEXT_DIRECTION_LTR;

StTextDirection
st_widget_get_default_direction (void)
{
  return default_direction;
}

void
st_widget_set_default_direction (StTextDirection dir)
{
  g_return_if_fail (dir != ST_TEXT_DIRECTION_NONE);

  default_direction = dir;
}

StTextDirection
st_widget_get_direction (StWidget *self)
{
  g_return_val_if_fail (ST_IS_WIDGET (self), ST_TEXT_DIRECTION_LTR);

  if (self->priv->direction != ST_TEXT_DIRECTION_NONE)
    return self->priv->direction;
  else
    return default_direction;
}

void
st_widget_set_direction (StWidget *self, StTextDirection dir)
{
  g_return_if_fail (ST_IS_WIDGET (self));
  self->priv->direction = dir;
}

/**
 * st_widget_set_has_tooltip:
 * @widget: A #StWidget
 * @has_tooltip: %TRUE if the widget should display a tooltip
 *
 * Enables tooltip support on the #StWidget.
 *
 * Note that setting has-tooltip to %TRUE will cause
 * #ClutterActor:reactive and #StWidget:track-hover to be set %TRUE as
 * well, but you must clear these flags yourself (if appropriate) when
 * setting it %FALSE.
 */
void
st_widget_set_has_tooltip (StWidget *widget,
                           gboolean  has_tooltip)
{
  StWidgetPrivate *priv;

  g_return_if_fail (ST_IS_WIDGET (widget));

  priv = widget->priv;

  priv->has_tooltip = has_tooltip;

  if (has_tooltip)
    {
      clutter_actor_set_reactive ((ClutterActor*) widget, TRUE);
      st_widget_set_track_hover (widget, TRUE);

      if (!priv->tooltip)
        {
          priv->tooltip = g_object_new (ST_TYPE_TOOLTIP, NULL);
          clutter_actor_set_parent ((ClutterActor *) priv->tooltip,
                                    (ClutterActor *) widget);
        }
    }
  else
    {
      if (priv->tooltip)
        {
          clutter_actor_unparent (CLUTTER_ACTOR (priv->tooltip));
          priv->tooltip = NULL;
        }
    }
}

/**
 * st_widget_get_has_tooltip:
 * @widget: A #StWidget
 *
 * Returns the current value of the has-tooltip property. See
 * st_tooltip_set_has_tooltip() for more information.
 *
 * Returns: current value of has-tooltip on @widget
 */
gboolean
st_widget_get_has_tooltip (StWidget *widget)
{
  g_return_val_if_fail (ST_IS_WIDGET (widget), FALSE);

  return widget->priv->has_tooltip;
}

/**
 * st_widget_set_tooltip_text:
 * @widget: A #StWidget
 * @text: text to set as the tooltip
 *
 * Set the tooltip text of the widget. This will set StWidget::has-tooltip to
 * %TRUE. A value of %NULL will unset the tooltip and set has-tooltip to %FALSE.
 *
 */
void
st_widget_set_tooltip_text (StWidget    *widget,
                            const gchar *text)
{
  StWidgetPrivate *priv;

  g_return_if_fail (ST_IS_WIDGET (widget));

  priv = widget->priv;

  if (text == NULL)
    st_widget_set_has_tooltip (widget, FALSE);
  else
    st_widget_set_has_tooltip (widget, TRUE);

  st_tooltip_set_label (priv->tooltip, text);
}

/**
 * st_widget_get_tooltip_text:
 * @widget: A #StWidget
 *
 * Get the current tooltip string
 *
 * Returns: The current tooltip string, owned by the #StWidget
 */
const gchar*
st_widget_get_tooltip_text (StWidget *widget)
{
  StWidgetPrivate *priv;

  g_return_val_if_fail (ST_IS_WIDGET (widget), NULL);
  priv = widget->priv;

  if (!priv->has_tooltip)
    return NULL;

  return st_tooltip_get_label (widget->priv->tooltip);
}

/**
 * st_widget_show_tooltip:
 * @widget: A #StWidget
 *
 * Show the tooltip for @widget
 *
 */
void
st_widget_show_tooltip (StWidget *widget)
{
  gfloat x, y, width, height;
  ClutterGeometry area;

  g_return_if_fail (ST_IS_WIDGET (widget));

  /* XXX not necceary, but first allocate transform is wrong */

  clutter_actor_get_transformed_position ((ClutterActor*) widget,
                                          &x, &y);

  clutter_actor_get_size ((ClutterActor*) widget, &width, &height);

  area.x = x;
  area.y = y;
  area.width = width;
  area.height = height;


  if (widget->priv->tooltip)
    {
      st_tooltip_set_tip_area (widget->priv->tooltip, &area);
      st_tooltip_show (widget->priv->tooltip);
    }
}

/**
 * st_widget_hide_tooltip:
 * @widget: A #StWidget
 *
 * Hide the tooltip for @widget
 *
 */
void
st_widget_hide_tooltip (StWidget *widget)
{
  g_return_if_fail (ST_IS_WIDGET (widget));

  if (widget->priv->tooltip)
    st_tooltip_hide (widget->priv->tooltip);
}

/**
 * st_widget_set_track_hover:
 * @widget: A #StWidget
 * @track_hover: %TRUE if the widget should track the pointer hover state
 *
 * Enables hover tracking on the #StWidget.
 *
 * If hover tracking is enabled, and the widget is visible and
 * reactive, then @widget's #StWidget:hover property will be updated
 * automatically to reflect whether the pointer is in @widget (or one
 * of its children), and @widget's #StWidget:pseudo-class will have
 * the "hover" class added and removed from it accordingly.
 *
 * Note that currently it is not possible to correctly track the hover
 * state when another actor has a pointer grab. You can use
 * st_widget_sync_hover() to update the property manually in this
 * case.
 */
void
st_widget_set_track_hover (StWidget *widget,
                           gboolean  track_hover)
{
  StWidgetPrivate *priv;

  g_return_if_fail (ST_IS_WIDGET (widget));

  priv = widget->priv;

  if (priv->track_hover != track_hover)
    {
      priv->track_hover = track_hover;
      g_object_notify (G_OBJECT (widget), "track-hover");

      if (priv->track_hover)
        st_widget_sync_hover (widget);
    }
}

/**
 * st_widget_get_track_hover:
 * @widget: A #StWidget
 *
 * Returns the current value of the track-hover property. See
 * st_tooltip_set_track_hover() for more information.
 *
 * Returns: current value of track-hover on @widget
 */
gboolean
st_widget_get_track_hover (StWidget *widget)
{
  g_return_val_if_fail (ST_IS_WIDGET (widget), FALSE);

  return widget->priv->track_hover;
}

/**
 * st_widget_set_hover:
 * @widget: A #StWidget
 * @hover: whether the pointer is hovering over the widget
 *
 * Sets @widget's hover property and adds or removes "hover" from its
 * pseudo class accordingly. If #StWidget:has-tooltip is %TRUE, this
 * will also show or hide the tooltip, as appropriate.
 *
 * If you have set #StWidget:track-hover, you should not need to call
 * this directly. You can call st_widget_sync_hover() if the hover
 * state might be out of sync due to another actor's pointer grab.
 */
void
st_widget_set_hover (StWidget *widget,
                     gboolean  hover)
{
  StWidgetPrivate *priv;

  g_return_if_fail (ST_IS_WIDGET (widget));

  priv = widget->priv;

  if (priv->hover != hover)
    {
      priv->hover = hover;
      if (priv->hover)
        {
          st_widget_add_style_pseudo_class (widget, "hover");
          if (priv->has_tooltip)
            st_widget_show_tooltip (widget);
        }
      else
        {
          st_widget_remove_style_pseudo_class (widget, "hover");
          if (priv->has_tooltip)
            st_widget_hide_tooltip (widget);
        }
      g_object_notify (G_OBJECT (widget), "hover");
    }
}

/**
 * st_widget_sync_hover:
 * @widget: A #StWidget
 *
 * Sets @widget's hover state according to the current pointer
 * position. This can be used to ensure that it is correct after
 * (or during) a pointer grab.
 */
void
st_widget_sync_hover (StWidget *widget)
{
  ClutterDeviceManager *device_manager;
  ClutterInputDevice *pointer;
  ClutterActor *actor;

  device_manager = clutter_device_manager_get_default ();
  pointer = clutter_device_manager_get_core_device (device_manager,
                                                    CLUTTER_POINTER_DEVICE);
  actor = clutter_input_device_get_pointer_actor (pointer);

  while (actor && actor != (ClutterActor *)widget)
    actor = clutter_actor_get_parent (actor);

  st_widget_set_hover (widget, actor == (ClutterActor *)widget);
}

/**
 * st_widget_get_hover:
 * @widget: A #StWidget
 *
 * If #StWidget:track-hover is set, this returns whether the pointer
 * is currently over the widget.
 *
 * Returns: current value of hover on @widget
 */
gboolean
st_widget_get_hover (StWidget *widget)
{
  g_return_val_if_fail (ST_IS_WIDGET (widget), FALSE);

  return widget->priv->hover;
}
