/*
 *  Copyright (c) 2008 Stephan Arts <stephan@xfce.org>
 *  Copyright (c) 2008 Jannis Pohlmann <jannis@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>
#include <xfconf/xfconf.h>

#include "appearance-dialog_ui.h"
#include "images.h"

#define XFWM4_DEFAULT_THEME             "Default"

#define INCH_MM      25.4

/* Use a fallback DPI of 96 which should be ok-ish on most systems
 * and is only applied on rare occasions */
#define FALLBACK_DPI 96

/* Increase this number if new gtk settings have been added */
#define INITIALIZE_UINT (1)

typedef struct _MenuTemplate     MenuTemplate;
struct _MenuTemplate
{
  const gchar *name;
  const gchar *value;
};

static const MenuTemplate title_align_values[] = {
  { N_("Left"), "left" },
  { N_("Center"), "center" },
  { N_("Right"), "right" },
  { NULL, NULL },
};

enum
{
  XFWM4_THEME_COLUMN_NAME,
  XFWM4_THEME_COLUMN_RC,
  N_XFWM4_THEME_COLUMNS
};

enum
{
    COLUMN_THEME_NAME,
    COLUMN_THEME_DISPLAY_NAME,
    COLUMN_THEME_COMMENT,
    N_THEME_COLUMNS
};

enum
{
    COLUMN_RGBA_PIXBUF,
    COLUMN_RGBA_NAME,
    N_RGBA_COLUMNS
};

/* String arrays with the settings in combo boxes */
static const gchar* toolbar_styles_array[] =
{
    "icons", "text", "both", "both-horiz"
};

static const gchar* xft_hint_styles_array[] =
{
    "hintnone", "hintslight", "hintmedium", "hintfull"
};

static const gchar* xft_rgba_array[] =
{
    "none", "rgb", "bgr", "vrgb", "vbgr"
};

static const GtkTargetEntry theme_drop_targets[] =
{
  { "text/uri-list", 0, 0 }
};

/* Option entries */
static GdkNativeWindow opt_socket_id = 0;
static gboolean opt_version = FALSE;
static GOptionEntry option_entries[] =
{
    { "socket-id", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_socket_id, N_("Settings manager socket"), N_("SOCKET ID") },
    { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version, N_("Version information"), NULL },
    { NULL }
};

static void
xfwm_settings_active_frame_drag_data (GtkWidget        *widget,
                                      GdkDragContext   *drag_context,
                                      gint              x,
                                      gint              y,
                                      GtkSelectionData *data,
                                      guint             info,
                                      guint             timestamp,
                                      gpointer         *user_data);

static GdkPixbuf *
xfwm_settings_create_icon_from_widget (GtkWidget *widget);

static void
cb_xfwm_title_button_alignment_changed (GtkComboBox *combo,
                                        GtkWidget   *button);
static void
cb_xfwm_title_button_drag_data (GtkWidget        *widget,
                                GdkDragContext   *drag_context,
                                GtkSelectionData *data,
                                guint             info,
                                guint             timestamp);
static void
cb_xfwm_title_button_drag_begin (GtkWidget      *widget,
                                 GdkDragContext *drag_context);
static void
cb_xfwm_title_button_drag_end (GtkWidget      *widget,
                               GdkDragContext *drag_context);
static gboolean
cb_appearance_settings_signal_blocker (GtkWidget *widget);

/* Global xfconf channels */
static XfconfChannel *xsettings_channel;
static XfconfChannel *xfwm4_channel;

static int
compute_xsettings_dpi (GtkWidget *widget)
{
    GdkScreen *screen;
    int width_mm, height_mm;
    int width, height;
    int dpi;

    screen = gtk_widget_get_screen (widget);
    width_mm = gdk_screen_get_width_mm (screen);
    height_mm = gdk_screen_get_height_mm (screen);
    dpi = FALLBACK_DPI;

    if (width_mm > 0 && height_mm > 0)
    {
        width = gdk_screen_get_width (screen);
        height = gdk_screen_get_height (screen);
        dpi = MIN (INCH_MM * width  / width_mm,
                   INCH_MM * height / height_mm);
    }

    return dpi;
}

static void
cb_xfwm_theme_tree_selection_changed (GtkTreeSelection *selection,
                                      GtkBuilder       *builder)
{
    GtkTreeModel *model;
    gboolean      has_selection;
    XfceRc       *rc;
    GtkWidget    *widget;
    gchar        *name;
    gchar        *filename;
    GtkTreeIter   iter;
    gboolean      button_layout = FALSE;
    gboolean      title_alignment = FALSE;

    /* Get the selected list iter */
    has_selection = gtk_tree_selection_get_selected (selection, &model, &iter);
    if (G_LIKELY (has_selection))
    {
        /* Get the theme name */
        gtk_tree_model_get (model, &iter,
                            XFWM4_THEME_COLUMN_NAME, &name,
                            XFWM4_THEME_COLUMN_RC, &filename, -1);

        /* Store the new theme */
        xfconf_channel_set_string (xfwm4_channel, "/general/theme", name);

        /* check in the rc if the theme supports a custom button layout and/or
         * title alignement */
        rc = xfce_rc_simple_open (filename, TRUE);
        g_free (filename);
        if (G_LIKELY (rc != NULL))
        {
            button_layout = !xfce_rc_has_entry (rc, "button_layout");
            title_alignment = !xfce_rc_has_entry (rc, "title_alignment");
            xfce_rc_close (rc);
        }

        /* Cleanup */
        g_free (name);
    }

    widget = GTK_WIDGET (gtk_builder_get_object (builder, "button_layout_box"));
    gtk_widget_set_sensitive (widget, button_layout);

    widget = GTK_WIDGET (gtk_builder_get_object (builder, "title_align_box"));
    gtk_widget_set_sensitive (widget, title_alignment);
}

static void
cb_theme_tree_selection_changed (GtkTreeSelection *selection,
                                 const gchar      *property)
{
    GtkTreeModel *model;
    gboolean      has_selection;
    gchar        *name;
    GtkTreeIter   iter;

    /* Get the selected list iter */
    has_selection = gtk_tree_selection_get_selected (selection, &model, &iter);
    if (G_LIKELY (has_selection))
    {
        /* Get the theme name */
        gtk_tree_model_get (model, &iter, COLUMN_THEME_NAME, &name, -1);

        /* Store the new theme */
        xfconf_channel_set_string (xsettings_channel, property, name);

        /* Cleanup */
        g_free (name);
    }
}

static void
cb_icon_theme_tree_selection_changed (GtkTreeSelection *selection)
{
    /* Set the new icon theme */
    cb_theme_tree_selection_changed (selection, "/Net/IconThemeName");
}

static void
cb_ui_theme_tree_selection_changed (GtkTreeSelection *selection)
{
    /* Set the new UI theme */
    cb_theme_tree_selection_changed (selection, "/Net/ThemeName");
}

static void
cb_toolbar_style_combo_changed (GtkComboBox *combo)
{
    gint active;

    /* Get active item, prevent number outside the array */
    active = CLAMP (gtk_combo_box_get_active (combo), 0, (gint) G_N_ELEMENTS (toolbar_styles_array));

    /* Save setting */
    xfconf_channel_set_string (xsettings_channel, "/Gtk/ToolbarStyle", toolbar_styles_array[active]);
}

static void
cb_antialias_check_button_toggled (GtkToggleButton *toggle)
{
    gint active;

    /* Don't allow an inconsistent button anymore */
    gtk_toggle_button_set_inconsistent (toggle, FALSE);

    /* Get active */
    active = gtk_toggle_button_get_active (toggle) ? 1 : 0;

    /* Save setting */
    xfconf_channel_set_int (xsettings_channel, "/Xft/Antialias", active);
}

