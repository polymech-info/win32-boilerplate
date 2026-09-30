/*
 * PM-Image Linux GUI
 * Web panel scaffolding with WebKitGTK, GTK3/GTK4 compatible.
 */

#include <gtk/gtk.h>
#if defined(PM_GTK_USE_ADWAITA)
#  include <adwaita.h>
#endif
#if defined(PM_HAVE_LIBPANEL)
#  include <libpanel.h>
#endif
#if defined(PM_HAVE_WEBKITGTK)
#  if defined(PM_WEBKITGTK_API_60)
#    include <webkit/webkit.h>
#  else
#    include <webkit2/webkit2.h>
#  endif
#endif
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// App ID for GTK/GNOME (matches .desktop entry expected ID)
constexpr const char* APP_ID = "com.polymech.pm-image";

// Window default size
constexpr int WINDOW_DEFAULT_WIDTH = 1400;
constexpr int WINDOW_DEFAULT_HEIGHT = 900;

struct LinuxUiState {
  GtkApplication* app = nullptr;
  GtkStack* main_stack = nullptr;
  GtkDirectoryList* file_directory_list = nullptr;
  GtkLabel* file_browser_path_label = nullptr;
#if defined(PM_HAVE_LIBPANEL)
  PanelWorkbench* workbench = nullptr;
#endif
#if defined(PM_HAVE_WEBKITGTK)
  GtkWidget* webview = nullptr;
  GtkWindow* window = nullptr;
#endif
};

