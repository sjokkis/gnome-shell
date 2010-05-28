/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

#include "config.h"

#include "shell-global-private.h"
#include "shell-perf-log.h"
#include "shell-wm.h"
#include "shell-global.h"

#include "display.h"
#include "util.h"
#include <clutter/glx/clutter-glx.h>
#include <clutter/x11/clutter-x11.h>
#include <gdk/gdkx.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus-glib.h>
#include <gio/gio.h>
#include <math.h>
#include <X11/extensions/Xfixes.h>
#include <gjs/gjs.h>
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#define SHELL_DBUS_SERVICE "org.gnome.Shell"
#define MAGNIFIER_DBUS_SERVICE "org.gnome.Magnifier"

static void grab_notify (GtkWidget *widget, gboolean is_grab, gpointer user_data);
static void update_root_window_pixmap (ShellGlobal *global);

struct _ShellGlobal {
  GObject parent;
  
  /* We use this window to get a notification from GTK+ when
   * a widget in our process does a GTK+ grab.  See
   * http://bugzilla.gnome.org/show_bug.cgi?id=570641
   * 
   * This window is never mapped or shown.
   */
  GtkWindow *grab_notifier;
  gboolean gtk_grab_active;

  ShellStageInputMode input_mode;
  XserverRegion input_region;

  GjsContext *js_context;
  MutterPlugin *plugin;
  ShellWM *wm;
  const char *datadir;
  const char *imagedir;
  const char *userdatadir;

  /* Displays the root window; see shell_global_create_root_pixmap_actor() */
  ClutterActor *root_pixmap;

  gint last_change_screen_width, last_change_screen_height;

  guint work_count;
  GSList *leisure_closures;
  guint leisure_function_id;
};

enum {
  PROP_0,

  PROP_OVERLAY_GROUP,
  PROP_SCREEN,
  PROP_SCREEN_WIDTH,
  PROP_SCREEN_HEIGHT,
  PROP_STAGE,
  PROP_WINDOW_GROUP,
  PROP_WINDOW_MANAGER,
  PROP_DATADIR,
  PROP_IMAGEDIR,
  PROP_USERDATADIR,
};

/* Signals */
enum
{
  SCREEN_SIZE_CHANGED,
  LAST_SIGNAL
};

G_DEFINE_TYPE(ShellGlobal, shell_global, G_TYPE_OBJECT);

static guint shell_global_signals [LAST_SIGNAL] = { 0 };