static void
cb_hinting_style_combo_changed (GtkComboBox *combo)
{
    gint active;

    /* Get active item, prevent number outside the array */
    active = CLAMP (gtk_combo_box_get_active (combo), 0, (gint) G_N_ELEMENTS (xft_hint_styles_array));

    /* Save setting */
    xfconf_channel_set_string (xsettings_channel, "/Xft/HintStyle", xft_hint_styles_array[active]);
}

static void
cb_rgba_style_combo_changed (GtkComboBox *combo)
{
    gint active;

    /* Get active item, prevent number outside the array */
    active = CLAMP (gtk_combo_box_get_active (combo), 0, (gint) G_N_ELEMENTS (xft_rgba_array));

    /* Save setting */
    xfconf_channel_set_string (xsettings_channel, "/Xft/RGBA", xft_rgba_array[active]);
}

static void
cb_custom_dpi_check_button_toggled (GtkToggleButton *custom_dpi_toggle,
                                    GtkSpinButton   *custom_dpi_spin)
{
    gint dpi;

    if (gtk_toggle_button_get_active (custom_dpi_toggle))
    {
        /* Custom DPI is activated, so restore the last custom DPI we know about */
        dpi = xfconf_channel_get_int (xsettings_channel, "/Xfce/LastCustomDPI", -1);

        /* Unfortunately, we don't have a valid custom DPI value to use, so compute it */
        if (dpi <= 0)
            dpi = compute_xsettings_dpi (GTK_WIDGET (custom_dpi_toggle));

        /* Apply the computed custom DPI value */
        xfconf_channel_set_int (xsettings_channel, "/Xft/DPI", dpi);

        gtk_widget_set_sensitive (GTK_WIDGET (custom_dpi_spin), TRUE);
    }
    else
    {
        /* Custom DPI is deactivated, so remember the current value as the last custom DPI */
        dpi = gtk_spin_button_get_value_as_int (custom_dpi_spin);
        xfconf_channel_set_int (xsettings_channel, "/Xfce/LastCustomDPI", dpi);

        /* Tell xfsettingsd to compute the value itself */
        xfconf_channel_set_int (xsettings_channel, "/Xft/DPI", -1);

        /* Make the spin button insensitive */
        gtk_widget_set_sensitive (GTK_WIDGET (custom_dpi_spin), FALSE);
    }
}

static void
cb_custom_dpi_spin_button_changed (GtkSpinButton   *custom_dpi_spin,
                                   GtkToggleButton *custom_dpi_toggle)
{
    gint dpi = gtk_spin_button_get_value_as_int (custom_dpi_spin);

    if (GTK_WIDGET_IS_SENSITIVE (custom_dpi_spin) && gtk_toggle_button_get_active (custom_dpi_toggle))
    {
        /* Custom DPI is turned on and the spin button has changed, so remember the value */
        xfconf_channel_set_int (xsettings_channel, "/Xfce/LastCustomDPI", dpi);
    }

    /* Tell xfsettingsd to apply the custom DPI value */
    xfconf_channel_set_int (xsettings_channel, "/Xft/DPI", dpi);
}

static void
cb_xfwm_title_alignment_changed (GtkComboBox  *combo)
{
  GtkTreeModel *model;
  GtkTreeIter   iter;
  gchar        *alignment;

  model = gtk_combo_box_get_model (combo);

  gtk_combo_box_get_active_iter (combo, &iter);
  gtk_tree_model_get (model, &iter, 1, &alignment, -1);

  xfconf_channel_set_string (xfwm4_channel, "/general/title_alignment", alignment);

  g_free (alignment);
}

static void
xfwm_settings_save_button_layout (GtkBuilder *builder)
{
  GList        *children;
  GList        *iter;
  const gchar **key_chars;
  gchar        *value;
  const gchar  *name;
  gint          i;
  GtkContainer *container;

  container = (GtkContainer *)gtk_builder_get_object (builder, "active-frame");
  children = gtk_container_get_children (container);

  key_chars = g_new0 (const char *, g_list_length (children) + 1);

  for (i = 0, iter = children; iter != NULL; ++i, iter = g_list_next (iter))
  {
    name = gtk_buildable_get_name (GTK_BUILDABLE (iter->data));
    key_chars[i] = &name[strlen(name)-1]; //((const gchar *) g_object_get_data (G_OBJECT (iter->data), "key_char"));
  }

  value = g_strjoinv ("", (gchar **) key_chars);
  g_debug("%s", value);

  //xfconf_channel_set_string (xfwm4_channel, "/general/button_layout", value);

  g_list_free (children);
  g_free (key_chars);
  g_free (value);
}


#ifdef ENABLE_SOUND_SETTINGS
static void
cb_enable_event_sounds_check_button_toggled (GtkToggleButton *toggle, GtkWidget *button)
{
    gboolean active;

    active = gtk_toggle_button_get_active (toggle);
    gtk_widget_set_sensitive (button, active);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), active);
}
#endif

static void
appearance_settings_load_xfwm4_themes (GtkListStore *list_store,
                                       GtkTreeView  *tree_view)
{
    GtkTreeIter   iter;
    GDir         *dir;
    const gchar  *file;
    gint          i;
    gchar        *filename;
    GHashTable   *themes;
    gchar       **theme_dirs;
    gchar        *active_theme_name;

    themes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    active_theme_name = xfconf_channel_get_string (xfwm4_channel,
                                                   "/general/theme",
                                                   XFWM4_DEFAULT_THEME);
    xfce_resource_push_path (XFCE_RESOURCE_THEMES, DATADIR G_DIR_SEPARATOR_S "themes");
    theme_dirs = xfce_resource_dirs (XFCE_RESOURCE_THEMES);
    xfce_resource_pop_path (XFCE_RESOURCE_THEMES);

    for (i = 0; theme_dirs[i] != NULL; ++i)
    {
        dir = g_dir_open (theme_dirs[i], 0, NULL);

        if (G_UNLIKELY (dir == NULL))
            continue;

        while ((file = g_dir_read_name (dir)) != NULL)
        {
            filename = g_build_filename (theme_dirs[i], file, "xfwm4", "themerc", NULL);

            /* check if the theme rc exists and there is not already a theme with the
             * same name in the database */
            if (g_file_test (filename, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR) &&
                g_hash_table_lookup (themes, file) == NULL)
            {
              g_hash_table_insert (themes, g_strdup (file), GINT_TO_POINTER (1));

              /* insert in the list store */
              gtk_list_store_append (list_store, &iter);
              gtk_list_store_set (list_store, &iter,
                                  XFWM4_THEME_COLUMN_NAME, file,
                                  XFWM4_THEME_COLUMN_RC, filename, -1);

              if (G_UNLIKELY (g_str_equal (active_theme_name, file)))
                {
                  GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL(list_store), &iter);

                  gtk_tree_selection_select_iter (gtk_tree_view_get_selection (tree_view),
                                                  &iter);
                  gtk_tree_view_scroll_to_cell (tree_view, path, NULL, TRUE, 0.5, 0.5);

                  gtk_tree_path_free (path);
                }
            }

          g_free (filename);
        }

      g_dir_close (dir);
    }

  g_free (active_theme_name);
  g_strfreev (theme_dirs);
  g_hash_table_destroy (themes);
}