// Forward declarations
static void on_activate(GtkApplication* app, gpointer user_data);
static void on_startup(GtkApplication* app, gpointer user_data);
static GtkWidget* create_main_window(GtkApplication* app, LinuxUiState* state);
static GtkWidget* create_content_area(LinuxUiState* state);
static void add_to_box(GtkWidget* box, GtkWidget* child, bool expand);
static void add_to_list_box(GtkWidget* list_box, GtkWidget* row_child);
static void paned_set_start(GtkWidget* paned, GtkWidget* child);
static void paned_set_end(GtkWidget* paned, GtkWidget* child);
static void on_nav_row_selected(GtkListBox* list_box, GtkListBoxRow* row, gpointer user_data);
static std::string format_size_readable(goffset bytes);
static void set_file_browser_directory(LinuxUiState* state, GFile* dir_file);
static void on_file_browser_home_clicked(GtkButton* button, gpointer user_data);
static void on_file_browser_up_clicked(GtkButton* button, gpointer user_data);
static void on_file_browser_activate(GtkColumnView* view, guint position, gpointer user_data);
static void on_name_factory_setup(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void on_name_factory_bind(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void on_type_factory_setup(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void on_type_factory_bind(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void on_size_factory_setup(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
static void on_size_factory_bind(GtkSignalListItemFactory* factory, GtkListItem* list_item, gpointer user_data);
#if defined(PM_HAVE_LIBPANEL)
static GtkWidget* create_libpanel_content_area(LinuxUiState* state);
static void add_libpanel_widget(PanelDocumentWorkspace* workspace,
                                GtkWidget* child,
                                const char* title,
                                const char* icon_name,
                                PanelArea area);
#endif
#if defined(PM_GTK_USE_ADWAITA)
static void on_color_scheme_activate(GSimpleAction* action, GVariant* parameter, gpointer user_data);
#endif
static void on_theme_toggle_toggled(GtkToggleButton* button, gpointer user_data);
static void on_command_palette_activate(GSimpleAction* action, GVariant* parameter, gpointer user_data);
static void on_quit_activate(GSimpleAction* action, GVariant* parameter, gpointer user_data);
#if defined(PM_HAVE_WEBKITGTK)
static void load_initial_web_panel(LinuxUiState* state);
static std::string load_chat_web_html_from_dist();
static void web_post_to_panel(LinuxUiState* state, const std::string& json_utf8);
static std::string json_escape(const std::string& s);
static void handle_web_message(LinuxUiState* state, const std::string& msg);
static void handle_pick_file(LinuxUiState* state);
static bool looks_like_image_path(const std::string& path);
static bool looks_like_video_path(const std::string& path);
static void install_webkit_bridge(LinuxUiState* state);
static void on_web_message_received(WebKitUserContentManager* manager,
#if defined(PM_WEBKITGTK_API_60)
                                    JSCValue* js_value,
#else
                                    WebKitJavascriptResult* js_result,
#endif
                                    gpointer user_data);
#endif

int main(int argc, char* argv[]) {
  spdlog::info("pm-image-gui: starting Linux UI");

#if defined(PM_GTK_USE_ADWAITA)
  // Initialize libadwaita (if available)
  adw_init();
#endif

  GtkApplication* app = gtk_application_new(APP_ID, G_APPLICATION_FLAGS_NONE);
  auto* ui_state = new LinuxUiState{};
  ui_state->app = app;

  g_signal_connect(app, "startup", G_CALLBACK(on_startup), ui_state);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), ui_state);

  int status = g_application_run(G_APPLICATION(app), argc, argv);

  delete ui_state;
  g_object_unref(app);
  return status;
}

static void on_startup(GtkApplication* /*app*/, gpointer /*user_data*/) {
  spdlog::info("pm-image-gui: startup");
#if defined(PM_GTK_USE_ADWAITA)
  // Theme selector in libpanel binds to this app action.
  GSimpleAction* action =
      g_simple_action_new_stateful("color-scheme", G_VARIANT_TYPE_STRING, g_variant_new_string("system"));
  g_signal_connect(action, "activate", G_CALLBACK(on_color_scheme_activate), nullptr);
  g_action_map_add_action(G_ACTION_MAP(g_application_get_default()), G_ACTION(action));
  g_object_unref(action);
#endif

  GSimpleAction* cmd_action = g_simple_action_new("command-palette", nullptr);
  g_signal_connect(cmd_action, "activate", G_CALLBACK(on_command_palette_activate), nullptr);
  g_action_map_add_action(G_ACTION_MAP(g_application_get_default()), G_ACTION(cmd_action));
  g_object_unref(cmd_action);

  GSimpleAction* quit_action = g_simple_action_new("quit", nullptr);
  g_signal_connect(quit_action, "activate", G_CALLBACK(on_quit_activate), nullptr);
  g_action_map_add_action(G_ACTION_MAP(g_application_get_default()), G_ACTION(quit_action));
  g_object_unref(quit_action);
}

static void on_activate(GtkApplication* app, gpointer user_data) {
  auto* state = static_cast<LinuxUiState*>(user_data);
  spdlog::info("pm-image-gui: activate");

  GtkWindow* existing = gtk_application_get_active_window(app);
  if (existing) {
    gtk_window_present(existing);
    return;
  }

#if defined(PM_HAVE_LIBPANEL)
  GtkWidget* workspace = create_libpanel_content_area(state);
#if defined(PM_HAVE_WEBKITGTK)
  state->window = GTK_WINDOW(workspace);
#endif
  state->workbench = panel_workbench_new();
  panel_workbench_add_workspace(state->workbench, PANEL_WORKSPACE(workspace));
  panel_workbench_focus_workspace(state->workbench, PANEL_WORKSPACE(workspace));
#else
  GtkWidget* window = create_main_window(app, state);
  gtk_window_present(GTK_WINDOW(window));
#endif
}

static GtkWidget* create_main_window(GtkApplication* app, LinuxUiState* state) {
#if defined(PM_HAVE_LIBPANEL)
  (void)app;
  return create_libpanel_content_area(state);
#else
  GtkWidget* window = gtk_application_window_new(app);
#if defined(PM_HAVE_WEBKITGTK)
  state->window = GTK_WINDOW(window);
#else
  (void)state;
#endif
  gtk_window_set_title(GTK_WINDOW(window), "PM-Image");
  gtk_window_set_default_size(GTK_WINDOW(window), WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT);

  GtkWidget* content = create_content_area(state);
#if defined(PM_GTK_USE_GTK4)
  gtk_window_set_child(GTK_WINDOW(window), content);
#else
  gtk_container_add(GTK_CONTAINER(window), content);
  gtk_widget_show_all(window);
#endif
  return window;
#endif
}

static void add_to_box(GtkWidget* box, GtkWidget* child, bool expand) {
#if defined(PM_GTK_USE_GTK4)
  gtk_widget_set_hexpand(child, expand ? TRUE : FALSE);
  gtk_widget_set_vexpand(child, expand ? TRUE : FALSE);
  gtk_box_append(GTK_BOX(box), child);
#else
  gtk_box_pack_start(GTK_BOX(box), child, expand ? TRUE : FALSE, expand ? TRUE : FALSE, 0);
#endif
}

static void add_to_list_box(GtkWidget* list_box, GtkWidget* row_child) {
#if defined(PM_GTK_USE_GTK4)
  gtk_list_box_append(GTK_LIST_BOX(list_box), row_child);
#else
  gtk_container_add(GTK_CONTAINER(list_box), row_child);
#endif
}

static void paned_set_start(GtkWidget* paned, GtkWidget* child) {
#if defined(PM_GTK_USE_GTK4)
  gtk_paned_set_start_child(GTK_PANED(paned), child);
#else
  gtk_paned_pack1(GTK_PANED(paned), child, TRUE, FALSE);
#endif
}

static void paned_set_end(GtkWidget* paned, GtkWidget* child) {
#if defined(PM_GTK_USE_GTK4)
  gtk_paned_set_end_child(GTK_PANED(paned), child);
#else
  gtk_paned_pack2(GTK_PANED(paned), child, TRUE, FALSE);
#endif
}

static void on_nav_row_selected(GtkListBox* /*list_box*/, GtkListBoxRow* row, gpointer user_data) {
  if (!row) {
    return;
  }
  auto* state = static_cast<LinuxUiState*>(user_data);
  if (!state || !state->main_stack) {
    return;
  }
  const int index = gtk_list_box_row_get_index(row);
  const char* target = "main";
  if (index == 0) {
    target = "files";
  } else if (index == 4) {
    target = "chat";
  }
  gtk_stack_set_visible_child_name(state->main_stack, target);
}

static std::string format_size_readable(goffset bytes) {
  if (bytes < 0) {
    return "-";
  }
  static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit < (sizeof(kUnits) / sizeof(kUnits[0])) - 1) {
    value /= 1024.0;
    ++unit;
  }
  std::ostringstream oss;
  if (unit == 0) {
    oss << static_cast<long long>(bytes) << ' ' << kUnits[unit];
  } else {
    oss.setf(std::ios::fixed);
    oss.precision(value >= 10.0 ? 1 : 2);
    oss << value << ' ' << kUnits[unit];
  }
  return oss.str();
}

static void set_file_browser_directory(LinuxUiState* state, GFile* dir_file) {
  if (!state || !state->file_directory_list || !dir_file) {
    return;
  }
  gtk_directory_list_set_file(state->file_directory_list, dir_file);
  if (state->file_browser_path_label) {
    char* path = g_file_get_path(dir_file);
    if (path) {
      gtk_label_set_text(state->file_browser_path_label, path);
      g_free(path);
    } else {
      char* uri = g_file_get_uri(dir_file);
      if (uri) {
        gtk_label_set_text(state->file_browser_path_label, uri);
        g_free(uri);
      }
    }
  }
}

static void on_file_browser_home_clicked(GtkButton* /*button*/, gpointer user_data) {
  auto* state = static_cast<LinuxUiState*>(user_data);
  if (!state) {
    return;
  }
  GFile* home = g_file_new_for_path(g_get_home_dir());
  set_file_browser_directory(state, home);
  g_object_unref(home);
}

static void on_file_browser_up_clicked(GtkButton* /*button*/, gpointer user_data) {
  auto* state = static_cast<LinuxUiState*>(user_data);
  if (!state || !state->file_directory_list) {
    return;
  }
  GFile* current = gtk_directory_list_get_file(state->file_directory_list);
  if (!current) {
    return;
  }
  GFile* parent = g_file_get_parent(current);
  if (!parent) {
    return;
  }
  set_file_browser_directory(state, parent);
  g_object_unref(parent);
}

static void on_file_browser_activate(GtkColumnView* view, guint position, gpointer user_data) {
  auto* state = static_cast<LinuxUiState*>(user_data);
  if (!state || !state->file_directory_list) {
    return;
  }
  GtkSelectionModel* model = gtk_column_view_get_model(view);
  if (!model) {
    return;
  }
  GObject* item = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(model), position));
  if (!item) {
    return;
  }
  auto* info = G_FILE_INFO(item);
  const auto file_type = g_file_info_get_file_type(info);
  if (file_type == G_FILE_TYPE_DIRECTORY) {
    const char* name = g_file_info_get_name(info);
    GFile* current = gtk_directory_list_get_file(state->file_directory_list);
    if (name && current) {
      GFile* child = g_file_get_child(current, name);
      set_file_browser_directory(state, child);
      g_object_unref(child);
    }
  }
  g_object_unref(item);
}