static void
shell_global_set_property(GObject         *object,
                          guint            prop_id,
                          const GValue    *value,
                          GParamSpec      *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
shell_global_get_property(GObject         *object,
                          guint            prop_id,
                          GValue          *value,
                          GParamSpec      *pspec)
{
  ShellGlobal *global = SHELL_GLOBAL (object);

  switch (prop_id)
    {
    case PROP_OVERLAY_GROUP:
      g_value_set_object (value, mutter_plugin_get_overlay_group (global->plugin));
      break;
    case PROP_SCREEN:
      g_value_set_object (value, shell_global_get_screen (global));
      break;
    case PROP_SCREEN_WIDTH:
      {
        int width, height;

        mutter_plugin_query_screen_size (global->plugin, &width, &height);
        g_value_set_int (value, width);
      }
      break;
    case PROP_SCREEN_HEIGHT:
      {
        int width, height;

        mutter_plugin_query_screen_size (global->plugin, &width, &height);
        g_value_set_int (value, height);
      }
      break;
    case PROP_STAGE:
      g_value_set_object (value, mutter_plugin_get_stage (global->plugin));
      break;
    case PROP_WINDOW_GROUP:
      g_value_set_object (value, mutter_plugin_get_window_group (global->plugin));
      break;
    case PROP_WINDOW_MANAGER:
      g_value_set_object (value, global->wm);
      break;
    case PROP_DATADIR:
      g_value_set_string (value, global->datadir);
      break;
    case PROP_IMAGEDIR:
      g_value_set_string (value, global->imagedir);
      break;
    case PROP_USERDATADIR:
      g_value_set_string (value, global->userdatadir);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
shell_global_init (ShellGlobal *global)
{
  const char *datadir = g_getenv ("GNOME_SHELL_DATADIR");
  char *imagedir;

  if (!datadir)
    datadir = GNOME_SHELL_DATADIR;
  global->datadir = datadir;

  /* We make sure imagedir ends with a '/', since the JS won't have
   * access to g_build_filename() and so will end up just
   * concatenating global.imagedir to a filename.
   */
  imagedir = g_build_filename (datadir, "images/", NULL);
  if (g_file_test (imagedir, G_FILE_TEST_IS_DIR))
    global->imagedir = imagedir;
  else
    {
      g_free (imagedir);
      global->imagedir = g_strdup_printf ("%s/", datadir);
    }

  /* Ensure config dir exists for later use */
  global->userdatadir = g_build_filename (g_get_user_data_dir (), "gnome-shell", NULL);
  g_mkdir_with_parents (global->userdatadir, 0700);
  
  global->grab_notifier = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
  g_signal_connect (global->grab_notifier, "grab-notify", G_CALLBACK (grab_notify), global);
  global->gtk_grab_active = FALSE;

  global->root_pixmap = NULL;

  global->input_mode = SHELL_STAGE_INPUT_MODE_NORMAL;

  global->last_change_screen_width = 0;
  global->last_change_screen_height = 0;
}

static void
shell_global_class_init (ShellGlobalClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = shell_global_get_property;
  gobject_class->set_property = shell_global_set_property;

  shell_global_signals[SCREEN_SIZE_CHANGED] =
    g_signal_new ("screen-size-changed",
		  G_TYPE_FROM_CLASS (klass),
		  G_SIGNAL_RUN_LAST,
		  G_STRUCT_OFFSET (ShellGlobalClass, screen_size_changed),
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE, 0);

  g_object_class_install_property (gobject_class,
                                   PROP_OVERLAY_GROUP,
                                   g_param_spec_object ("overlay-group",
                                                        "Overlay Group",
                                                        "Actor holding objects that appear above the desktop contents",
                                                        CLUTTER_TYPE_ACTOR,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_SCREEN,
                                   g_param_spec_object ("screen",
                                                        "Screen",
                                                        "Metacity screen object for the shell",
                                                        META_TYPE_SCREEN,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_SCREEN_WIDTH,
                                   g_param_spec_int ("screen-width",
                                                     "Screen Width",
                                                     "Screen width, in pixels",
                                                     0, G_MAXINT, 1,
                                                     G_PARAM_READABLE));

  g_object_class_install_property (gobject_class,
                                   PROP_SCREEN_HEIGHT,
                                   g_param_spec_int ("screen-height",
                                                     "Screen Height",
                                                     "Screen height, in pixels",
                                                     0, G_MAXINT, 1,
                                                     G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_STAGE,
                                   g_param_spec_object ("stage",
                                                        "Stage",
                                                        "Stage holding the desktop scene graph",
                                                        CLUTTER_TYPE_ACTOR,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_WINDOW_GROUP,
                                   g_param_spec_object ("window-group",
                                                        "Window Group",
                                                        "Actor holding window actors",
                                                        CLUTTER_TYPE_ACTOR,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_WINDOW_MANAGER,
                                   g_param_spec_object ("window-manager",
                                                        "Window Manager",
                                                        "Window management interface",
                                                        SHELL_TYPE_WM,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_DATADIR,
                                   g_param_spec_string ("datadir",
                                                        "Data directory",
                                                        "Directory containing gnome-shell data files",
                                                        NULL,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_IMAGEDIR,
                                   g_param_spec_string ("imagedir",
                                                        "Image directory",
                                                        "Directory containing gnome-shell image files",
                                                        NULL,
                                                        G_PARAM_READABLE));
  g_object_class_install_property (gobject_class,
                                   PROP_USERDATADIR,
                                   g_param_spec_string ("userdatadir",
                                                        "User data directory",
                                                        "Directory containing gnome-shell user data",
                                                        NULL,
                                                        G_PARAM_READABLE));
}

/**
 * shell_clutter_texture_set_from_pixbuf: 
 * texture: #ClutterTexture to be modified
 * pixbuf: #GdkPixbuf to set as an image for #ClutterTexture
 *
 * Convenience function for setting an image for #ClutterTexture based on #GdkPixbuf.
 * Copied from an example posted by hp in this thread http://mail.gnome.org/archives/gtk-devel-list/2008-September/msg00218.html
 *
 * Return value: %TRUE on success, %FALSE on failure
 */
gboolean
shell_clutter_texture_set_from_pixbuf (ClutterTexture *texture,
                                       GdkPixbuf      *pixbuf)
{
    return clutter_texture_set_from_rgb_data (texture,
                                              gdk_pixbuf_get_pixels (pixbuf),
                                              gdk_pixbuf_get_has_alpha (pixbuf),
                                              gdk_pixbuf_get_width (pixbuf),
                                              gdk_pixbuf_get_height (pixbuf),
                                              gdk_pixbuf_get_rowstride (pixbuf),
                                              gdk_pixbuf_get_has_alpha (pixbuf)
                                              ? 4 : 3,
                                              0, NULL);
}

/**
 * shell_global_get:
 *
 * Gets the singleton global object that represents the desktop.
 *
 * Return value: (transfer none): the singleton global object
 */
ShellGlobal *
shell_global_get (void)
{
  static ShellGlobal *the_object = NULL;

  if (!the_object)
    the_object = g_object_new (SHELL_TYPE_GLOBAL, 0);

  return the_object;
}

/**
 * shell_global_set_stage_input_mode:
 * @global: the #ShellGlobal
 * @mode: the stage input mode
 *
 * Sets the input mode of the stage; when @mode is
 * %SHELL_STAGE_INPUT_MODE_NONREACTIVE, then the stage does not absorb
 * any clicks, but just passes them through to underlying windows.
 * When it is %SHELL_STAGE_INPUT_MODE_NORMAL, then the stage accepts
 * clicks in the region defined by
 * shell_global_set_stage_input_region() but passes through clicks
 * outside that region. When it is %SHELL_STAGE_INPUT_MODE_FULLSCREEN,
 * the stage absorbs all input.
 *
 * Note that whenever a mutter-internal Gtk widget has a pointer grab,
 * the shell behaves as though it was in
 * %SHELL_STAGE_INPUT_MODE_NONREACTIVE, to ensure that the widget gets
 * any clicks it is expecting.
 */
void
shell_global_set_stage_input_mode (ShellGlobal         *global,
                                   ShellStageInputMode  mode)
{
  g_return_if_fail (SHELL_IS_GLOBAL (global));

  if (mode == SHELL_STAGE_INPUT_MODE_NONREACTIVE || global->gtk_grab_active)
    mutter_plugin_set_stage_reactive (global->plugin, FALSE);
  else if (mode == SHELL_STAGE_INPUT_MODE_FULLSCREEN || !global->input_region)
    mutter_plugin_set_stage_reactive (global->plugin, TRUE);
  else
    mutter_plugin_set_stage_input_region (global->plugin, global->input_region);

  global->input_mode = mode;
}

/**
 * shell_global_set_stage_input_region:
 * @global: the #ShellGlobal
 * @rectangles: (element-type Meta.Rectangle): a list of #MetaRectangle
 * describing the input region.
 *
 * Sets the area of the stage that is responsive to mouse clicks when
 * the stage mode is %SHELL_STAGE_INPUT_MODE_NORMAL (but does not change the
 * current stage mode).
 */
void
shell_global_set_stage_input_region (ShellGlobal *global,
                                     GSList      *rectangles)
{
  MetaScreen *screen = mutter_plugin_get_screen (global->plugin);
  MetaDisplay *display = meta_screen_get_display (screen);
  Display *xdpy = meta_display_get_xdisplay (display);
  MetaRectangle *rect;
  XRectangle *rects;
  int nrects, i;
  GSList *r;

  g_return_if_fail (SHELL_IS_GLOBAL (global));

  nrects = g_slist_length (rectangles);
  rects = g_new (XRectangle, nrects);
  for (r = rectangles, i = 0; r; r = r->next, i++)
    {
      rect = (MetaRectangle *)r->data;
      rects[i].x = rect->x;
      rects[i].y = rect->y;
      rects[i].width = rect->width;
      rects[i].height = rect->height;
    }

  if (global->input_region)
    XFixesDestroyRegion (xdpy, global->input_region);

  global->input_region = XFixesCreateRegion (xdpy, rects, nrects);
  g_free (rects);

  /* set_stage_input_mode() will figure out whether or not we
   * should actually change the input region right now.
   */
  shell_global_set_stage_input_mode (global, global->input_mode);
}

/**
 * shell_global_get_screen:
 *
 * Return value: (transfer none): The default #MetaScreen
 */
MetaScreen *
shell_global_get_screen (ShellGlobal  *global)
{
  return mutter_plugin_get_screen (global->plugin);
}

/**
 * shell_global_get_windows:
 *
 * Gets the list of MutterWindows for the plugin's screen
 *
 * Return value: (element-type MutterWindow) (transfer none): the list of windows
 */
GList *
shell_global_get_windows (ShellGlobal *global)
{
  g_return_val_if_fail (SHELL_IS_GLOBAL (global), NULL);

  return mutter_plugin_get_windows (global->plugin);
}

static gboolean
update_screen_size (ShellGlobal *global)
{
  int width, height;

  mutter_plugin_query_screen_size (global->plugin, &width, &height);

  if (global->last_change_screen_width == width && global->last_change_screen_height == height)
    return FALSE;

  global->last_change_screen_width = width;
  global->last_change_screen_height = height;

  /* update size of background actor to fix tiled backgrounds */
  if (global->root_pixmap)
    clutter_actor_set_size (CLUTTER_ACTOR (global->root_pixmap),
                            width, height);

  return TRUE;
}

static gboolean
on_screen_size_changed_cb (gpointer data)
{
  ShellGlobal *global = SHELL_GLOBAL (data);

  if (update_screen_size (global))
    g_signal_emit (G_OBJECT (global), shell_global_signals[SCREEN_SIZE_CHANGED], 0);

  return FALSE;
}

static void
global_stage_notify_width (GObject    *gobject,
                           GParamSpec *pspec,
                           gpointer    data)
{
  ShellGlobal *global = SHELL_GLOBAL (data);

  g_object_notify (G_OBJECT (global), "screen-width");

  meta_later_add (META_LATER_BEFORE_REDRAW,
                  on_screen_size_changed_cb,
                  global,
                  NULL);
}

static void
global_stage_notify_height (GObject    *gobject,
                            GParamSpec *pspec,
                            gpointer    data)
{
  ShellGlobal *global = SHELL_GLOBAL (data);

  g_object_notify (G_OBJECT (global), "screen-height");

  meta_later_add (META_LATER_BEFORE_REDRAW,
                  on_screen_size_changed_cb,
                  global,
                  NULL);
}

static void
global_stage_before_paint (ClutterStage *stage,
                           ShellGlobal  *global)
{
  shell_perf_log_event (shell_perf_log_get_default (),
                        "clutter.stagePaintStart");
}

static void
global_stage_after_paint (ClutterStage *stage,
                          ShellGlobal  *global)
{
  shell_perf_log_event (shell_perf_log_get_default (),
                        "clutter.stagePaintDone");
}

void
_shell_global_set_plugin (ShellGlobal  *global,
                          MutterPlugin *plugin)
{
  ClutterActor *stage;

  g_return_if_fail (SHELL_IS_GLOBAL (global));
  g_return_if_fail (global->plugin == NULL);

  global->plugin = plugin;
  global->wm = shell_wm_new (plugin);

  stage = mutter_plugin_get_stage (plugin);

  g_signal_connect (stage, "notify::width",
                    G_CALLBACK (global_stage_notify_width), global);
  g_signal_connect (stage, "notify::height",
                    G_CALLBACK (global_stage_notify_height), global);
  update_screen_size (global);

  g_signal_connect (stage, "paint",
                    G_CALLBACK (global_stage_before_paint), global);
  g_signal_connect_after (stage, "paint",
                          G_CALLBACK (global_stage_after_paint), global);

  shell_perf_log_define_event (shell_perf_log_get_default(),
                               "clutter.stagePaintStart",
                               "Start of stage page repaint",
                               "");
  shell_perf_log_define_event (shell_perf_log_get_default(),
                               "clutter.stagePaintDone",
                               "End of stage page repaint",
                               "");
}

void
_shell_global_set_gjs_context (ShellGlobal *global,
                               GjsContext  *context)
{
  global->js_context = context;
}

/**
 * shell_global_begin_modal:
 * @global: a #ShellGlobal
 *
 * Grabs the keyboard and mouse to the stage window. The stage will
 * receive all keyboard and mouse events until shell_global_end_modal()
 * is called. This is used to implement "modes" for the shell, such as the
 * overview mode or the "looking glass" debug overlay, that block
 * application and normal key shortcuts.
 *
 * Returns value: %TRUE if we succesfully entered the mode. %FALSE if we couldn't
 *  enter the mode. Failure may occur because an application has the pointer
 *  or keyboard grabbed, because Mutter is in a mode itself like moving a
 *  window or alt-Tab window selection, or because shell_global_begin_modal()
 *  was previouly called.
 */
gboolean
shell_global_begin_modal (ShellGlobal *global,
                          guint32      timestamp)
{
  ClutterStage *stage = CLUTTER_STAGE (mutter_plugin_get_stage (global->plugin));
  Window stagewin = clutter_x11_get_stage_window (stage);

  return mutter_plugin_begin_modal (global->plugin, stagewin, None, 0, timestamp);
}

/**
 * shell_global_end_modal:
 * @global: a #ShellGlobal
 *
 * Undoes the effect of shell_global_begin_modal().
 */
void
shell_global_end_modal (ShellGlobal *global,
                        guint32      timestamp)
{
  mutter_plugin_end_modal (global->plugin, timestamp);
}

/**
 * shell_global_display_is_grabbed
 * @global: a #ShellGlobal
 *
 * Determines whether Mutter currently has a grab (keyboard or mouse or
 * both) on the display. This could be the result of a current window
 * management operation like a window move, or could be from
 * shell_global_begin_modal().
 *
 * This function is useful to for ad-hoc checks to avoid over-grabbing
 * the Mutter grab a grab from GTK+. Longer-term we might instead want a
 * mechanism to make Mutter use GDK grabs instead of raw XGrabPointer().
 *
 * Return value: %TRUE if Mutter has a grab on the display
 */
gboolean
shell_global_display_is_grabbed (ShellGlobal *global)
{
  MetaScreen *screen = mutter_plugin_get_screen (global->plugin);
  MetaDisplay *display = meta_screen_get_display (screen);

  return meta_display_get_grab_op (display) != META_GRAB_OP_NONE;
}

/**
 * shell_global_add_extension_importer:
 * @target_object_script: JavaScript code evaluating to a target object
 * @target_property: Name of property to use for importer
 * @directory: Source directory:
 * @error: A #GError
 *
 * This function sets a property named @target_property on the object
 * resulting from the evaluation of @target_object_script code, which
 * acts as a GJS importer for directory @directory.
 *
 * Returns: %TRUE on success
 */
gboolean
shell_global_add_extension_importer (ShellGlobal *global,
                                     const char  *target_object_script,
                                     const char  *target_property,
                                     const char  *directory,
                                     GError     **error)
{
  jsval target_object;
  JSObject *importer;
  JSContext *context = gjs_context_get_native_context (global->js_context);
  char *search_path[2] = { 0, 0 };

  // This is a bit of a hack; ideally we'd be able to pass our target
  // object directly into this function, but introspection doesn't
  // support that at the moment.  Instead evaluate a string to get it.
  if (!JS_EvaluateScript(context,
                         JS_GetGlobalObject(context),
                         target_object_script,
                         strlen (target_object_script),
                         "<target_object_script>",
                         0,
                         &target_object))
    {
      char *message;
      gjs_log_exception(context,
                        &message);
      g_set_error(error,
                  G_IO_ERROR,
                  G_IO_ERROR_FAILED,
                  "%s", message ? message : "(unknown)");
      g_free(message);
      return FALSE;
    }

  if (!JSVAL_IS_OBJECT (target_object))
    {
      g_error ("shell_global_add_extension_importer: invalid target object");
      return FALSE;
    }

  search_path[0] = (char*)directory;
  importer = gjs_define_importer (context, JSVAL_TO_OBJECT (target_object), target_property, (const char **)search_path, FALSE);
  return TRUE;
}

/* Code to close all file descriptors before we exec; copied from gspawn.c in GLib.
 *
 * Authors: Padraig O'Briain, Matthias Clasen, Lennart Poettering
 *
 * http://bugzilla.gnome.org/show_bug.cgi?id=469231
 * http://bugzilla.gnome.org/show_bug.cgi?id=357585
 */

static int
set_cloexec (void *data, gint fd)
{
  if (fd >= GPOINTER_TO_INT (data))
    fcntl (fd, F_SETFD, FD_CLOEXEC);

  return 0;
}

#ifndef HAVE_FDWALK
static int
fdwalk (int (*cb)(void *data, int fd), void *data)
{
  gint open_max;
  gint fd;
  gint res = 0;

#ifdef HAVE_SYS_RESOURCE_H
  struct rlimit rl;
#endif

#ifdef __linux__
  DIR *d;

  if ((d = opendir("/proc/self/fd"))) {
      struct dirent *de;

      while ((de = readdir(d))) {
          glong l;
          gchar *e = NULL;

          if (de->d_name[0] == '.')
              continue;

          errno = 0;
          l = strtol(de->d_name, &e, 10);
          if (errno != 0 || !e || *e)
              continue;

          fd = (gint) l;

          if ((glong) fd != l)
              continue;

          if (fd == dirfd(d))
              continue;

          if ((res = cb (data, fd)) != 0)
              break;
        }

      closedir(d);
      return res;
  }

  /* If /proc is not mounted or not accessible we fall back to the old
   * rlimit trick */

#endif

#ifdef HAVE_SYS_RESOURCE_H
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_max != RLIM_INFINITY)
      open_max = rl.rlim_max;
  else
#endif
      open_max = sysconf (_SC_OPEN_MAX);

  for (fd = 0; fd < open_max; fd++)
      if ((res = cb (data, fd)) != 0)
          break;

  return res;
}
#endif

static void
pre_exec_close_fds(void)
{
  fdwalk (set_cloexec, GINT_TO_POINTER(3));
}

/**
 * shell_global_reexec_self:
 * @global: A #ShellGlobal
 * 
 * Restart the current process.  Only intended for development purposes. 
 */
void 
shell_global_reexec_self (ShellGlobal *global)
{
  GPtrArray *arr;
  gsize len;
  char *buf;
  char *buf_p;
  char *buf_end;
  GError *error = NULL;
  
  /* Linux specific (I think, anyways). */
  if (!g_file_get_contents ("/proc/self/cmdline", &buf, &len, &error))
    {
      g_warning ("failed to get /proc/self/cmdline: %s", error->message);
      return;
    }
      
  buf_end = buf+len;
  arr = g_ptr_array_new ();
  /* The cmdline file is NUL-separated */
  for (buf_p = buf; buf_p < buf_end; buf_p = buf_p + strlen (buf_p) + 1)
    g_ptr_array_add (arr, buf_p);
  
  g_ptr_array_add (arr, NULL);

  /* Close all file descriptors other than stdin/stdout/stderr, otherwise
   * they will leak and stay open after the exec. In particular, this is
   * important for file descriptors that represent mapped graphics buffer
   * objects.
   */
  pre_exec_close_fds ();

  execvp (arr->pdata[0], (char**)arr->pdata);
  g_warning ("failed to reexec: %s", g_strerror (errno));
  g_ptr_array_free (arr, TRUE);
}

/**
 * shell_global_breakpoint:
 * @global: A #ShellGlobal
 *
 * Using G_BREAKPOINT(), interrupt the current process.  This is useful
 * in conjunction with a debugger such as gdb.
 */
void
shell_global_breakpoint (ShellGlobal *global)
{
  G_BREAKPOINT ();
}

/**
 * shell_global_gc:
 * @global: A #ShellGlobal
 *
 * Start a garbage collection process.  For more information, see
 * https://developer.mozilla.org/En/JS_GC
 */
void
shell_global_gc (ShellGlobal *global)
{
  JSContext *context = gjs_context_get_native_context (global->js_context);

  JS_GC (context);
}

/**
 * shell_global_maybe_gc:
 * @global: A #ShellGlobal
 *
 * Start a garbage collection process when it would free up enough memory
 * to be worth the amount of time it would take
 * https://developer.mozilla.org/en/SpiderMonkey/JSAPI_Reference/JS_MaybeGC
 */
void
shell_global_maybe_gc (ShellGlobal *global)
{
  JSContext *context = gjs_context_get_native_context (global->js_context);

  JS_MaybeGC (context);
}

void
shell_global_grab_dbus_service (ShellGlobal *global)
{
  GError *error = NULL;
  DBusGConnection *session;
  DBusGProxy *bus;
  guint32 request_name_result;
  
  session = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);
  
  bus = dbus_g_proxy_new_for_name (session,
                                   DBUS_SERVICE_DBUS,
                                   DBUS_PATH_DBUS,
                                   DBUS_INTERFACE_DBUS);
  
  if (!dbus_g_proxy_call (bus, "RequestName", &error,
                          G_TYPE_STRING, SHELL_DBUS_SERVICE,
                          G_TYPE_UINT, 0,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID)) 
    {
      g_print ("failed to acquire org.gnome.Shell: %s\n", error->message);
      /* If we somehow got started again, it's not an error to be running
       * already.  So just exit 0.
       */
      exit (0);  
    }

  /* Also grab org.gnome.Panel to replace any existing panel process,
   * unless a special environment variable is passed.  The environment
   * variable is used by the gnome-shell (no --replace) launcher in
   * Xephyr */
  if (!g_getenv ("GNOME_SHELL_NO_REPLACE"))
    {
      if (!dbus_g_proxy_call (bus, "RequestName", &error, G_TYPE_STRING,
                              "org.gnome.Panel", G_TYPE_UINT,
                              DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE,
                              G_TYPE_INVALID, G_TYPE_UINT,
                              &request_name_result, G_TYPE_INVALID))
        {
          g_print ("failed to acquire org.gnome.Panel: %s\n", error->message);
          exit (1);
        }
    }

  /* ...and the org.gnome.Magnifier service.
   */
  if (!dbus_g_proxy_call (bus, "RequestName", &error,
                          G_TYPE_STRING, MAGNIFIER_DBUS_SERVICE,
                          G_TYPE_UINT, 0,
                          G_TYPE_INVALID,
                          G_TYPE_UINT, &request_name_result,
                          G_TYPE_INVALID))
    {
      g_print ("failed to acquire %s: %s\n", MAGNIFIER_DBUS_SERVICE, error->message);
      /* Failing to acquire the magnifer service is not fatal.  Log the error,
       * but keep going. */
    }
  g_object_unref (bus);
}

static void
grab_notify (GtkWidget *widget, gboolean was_grabbed, gpointer user_data)
{
  ShellGlobal *global = SHELL_GLOBAL (user_data);
  
  global->gtk_grab_active = !was_grabbed;

  /* Update for the new setting of gtk_grab_active */
  shell_global_set_stage_input_mode (global, global->input_mode);
}

/*
 * Updates the global->root_pixmap actor with the root window's pixmap or fails
 * with a warning.
 */
static void
update_root_window_pixmap (ShellGlobal *global)
{
  Atom type;
  int format;
  gulong nitems;
  gulong bytes_after;
  guchar *data;
  Pixmap root_pixmap_id = None;

  if (!XGetWindowProperty (gdk_x11_get_default_xdisplay (),
                           gdk_x11_get_default_root_xwindow (),
                           gdk_x11_get_xatom_by_name ("_XROOTPMAP_ID"),
                           0, LONG_MAX,
                           False,
                           AnyPropertyType,
                           &type, &format, &nitems, &bytes_after, &data) &&
      type != None)
  {
     /* Got a property. */
     if (type == XA_PIXMAP && format == 32 && nitems == 1)
       {
         /* Was what we expected. */
         root_pixmap_id = *(Pixmap *)data;
       }
     else
       {
         g_warning ("Could not get the root window pixmap");
       }

     XFree(data);
  }

  clutter_x11_texture_pixmap_set_pixmap (CLUTTER_X11_TEXTURE_PIXMAP (global->root_pixmap),
                                         root_pixmap_id);
}

/*
 * Called when the X server emits a root window change event. If the event is
 * about a new pixmap, update the global->root_pixmap actor.
 */
static GdkFilterReturn
root_window_filter (GdkXEvent *native, GdkEvent *event, gpointer data)
{
  XEvent *xevent = (XEvent *)native;

  if ((xevent->type == PropertyNotify) &&
      (xevent->xproperty.window == gdk_x11_get_default_root_xwindow ()) &&
      (xevent->xproperty.atom == gdk_x11_get_xatom_by_name ("_XROOTPMAP_ID")))
    update_root_window_pixmap (SHELL_GLOBAL (data));

  return GDK_FILTER_CONTINUE;
}

/*
 * Called when the root window pixmap actor is destroyed.
 */
static void
root_pixmap_destroy (GObject *sender, gpointer data)
{
  ShellGlobal *global = SHELL_GLOBAL (data);

  gdk_window_remove_filter (gdk_get_default_root_window (),
                            root_window_filter, global);
  global->root_pixmap = NULL;
}

/**
 * shell_global_format_time_relative_pretty:
 * @global:
 * @delta: Time in seconds since the current time
 * @text: (out): Relative human-consumption-only time string
 * @next_update: (out): Time in seconds until we should redisplay the time
 *
 * Format a time value for human consumption only.  The passed time
 * value is a delta in terms of seconds from the current time.
 * This function needs to be in C because of its use of ngettext() which
 * is not accessible from JavaScript.
 */
void
shell_global_format_time_relative_pretty (ShellGlobal *global,
                                          guint        delta,
                                          char       **text,
                                          guint       *next_update)
{
#define MINUTE (60)
#define HOUR (MINUTE*60)
#define DAY (HOUR*24)
#define WEEK (DAY*7)
  if (delta < MINUTE) {
    *text = g_strdup (_("Less than a minute ago"));
    *next_update = MINUTE - delta;
   } else if (delta < HOUR) {
     *text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                         "%d minute ago", "%d minutes ago",
                                         delta / MINUTE), delta / MINUTE);
     *next_update = MINUTE - (delta % MINUTE);
   } else if (delta < DAY) {
     *text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                         "%d hour ago", "%d hours ago",
                                         delta / HOUR), delta / HOUR);
     *next_update = HOUR - (delta % HOUR);
   } else if (delta < WEEK) {
     *text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                         "%d day ago", "%d days ago",
                                         delta / DAY), delta / DAY);
     *next_update = DAY - (delta % DAY);
   } else {
     *text = g_strdup_printf (dngettext (GETTEXT_PACKAGE,
                                         "%d week ago", "%d weeks ago",
                                         delta / WEEK), delta / WEEK);
     *next_update = WEEK - (delta % WEEK);
   }
}

/**
 * shell_global_create_root_pixmap_actor:
 * @global: a #ShellGlobal
 *
 * Creates an actor showing the root window pixmap.
 *
 * Return value: (transfer none): a #ClutterActor with the root window pixmap.
 *               The actor is floating, hence (transfer none).
 */
ClutterActor *
shell_global_create_root_pixmap_actor (ShellGlobal *global)
{
  GdkWindow *window;
  ClutterActor *stage;
  ClutterColor stage_color;

  /* The actor created is actually a ClutterClone of global->root_pixmap. */

  if (global->root_pixmap == NULL)
    {
      global->root_pixmap = clutter_glx_texture_pixmap_new ();

      clutter_actor_set_size (CLUTTER_ACTOR (global->root_pixmap),
                              global->last_change_screen_width,
                              global->last_change_screen_height);

      clutter_texture_set_repeat (CLUTTER_TEXTURE (global->root_pixmap),
                                  TRUE, TRUE);

      /* The low and medium quality filters give nearest-neighbor resizing. */
      clutter_texture_set_filter_quality (CLUTTER_TEXTURE (global->root_pixmap),
                                          CLUTTER_TEXTURE_QUALITY_HIGH);

      /* Initialize to the stage color, since that's what will be seen
       * in the main view if there's no actual background window.
       */
      stage = mutter_plugin_get_stage (global->plugin);
      clutter_stage_get_color (CLUTTER_STAGE (stage), &stage_color);
      clutter_texture_set_from_rgb_data (CLUTTER_TEXTURE (global->root_pixmap),
                                         /* ClutterColor has the same layout
                                          * as one pixel of RGB(A) data.
                                          */
                                         (const guchar *)&stage_color, FALSE,
                                         /* w, h, rowstride, bpp, flags */
                                         1, 1, 3, 3, 0, NULL);

      /* We can only clone an actor within a stage, so we hide the source
       * texture then add it to the stage */
      clutter_actor_hide (global->root_pixmap);
      clutter_container_add_actor (CLUTTER_CONTAINER (stage),
                                   global->root_pixmap);

      /* This really should never happen; but just in case... */
      g_signal_connect (global->root_pixmap, "destroy",
                        G_CALLBACK (root_pixmap_destroy), global);

      /* Metacity handles changes to some root window properties in its global
       * event filter, though not _XROOTPMAP_ID. For all root window property
       * changes, the global filter returns GDK_FILTER_CONTINUE, so our
       * window specific filter will be called after the global one.
       *
       * Because Metacity is already handling root window property updates,
       * we don't have to worry about adding the PropertyChange mask to the
       * root window to get PropertyNotify events.
       */
      window = gdk_get_default_root_window ();
      gdk_window_add_filter (window, root_window_filter, global);

      update_root_window_pixmap (global);
    }

  return clutter_clone_new (global->root_pixmap);
}

/**
 * shell_global_get_monitors:
 * @global: the #ShellGlobal
 *
 * Gets a list of the bounding boxes of the active screen's monitors.
 *
 * Return value: (transfer full) (element-type GdkRectangle): a list
 * of monitor bounding boxes.
 */
GSList *
shell_global_get_monitors (ShellGlobal *global)
{
  MetaScreen *screen = shell_global_get_screen (global);
  GSList *monitors = NULL;
  MetaRectangle rect;
  int i;

  g_assert (sizeof (MetaRectangle) == sizeof (GdkRectangle) &&
            G_STRUCT_OFFSET (MetaRectangle, x) == G_STRUCT_OFFSET (GdkRectangle, x) &&
            G_STRUCT_OFFSET (MetaRectangle, y) == G_STRUCT_OFFSET (GdkRectangle, y) &&
            G_STRUCT_OFFSET (MetaRectangle, width) == G_STRUCT_OFFSET (GdkRectangle, width) &&
            G_STRUCT_OFFSET (MetaRectangle, height) == G_STRUCT_OFFSET (GdkRectangle, height));

  for (i = meta_screen_get_n_monitors (screen) - 1; i >= 0; i--)
    {
      meta_screen_get_monitor_geometry (screen, i, &rect);
      monitors = g_slist_prepend (monitors,
                                  g_boxed_copy (GDK_TYPE_RECTANGLE, &rect));
    }
  return monitors;
}

/**
 * shell_global_get_primary_monitor:
 * @global: the #ShellGlobal
 *
 * Gets the bounding box of the primary monitor (the one that the
 * panel is on).
 *
 * Return value: the bounding box of the primary monitor
 */
GdkRectangle *
shell_global_get_primary_monitor (ShellGlobal  *global)
{
  GdkScreen *screen = gdk_screen_get_default ();
  GdkRectangle rect;
  gint primary = 0;

  /* gdk_screen_get_primary_monitor is only present in gtk-2.20+
   * and is in a useable state (supports heuristics and fallback modes)
   * starting with 2.20.1
   */
#if !GTK_CHECK_VERSION (2, 20, 1)
  gint i;
  gchar *output_name = NULL;
  gint num_monitors = gdk_screen_get_n_monitors (screen);

  for (i = 0; i < num_monitors; i++)
    {
      /* Prefer the laptop's internal screen if present */
      output_name = gdk_screen_get_monitor_plug_name (screen, i);
      if (output_name)
        {
          gboolean is_lvds = g_ascii_strncasecmp (output_name, "LVDS", 4) == 0;
          g_free (output_name);
          if (is_lvds)
            {
              primary = i;
              break;
            }
        }
    }
#else
  primary = gdk_screen_get_primary_monitor (screen);
#endif

  gdk_screen_get_monitor_geometry (screen, primary, &rect);

  return g_boxed_copy (GDK_TYPE_RECTANGLE, &rect);
}

/**
 * shell_global_get_focus_monitor:
 * @global: the #ShellGlobal
 *
 * Gets the bounding box of the monitor containing the window that
 * currently contains the keyboard focus.
 *
 * Return value: the bounding box of the focus monitor
 */
GdkRectangle *
shell_global_get_focus_monitor (ShellGlobal  *global)
{
  MetaScreen *screen = shell_global_get_screen (global);
  MetaDisplay *display = meta_screen_get_display (screen);
  MetaWindow *focus = meta_display_get_focus_window (display);
  MetaRectangle rect, wrect;
  int nmonitors, i;

  if (focus)
    {
      meta_window_get_outer_rect (focus, &wrect);
      nmonitors = meta_screen_get_n_monitors (screen);

      /* Find the monitor that the top-left corner of @focus is on. */
      for (i = 0; i < nmonitors; i++)
        {
          meta_screen_get_monitor_geometry (screen, i, &rect);

          if (rect.x <= wrect.x && rect.y <= wrect.y &&
              rect.x + rect.width > wrect.x &&
              rect.y + rect.height > wrect.y)
            return g_boxed_copy (GDK_TYPE_RECTANGLE, &rect);
        }
    }

  meta_screen_get_monitor_geometry (screen, 0, &rect);
  return g_boxed_copy (GDK_TYPE_RECTANGLE, &rect);
}

/**
 * shell_global_get_pointer:
 * @global: the #ShellGlobal
 * @x: (out): the X coordinate of the pointer, in global coordinates
 * @y: (out): the Y coordinate of the pointer, in global coordinates
 * @mods: (out): the current set of modifier keys that are pressed down
 *
 * Gets the pointer coordinates and current modifier key state.
 * This is a wrapper around gdk_display_get_pointer() that strips
 * out any un-declared modifier flags, to make gjs happy; see
 * https://bugzilla.gnome.org/show_bug.cgi?id=597292.
 */
void
shell_global_get_pointer (ShellGlobal         *global,
                          int                 *x,
                          int                 *y,
                          ClutterModifierType *mods)
{
  GdkModifierType raw_mods;

  gdk_display_get_pointer (gdk_display_get_default (), NULL, x, y, &raw_mods);
  *mods = raw_mods & GDK_MODIFIER_MASK;
}

/**
 * shell_get_event_state:
 * @event: a #ClutterEvent
 *
 * Gets the current state of the event (the set of modifier keys that
 * are pressed down). Thhis is a wrapper around
 * clutter_event_get_state() that strips out any un-declared modifier
 * flags, to make gjs happy; see
 * https://bugzilla.gnome.org/show_bug.cgi?id=597292.
 *
 * Return value: the state from the event
 */
ClutterModifierType
shell_get_event_state (ClutterEvent *event)
{
  ClutterModifierType state = clutter_event_get_state (event);
  return state & CLUTTER_MODIFIER_MASK;
}

static void
shell_popup_menu_position_func (GtkMenu   *menu,
                                int       *x,
                                int       *y,
                                gboolean  *push_in,
                                gpointer   user_data)
{
  *x = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu), "shell-menu-x"));
  *y = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (menu), "shell-menu-y"));
}