static void
appearance_settings_load_icon_themes (GtkListStore *list_store,
                                      GtkTreeView  *tree_view)
{
    GDir         *dir;
    GtkTreePath  *tree_path;
    GtkTreeIter   iter;
    XfceRc       *index_file;
    const gchar  *file;
    gchar       **icon_theme_dirs;
    gchar        *index_filename;
    const gchar  *theme_name;
    const gchar  *theme_comment;
    gchar        *comment_escaped;
    gchar        *active_theme_name;
    gint          i;
    GSList       *check_list = NULL;

    /* Determine current theme */
    active_theme_name = xfconf_channel_get_string (xsettings_channel, "/Net/IconThemeName", "Rodent");

    /* Determine directories to look in for icon themes */
    xfce_resource_push_path (XFCE_RESOURCE_ICONS, DATADIR G_DIR_SEPARATOR_S "icons");
    icon_theme_dirs = xfce_resource_dirs (XFCE_RESOURCE_ICONS);
    xfce_resource_pop_path (XFCE_RESOURCE_ICONS);

    /* Iterate over all base directories */
    for (i = 0; icon_theme_dirs[i] != NULL; ++i)
    {
        /* Open directory handle */
        dir = g_dir_open (icon_theme_dirs[i], 0, NULL);

        /* Try next base directory if this one cannot be read */
        if (G_UNLIKELY (dir == NULL))
            continue;

        /* Iterate over filenames in the directory */
        while ((file = g_dir_read_name (dir)) != NULL)
        {
            /* Build filename for the index.theme of the current icon theme directory */
            index_filename = g_build_filename (icon_theme_dirs[i], file, "index.theme", NULL);

            /* Try to open the theme index file */
            index_file = xfce_rc_simple_open (index_filename, TRUE);

            if (index_file != NULL
                && g_slist_find_custom (check_list, file, (GCompareFunc) g_utf8_collate) == NULL)
            {
                /* Set the icon theme group */
                xfce_rc_set_group (index_file, "Icon Theme");

                /* Check if the icon theme is valid and visible to the user */
                if (G_LIKELY (xfce_rc_has_entry (index_file, "Directories")
                              && !xfce_rc_read_bool_entry (index_file, "Hidden", FALSE)))
                {
                    /* Insert the theme in the check list */
                    check_list = g_slist_prepend (check_list, g_strdup (file));

                    /* Get translated icon theme name and comment */
                    theme_name = xfce_rc_read_entry (index_file, "Name", file);
                    theme_comment = xfce_rc_read_entry (index_file, "Comment", NULL);

                    /* Escape the comment, since tooltips are markup, not text */
                    comment_escaped = theme_comment ? g_markup_escape_text (theme_comment, -1) : NULL;

                    /* Append icon theme to the list store */
                    gtk_list_store_append (list_store, &iter);
                    gtk_list_store_set (list_store, &iter,
                                        COLUMN_THEME_NAME, file,
                                        COLUMN_THEME_DISPLAY_NAME, theme_name,
                                        COLUMN_THEME_COMMENT, comment_escaped, -1);

                    /* Cleanup */
                    g_free (comment_escaped);

                    /* Check if this is the active theme, if so, select it */
                    if (G_UNLIKELY (g_utf8_collate (file, active_theme_name) == 0))
                    {
                        tree_path = gtk_tree_model_get_path (GTK_TREE_MODEL (list_store), &iter);
                        gtk_tree_selection_select_path (gtk_tree_view_get_selection (tree_view), tree_path);
                        gtk_tree_view_scroll_to_cell (tree_view, tree_path, NULL, TRUE, 0.5, 0);
                        gtk_tree_path_free (tree_path);
                    }
                }
            }

            /* Close theme index file */
            if (G_LIKELY (index_file))
                xfce_rc_close (index_file);

            /* Free theme index filename */
            g_free (index_filename);
        }

        /* Close directory handle */
        g_dir_close (dir);
    }

    /* Free active theme name */
    g_free (active_theme_name);

    /* Free list of base directories */
    g_strfreev (icon_theme_dirs);

    /* Free the check list */
    if (G_LIKELY (check_list))
    {
        g_slist_foreach (check_list, (GFunc) g_free, NULL);
        g_slist_free (check_list);
    }
}

static void
appearance_settings_load_ui_themes (GtkListStore *list_store,
                                    GtkTreeView  *tree_view)
{
    GDir         *dir;
    GtkTreePath  *tree_path;
    GtkTreeIter   iter;
    XfceRc       *index_file;
    const gchar  *file;
    gchar       **ui_theme_dirs;
    gchar        *index_filename;
    const gchar  *theme_name;
    const gchar  *theme_comment;
    gchar        *active_theme_name;
    gchar        *gtkrc_filename;
    gchar        *comment_escaped;
    gint          i;
    GSList       *check_list = NULL;

    /* Determine current theme */
    active_theme_name = xfconf_channel_get_string (xsettings_channel, "/Net/ThemeName", "Default");

    /* Determine directories to look in for ui themes */
    xfce_resource_push_path (XFCE_RESOURCE_THEMES, DATADIR G_DIR_SEPARATOR_S "themes");
    ui_theme_dirs = xfce_resource_dirs (XFCE_RESOURCE_THEMES);
    xfce_resource_pop_path (XFCE_RESOURCE_THEMES);

    /* Iterate over all base directories */
    for (i = 0; ui_theme_dirs[i] != NULL; ++i)
    {
        /* Open directory handle */
        dir = g_dir_open (ui_theme_dirs[i], 0, NULL);

        /* Try next base directory if this one cannot be read */
        if (G_UNLIKELY (dir == NULL))
            continue;

        /* Iterate over filenames in the directory */
        while ((file = g_dir_read_name (dir)) != NULL)
        {
            /* Build the theme style filename */
            gtkrc_filename = g_build_filename (ui_theme_dirs[i], file, "gtk-2.0", "gtkrc", NULL);

            /* Check if the gtkrc file exists and the theme is not already in the list */
            if (g_file_test (gtkrc_filename, G_FILE_TEST_EXISTS)
                && g_slist_find_custom (check_list, file, (GCompareFunc) g_utf8_collate) == NULL)
            {
                /* Insert the theme in the check list */
                check_list = g_slist_prepend (check_list, g_strdup (file));

                /* Build filename for the index.theme of the current ui theme directory */
                index_filename = g_build_filename (ui_theme_dirs[i], file, "index.theme", NULL);

                /* Try to open the theme index file */
                index_file = xfce_rc_simple_open (index_filename, TRUE);

                if (G_LIKELY (index_file != NULL))
                {
                    /* Get translated ui theme name and comment */
                    theme_name = xfce_rc_read_entry (index_file, "Name", file);
                    theme_comment = xfce_rc_read_entry (index_file, "Comment", NULL);

                    /* Escape the comment because tooltips are markup, not text */
                    comment_escaped = theme_comment ? g_markup_escape_text (theme_comment, -1) : NULL;
                }
                else
                {
                    /* Set defaults */
                    theme_name = file;
                    comment_escaped = NULL;
                }

                /* Append ui theme to the list store */
                gtk_list_store_append (list_store, &iter);
                gtk_list_store_set (list_store, &iter,
                                    COLUMN_THEME_NAME, file,
                                    COLUMN_THEME_DISPLAY_NAME, theme_name,
                                    COLUMN_THEME_COMMENT, comment_escaped, -1);

                /* Cleanup */
                if (G_LIKELY (index_file != NULL))
                    xfce_rc_close (index_file);
                g_free (comment_escaped);

                /* Check if this is the active theme, if so, select it */
                if (G_UNLIKELY (g_utf8_collate (file, active_theme_name) == 0))
                {
                    tree_path = gtk_tree_model_get_path (GTK_TREE_MODEL (list_store), &iter);
                    gtk_tree_selection_select_path (gtk_tree_view_get_selection (tree_view), tree_path);
                    gtk_tree_view_scroll_to_cell (tree_view, tree_path, NULL, TRUE, 0.5, 0);
                    gtk_tree_path_free (tree_path);
                }

                /* Free theme index filename */
                g_free (index_filename);
            }

            /* Free gtkrc filename */
            g_free (gtkrc_filename);
        }

        /* Close directory handle */
        g_dir_close (dir);
    }

    /* Free active theme name */
    g_free (active_theme_name);

    /* Free list of base directories */
    g_strfreev (ui_theme_dirs);

    /* Free the check list */
    if (G_LIKELY (check_list))
    {
        g_slist_foreach (check_list, (GFunc) g_free, NULL);
        g_slist_free (check_list);
    }
}