static void on_name_factory_setup(GtkSignalListItemFactory* /*factory*/, GtkListItem* list_item, gpointer /*user_data*/) {
  GtkWidget* label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_list_item_set_child(list_item, label);
}

static void on_name_factory_bind(GtkSignalListItemFactory* /*factory*/, GtkListItem* list_item, gpointer /*user_data*/) {
  auto* info = G_FILE_INFO(gtk_list_item_get_item(list_item));
  auto* label = GTK_LABEL(gtk_list_item_get_child(list_item));
  if (!info || !label) {
    return;
  }
  const char* display = g_file_info_get_display_name(info);
  gtk_label_set_text(label, (display && *display) ? display : g_file_info_get_name(info));
}

static void on_type_factory_setup(GtkSignalListItemFactory* /*factory*/, GtkListItem* list_item, gpointer /*user_data*/) {
  GtkWidget* label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_list_item_set_child(list_item, label);
}

static void on_type_factory_bind(GtkSignalListItemFactory* /*factory*/, GtkListItem* list_item, gpointer /*user_data*/) {
  auto* info = G_FILE_INFO(gtk_list_item_get_item(list_item));
  auto* label = GTK_LABEL(gtk_list_item_get_child(list_item));
  if (!info || !label) {
    return;
  }
  const auto file_type = g_file_info_get_file_type(info);
  const char* kind = "Other";
  if (file_type == G_FILE_TYPE_DIRECTORY) {
    kind = "Folder";
  } else if (file_type == G_FILE_TYPE_REGULAR) {
    kind = "File";
  } else if (file_type == G_FILE_TYPE_SYMBOLIC_LINK) {
    kind = "Symlink";
  }
  gtk_label_set_text(label, kind);
}

static void on_size_factory_setup(GtkSignalListItemFactory* /*factory*/, GtkListItem* list_item, gpointer /*user_data*/) {
  GtkWidget* label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(label), 1.0f);
  gtk_list_item_set_child(list_item, label);
}

static void on_size_factory_bind(GtkSignalListItemFactory* /*factory*/, GtkListItem* list_item, gpointer /*user_data*/) {
  auto* info = G_FILE_INFO(gtk_list_item_get_item(list_item));
  auto* label = GTK_LABEL(gtk_list_item_get_child(list_item));
  if (!info || !label) {
    return;
  }
  const auto file_type = g_file_info_get_file_type(info);
  if (file_type == G_FILE_TYPE_DIRECTORY) {
    gtk_label_set_text(label, "-");
    return;
  }
  const std::string size = format_size_readable(g_file_info_get_size(info));
  gtk_label_set_text(label, size.c_str());
}