/**
 * shell_popup_menu:
 * @menu: a #GtkMenu
 * @button: mouse button that triggered the menu
 * @time: timestamp of event that triggered the menu
 * @menu_x: x coordinate to display the menu at
 * @menu_y: y coordinate to display the menu at
 *
 * Wraps gtk_menu_popup(), but using @menu_x, @menu_y for the location
 * rather than needing a callback.
 **/
void
shell_popup_menu (GtkMenu *menu, int button, guint32 time,
                  int menu_x, int menu_y)
{
  g_object_set_data (G_OBJECT (menu), "shell-menu-x", GINT_TO_POINTER (menu_x));
  g_object_set_data (G_OBJECT (menu), "shell-menu-y", GINT_TO_POINTER (menu_y));

  gtk_menu_popup (menu, NULL, NULL, shell_popup_menu_position_func, NULL,
                  button, time);
}

/**
 * shell_write_string_to_stream:
 * @stream: a #GOutputStream
 * @str: a UTF-8 string to write to @stream
 * @error: location to store GError
 *
 * Write a string to a GOutputStream as UTF-8. This is a workaround
 * for not having binary buffers in GJS.
 *
 * Return value: %TRUE if write succeeded
 */
gboolean
shell_write_string_to_stream (GOutputStream *stream,
                              const char    *str,
                              GError       **error)
{
  return g_output_stream_write_all (stream, str, strlen (str),
                                    NULL, NULL, error);
}