static void
appearance_settings_dialog_channel_property_changed (XfconfChannel *channel,
                                                     const gchar   *property_name,
                                                     const GValue  *value,
                                                     GtkBuilder    *builder)
{
    GObject      *object;
    gchar        *str;
    guint         i;
    gint          antialias, dpi, custom_dpi;
    GtkTreeModel *model;

    g_return_if_fail (property_name != NULL);
    g_return_if_fail (GTK_IS_BUILDER (builder));
    if (channel == xsettings_channel)
    {
        if (strcmp (property_name, "/Xft/RGBA") == 0)
        {
            str = xfconf_channel_get_string (xsettings_channel, property_name, xft_rgba_array[0]);
            for (i = 0; i < G_N_ELEMENTS (xft_rgba_array); i++)
            {
                if (strcmp (str, xft_rgba_array[i]) == 0)
                {
                    object = gtk_builder_get_object (builder, "xft_rgba_combo_box");
                    gtk_combo_box_set_active (GTK_COMBO_BOX (object), i);
                    break;
                }
            }
            g_free (str);
        }
        else if (strcmp (property_name, "/Gtk/ToolbarStyle") == 0)
        {
            str = xfconf_channel_get_string (xsettings_channel, property_name, toolbar_styles_array[2]);
            for (i = 0; i < G_N_ELEMENTS (toolbar_styles_array); i++)
            {
                if (strcmp (str, toolbar_styles_array[i]) == 0)
                {
                    object = gtk_builder_get_object (builder, "gtk_toolbar_style_combo_box");
                    gtk_combo_box_set_active (GTK_COMBO_BOX (object), i);
                    break;
                }
            }
            g_free (str);
        }
        else if (strcmp (property_name, "/Xft/HintStyle") == 0)
        {
            str = xfconf_channel_get_string (xsettings_channel, property_name, xft_hint_styles_array[0]);
            for (i = 0; i < G_N_ELEMENTS (xft_hint_styles_array); i++)
            {
                if (strcmp (str, xft_hint_styles_array[i]) == 0)
                {
                    object = gtk_builder_get_object (builder, "xft_hinting_style_combo_box");
                    gtk_combo_box_set_active (GTK_COMBO_BOX (object), i);
                    break;
                }
            }
            g_free (str);
        }
        else if (strcmp (property_name, "/Xft/Antialias") == 0)
        {
            object = gtk_builder_get_object (builder, "xft_antialias_check_button");
            antialias = xfconf_channel_get_int (xsettings_channel, property_name, -1);
            switch (antialias)
            {
                case 1:
                    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (object), TRUE);
                    break;

                case 0:
                    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (object), FALSE);
                    break;

                default: /* -1 */
                    gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (object), TRUE);
                    break;
            }
        }
        else if (strcmp (property_name, "/Xft/DPI") == 0)
        {
            /* The DPI has changed, so get its value and the last known custom value */
            dpi = xfconf_channel_get_int (xsettings_channel, property_name, FALLBACK_DPI);
            custom_dpi = xfconf_channel_get_int (xsettings_channel, "/Xfce/LastCustomDPI", -1);

            /* Activate the check button if we're using a custom DPI */
            object = gtk_builder_get_object (builder, "xft_custom_dpi_check_button");
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (object), dpi >= 0);

            /* If we're not using a custom DPI, compute the future custom DPI automatically */
            if (custom_dpi == -1)
                custom_dpi = compute_xsettings_dpi (GTK_WIDGET (object));

            object = gtk_builder_get_object (builder, "xft_custom_dpi_spin_button");

            if (dpi > 0)
            {
                /* We're using a custom DPI, so use the current DPI setting for the spin value */
                gtk_spin_button_set_value (GTK_SPIN_BUTTON (object), dpi);
            }
            else
            {
                /* Set the spin button value to the last custom DPI */
                gtk_spin_button_set_value (GTK_SPIN_BUTTON (object), custom_dpi);
            }
        }
        else if (strcmp (property_name, "/Net/ThemeName") == 0)
        {
            GtkTreeIter iter;
            gboolean    reload;

            object = gtk_builder_get_object (builder, "gtk_theme_treeview");
            model = gtk_tree_view_get_model (GTK_TREE_VIEW (object));
            reload = TRUE;

            if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (object)),
                                                 &model,
                                                 &iter))
            {
                gchar *selected_name;
                gchar *new_name;

                gtk_tree_model_get (model, &iter, COLUMN_THEME_NAME, &selected_name, -1);

                new_name = xfconf_channel_get_string (channel, property_name, NULL);

                reload = (strcmp (new_name, selected_name) != 0);

                g_free (selected_name);
                g_free (new_name);
            }

            if (reload)
            {
                gtk_list_store_clear (GTK_LIST_STORE (model));
                appearance_settings_load_ui_themes (GTK_LIST_STORE (model), GTK_TREE_VIEW (object));
            }
        }
        else if (strcmp (property_name, "/Net/IconThemeName") == 0)
        {
            GtkTreeIter iter;
            gboolean    reload;

            reload = TRUE;

            object = gtk_builder_get_object (builder, "icon_theme_treeview");
            model = gtk_tree_view_get_model (GTK_TREE_VIEW (object));

            if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (object)),
                                                 &model,
                                                 &iter))
            {
                gchar *selected_name;
                gchar *new_name;

                gtk_tree_model_get (model, &iter, COLUMN_THEME_NAME, &selected_name, -1);

                new_name = xfconf_channel_get_string (channel, property_name, NULL);

                reload = (strcmp (new_name, selected_name) != 0);

                g_free (selected_name);
                g_free (new_name);
            }


            if (reload)
            {
                gtk_list_store_clear (GTK_LIST_STORE (model));
                appearance_settings_load_icon_themes (GTK_LIST_STORE (model), GTK_TREE_VIEW (object));
            }
        }
    }

    if (channel == xfwm4_channel)
    {
        if (strcmp (property_name, "/general/theme") == 0)
        {
            GtkTreeIter iter;
            gboolean    reload;

            reload = TRUE;

            object = gtk_builder_get_object (builder, "xfwm4_theme_treeview");
            model = gtk_tree_view_get_model (GTK_TREE_VIEW (object));

            if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (object)),
                                                 &model,
                                                 &iter))
            {
                gchar *selected_name;
                gchar *new_name;

                gtk_tree_model_get (model, &iter, XFWM4_THEME_COLUMN_NAME, &selected_name, -1);

                new_name = xfconf_channel_get_string (channel, property_name, NULL);

                reload = (strcmp (new_name, selected_name) != 0);

                g_free (selected_name);
                g_free (new_name);
            }


            if (reload)
            {
                gtk_list_store_clear (GTK_LIST_STORE (model));
                appearance_settings_load_xfwm4_themes (GTK_LIST_STORE (model), GTK_TREE_VIEW (object));
            }
        }
        else if (strcmp (property_name, "/general/title_alignment") == 0)
        {
            GtkWidget    *combo;
            gchar        *alignment;
            GtkTreeIter   iter;
            const gchar  *new_value;

            combo = GTK_WIDGET (gtk_builder_get_object (builder, "title_align_combo"));
            model = gtk_combo_box_get_model (GTK_COMBO_BOX (combo));

            if (gtk_tree_model_get_iter_first (model, &iter))
            {
                do
                {
                    gtk_tree_model_get (model, &iter, 1, &alignment, -1);

                    if (G_UNLIKELY (G_VALUE_TYPE (value) == G_TYPE_INVALID))
                        new_value = "center";
                    else
                        new_value = g_value_get_string (value);

                    if (G_UNLIKELY (g_str_equal (alignment, new_value)))
                    {
                        g_free (alignment);
                        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (combo), &iter);
                        break;
                    }

                    g_free (alignment);
                }
                while (gtk_tree_model_iter_next (model, &iter));
            }
        }
        else if (strcmp (property_name, "/general/button_layout") == 0)
        {
            GtkWidget   *active_box;
            GtkWidget   *hidden_box;
            GtkWidget   *button;
            GList       *children;
            GList       *iter;
            const gchar *str_value;
            const gchar *key_char;

            hidden_box = GTK_WIDGET (gtk_builder_get_object (builder, "hidden-box"));
            active_box = GTK_WIDGET (gtk_builder_get_object (builder, "active-box"));

            gtk_widget_set_app_paintable (active_box, FALSE);
            gtk_widget_set_app_paintable (hidden_box, FALSE);

            children = gtk_container_get_children (GTK_CONTAINER (active_box));

            /* Move all buttons to the hidden list, except for the title */
            for (iter = children; iter != NULL; iter = g_list_next (iter))
            {
                button = GTK_WIDGET (iter->data);
                key_char = (const gchar *) g_object_get_data (G_OBJECT (button), "key_char");

                if (G_LIKELY (key_char[0] != '|'))
                {
                    g_object_ref (button);
                    gtk_container_remove (GTK_CONTAINER (active_box), button);
                    gtk_box_pack_start (GTK_BOX (hidden_box), button, FALSE, FALSE, 0);
                    g_object_unref (button);
                }
            }

            g_list_free (children);

            children = g_list_concat (gtk_container_get_children (GTK_CONTAINER (active_box)),
                                      gtk_container_get_children (GTK_CONTAINER (hidden_box)));

            /* Move buttons to the active list */
            for (str_value = g_value_get_string (value); *str_value != '\0'; ++str_value)
            {
                for (iter = children; iter != NULL; iter = g_list_next (iter))
                {
                    button = GTK_WIDGET (iter->data);
                    key_char = (const gchar *) g_object_get_data (G_OBJECT (button), "key_char");

                    if (g_str_has_prefix (str_value, key_char))
                    {
                        g_object_ref (button);
                        gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (button)), button);
                        gtk_box_pack_start (GTK_BOX (active_box), button,
                                            key_char[0] == '|', key_char[0] == '|', 0);
                        g_object_unref (button);
                    }
                }
            }

            g_list_free (children);

            gtk_widget_set_app_paintable (active_box, TRUE);
            gtk_widget_set_app_paintable (hidden_box, TRUE);
        }
    } 
}