static GtkWidget* create_content_area(LinuxUiState* state) {
#if defined(PM_HAVE_LIBPANEL)
  return create_libpanel_content_area(state);
#else
  // Left navigation panel
  GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* nav_title = gtk_label_new("Navigation");
  gtk_widget_set_halign(nav_title, GTK_ALIGN_START);
  add_to_box(nav_box, nav_title, false);

  GtkWidget* nav_list = gtk_list_box_new();
  add_to_box(nav_box, nav_list, true);
  const char* nav_items[] = {"Files", "Projects", "Recent", "Favorites", "Chat"};
  for (const char* item : nav_items) {
    GtkWidget* row = gtk_label_new(item);
    gtk_widget_set_halign(row, GTK_ALIGN_START);
    add_to_list_box(nav_list, row);
  }
  g_signal_connect(nav_list, "row-selected", G_CALLBACK(on_nav_row_selected), state);

  // Main center area: tabs + native files + chat.
  GtkWidget* main_stack = gtk_stack_new();
  state->main_stack = GTK_STACK(main_stack);
  gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

  // Default "Main" destination with tabs.
  GtkWidget* notebook = gtk_notebook_new();
  GtkWidget* editor_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* editor_label = gtk_label_new("Main tab content");
  add_to_box(editor_tab, editor_label, true);

  GtkWidget* media_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* media_label = gtk_label_new("Media / Preview tab");
  add_to_box(media_tab, media_label, true);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), editor_tab, gtk_label_new("Main"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), media_tab, gtk_label_new("Preview"));
  gtk_stack_add_named(GTK_STACK(main_stack), notebook, "main");

  // Files destination from navigation (native GTK browser).
  GtkWidget* files_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* files_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* home_btn = gtk_button_new_with_label("Home");
  GtkWidget* up_btn = gtk_button_new_with_label("Up");
  GtkWidget* path_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(path_label), 0.0f);
  gtk_widget_set_hexpand(path_label, TRUE);
  add_to_box(files_toolbar, home_btn, false);
  add_to_box(files_toolbar, up_btn, false);
  add_to_box(files_toolbar, path_label, true);
  add_to_box(files_page, files_toolbar, false);
  state->file_browser_path_label = GTK_LABEL(path_label);

  GtkDirectoryList* dir_list = gtk_directory_list_new(
      "standard::name,standard::display-name,standard::type,standard::size", nullptr);
  state->file_directory_list = dir_list;
  GtkSelectionModel* file_selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(dir_list)));
  GtkWidget* file_view = gtk_column_view_new(file_selection);
  gtk_widget_set_vexpand(file_view, TRUE);

  GtkListItemFactory* name_factory = gtk_signal_list_item_factory_new();
  g_signal_connect(name_factory, "setup", G_CALLBACK(on_name_factory_setup), nullptr);
  g_signal_connect(name_factory, "bind", G_CALLBACK(on_name_factory_bind), nullptr);
  GtkColumnViewColumn* name_col = gtk_column_view_column_new("Name", name_factory);
  gtk_column_view_column_set_expand(name_col, TRUE);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(file_view), name_col);

  GtkListItemFactory* type_factory = gtk_signal_list_item_factory_new();
  g_signal_connect(type_factory, "setup", G_CALLBACK(on_type_factory_setup), nullptr);
  g_signal_connect(type_factory, "bind", G_CALLBACK(on_type_factory_bind), nullptr);
  GtkColumnViewColumn* type_col = gtk_column_view_column_new("Type", type_factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(file_view), type_col);

  GtkListItemFactory* size_factory = gtk_signal_list_item_factory_new();
  g_signal_connect(size_factory, "setup", G_CALLBACK(on_size_factory_setup), nullptr);
  g_signal_connect(size_factory, "bind", G_CALLBACK(on_size_factory_bind), nullptr);
  GtkColumnViewColumn* size_col = gtk_column_view_column_new("Size", size_factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(file_view), size_col);

  GtkWidget* files_scroller = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(files_scroller), file_view);
  add_to_box(files_page, files_scroller, true);
  gtk_stack_add_named(GTK_STACK(main_stack), files_page, "files");

  g_signal_connect(home_btn, "clicked", G_CALLBACK(on_file_browser_home_clicked), state);
  g_signal_connect(up_btn, "clicked", G_CALLBACK(on_file_browser_up_clicked), state);
  g_signal_connect(file_view, "activate", G_CALLBACK(on_file_browser_activate), state);
  GFile* home_dir = g_file_new_for_path(g_get_home_dir());
  set_file_browser_directory(state, home_dir);
  g_object_unref(home_dir);

  // Chat destination from navigation.
  GtkWidget* chat_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
#if defined(PM_HAVE_WEBKITGTK)
#if defined(PM_WEBKITGTK_API_40) || defined(PM_WEBKITGTK_API_41) || defined(PM_WEBKITGTK_API_60)
  state->webview = webkit_web_view_new();
  auto* content_manager = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(state->webview));
  g_signal_connect(content_manager, "script-message-received::pmHost", G_CALLBACK(on_web_message_received), state);
#if defined(PM_WEBKITGTK_API_60)
  webkit_user_content_manager_register_script_message_handler(content_manager, "pmHost", nullptr);
#else
  webkit_user_content_manager_register_script_message_handler(content_manager, "pmHost");
#endif
#else
  state->webview = webkit_web_view_new();
#endif
  add_to_box(chat_page, state->webview, true);
#else
  add_to_box(chat_page, gtk_label_new("Chat web app unavailable (install WebKitGTK dev/runtime packages)."), true);
#endif
  gtk_stack_add_named(GTK_STACK(main_stack), chat_page, "chat");
  gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "main");
#if defined(PM_HAVE_WEBKITGTK)
  if (state && state->webview) {
    load_initial_web_panel(state);
  }