/**
 * shell_global_get_current_time:
 * @global: A #ShellGlobal
 *
 * Returns: the current X server time from the current Clutter, Gdk, or X
 * event. If called from outside an event handler, this may return
 * %Clutter.CURRENT_TIME (aka 0), or it may return a slightly
 * out-of-date timestamp.
 */
guint32
shell_global_get_current_time (ShellGlobal *global)
{
  guint32 time;
  MetaDisplay *display;

  /* meta_display_get_current_time() will return the correct time
     when handling an X or Gdk event, but will return CurrentTime
     from some Clutter event callbacks.

     clutter_get_current_event_time() will return the correct time
     from a Clutter event callback, but may return an out-of-date
     timestamp if called at other times.

     So we try meta_display_get_current_time() first, since we
     can recognize a "wrong" answer from that, and then fall back
     to clutter_get_current_event_time().
   */

  display = meta_screen_get_display (shell_global_get_screen (global));
  time = meta_display_get_current_time (display);
  if (time != CLUTTER_CURRENT_TIME)
      return time;

  return clutter_get_current_event_time ();
}

/**
 * shell_global_get_app_launch_context:
 * @global: A #ShellGlobal
 *
 * Create a #GAppLaunchContext set up with the correct timestamp, and
 * targeted to activate on the current workspace.
 *
 * Return value: A new #GAppLaunchContext
 */