static void
cb_theme_uri_dropped (GtkWidget        *widget,
                      GdkDragContext   *drag_context,
                      gint              x,
                      gint              y,
                      GtkSelectionData *data,
                      guint             info,
                      guint             timestamp,
                      GtkBuilder       *builder)
{
    gchar        **uris;
    gchar         *argv[3];
    guint          i;
    GError        *error = NULL;
    gint           status;
    GtkWidget     *toplevel = gtk_widget_get_toplevel (widget);
    gchar         *filename;
    GdkCursor     *cursor;
    GdkWindow     *gdkwindow;
    gboolean       something_installed = FALSE;
    GObject       *object;
    GtkTreeModel  *model;

    uris = gtk_selection_data_get_uris (data);
    if (uris == NULL)
        return;

    argv[0] = HELPERDIR G_DIR_SEPARATOR_S "appearance-install-theme";
    argv[2] = NULL;

    /* inform the user we are installing the theme */
    gdkwindow = gtk_widget_get_window (widget);
    cursor = gdk_cursor_new_for_display (gtk_widget_get_display (widget), GDK_WATCH);
    gdk_window_set_cursor (gdkwindow, cursor);

    /* iterate main loop to show cursor */
    while (gtk_events_pending ())
        gtk_main_iteration ();

    for (i = 0; uris[i] != NULL; i++)
    {
        filename = g_filename_from_uri (uris[i], NULL, NULL);
        if (filename == NULL)
            continue;

        argv[1] = filename;

        if (g_spawn_sync (NULL, argv, NULL, 0, NULL, NULL, NULL, NULL, &status, &error)
            && status > 0)
        {
            switch (WEXITSTATUS (status))
            {
                case 2:
                    g_set_error (&error, G_SPAWN_ERROR, 0,
                        _("File is larger then %d MB, installation aborted"), 50);
                    break;

                case 3:
                    g_set_error_literal (&error, G_SPAWN_ERROR, 0,
                        _("Failed to create temporary directory"));
                    break;

                case 4:
                    g_set_error_literal (&error, G_SPAWN_ERROR, 0,
                        _("Failed to extract archive"));
                    break;

                case 5:
                    g_set_error_literal (&error, G_SPAWN_ERROR, 0,
                        _("Unknown format, only archives and directories are supported"));
                    break;

                default:
                    g_set_error (&error, G_SPAWN_ERROR,
                        0, _("An unknown error, exit code is %d"), WEXITSTATUS (status));
                    break;
            }
        }

        if (error != NULL)
        {
            xfce_dialog_show_error (GTK_WINDOW (toplevel), error, _("Failed to install theme"));
            g_clear_error (&error);
        }
        else
        {
            something_installed = TRUE;
        }

        g_free (filename);
    }

    g_strfreev (uris);
    gdk_window_set_cursor (gdkwindow, NULL);
    gdk_cursor_unref (cursor);

    if (something_installed)
    {
        /* reload icon theme treeview */
        object = gtk_builder_get_object (builder, "icon_theme_treeview");
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (object));
        gtk_list_store_clear (GTK_LIST_STORE (model));
        appearance_settings_load_icon_themes (GTK_LIST_STORE (model), GTK_TREE_VIEW (object));

        /* reload gtk theme treeview */
        object = gtk_builder_get_object (builder, "gtk_theme_treeview");
        model = gtk_tree_view_get_model (GTK_TREE_VIEW (object));
        gtk_list_store_clear (GTK_LIST_STORE (model));
        appearance_settings_load_ui_themes (GTK_LIST_STORE (model), GTK_TREE_VIEW (object));
    }
}