#endif

  // Right utility panel
  GtkWidget* right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* right_title = gtk_label_new("Right Panel");
  gtk_widget_set_halign(right_title, GTK_ALIGN_START);
  add_to_box(right_box, right_title, false);
  add_to_box(right_box, gtk_label_new("Inspector"), false);
  add_to_box(right_box, gtk_label_new("Properties"), false);
  add_to_box(right_box, gtk_label_new("History"), true);

  // 3-pane resizable layout: [left] | [main tabs] | [right]
  GtkWidget* center_right = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  paned_set_start(center_right, main_stack);
  paned_set_end(center_right, right_box);
  gtk_paned_set_position(GTK_PANED(center_right), 840);

  GtkWidget* root = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  paned_set_start(root, nav_box);
  paned_set_end(root, center_right);
  gtk_paned_set_position(GTK_PANED(root), 250);

  return root;
#endif
}

#if defined(PM_HAVE_LIBPANEL)
static void add_libpanel_widget(PanelDocumentWorkspace* workspace,
                                GtkWidget* child,
                                const char* title,
                                const char* icon_name,
                                PanelArea area) {
  PanelWidget* panel = PANEL_WIDGET(panel_widget_new());
  panel_widget_set_title(panel, title);
  panel_widget_set_icon_name(panel, icon_name);
  panel_widget_set_can_maximize(panel, TRUE);
  panel_widget_set_child(panel, child);

  PanelPosition* pos = panel_position_new();
  panel_position_set_area(pos, area);
  panel_document_workspace_add_widget(workspace, panel, pos);
  g_object_unref(pos);
}

static GtkWidget* create_libpanel_content_area(LinuxUiState* state) {
  (void)state;

  auto* workspace = PANEL_DOCUMENT_WORKSPACE(panel_document_workspace_new());
  auto* dock = panel_document_workspace_get_dock(workspace);

  panel_dock_set_reveal_start(dock, TRUE);
  panel_dock_set_reveal_end(dock, TRUE);
  panel_dock_set_reveal_bottom(dock, FALSE);
  panel_dock_set_start_width(dock, 260);
  panel_dock_set_end_width(dock, 340);

  // Header bar: title + dock toggles + icon buttons + theme selector
  GtkWidget* header = adw_header_bar_new();
  GtkWidget* title = adw_window_title_new("PM-Image", "libpanel shell");
  adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title);

  GtkWidget* left_toggle = panel_toggle_button_new(dock, PANEL_AREA_START);
  GtkWidget* right_toggle = panel_toggle_button_new(dock, PANEL_AREA_END);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), left_toggle);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), right_toggle);

  GtkWidget* refresh_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
  gtk_widget_set_tooltip_text(refresh_btn, "Refresh");
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), refresh_btn);

  GtkWidget* add_btn = gtk_button_new_from_icon_name("list-add-symbolic");
  gtk_widget_set_tooltip_text(add_btn, "Add");
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), add_btn);

  GMenu* primary_menu = g_menu_new();
  g_menu_append(primary_menu, "Command Palette", "app.command-palette");
  g_menu_append(primary_menu, "Quit", "app.quit");
  GtkWidget* menu_btn = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_btn), "open-menu-symbolic");
  gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_btn), G_MENU_MODEL(primary_menu));
  gtk_widget_set_tooltip_text(menu_btn, "Primary Menu");
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_btn);
  g_object_unref(primary_menu);

  GtkWidget* omni = panel_omni_bar_new();
  panel_omni_bar_set_progress(PANEL_OMNI_BAR(omni), 0.35);
  panel_omni_bar_add_prefix(PANEL_OMNI_BAR(omni), 0, gtk_label_new("Workspace"));
  GtkWidget* command_btn = gtk_button_new_from_icon_name("system-search-symbolic");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(command_btn), "app.command-palette");
  gtk_widget_set_tooltip_text(command_btn, "Command Palette");
  panel_omni_bar_add_suffix(PANEL_OMNI_BAR(omni), 0, command_btn);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), omni);

  panel_document_workspace_set_titlebar(workspace, header);

  // Left navigation widget
  GtkWidget* nav_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* nav_title = gtk_label_new("Navigation");
  gtk_widget_set_halign(nav_title, GTK_ALIGN_START);
  add_to_box(nav_box, nav_title, false);
  GtkWidget* nav_list = gtk_list_box_new();
  add_to_box(nav_box, nav_list, true);
  const char* nav_items[] = {"Files", "Projects", "Recent", "Favorites", "Chat"};
  for (const char* item : nav_items) {
    GtkWidget* row = gtk_label_new(item);
    gtk_widget_set_halign(row, GTK_ALIGN_START);
    add_to_list_box(nav_list, row);
  }
  g_signal_connect(nav_list, "row-selected", G_CALLBACK(on_nav_row_selected), state);
  add_libpanel_widget(workspace, nav_box, "Navigation", "view-list-symbolic", PANEL_AREA_START);

  // Main center area: standard tabs + chat destination.
  GtkWidget* main_stack = gtk_stack_new();
  state->main_stack = GTK_STACK(main_stack);
  gtk_stack_set_transition_type(GTK_STACK(main_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

  // Default "Main" destination with tabs.
  GtkWidget* notebook = gtk_notebook_new();
  GtkWidget* main_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  add_to_box(main_tab, gtk_label_new("Main tab content"), true);
  GtkWidget* preview_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  add_to_box(preview_tab, gtk_label_new("Media / Preview tab"), true);
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), main_tab, gtk_label_new("Main"));
  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), preview_tab, gtk_label_new("Preview"));
  gtk_stack_add_named(GTK_STACK(main_stack), notebook, "main");

  // Files destination from navigation (native GTK browser).
  GtkWidget* files_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* files_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget* home_btn = gtk_button_new_with_label("Home");
  GtkWidget* up_btn = gtk_button_new_with_label("Up");
  GtkWidget* path_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(path_label), 0.0f);
  gtk_widget_set_hexpand(path_label, TRUE);
  add_to_box(files_toolbar, home_btn, false);
  add_to_box(files_toolbar, up_btn, false);
  add_to_box(files_toolbar, path_label, true);
  add_to_box(files_page, files_toolbar, false);
  state->file_browser_path_label = GTK_LABEL(path_label);

  GtkDirectoryList* dir_list = gtk_directory_list_new(
      "standard::name,standard::display-name,standard::type,standard::size", nullptr);
  state->file_directory_list = dir_list;
  GtkSelectionModel* file_selection = GTK_SELECTION_MODEL(gtk_single_selection_new(G_LIST_MODEL(dir_list)));
  GtkWidget* file_view = gtk_column_view_new(file_selection);
  gtk_widget_set_vexpand(file_view, TRUE);

  GtkListItemFactory* name_factory = gtk_signal_list_item_factory_new();
  g_signal_connect(name_factory, "setup", G_CALLBACK(on_name_factory_setup), nullptr);
  g_signal_connect(name_factory, "bind", G_CALLBACK(on_name_factory_bind), nullptr);
  GtkColumnViewColumn* name_col = gtk_column_view_column_new("Name", name_factory);
  gtk_column_view_column_set_expand(name_col, TRUE);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(file_view), name_col);

  GtkListItemFactory* type_factory = gtk_signal_list_item_factory_new();
  g_signal_connect(type_factory, "setup", G_CALLBACK(on_type_factory_setup), nullptr);
  g_signal_connect(type_factory, "bind", G_CALLBACK(on_type_factory_bind), nullptr);
  GtkColumnViewColumn* type_col = gtk_column_view_column_new("Type", type_factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(file_view), type_col);

  GtkListItemFactory* size_factory = gtk_signal_list_item_factory_new();
  g_signal_connect(size_factory, "setup", G_CALLBACK(on_size_factory_setup), nullptr);
  g_signal_connect(size_factory, "bind", G_CALLBACK(on_size_factory_bind), nullptr);
  GtkColumnViewColumn* size_col = gtk_column_view_column_new("Size", size_factory);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(file_view), size_col);

  GtkWidget* scroller = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), file_view);
  add_to_box(files_page, scroller, true);
  gtk_stack_add_named(GTK_STACK(main_stack), files_page, "files");

  g_signal_connect(home_btn, "clicked", G_CALLBACK(on_file_browser_home_clicked), state);
  g_signal_connect(up_btn, "clicked", G_CALLBACK(on_file_browser_up_clicked), state);
  g_signal_connect(file_view, "activate", G_CALLBACK(on_file_browser_activate), state);
  GFile* home_dir = g_file_new_for_path(g_get_home_dir());
  set_file_browser_directory(state, home_dir);
  g_object_unref(home_dir);

  // Chat destination from navigation.
  GtkWidget* chat_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
