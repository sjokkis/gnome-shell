/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
#ifndef __SHELL_GLOBAL_H__
#define __SHELL_GLOBAL_H__

#include "mutter-plugin.h"
#include <clutter/clutter.h>
#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include <telepathy-glib/contact.h>

G_BEGIN_DECLS

typedef struct _ShellGlobal      ShellGlobal;
typedef struct _ShellGlobalClass ShellGlobalClass;

#define SHELL_TYPE_GLOBAL              (shell_global_get_type ())
#define SHELL_GLOBAL(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), SHELL_TYPE_GLOBAL, ShellGlobal))
#define SHELL_GLOBAL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), SHELL_TYPE_GLOBAL, ShellGlobalClass))
#define SHELL_IS_GLOBAL(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), SHELL_TYPE_GLOBAL))
#define SHELL_IS_GLOBAL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), SHELL_TYPE_GLOBAL))
#define SHELL_GLOBAL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), SHELL_TYPE_GLOBAL, ShellGlobalClass))

#define SHELL_GCONF_DIR "/desktop/gnome/shell"

struct _ShellGlobalClass
{
  GObjectClass parent_class;

  void (*screen_size_changed) (ShellGlobal *global);
};

GType            shell_global_get_type            (void) G_GNUC_CONST;

gboolean shell_clutter_texture_set_from_pixbuf (ClutterTexture *texture,
                                                GdkPixbuf      *pixbuf);

ClutterActor *shell_get_event_related(ClutterEvent *event);

ShellGlobal *shell_global_get (void);

MetaScreen *shell_global_get_screen (ShellGlobal  *global);

gboolean shell_global_add_extension_importer (ShellGlobal *global,
                                              const char  *target_object_script,
                                              const char  *target_property,
                                              const char  *directory,
                                              GError     **error);

void shell_global_grab_dbus_service (ShellGlobal *global);

typedef enum {
  SHELL_STAGE_INPUT_MODE_NONREACTIVE,
  SHELL_STAGE_INPUT_MODE_NORMAL,
  SHELL_STAGE_INPUT_MODE_FULLSCREEN
} ShellStageInputMode;

void shell_global_set_stage_input_mode   (ShellGlobal         *global,
					  ShellStageInputMode  mode);
void shell_global_set_stage_input_region (ShellGlobal         *global,
					  GSList              *rectangles);

GList *shell_global_get_windows (ShellGlobal *global);

gboolean shell_global_begin_modal (ShellGlobal *global,
				   guint32      timestamp);
void     shell_global_end_modal   (ShellGlobal *global,
				   guint32      timestamp);

gboolean shell_global_display_is_grabbed (ShellGlobal *global);

void shell_global_reexec_self (ShellGlobal *global);

void shell_global_breakpoint (ShellGlobal *global);

void shell_global_gc (ShellGlobal *global);

void shell_global_maybe_gc (ShellGlobal *global);

void shell_global_format_time_relative_pretty (ShellGlobal *global, guint delta, char **text, guint *update_time);

ClutterActor *shell_global_create_root_pixmap_actor (ShellGlobal *global);

GSList       *shell_global_get_monitors        (ShellGlobal  *global);
GdkRectangle *shell_global_get_primary_monitor (ShellGlobal  *global);
GdkRectangle *shell_global_get_focus_monitor   (ShellGlobal  *global);

void shell_global_get_pointer (ShellGlobal         *global,
                               int                 *x,
                               int                 *y,
                               ClutterModifierType *mods);

ClutterModifierType shell_get_event_state (ClutterEvent *event);

void shell_popup_menu (GtkMenu *menu, int button, guint32 time,
                       int menu_x, int menu_y);

gboolean shell_write_string_to_stream (GOutputStream *stream,
                                       const char    *str,
                                       GError       **error);

guint32 shell_global_get_current_time (ShellGlobal *global);

GAppLaunchContext *shell_global_create_app_launch_context (ShellGlobal *global);

gboolean shell_global_set_property_mutable (ShellGlobal *global,
                                            const char  *object,
                                            const char  *property,
                                            gboolean     mutable);

void shell_global_begin_work     (ShellGlobal         *global);
void shell_global_end_work       (ShellGlobal         *global);

typedef void (*ShellLeisureFunction) (gpointer data);

void shell_global_run_at_leisure (ShellGlobal         *global,
                                  ShellLeisureFunction func,
                                  gpointer             user_data,
                                  GDestroyNotify       notify);

typedef void (*GetTpContactsCb) (TpConnection *connection,
                                 GList *contacts,
                                 TpHandle *failed);

void shell_global_get_tp_contacts (TpConnection *self,
                                   guint n_handles,
                                   const TpHandle *handles,
                                   guint n_features,
                                   const TpContactFeature *features,
                                   GetTpContactsCb callback,
                                   gpointer user_data,
                                   GDestroyNotify destroy,
                                   GObject *weak_object);

G_END_DECLS

#endif /* __SHELL_GLOBAL_H__ */