static void
appearance_settings_dialog_configure_widgets (GtkBuilder *builder)
{
    GObject            *object, *object2;
    GtkTreeIter         iter;
    const MenuTemplate *template;
    GtkListStore       *list_store;
    GtkCellRenderer    *renderer;
    GdkPixbuf          *pixbuf;
    GtkTreeSelection   *selection;
    GtkWidget          *hidden_frame;
    GtkWidget          *hidden_box;
    GtkWidget          *active_frame;
    GtkWidget          *active_box;
    GtkWidget          *title_font_button;
    GtkWidget          *title_align_combo;
    GList              *children;
    GList              *list_iter;
    GValue              value = { 0, };
    GtkTargetEntry      target_entry[2];
    const gchar        *name;

    /* xfwm4 themes */
    object = gtk_builder_get_object (builder, "xfwm4_theme_treeview");

    list_store = gtk_list_store_new (N_XFWM4_THEME_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store), XFWM4_THEME_COLUMN_NAME, GTK_SORT_ASCENDING);
    gtk_tree_view_set_model (GTK_TREE_VIEW (object), GTK_TREE_MODEL (list_store));
    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (object), 0, "", renderer, "text", XFWM4_THEME_COLUMN_NAME, NULL);

    appearance_settings_load_xfwm4_themes (list_store, GTK_TREE_VIEW (object));

    g_object_unref (G_OBJECT (list_store));

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (object));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
    g_signal_connect (G_OBJECT (selection), "changed", G_CALLBACK (cb_xfwm_theme_tree_selection_changed), builder);

    title_font_button = GTK_WIDGET (gtk_builder_get_object (builder, "title_font_button"));
    xfconf_g_property_bind (xfwm4_channel,
                            "/general/title_font", G_TYPE_STRING,
                            title_font_button, "font-name");

    title_align_combo = GTK_WIDGET (gtk_builder_get_object (builder, "title_align_combo"));
    gtk_cell_layout_clear (GTK_CELL_LAYOUT (title_align_combo));

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (title_align_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (title_align_combo), renderer, "text", 0);

    list_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
    gtk_combo_box_set_model (GTK_COMBO_BOX (title_align_combo), GTK_TREE_MODEL (list_store));

    for (template = title_align_values; template->name != NULL; ++template)
    {
        gtk_list_store_append (list_store, &iter);
        gtk_list_store_set (list_store, &iter, 0, _(template->name), 1, template->value, -1);
    }
    g_object_unref (G_OBJECT (list_store));

    xfconf_channel_get_property (xfwm4_channel, "/general/title_alignment", &value);
    appearance_settings_dialog_channel_property_changed (xfwm4_channel, "/general/title_alignment", &value, builder);
    g_value_unset (&value);

    g_signal_connect (title_align_combo, "changed",
                      G_CALLBACK (cb_xfwm_title_alignment_changed), NULL);

    /* Style tab: button layout */
    {

        active_frame = GTK_WIDGET (gtk_builder_get_object (builder, "active-frame"));
        hidden_frame = GTK_WIDGET (gtk_builder_get_object (builder, "hidden-frame"));
        active_box = GTK_WIDGET (gtk_builder_get_object (builder, "active-box"));
        hidden_box = GTK_WIDGET (gtk_builder_get_object (builder, "hidden-box"));

        target_entry[0].target = "_xfwm4_button_layout";
        target_entry[0].flags = 0;
        target_entry[0].info = 2;

        target_entry[1].target = "_xfwm4_active_layout";
        target_entry[1].flags = 0;
        target_entry[1].info = 3;

        gtk_drag_dest_set (active_frame, GTK_DEST_DEFAULT_ALL, target_entry, 2, GDK_ACTION_MOVE);

        g_signal_connect (active_frame, "drag-data-received",
                          G_CALLBACK (xfwm_settings_active_frame_drag_data), builder);
#if 0
        g_signal_connect (active_frame, "drag-motion",
                          G_CALLBACK (xfwm_settings_active_frame_drag_motion), builder);
        g_signal_connect (active_frame, "drag-leave",
                          G_CALLBACK (xfwm_settings_active_frame_drag_leave), builder);
#endif

        gtk_drag_dest_set (hidden_frame, GTK_DEST_DEFAULT_ALL, target_entry, 1, GDK_ACTION_MOVE);

#if 0
        g_signal_connect (hidden_frame, "drag-data-received",
                          G_CALLBACK (xfwm_settings_hidden_frame_drag_data), settings);
#endif

        children = gtk_container_get_children (GTK_CONTAINER (active_box));
        for (list_iter = children; list_iter != NULL; list_iter = g_list_next (list_iter))
        {
            object = list_iter->data;
            name = gtk_buildable_get_name (GTK_BUILDABLE (object));

            if (name[strlen (name) - 1] == '|')
            {
                g_signal_connect (title_align_combo, "changed",
                                  G_CALLBACK (cb_xfwm_title_button_alignment_changed), object);
                cb_xfwm_title_button_alignment_changed (GTK_COMBO_BOX (title_align_combo),
                                                              GTK_WIDGET (object));
            }

            g_object_set_data (object, "key_char", (gpointer) &name[strlen (name) - 1]);
            g_debug("%s", (char *)g_object_get_data (object, "key_char"));
            gtk_drag_source_set (GTK_WIDGET(object), GDK_BUTTON1_MASK, &target_entry[1], 1, GDK_ACTION_MOVE);

            g_signal_connect (object, "drag_data_get",
                              G_CALLBACK (cb_xfwm_title_button_drag_data), NULL);
            g_signal_connect (object, "drag_begin", G_CALLBACK (cb_xfwm_title_button_drag_begin),
                              NULL);
            g_signal_connect (object, "drag_end", G_CALLBACK (cb_xfwm_title_button_drag_end),
                              NULL);
            g_signal_connect (object, "button_press_event",
                              G_CALLBACK (cb_appearance_settings_signal_blocker), NULL);
            g_signal_connect (object, "enter_notify_event",
                              G_CALLBACK (cb_appearance_settings_signal_blocker), NULL);
            g_signal_connect (object, "focus",  G_CALLBACK (cb_appearance_settings_signal_blocker), NULL);
        }
        g_list_free (children);

        children = gtk_container_get_children (GTK_CONTAINER (hidden_box));
        for (list_iter = children; list_iter != NULL; list_iter = g_list_next (list_iter))
        {
            object = list_iter->data;
            name = gtk_buildable_get_name (GTK_BUILDABLE (object));

            g_object_set_data (object, "key_char", (gpointer) &name[strlen (name) - 1]);
            g_debug("%s", &name[strlen(name)-1]);
            gtk_drag_source_set (GTK_WIDGET (object), GDK_BUTTON1_MASK, &target_entry[0], 1, GDK_ACTION_MOVE);

              g_signal_connect (object, "drag_data_get",
                                G_CALLBACK (cb_xfwm_title_button_drag_data), NULL);
              g_signal_connect (object, "drag_begin", G_CALLBACK (cb_xfwm_title_button_drag_begin),
                                NULL);
              g_signal_connect (object, "drag_end", G_CALLBACK (cb_xfwm_title_button_drag_end),
                                NULL);
              g_signal_connect (object, "button_press_event",
                                G_CALLBACK (cb_appearance_settings_signal_blocker), NULL);
              g_signal_connect (object, "enter_notify_event",
                                G_CALLBACK (cb_appearance_settings_signal_blocker), NULL);
              g_signal_connect (object, "focus",  G_CALLBACK (cb_appearance_settings_signal_blocker), NULL);
        }
        g_list_free (children);

        xfconf_channel_get_property (xfwm4_channel, "/general/button_layout", &value);
        appearance_settings_dialog_channel_property_changed(xfwm4_channel,
                                                            "/general/button_layout", &value, builder);
        g_value_unset (&value);
    }

    /* Icon themes list */
    object = gtk_builder_get_object (builder, "icon_theme_treeview");

    list_store = gtk_list_store_new (N_THEME_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store), COLUMN_THEME_DISPLAY_NAME, GTK_SORT_ASCENDING);
    gtk_tree_view_set_model (GTK_TREE_VIEW (object), GTK_TREE_MODEL (list_store));
    gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (object), COLUMN_THEME_COMMENT);

    renderer = gtk_cell_renderer_text_new ();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (object), 0, "", renderer, "text", COLUMN_THEME_DISPLAY_NAME, NULL);

    appearance_settings_load_icon_themes (list_store, GTK_TREE_VIEW (object));

    g_object_unref (G_OBJECT (list_store));

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (object));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
    g_signal_connect (G_OBJECT (selection), "changed", G_CALLBACK (cb_icon_theme_tree_selection_changed), NULL);

    gtk_drag_dest_set (GTK_WIDGET (object), GTK_DEST_DEFAULT_ALL,
                       theme_drop_targets, G_N_ELEMENTS (theme_drop_targets),
                       GDK_ACTION_COPY);
    g_signal_connect (G_OBJECT (object), "drag-data-received", G_CALLBACK (cb_theme_uri_dropped), builder);

    /* Gtk (UI) themes */
    object = gtk_builder_get_object (builder, "gtk_theme_treeview");

    list_store = gtk_list_store_new (N_THEME_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (list_store), COLUMN_THEME_DISPLAY_NAME, GTK_SORT_ASCENDING);
    gtk_tree_view_set_model (GTK_TREE_VIEW (object), GTK_TREE_MODEL (list_store));
    gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (object), COLUMN_THEME_COMMENT);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (object), 0, "", renderer, "text", COLUMN_THEME_DISPLAY_NAME, NULL);

    appearance_settings_load_ui_themes (list_store, GTK_TREE_VIEW (object));

    g_object_unref (G_OBJECT (list_store));

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (object));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
    g_signal_connect (G_OBJECT (selection), "changed", G_CALLBACK (cb_ui_theme_tree_selection_changed), NULL);

    gtk_drag_dest_set (GTK_WIDGET (object), GTK_DEST_DEFAULT_ALL,
                       theme_drop_targets, G_N_ELEMENTS (theme_drop_targets),
                       GDK_ACTION_COPY);
    g_signal_connect (G_OBJECT (object), "drag-data-received", G_CALLBACK (cb_theme_uri_dropped), builder);

    /* Subpixel (rgba) hinting Combo */
    object = gtk_builder_get_object (builder, "xft_rgba_store");

    pixbuf = gdk_pixbuf_new_from_xpm_data (rgba_image_none_xpm);
    gtk_list_store_insert_with_values (GTK_LIST_STORE (object), NULL, 0, 0, pixbuf, 1, _("None"), -1);
    g_object_unref (G_OBJECT (pixbuf));

    pixbuf = gdk_pixbuf_new_from_xpm_data (rgba_image_rgb_xpm);
    gtk_list_store_insert_with_values (GTK_LIST_STORE (object), NULL, 1, 0, pixbuf, 1, _("RGB"), -1);
    g_object_unref (G_OBJECT (pixbuf));

    pixbuf = gdk_pixbuf_new_from_xpm_data (rgba_image_bgr_xpm);
    gtk_list_store_insert_with_values (GTK_LIST_STORE (object), NULL, 2, 0, pixbuf, 1, _("BGR"), -1);
    g_object_unref (G_OBJECT (pixbuf));

    pixbuf = gdk_pixbuf_new_from_xpm_data (rgba_image_vrgb_xpm);
    gtk_list_store_insert_with_values (GTK_LIST_STORE (object), NULL, 3, 0, pixbuf, 1, _("Vertical RGB"), -1);
    g_object_unref (G_OBJECT (pixbuf));

    pixbuf = gdk_pixbuf_new_from_xpm_data (rgba_image_vbgr_xpm);
    gtk_list_store_insert_with_values (GTK_LIST_STORE (object), NULL, 4, 0, pixbuf, 1, _("Vertical BGR"), -1);
    g_object_unref (G_OBJECT (pixbuf));

    object = gtk_builder_get_object (builder, "xft_rgba_combo_box");
    appearance_settings_dialog_channel_property_changed (xsettings_channel, "/Xft/RGBA", NULL, builder);
    g_signal_connect (G_OBJECT (object), "changed", G_CALLBACK (cb_rgba_style_combo_changed), NULL);

    /* Enable editable menu accelerators */
    object = gtk_builder_get_object (builder, "gtk_caneditaccels_check_button");
    xfconf_g_property_bind (xsettings_channel, "/Gtk/CanChangeAccels", G_TYPE_BOOLEAN,
                            G_OBJECT (object), "active");

    /* Show menu images */
    object = gtk_builder_get_object (builder, "gtk_menu_images_check_button");
    xfconf_g_property_bind (xsettings_channel, "/Gtk/MenuImages", G_TYPE_BOOLEAN,
                            G_OBJECT (object), "active");

    /* Show button images */
    object = gtk_builder_get_object (builder, "gtk_button_images_check_button");
    xfconf_g_property_bind (xsettings_channel, "/Gtk/ButtonImages", G_TYPE_BOOLEAN,
                            G_OBJECT (object), "active");

    /* Font name */
    object = gtk_builder_get_object (builder, "gtk_fontname_button");
    xfconf_g_property_bind (xsettings_channel,  "/Gtk/FontName", G_TYPE_STRING,
                            G_OBJECT (object), "font-name");

    /* Toolbar style */
    object = gtk_builder_get_object (builder, "gtk_toolbar_style_combo_box");
    appearance_settings_dialog_channel_property_changed (xsettings_channel, "/Gtk/ToolbarStyle", NULL, builder);
    g_signal_connect (G_OBJECT (object), "changed", G_CALLBACK(cb_toolbar_style_combo_changed), NULL);

    /* Hinting style */
    object = gtk_builder_get_object (builder, "xft_hinting_style_combo_box");
    appearance_settings_dialog_channel_property_changed (xsettings_channel, "/Xft/HintStyle", NULL, builder);
    g_signal_connect (G_OBJECT (object), "changed", G_CALLBACK (cb_hinting_style_combo_changed), NULL);

    /* Hinting */
    object = gtk_builder_get_object (builder, "xft_antialias_check_button");
    appearance_settings_dialog_channel_property_changed (xsettings_channel, "/Xft/Antialias", NULL, builder);
    g_signal_connect (G_OBJECT (object), "toggled", G_CALLBACK (cb_antialias_check_button_toggled), NULL);

    /* DPI */
    object = gtk_builder_get_object (builder, "xft_custom_dpi_check_button");
    object2 = gtk_builder_get_object (builder, "xft_custom_dpi_spin_button");
    appearance_settings_dialog_channel_property_changed (xsettings_channel, "/Xft/DPI", NULL, builder);
    gtk_widget_set_sensitive (GTK_WIDGET (object2), gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object)));
    g_signal_connect (G_OBJECT (object), "toggled", G_CALLBACK (cb_custom_dpi_check_button_toggled), object2);
    g_signal_connect (G_OBJECT (object2), "value-changed", G_CALLBACK (cb_custom_dpi_spin_button_changed), object);