#if defined(PM_HAVE_WEBKITGTK)
#if defined(PM_WEBKITGTK_API_40) || defined(PM_WEBKITGTK_API_41) || defined(PM_WEBKITGTK_API_60)
  state->webview = webkit_web_view_new();
  auto* content_manager = webkit_web_view_get_user_content_manager(WEBKIT_WEB_VIEW(state->webview));
  g_signal_connect(content_manager, "script-message-received::pmHost", G_CALLBACK(on_web_message_received), state);
#if defined(PM_WEBKITGTK_API_60)
  webkit_user_content_manager_register_script_message_handler(content_manager, "pmHost", nullptr);
#else
  webkit_user_content_manager_register_script_message_handler(content_manager, "pmHost");
#endif
#else
  state->webview = webkit_web_view_new();
#endif
  add_to_box(chat_page, state->webview, true);
#else
  add_to_box(chat_page, gtk_label_new("Chat web app unavailable (install WebKitGTK dev/runtime packages)."), true);
#endif
  gtk_stack_add_named(GTK_STACK(main_stack), chat_page, "chat");
  gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "main");

  add_libpanel_widget(workspace, main_stack, "Main", "applications-graphics-symbolic", PANEL_AREA_CENTER);

  // Right inspector widget
  GtkWidget* right_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* right_title = gtk_label_new("Inspector");
  gtk_widget_set_halign(right_title, GTK_ALIGN_START);
  add_to_box(right_box, right_title, false);
  add_to_box(right_box, gtk_label_new("Properties"), false);
  add_to_box(right_box, gtk_label_new("History"), false);
  add_to_box(right_box, gtk_label_new("Details"), true);
  add_libpanel_widget(workspace, right_box, "Inspector", "sidebar-show-right-symbolic", PANEL_AREA_END);

  // Bottom tool panels (tabbed in bottom dock frame).
  GtkWidget* logs_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  add_to_box(logs_box, gtk_label_new("Logs stream"), false);
  add_to_box(logs_box, gtk_label_new("Build completed, app ready."), true);
  add_libpanel_widget(workspace, logs_box, "Logs", "utilities-terminal-symbolic", PANEL_AREA_BOTTOM);

  GtkWidget* problems_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  add_to_box(problems_box, gtk_label_new("Problems"), false);
  add_to_box(problems_box, gtk_label_new("No active diagnostics."), true);
  add_libpanel_widget(workspace, problems_box, "Problems", "dialog-warning-symbolic", PANEL_AREA_BOTTOM);

  GtkWidget* tasks_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  add_to_box(tasks_box, gtk_label_new("Tasks"), false);
  add_to_box(tasks_box, gtk_label_new("No running tasks."), true);
  add_libpanel_widget(workspace, tasks_box, "Tasks", "view-list-bullet-symbolic", PANEL_AREA_BOTTOM);

  // Demo-style status bar: bottom area toggle on the suffix side.
  PanelStatusbar* status = panel_document_workspace_get_statusbar(workspace);
  GtkWidget* theme_toggle = gtk_toggle_button_new_with_label("Light");
  gtk_widget_set_tooltip_text(theme_toggle, "Toggle dark/light theme");
  g_signal_connect(theme_toggle, "toggled", G_CALLBACK(on_theme_toggle_toggled), nullptr);
  GtkWidget* bottom_toggle = panel_toggle_button_new(dock, PANEL_AREA_BOTTOM);
  panel_statusbar_add_suffix(status, 0, theme_toggle);
  panel_statusbar_add_suffix(status, 0, bottom_toggle);