GAppLaunchContext *
shell_global_create_app_launch_context (ShellGlobal *global)
{
  GdkAppLaunchContext *context;

  context = gdk_app_launch_context_new ();
  gdk_app_launch_context_set_timestamp (context, shell_global_get_current_time (global));

  // Make sure that the app is opened on the current workspace even if
  // the user switches before it starts
  gdk_app_launch_context_set_desktop (context, meta_screen_get_active_workspace_index (shell_global_get_screen (global)));

  return (GAppLaunchContext *)context;
}

/**
 * shell_global_set_property_mutable:
 * @global: the #ShellGlobal
 * @object: the "path" to a JS object, starting from the root object.
 *  (Eg, "global.stage" or "imports.gi.Gtk.Window.prototype")
 * @property: a property on @object
 * @mutable: %TRUE or %FALSE
 *
 * If @mutable is %TRUE, this clears the "permanent" and "readonly" flags
 * on @property of @object. If @mutable is %FALSE, it sets them.
 *
 * You can use this to make it possible to modify properties that
 * would otherwise be read-only from JavaScript.
 *
 * Return value: success or failure.
 */
gboolean
shell_global_set_property_mutable (ShellGlobal *global,
                                   const char  *object,
                                   const char  *property,
                                   gboolean     mutable)
{
  JSContext *context = gjs_context_get_native_context (global->js_context);
  char **parts;
  JSObject *obj;
  jsval val = JSVAL_VOID;
  int i;
  jsuint attrs;
  JSBool found;

  JS_AddRoot (context, &val);

  parts = g_strsplit (object, ".", -1);
  obj = JS_GetGlobalObject (context);
  for (i = 0; parts[i]; i++)
    {
      if (!JS_GetProperty (context, obj, parts[i], &val))
        {
          g_strfreev (parts);
          JS_RemoveRoot (context, &val);
          gjs_log_exception (context, NULL);
          return FALSE;
        }
      obj = JSVAL_TO_OBJECT (val);
    }
  g_strfreev (parts);

  if (!JS_GetPropertyAttributes (context, obj, property, &attrs, &found) || !found)
    {
      JS_RemoveRoot (context, &val);
      gjs_log_exception (context, NULL);
      return FALSE;
    }

  if (mutable)
    attrs &= ~(JSPROP_PERMANENT | JSPROP_READONLY);
  else
    attrs |= (JSPROP_PERMANENT | JSPROP_READONLY);

  JS_SetPropertyAttributes (context, obj, property, attrs, &found);

  JS_RemoveRoot (context, &val);
  return !gjs_log_exception (context, NULL);
}