#ifdef ENABLE_SOUND_SETTINGS
    /* Sounds */
    object = gtk_builder_get_object (builder, "event_sounds_frame");
    gtk_widget_show (GTK_WIDGET (object));

    object = gtk_builder_get_object (builder, "enable_event_sounds_check_button");
    object2  = gtk_builder_get_object (builder, "enable_input_feedback_sounds_button");

    g_signal_connect (G_OBJECT (object), "toggled",
                      G_CALLBACK (cb_enable_event_sounds_check_button_toggled), object2);

    xfconf_g_property_bind (xsettings_channel, "/Net/EnableEventSounds", G_TYPE_BOOLEAN,
                            G_OBJECT (object), "active");
    xfconf_g_property_bind (xsettings_channel, "/Net/EnableInputFeedbackSounds", G_TYPE_BOOLEAN,
                            G_OBJECT (object2), "active");

    gtk_widget_set_sensitive (GTK_WIDGET (object2), gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (object)));
#endif
}

static void
appearance_settings_dialog_response (GtkWidget *dialog,
                                     gint       response_id)
{
    if (response_id == GTK_RESPONSE_HELP)
        xfce_dialog_show_help (GTK_WINDOW (dialog), "xfce4-settings", "appearance", NULL);
    else
        gtk_main_quit ();
}