#if defined(PM_HAVE_WEBKITGTK)
  if (state && state->webview) {
    load_initial_web_panel(state);
  }
#endif

  return GTK_WIDGET(workspace);
}
#endif

#if defined(PM_GTK_USE_ADWAITA)
static void on_color_scheme_activate(GSimpleAction* action, GVariant* parameter, gpointer /*user_data*/) {
  if (!parameter) {
    return;
  }
  const char* value = g_variant_get_string(parameter, nullptr);
  AdwStyleManager* sm = adw_style_manager_get_default();

  if (g_strcmp0(value, "dark") == 0) {
    adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_DARK);
  } else if (g_strcmp0(value, "light") == 0) {
    adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_FORCE_LIGHT);
  } else {
    adw_style_manager_set_color_scheme(sm, ADW_COLOR_SCHEME_DEFAULT);
  }

  g_simple_action_set_state(action, parameter);
}
#endif

static void on_theme_toggle_toggled(GtkToggleButton* button, gpointer /*user_data*/) {
#if defined(PM_GTK_USE_ADWAITA)
  const gboolean dark_enabled = gtk_toggle_button_get_active(button);
  AdwStyleManager* sm = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(sm, dark_enabled ? ADW_COLOR_SCHEME_FORCE_DARK : ADW_COLOR_SCHEME_FORCE_LIGHT);
  gtk_button_set_label(GTK_BUTTON(button), dark_enabled ? "Dark" : "Light");

  GAction* action = g_action_map_lookup_action(G_ACTION_MAP(g_application_get_default()), "color-scheme");
  if (G_IS_SIMPLE_ACTION(action)) {
    g_simple_action_set_state(G_SIMPLE_ACTION(action), g_variant_new_string(dark_enabled ? "dark" : "light"));
  }
#else
  (void)button;
#endif
}

static void on_command_palette_activate(GSimpleAction* /*action*/, GVariant* /*parameter*/, gpointer /*user_data*/) {
  spdlog::info("pm-image-gui: command palette requested");
}

static void on_quit_activate(GSimpleAction* /*action*/, GVariant* /*parameter*/, gpointer /*user_data*/) {
  auto* app = g_application_get_default();
  if (app) {
    g_application_quit(app);
  }
}

#if defined(PM_HAVE_WEBKITGTK)
static void load_initial_web_panel(LinuxUiState* state) {
  if (!state || !state->webview) {
    return;
  }
  install_webkit_bridge(state);

  std::string html = load_chat_web_html_from_dist();
  if (!html.empty()) {
    // First-pass parity with ChatWebPanel: load shipped chat.html from dist.
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(state->webview), html.c_str(), "https://pm-linux.invalid/");
    spdlog::info("pm-image-gui: loaded dist/chat.html into WebKit panel");
    return;
  }

  // Fallback content while web bundle is not present yet.
  const char* fallback_html =
      "<!doctype html><html><head><meta charset='utf-8'/>"
      "<title>PM-Image Web Panel</title>"
      "<style>body{font-family:sans-serif;margin:0;padding:16px;background:#111;color:#eee}"
      "h1{font-size:18px;margin:0 0 8px 0}code{background:#222;padding:2px 6px;border-radius:4px}</style>"
      "</head><body><h1>PM-Image Web Panel (Linux)</h1>"
      "<p>WebView is wired with WebKitGTK.</p>"
      "<p><code>dist/chat.html</code> not found. Run <code>npm run build:chat-next:embed</code>.</p>"
      "</body></html>";
  webkit_web_view_load_html(WEBKIT_WEB_VIEW(state->webview), fallback_html, "https://pm-linux.invalid/");
  spdlog::warn("pm-image-gui: dist/chat.html not found; using fallback page");
}

static std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