typedef struct
{
  ShellLeisureFunction func;
  gpointer user_data;
  GDestroyNotify notify;
} LeisureClosure;

static gboolean
run_leisure_functions (gpointer data)
{
  ShellGlobal *global = data;
  GSList *closures;
  GSList *iter;

  global->leisure_function_id = 0;

  /* We started more work since we scheduled the idle */
  if (global->work_count > 0)
    return FALSE;

  /*
   * We do call MAYBE_GC() here to free up some memory and
   * prevent the GC from running when we are busy doing other things.
   */
  shell_global_maybe_gc (global);

  /* No leisure closures, so we are done */
  if (global->leisure_closures == NULL)
    return FALSE;

  closures = global->leisure_closures;
  global->leisure_closures = NULL;

  for (iter = closures; iter; iter = iter->next)
    {
      LeisureClosure *closure = closures->data;
      closure->func (closure->user_data);

      if (closure->notify)
        closure->notify (closure->user_data);

      g_slice_free (LeisureClosure, closure);
    }

  g_slist_free (closures);

  return FALSE;
}

static void
schedule_leisure_functions (ShellGlobal *global)
{
  /* This is called when we think we are ready to run leisure functions
   * by our own accounting. We try to handle other types of business
   * (like ClutterAnimation) by adding a low priority idle function.
   *
   * This won't work properly if the mainloop goes idle waiting for
   * the vertical blanking interval or waiting for work being done
   * in another thread.
   */
  if (!global->leisure_function_id)
    global->leisure_function_id = g_idle_add_full (G_PRIORITY_LOW,
                                                   run_leisure_functions,
                                                   global, NULL);
}