gint
main (gint argc, gchar **argv)
{
    GObject    *dialog, *plug_child;
    GtkWidget  *plug;
    GtkBuilder *builder;
    GError     *error = NULL;

    /* setup translation domain */
    xfce_textdomain (GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    /* initialize Gtk+ */
    if (!gtk_init_with_args (&argc, &argv, "", option_entries, GETTEXT_PACKAGE, &error))
    {
        if (G_LIKELY (error))
        {
            /* print error */
            g_print ("%s: %s.\n", G_LOG_DOMAIN, error->message);
            g_print (_("Type '%s --help' for usage."), G_LOG_DOMAIN);
            g_print ("\n");

            /* cleanup */
            g_error_free (error);
        }
        else
        {
            g_error ("Unable to open display.");
        }

        return EXIT_FAILURE;
    }

    /* print version information */
    if (G_UNLIKELY (opt_version))
    {
        g_print ("%s %s (Xfce %s)\n\n", G_LOG_DOMAIN, PACKAGE_VERSION, xfce_version_string ());
        g_print ("%s\n", "Copyright (c) 2008-2011");
        g_print ("\t%s\n\n", _("The Xfce development team. All rights reserved."));
        g_print (_("Please report bugs to <%s>."), PACKAGE_BUGREPORT);
        g_print ("\n");

        return EXIT_SUCCESS;
    }

    /* initialize xfconf */
    if (!xfconf_init (&error))
    {
        /* print error and exit */
        g_error ("Failed to connect to xfconf daemon: %s.", error->message);
        g_error_free (error);

        return EXIT_FAILURE;
    }

    /* open the xsettings channel */
    xsettings_channel = xfconf_channel_new ("xsettings");
    xfwm4_channel = xfconf_channel_new ("xfwm4");
    if (G_LIKELY (xsettings_channel))
    {
        /* hook to make sure the libxfce4ui library is linked */
        if (xfce_titled_dialog_get_type () == 0)
            return EXIT_FAILURE;

        /* load the gtk user interface file*/
        builder = gtk_builder_new ();
        if (gtk_builder_add_from_string (builder, appearance_dialog_ui,
                                         appearance_dialog_ui_length, &error) != 0)
          {
            /* connect signal to monitor the channel */
            g_signal_connect (G_OBJECT (xsettings_channel), "property-changed",
                G_CALLBACK (appearance_settings_dialog_channel_property_changed), builder);
            g_signal_connect (G_OBJECT (xfwm4_channel), "property-changed",
                G_CALLBACK (appearance_settings_dialog_channel_property_changed), builder);

            appearance_settings_dialog_configure_widgets (builder);

            if (G_UNLIKELY (opt_socket_id == 0))
            {
                /* build the dialog */
                dialog = gtk_builder_get_object (builder, "dialog");

                g_signal_connect (dialog, "response",
                    G_CALLBACK (appearance_settings_dialog_response), NULL);
                gtk_window_present (GTK_WINDOW (dialog));

                /* To prevent the settings dialog to be saved in the session */
                gdk_set_sm_client_id ("FAKE ID");

                gtk_main ();
            }
            else
            {
                /* Create plug widget */
                plug = gtk_plug_new (opt_socket_id);
                g_signal_connect (plug, "delete-event", G_CALLBACK (gtk_main_quit), NULL);
                gtk_widget_show (plug);

                /* Stop startup notification */
                gdk_notify_startup_complete ();

                /* Get plug child widget */
                plug_child = gtk_builder_get_object (builder, "plug-child");
                gtk_widget_reparent (GTK_WIDGET (plug_child), plug);
                gtk_widget_show (GTK_WIDGET (plug_child));

                /* To prevent the settings dialog to be saved in the session */
                gdk_set_sm_client_id ("FAKE ID");

                /* Enter main loop */
                gtk_main ();
            }
        }
        else
        {
            g_error ("Failed to load the UI file: %s.", error->message);
            g_error_free (error);
        }

        /* Release Builder */
        g_object_unref (G_OBJECT (builder));

        /* release the channels */
        g_object_unref (G_OBJECT (xsettings_channel));
        g_object_unref (G_OBJECT (xfwm4_channel));
    }

    /* shutdown xfconf */
    xfconf_shutdown ();

    return EXIT_SUCCESS;
}

static void
xfwm_settings_active_frame_drag_data (GtkWidget        *widget,
                                      GdkDragContext   *drag_context,
                                      gint              x,
                                      gint              y,
                                      GtkSelectionData *data,
                                      guint             info,
                                      guint             timestamp,
                                      gpointer         *user_data)
{
    GtkWidget *source;
    GtkWidget *parent;
    GtkWidget *active_box;
    GList     *children;
    GList     *iter;
    gint       xoffset;
    gint       i;
    GtkBuilder*builder = GTK_BUILDER (user_data);

    source = GTK_WIDGET (gtk_builder_get_object (builder,
                                (const gchar *)gtk_selection_data_get_data (data)));
    parent = gtk_widget_get_parent (source);

    active_box = GTK_WIDGET (gtk_builder_get_object (builder, "active-box"));

    g_object_ref (source);
    gtk_container_remove (GTK_CONTAINER (parent), source);
    gtk_box_pack_start (GTK_BOX (active_box), source, info == 3, info == 3, 0);
    g_object_unref (source);

    xoffset = widget->allocation.x;

    children = gtk_container_get_children (GTK_CONTAINER (active_box));

    for (i = 0, iter = children; iter != NULL; ++i, iter = g_list_next (iter))
    {
        if (GTK_WIDGET_VISIBLE (iter->data))
        {
            if (x < (GTK_WIDGET (iter->data)->allocation.width / 2 +
               GTK_WIDGET (iter->data)->allocation.x - xoffset))
            {
                break;
            }
        }
    }

    g_list_free (children);

    gtk_box_reorder_child (GTK_BOX (active_box), source, i);

    xfwm_settings_save_button_layout (builder);
}

static void
cb_xfwm_title_button_alignment_changed (GtkComboBox *combo,
                                        GtkWidget   *button)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;
    gchar        *value;
    float         align = 0.5f;

    model = gtk_combo_box_get_model (combo);
    gtk_combo_box_get_active_iter (combo, &iter);
    gtk_tree_model_get (model, &iter, 1, &value, -1);

    if (g_str_equal (value, "left"))
    {
        align = 0.0f;
    }
    else if (g_str_equal (value, "right"))
    {
        align = 1.0f;
    }

    gtk_button_set_alignment (GTK_BUTTON (button), align, 0.5f);

    g_free (value);
}

static void
cb_xfwm_title_button_drag_data (GtkWidget        *widget,
                                GdkDragContext   *drag_context,
                                GtkSelectionData *data,
                                guint             info,
                                guint             timestamp)
{
    const gchar *name;

    name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

    gtk_widget_hide (widget);
    gtk_selection_data_set (data, gdk_atom_intern ("_xfwm4_button_layout", FALSE), 8,
                            (const guchar *)name, strlen (name));
}



static void
cb_xfwm_title_button_drag_begin (GtkWidget      *widget,
                                 GdkDragContext *drag_context)
{
    GdkPixbuf *pixbuf;

    g_return_if_fail (GTK_IS_WIDGET (widget));

    pixbuf = xfwm_settings_create_icon_from_widget (widget);
    gtk_drag_source_set_icon_pixbuf (widget, pixbuf);
    g_object_unref (pixbuf);

    gtk_widget_hide (widget);
}

static void
cb_xfwm_title_button_drag_end (GtkWidget      *widget,
                               GdkDragContext *drag_context)
{
    gtk_widget_show (widget);
}

static gboolean
cb_appearance_settings_signal_blocker (GtkWidget *widget)
{
    return TRUE;
}

static GdkPixbuf *
xfwm_settings_create_icon_from_widget (GtkWidget *widget)
{
  GdkWindow *drawable;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  drawable = GDK_DRAWABLE (gtk_widget_get_parent_window (widget));
  return gdk_pixbuf_get_from_drawable (NULL, drawable, NULL,
                                       widget->allocation.x, widget->allocation.y, 0, 0,
                                       widget->allocation.width, widget->allocation.height);
}