static void web_post_to_panel(LinuxUiState* state, const std::string& json_utf8) {
  if (!state || !state->webview) {
    return;
  }
  const std::string escaped = json_escape(json_utf8);
  const std::string js =
      "if (window.cwWebOnHostMessage) {"
      "window.cwWebOnHostMessage(\"" +
      escaped + "\");"
                "} else {"
                "window.dispatchEvent(new CustomEvent('pm-host-message',{detail:\"" +
      escaped + "\"}));"
                "}";
  webkit_web_view_evaluate_javascript(
      WEBKIT_WEB_VIEW(state->webview), js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

static bool looks_like_image_path(const std::string& path) {
  static const char* exts[] = {".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif", ".tif", ".tiff"};
  for (const auto* ext : exts) {
    const auto n = std::strlen(ext);
    if (path.size() >= n && std::equal(ext, ext + n, path.end() - static_cast<std::ptrdiff_t>(n),
                                       [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
      return true;
    }
  }
  return false;
}

static bool looks_like_video_path(const std::string& path) {
  static const char* exts[] = {".mp4", ".mov", ".mkv", ".avi", ".webm", ".m4v"};
  for (const auto* ext : exts) {
    const auto n = std::strlen(ext);
    if (path.size() >= n && std::equal(ext, ext + n, path.end() - static_cast<std::ptrdiff_t>(n),
                                       [](char a, char b) { return std::tolower(a) == std::tolower(b); })) {
      return true;
    }
  }
  return false;
}

static void handle_pick_file(LinuxUiState* state) {
  if (!state || !state->window) {
    return;
  }
#if defined(PM_GTK_USE_GTK4)
  // TODO: Migrate to GtkFileDialog async flow. Keep build-compatible for now.
  spdlog::warn("pm-image-gui: file picker from web panel not yet implemented on GTK4");
  web_post_to_panel(state, "{\"t\":\"pm_pick_file_result\",\"ok\":false}");
  return;
#else
  GtkWidget* dialog = gtk_file_chooser_dialog_new("Pick media file",
                                                   state->window,
                                                   GTK_FILE_CHOOSER_ACTION_OPEN,
                                                   "_Cancel",
                                                   GTK_RESPONSE_CANCEL,
                                                   "_Open",
                                                   GTK_RESPONSE_ACCEPT,
                                                   nullptr);
  const int response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response == GTK_RESPONSE_ACCEPT) {
    gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if (filename) {
      const std::string path(filename);
      const bool is_image = looks_like_image_path(path);
      const bool is_video = looks_like_video_path(path);
      std::string kind = is_image ? "image" : (is_video ? "video" : "file");

      std::ostringstream oss;
      oss << "{\"t\":\"pm_preview_selected\",\"kind\":\"" << kind << "\",\"path\":\""
          << json_escape(path) << "\",\"uri\":\"file://" << json_escape(path) << "\"}";
      web_post_to_panel(state, oss.str());
      spdlog::info("pm-image-gui: selected file {}", path);
      g_free(filename);
    }
  }
  gtk_widget_destroy(dialog);
#endif
}

static void handle_web_message(LinuxUiState* state, const std::string& msg) {
  if (msg.find("\"t\":\"pm_pick_file\"") != std::string::npos || msg.find("\"t\": \"pm_pick_file\"") != std::string::npos) {
    handle_pick_file(state);
    return;
  }
  if (msg.find("\"t\":\"pm_ping\"") != std::string::npos || msg.find("\"t\": \"pm_ping\"") != std::string::npos) {
    web_post_to_panel(state, "{\"t\":\"pm_pong\",\"ok\":true}");
    return;
  }
  spdlog::info("pm-image-gui: web message {}", msg);
}

static void on_web_message_received(WebKitUserContentManager*,
#if defined(PM_WEBKITGTK_API_60)
                                    JSCValue* js_value,
#else
                                    WebKitJavascriptResult* js_result,
#endif
                                    gpointer user_data) {
  auto* state = static_cast<LinuxUiState*>(user_data);
#if defined(PM_WEBKITGTK_API_60)
  JSCValue* value = js_value;
#else
  JSCValue* value = webkit_javascript_result_get_js_value(js_result);
#endif
  if (!value || !jsc_value_is_string(value)) {
    return;
  }
  char* c_msg = jsc_value_to_string(value);
  if (!c_msg) {
    return;
  }
  std::string msg(c_msg);
  g_free(c_msg);
  handle_web_message(state, msg);
}

static void install_webkit_bridge(LinuxUiState* state) {
  if (!state || !state->webview) {
    return;
  }
  const char* bridge_js =
      "(function(){"
      "if(window.__pmBridgeInstalled)return;"
      "window.__pmBridgeInstalled=true;"
      "window.pmHost={"
      "postMessage:function(payload){"
      "var msg=(typeof payload==='string')?payload:JSON.stringify(payload||{});"
      "if(window.webkit&&window.webkit.messageHandlers&&window.webkit.messageHandlers.pmHost){"
      "window.webkit.messageHandlers.pmHost.postMessage(msg);"
      "}"
      "}"
      "};"
      "window.cwWebOnHostMessage=window.cwWebOnHostMessage||function(raw){"
      "try{var m=JSON.parse(raw);window.dispatchEvent(new CustomEvent('pm-host-message',{detail:m}));}"
      "catch(_){window.dispatchEvent(new CustomEvent('pm-host-message',{detail:raw}));}"
      "};"
      "})();";
  webkit_web_view_evaluate_javascript(
      WEBKIT_WEB_VIEW(state->webview), bridge_js, -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

static std::string load_chat_web_html_from_dist() {
  namespace fs = std::filesystem;
  std::vector<fs::path> candidates = {
      fs::path("dist") / "chat.html",
      fs::path("../dist") / "chat.html",
      fs::path("../../dist") / "chat.html",
  };

  for (const auto& path : candidates) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec || !fs::is_regular_file(path, ec)) {
      continue;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      continue;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  return {};
}
#endif