/**
 * shell_global_begin_work:
 * @global: the #ShellGlobal
 *
 * Marks that we are currently doing work. This is used to to track
 * whether we are busy for the purposes of shell_global_run_at_leisure().
 * A count is kept and shell_global_end_work() must be called exactly
 * as many times as shell_global_begin_work().
 */
void
shell_global_begin_work (ShellGlobal *global)
{
  global->work_count++;
}

/**
 * shell_global_end_work:
 * @global: the #ShellGlobal
 *
 * Marks the end of work that we started with shell_global_begin_work().
 * If no other work is ongoing and functions have been added with
 * shell_global_run_at_leisure(), they will be run at the next
 * opportunity.
 */
void
shell_global_end_work (ShellGlobal *global)
{
  g_return_if_fail (global->work_count > 0);

  global->work_count--;
  if (global->work_count == 0)
    schedule_leisure_functions (global);

}

/**
 * shell_global_run_at_leisure:
 * @global: the #ShellGlobal
 * @func: function to call at leisure
 * @user_data: data to pass to @func
 * @notify: function to call to free @user_data
 *
 * Schedules a function to be called the next time the shell is idle.
 * Idle means here no animations, no redrawing, and no ongoing background
 * work. Since there is currently no way to hook into the Clutter master
 * clock and know when is running, the implementation here is somewhat
 * approximation. Animations done through the shell's Tweener module will
 * be handled properly, but other animations may be detected as terminating
 * early if they can be drawn fast enough so that the event loop goes idle
 * between frames.
 *
 * The intent of this function is for performance measurement runs
 * where a number of actions should be run serially and each action is
 * timed individually. Using this function for other purposes will
 * interfere with the ability to use it for performance measurement so
 * should be avoided.
 */
