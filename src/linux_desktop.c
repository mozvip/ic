#include "linux_desktop.h"
#include <fontconfig/fontconfig.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <locale.h>
#include "comic_viewer.h"
#include "file_browser.h"
#include "imgui_layer.h"
#include "i18n.h"

static GtkWidget *controls;
static GtkWidget *status;
static GtkWidget *trash_control;
static bool gtk_ready;

void linux_desktop_init(void) {
    setlocale(LC_ALL, "");
    g_set_prgname(IC_APPLICATION_ID);
    const char *appdir = g_getenv("APPDIR");
    char *locale_dir = appdir ? g_build_filename(appdir, "usr", "share", "locale", NULL) : NULL;
    bindtextdomain("ic", locale_dir ? locale_dir : IC_LOCALE_DIR);
    bind_textdomain_codeset("ic", "UTF-8");
    g_free(locale_dir);
}

static void command(GtkButton *button, gpointer data) {
    (void)button;
    SDL_Keycode key = GPOINTER_TO_INT(data);
    if (key == SDLK_DELETE && viewer.type != SOURCE_DIRECTORY) return;
    imgui_layer_set_visible(false);
    SDL_Event event = {0};
    event.type = key == SDLK_ESCAPE ? SDL_EVENT_QUIT : SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.windowID = SDL_GetWindowID(viewer.window);
    if (key == SDLK_KP_PLUS && !viewer.zoomed) {
        event.key.key = SDLK_Z;
        SDL_PushEvent(&event);
        event.key.key = key;
    }
    SDL_PushEvent(&event);
}

static void selected_file(GtkDialog *dialog, gint response, gpointer data) {
    (void)data;
    if (response == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        file_browser_load_path(path);
        g_free(path);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void open_file(GtkButton *button, gpointer data) {
    (void)button;
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(IC_("Open Comic"), GTK_WINDOW(controls),
        GTK_FILE_CHOOSER_ACTION_OPEN, IC_("Cancel"), GTK_RESPONSE_CANCEL,
        IC_("Open"), GTK_RESPONSE_ACCEPT, NULL);
    g_signal_connect(dialog, "response", G_CALLBACK(selected_file), NULL);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_widget_show(dialog);
}

static void closed_controls(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    controls = status = trash_control = NULL;
}

void linux_desktop_show_controls(void) {
    if (!gtk_ready) gtk_ready = gtk_init_check(NULL, NULL);
    if (!gtk_ready) { SDL_Log("Native controls unavailable: no GTK display"); return; }
    if (controls) { gtk_window_present(GTK_WINDOW(controls)); return; }
    controls = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(controls), IC_("IC Reading Controls"));
    gtk_window_set_icon_name(GTK_WINDOW(controls), IC_APPLICATION_ID);
    gtk_window_set_default_size(GTK_WINDOW(controls), 420, 240);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(controls), box);
    status = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(status), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(status), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(status), 50);
    gtk_label_set_selectable(GTK_LABEL(status), TRUE);
    gtk_box_pack_start(GTK_BOX(box), status, TRUE, TRUE, 0);
    GtkWidget *open = gtk_button_new_with_label(IC_("Open Comic"));
    g_signal_connect(open, "clicked", G_CALLBACK(open_file), NULL);
    gtk_box_pack_start(GTK_BOX(box), open, FALSE, FALSE, 0);
    const struct { const char *label; SDL_Keycode key; } actions[] = {
        {IC_("Previous Page"), SDLK_LEFT}, {IC_("Next Page"), SDLK_RIGHT},
        {IC_("First Page"), SDLK_HOME}, {IC_("Last Page"), SDLK_END},
        {IC_("Zoom In"), SDLK_KP_PLUS}, {IC_("Zoom Out"), SDLK_MINUS},
        {IC_("Fullscreen"), SDLK_F}, {IC_("Move to Trash"), SDLK_DELETE},
        {IC_("Quit"), SDLK_ESCAPE}
    };
    for (size_t index = 0; index < SDL_arraysize(actions); index++) {
        GtkWidget *button = gtk_button_new_with_label(actions[index].label);
        if (actions[index].key == SDLK_DELETE) trash_control = button;
        g_signal_connect(button, "clicked", G_CALLBACK(command), GINT_TO_POINTER(actions[index].key));
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    }
    g_signal_connect(controls, "destroy", G_CALLBACK(closed_controls), NULL);
    gtk_widget_show_all(controls);
}

void linux_desktop_pump(void) {
    if (!gtk_ready) return;
    if (trash_control) gtk_widget_set_sensitive(trash_control,
        viewer.type == SOURCE_DIRECTORY && viewer.view_count > 1);
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
    if (status) {
        char *label = g_strdup_printf(IC_("%s\nPage %d of %d\n%s"),
            viewer.source_path ? viewer.source_path : IC_("No comic open"),
            viewer.current_view_node ? viewer.current_view_index + 1 : 0, viewer.view_count,
            SDL_GetTicks() <= viewer.gamepad_status_until ? viewer.gamepad_status_msg : "");
        if (strcmp(gtk_label_get_text(GTK_LABEL(status)), label)) gtk_label_set_text(GTK_LABEL(status), label);
        g_free(label);
    }
}

void linux_desktop_cleanup(void) {
    if (controls) gtk_widget_destroy(controls);
}

bool linux_desktop_trash_file(const char *path, char *error, size_t error_size) {
    GFile *file = g_file_new_for_path(path);
    GError *failure = NULL;
    bool result = g_file_trash(file, NULL, &failure);
    if (!result && error && error_size) {
        g_strlcpy(error, failure ? failure->message : "Trash unavailable", error_size);
    }
    g_clear_error(&failure);
    g_object_unref(file);
    return result;
}

TTF_Font *linux_desktop_open_font(float size) {
    FcConfig *config = FcInitLoadConfigAndFonts();
    if (!config) return NULL;
    FcPattern *pattern = FcNameParse((const FcChar8 *)"sans-serif");
    if (!pattern) { FcConfigDestroy(config); return NULL; }
    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcPattern *match = FcFontMatch(config, pattern, &result);
    FcPatternDestroy(pattern);
    TTF_Font *font = NULL;
    FcChar8 *path = NULL;
    if (match && FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch) {
        font = TTF_OpenFont((const char *)path, size);
    }
    if (match) FcPatternDestroy(match);
    FcConfigDestroy(config);
    return font;
}