void
shell_global_run_at_leisure (ShellGlobal         *global,
                             ShellLeisureFunction func,
                             gpointer             user_data,
                             GDestroyNotify       notify)
{
  LeisureClosure *closure = g_slice_new (LeisureClosure);
  closure->func = func;
  closure->user_data = user_data;
  closure->notify = notify;

  global->leisure_closures = g_slist_append (global->leisure_closures,
                                             closure);

  if (global->work_count == 0)
    schedule_leisure_functions (global);
}

/**
 * GetTpContactsCb:
 * @connection: The connection
 * @contacts: (element-type TelepathyGLib.Contact): List of contacts
 * @failed: Array of failed contacts
 */

/**
 * shell_global_get_tp_contacts_cb: (skip)
 */

static void
shell_global_get_tp_contacts_cb (TpConnection *self,
                                 guint n_contacts,
                                 TpContact * const *contacts,
                                 guint n_failed,
                                 const TpHandle *failed,
                                 const GError *error,
                                 gpointer callback,
                                 GObject *weak_object)
{
    int i;
    GList *contact_list = NULL;
    for(i = 0; i < n_contacts; i++) {
        contact_list = g_list_append(contact_list, contacts[i]);
    }

    TpHandle* failed_list = g_new0 (TpHandle, n_failed+1);
    memcpy(failed_list, failed, n_failed);
    
    ((GetTpContactsCb)callback)(self, contact_list, failed_list);
}


/**
 * shell_global_get_tp_contacts:
 * @self: A connection, which must be ready
 * @n_handles: Number of handles in handles
 * @handles: (array length=n_handles) (element-type uint): Array of handles
 * @n_features: Number of features in features
 * @features: (array length=n_features) (allow-none) (element-type uint):
 *  Array of features
 * @callback: User callback to run when the contacts are ready
 * @user_data: User data
 * @destroy: Called to destroy user_data
 * @weak_object: (allow-none): An object passed to the callback
 *
 * Wrap tp_connection_get_contacts_by_handle so we can transform the array
 * into a null-terminated one, which gjs can handle
 * We send the original callback to get_contacts_by_handle as
 * user_data, and we have our own function as callback, which does the
 * transforming. 
 */
void
shell_global_get_tp_contacts (TpConnection *self,
                              guint n_handles,
                              const TpHandle *handles,
                              guint n_features,
                              const TpContactFeature *features,
                              GetTpContactsCb callback,
                              gpointer user_data,
                              GDestroyNotify destroy,
                              GObject *weak_object)
{
    tp_connection_get_contacts_by_handle(self, n_handles, handles, 
            n_features, features, shell_global_get_tp_contacts_cb, 
            callback, NULL, NULL);
}
