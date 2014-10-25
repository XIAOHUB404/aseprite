/* Aseprite
 * Copyright (C) 2001-2014  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/commands/params.h"
#include "app/console.h"
#include "app/document.h"
#include "app/document_location.h"
#include "app/ini_file.h"
#include "app/modules/editors.h"
#include "app/modules/gfx.h"
#include "app/modules/gui.h"
#include "app/modules/palettes.h"
#include "app/settings/settings.h"
#include "app/tools/ink.h"
#include "app/tools/tool_box.h"
#include "app/ui/editor/editor.h"
#include "app/ui/main_menu_bar.h"
#include "app/ui/main_menu_bar.h"
#include "app/ui/main_window.h"
#include "app/ui/skin/button_icon_impl.h"
#include "app/ui/skin/skin_property.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/status_bar.h"
#include "app/ui/toolbar.h"
#include "app/ui_context.h"
#include "base/memory.h"
#include "base/shared_ptr.h"
#include "base/unique_ptr.h"
#include "raster/sprite.h"
#include "she/clipboard.h"
#include "she/display.h"
#include "she/error.h"
#include "she/surface.h"
#include "she/system.h"
#include "ui/intern.h"
#include "ui/ui.h"

#include <algorithm>
#include <allegro.h>
#include <allegro/internal/aintern.h>
#include <list>
#include <vector>

#ifdef ALLEGRO_WINDOWS
#include <winalleg.h>

#endif

#define SPRITEDITOR_ACTION_COPYSELECTION        "CopySelection"
#define SPRITEDITOR_ACTION_SNAPTOGRID           "SnapToGrid"
#define SPRITEDITOR_ACTION_ANGLESNAP            "AngleSnap"
#define SPRITEDITOR_ACTION_MAINTAINASPECTRATIO  "MaintainAspectRatio"
#define SPRITEDITOR_ACTION_LOCKAXIS             "LockAxis"
#define SPRITEDITOR_ACTION_ADDSEL               "AddSelection"
#define SPRITEDITOR_ACTION_SUBSEL               "SubtractSelection"

namespace app {

using namespace gfx;
using namespace ui;
using namespace app::skin;

static struct {
  int width;
  int height;
  int scale;
} try_resolutions[] = { { 1024, 768, 2 },
                        {  800, 600, 2 },
                        {  640, 480, 2 },
                        {  320, 240, 1 },
                        {  320, 200, 1 },
                        {    0,   0, 0 } };

//////////////////////////////////////////////////////////////////////

struct Shortcut {
  enum class Type {
    ExecuteCommand,
    ChangeTool,
    EditorQuicktool,
    SpriteEditor
  };

  Accelerator* accel;
  Type type;
  Command* command;
  KeyContext keycontext;
  tools::Tool* tool;
  std::string action;
  Params* params;

  Shortcut(Type type);
  ~Shortcut();

  void add_shortcut(const char* shortcut_string);
  bool is_pressed(Message* msg);
  bool is_pressed_from_key_array();

};

static Shortcut* get_keyboard_shortcut_for_command(const char* command_name, Params* params);
static Shortcut* get_keyboard_shortcut_for_tool(tools::Tool* tool);
static Shortcut* get_keyboard_shortcut_for_quicktool(tools::Tool* tool);
static Shortcut* get_keyboard_shortcut_for_spriteeditor(const char* action_name);

//////////////////////////////////////////////////////////////////////

class CustomizedGuiManager : public Manager
                           , public LayoutIO
{
protected:
  bool onProcessMessage(Message* msg) override;
  LayoutIO* onGetLayoutIO() override { return this; }

  // LayoutIO implementation
  std::string loadLayout(Widget* widget) override;
  void saveLayout(Widget* widget, const std::string& str) override;
};

static she::Display* main_display = NULL;
static she::Clipboard* main_clipboard = NULL;
static CustomizedGuiManager* manager = NULL;
static Theme* ase_theme = NULL;

static std::vector<Shortcut*>* shortcuts = NULL;

// Default GUI screen configuration
static int screen_scaling;

static void reload_default_font();

// Load & save graphics configuration
static void load_gui_config(int& w, int& h, bool& maximized);
static void save_gui_config();

static KeyContext get_current_keycontext()
{
  app::Context* ctx = UIContext::instance();
  DocumentLocation location = ctx->activeLocation();
  Document* doc = location.document();

  if (doc &&
      doc->isMaskVisible() &&
      ctx->settings()->getCurrentTool()->getInk(0)->isSelection())
    return KeyContext::Selection;
  else
    return KeyContext::Normal;
}

// Initializes GUI.
int init_module_gui()
{
  shortcuts = new std::vector<Shortcut*>;

  int w, h;
  bool maximized;
  load_gui_config(w, h, maximized);

  try {
    if (w > 0 && h > 0)
      main_display = she::instance()->createDisplay(w, h, screen_scaling);
  }
  catch (const she::DisplayCreationException&) {
    // Do nothing, the display wasn't created.
  }

  if (!main_display) {
    for (int c=0; try_resolutions[c].width; ++c) {
      try {
        main_display =
          she::instance()->createDisplay(try_resolutions[c].width,
                                         try_resolutions[c].height,
                                         try_resolutions[c].scale);

        screen_scaling = try_resolutions[c].scale;
        break;
      }
      catch (const she::DisplayCreationException&) {
        // Ignore
      }
    }
  }

  if (!main_display) {
    she::error_message("Unable to create a user-interface display.\n");
    return -1;
  }

  main_clipboard = she::instance()->createClipboard();

  // Create the default-manager
  manager = new CustomizedGuiManager();
  manager->setDisplay(main_display);
  manager->setClipboard(main_clipboard);

  // Setup the GUI theme for all widgets
  CurrentTheme::set(ase_theme = new SkinTheme());

  if (maximized)
    main_display->maximize();

  gui_setup_screen(true);

  // Set graphics options for next time
  save_gui_config();

  return 0;
}

void exit_module_gui()
{
  save_gui_config();

  // destroy shortcuts
  ASSERT(shortcuts != NULL);
  for (Shortcut* shortcut : *shortcuts)
    delete shortcut;
  delete shortcuts;
  shortcuts = NULL;

  delete manager;

  // Now we can destroy theme
  CurrentTheme::set(NULL);
  delete ase_theme;

  remove_keyboard();
  remove_mouse();

  main_clipboard->dispose();
  main_display->dispose();
}

static void load_gui_config(int& w, int& h, bool& maximized)
{
  w = get_config_int("GfxMode", "Width", 0);
  h = get_config_int("GfxMode", "Height", 0);
  screen_scaling = get_config_int("GfxMode", "ScreenScale", 2);
  screen_scaling = MID(1, screen_scaling, 4);
  maximized = get_config_bool("GfxMode", "Maximized", false);
}

static void save_gui_config()
{
  she::Display* display = Manager::getDefault()->getDisplay();
  if (display) {
    set_config_bool("GfxMode", "Maximized", display->isMaximized());
    set_config_int("GfxMode", "Width", display->originalWidth());
    set_config_int("GfxMode", "Height", display->originalHeight());
    set_config_int("GfxMode", "Depth", bitmap_color_depth(screen));
  }
  set_config_int("GfxMode", "ScreenScale", screen_scaling);
}

int get_screen_scaling()
{
  return screen_scaling;
}

void set_screen_scaling(int scaling)
{
  screen_scaling = scaling;
}

void update_screen_for_document(Document* document)
{
  // Without document.
  if (!document) {
    // Well, change to the default palette.
    if (set_current_palette(NULL, false)) {
      // If the palette changes, refresh the whole screen.
      Manager::getDefault()->invalidate();
    }
  }
  // With a document.
  else {
    document->notifyGeneralUpdate();

    // Update the tabs (maybe the modified status has been changed).
    app_rebuild_documents_tabs();
  }
}

void gui_run()
{
  manager->run();
}

void gui_feedback()
{
  Manager* manager = Manager::getDefault();
  OverlayManager* overlays = OverlayManager::instance();

  ui::update_cursor_overlay();

  // Avoid updating a non-dirty screen over and over again.
#if 0                           // TODO It doesn't work yet
  if (!dirty_display_flag)
    return;
#endif

  // Draw overlays.
  overlays->captureOverlappedAreas();
  overlays->drawOverlays();

  if (!manager->getDisplay()->flip()) {
    // In case that the display was resized.
    gui_setup_screen(false);
    App::instance()->getMainWindow()->remapWindow();
    manager->invalidate();
  }
  else
    overlays->restoreOverlappedAreas();

  dirty_display_flag = false;
}

// Refresh the UI display, font, etc.
void gui_setup_screen(bool reload_font)
{
  bool regen = false;
  bool reinit = false;

  main_display->setScale(screen_scaling);
  ui::set_display(main_display);

  // Update guiscale factor
  int old_guiscale = jguiscale();
  CurrentTheme::get()->guiscale = (screen_scaling == 1 &&
    ui::display_w() > 512 &&
    ui::display_h() > 256) ? 2: 1;

  // If the guiscale have changed
  if (old_guiscale != jguiscale()) {
    reload_font = true;
    regen = true;
  }

  if (reload_font) {
    reload_default_font();
    reinit = true;
  }

  // Regenerate the theme
  if (regen) {
    CurrentTheme::get()->regenerate();
    reinit = true;
  }

  if (reinit)
    reinitThemeForAllWidgets();

  // Set the configuration
  save_gui_config();
}

static void reload_default_font()
{
  Theme* theme = CurrentTheme::get();
  SkinTheme* skin_theme = static_cast<SkinTheme*>(theme);

  // Reload theme fonts
  skin_theme->reload_fonts();

  // Set all widgets fonts
  setFontOfAllWidgets(theme->default_font);
}

void load_window_pos(Widget* window, const char *section)
{
  // Default position
  Rect orig_pos = window->getBounds();
  Rect pos = orig_pos;

  // Load configurated position
  pos = get_config_rect(section, "WindowPos", pos);

  pos.w = MID(orig_pos.w, pos.w, ui::display_w());
  pos.h = MID(orig_pos.h, pos.h, ui::display_h());

  pos.setOrigin(Point(MID(0, pos.x, ui::display_w()-pos.w),
      MID(0, pos.y, ui::display_h()-pos.h)));

  window->setBounds(pos);
}

void save_window_pos(Widget* window, const char *section)
{
  set_config_rect(section, "WindowPos", window->getBounds());
}

Widget* setup_mini_font(Widget* widget)
{
  widget->setFont(((SkinTheme*)widget->getTheme())->getMiniFont());
  return widget;
}

Widget* setup_mini_look(Widget* widget)
{
  return setup_look(widget, MiniLook);
}

Widget* setup_look(Widget* widget, LookType lookType)
{
  SkinPropertyPtr skinProp = get_skin_property(widget);
  skinProp->setLook(lookType);
  return widget;
}

void setup_bevels(Widget* widget, int b1, int b2, int b3, int b4)
{
  SkinPropertyPtr skinProp = get_skin_property(widget);
  skinProp->setUpperLeft(b1);
  skinProp->setUpperRight(b2);
  skinProp->setLowerLeft(b3);
  skinProp->setLowerRight(b4);
}

// Sets the IconInterface pointer interface of the button to show the
// specified set of icons. Each icon is a part of the SkinTheme.
void set_gfxicon_to_button(ButtonBase* button,
                           int normal_part_id,
                           int selected_part_id,
                           int disabled_part_id, int icon_align)
{
  ButtonIconImpl* buttonIcon =
    new ButtonIconImpl(static_cast<SkinTheme*>(button->getTheme()),
                       normal_part_id,
                       selected_part_id,
                       disabled_part_id,
                       icon_align);

  button->setIconInterface(buttonIcon);
}

//////////////////////////////////////////////////////////////////////
// Button style (convert radio or check buttons and draw it like
// normal buttons)

CheckBox* check_button_new(const char *text, int b1, int b2, int b3, int b4)
{
  CheckBox* widget = new CheckBox(text, kButtonWidget);

  widget->setAlign(JI_CENTER | JI_MIDDLE);

  setup_mini_look(widget);
  setup_bevels(widget, b1, b2, b3, b4);
  return widget;
}

//////////////////////////////////////////////////////////////////////
// Keyboard shortcuts
//////////////////////////////////////////////////////////////////////

Accelerator* add_keyboard_shortcut_to_execute_command(const char* shortcut_string,
  const char* command_name, Params* params, KeyContext keyContext)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_command(command_name, params);
  if (!shortcut) {
    shortcut = new Shortcut(Shortcut::Type::ExecuteCommand);
    shortcut->command = CommandsModule::instance()->getCommandByName(command_name);
    shortcut->params = params ? params->clone(): new Params;
    shortcut->keycontext = keyContext;
    shortcuts->push_back(shortcut);
  }
  shortcut->add_shortcut(shortcut_string);
  return shortcut->accel;
}

Accelerator* add_keyboard_shortcut_to_change_tool(const char* shortcut_string, tools::Tool* tool)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_tool(tool);
  if (!shortcut) {
    shortcut = new Shortcut(Shortcut::Type::ChangeTool);
    shortcut->tool = tool;

    shortcuts->push_back(shortcut);
  }

  shortcut->add_shortcut(shortcut_string);
  return shortcut->accel;
}

Accelerator* add_keyboard_shortcut_to_quicktool(const char* shortcut_string, tools::Tool* tool)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_quicktool(tool);
  if (!shortcut) {
    shortcut = new Shortcut(Shortcut::Type::EditorQuicktool);
    shortcut->tool = tool;

    shortcuts->push_back(shortcut);
  }

  shortcut->add_shortcut(shortcut_string);
  return shortcut->accel;
}

Accelerator* add_keyboard_shortcut_to_spriteeditor(const char* shortcut_string, const char* action_name)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(action_name);
  if (!shortcut) {
    shortcut = new Shortcut(Shortcut::Type::SpriteEditor);
    shortcut->action = action_name;

    shortcuts->push_back(shortcut);
  }

  shortcut->add_shortcut(shortcut_string);
  return shortcut->accel;
}

bool get_command_from_key_message(Message* msg, Command** command, Params** params)
{
  for (Shortcut* shortcut : *shortcuts) {
    if (shortcut->type == Shortcut::Type::ExecuteCommand &&
        shortcut->is_pressed(msg)) {
      if (command) *command = shortcut->command;
      if (params) *params = shortcut->params;
      return true;
    }
  }
  return false;
}

Accelerator* get_accel_to_execute_command(const char* command_name, Params* params)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_command(command_name, params);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_change_tool(tools::Tool* tool)
{
  Shortcut* shortcut = get_keyboard_shortcut_for_tool(tool);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_copy_selection()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_COPYSELECTION);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_snap_to_grid()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_SNAPTOGRID);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_angle_snap()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_ANGLESNAP);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_maintain_aspect_ratio()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_MAINTAINASPECTRATIO);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_lock_axis()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_LOCKAXIS);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_add_selection()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_ADDSEL);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

Accelerator* get_accel_to_subtract_selection()
{
  Shortcut* shortcut = get_keyboard_shortcut_for_spriteeditor(SPRITEDITOR_ACTION_SUBSEL);
  if (shortcut)
    return shortcut->accel;
  else
    return NULL;
}

tools::Tool* get_selected_quicktool(tools::Tool* currentTool)
{
  if (currentTool && currentTool->getInk(0)->isSelection()) {
    Accelerator* copyselection_accel = get_accel_to_copy_selection();
    if (copyselection_accel && copyselection_accel->checkFromAllegroKeyArray())
      return NULL;
  }

  tools::ToolBox* toolbox = App::instance()->getToolBox();

  // Iterate over all tools
  for (tools::ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
    Shortcut* shortcut = get_keyboard_shortcut_for_quicktool(*it);

    // Collect all tools with the pressed keyboard-shortcut
    if (shortcut && shortcut->is_pressed_from_key_array()) {
      return *it;
    }
  }

  return NULL;
}

Shortcut::Shortcut(Shortcut::Type type)
{
  this->type = type;
  this->accel = new Accelerator;
  this->command = NULL;
  this->keycontext = KeyContext::Any;
  this->tool = NULL;
  this->params = NULL;
}

Shortcut::~Shortcut()
{
  delete params;
  delete accel;
}

void Shortcut::add_shortcut(const char* shortcut_string)
{
  this->accel->addKeysFromString(shortcut_string);
}

bool Shortcut::is_pressed(Message* msg)
{
  bool res = false;

  if (accel) {
    res = accel->check(msg->keyModifiers(),
      static_cast<KeyMessage*>(msg)->scancode(),
      static_cast<KeyMessage*>(msg)->unicodeChar());

    if (res &&
        keycontext != KeyContext::Any &&
        keycontext != get_current_keycontext()) {
      res = false;
    }
  }

  return res;
}

bool Shortcut::is_pressed_from_key_array()
{
  bool res = false;

  if (accel) {
    res = accel->checkFromAllegroKeyArray();

    if (res &&
        keycontext != KeyContext::Any &&
        keycontext != get_current_keycontext()) {
      res = false;
    }
  }

  return res;
}

static Shortcut* get_keyboard_shortcut_for_command(const char* command_name, Params* params)
{
  Command* command = CommandsModule::instance()->getCommandByName(command_name);
  if (!command)
    return NULL;

  for (Shortcut* shortcut : *shortcuts) {
    if (shortcut->type == Shortcut::Type::ExecuteCommand &&
        shortcut->command == command &&
        ((!params && shortcut->params->empty()) ||
         (params && *shortcut->params == *params))) {
      return shortcut;
    }
  }

  return NULL;
}

static Shortcut* get_keyboard_shortcut_for_tool(tools::Tool* tool)
{
  for (Shortcut* shortcut : *shortcuts) {
    if (shortcut->type == Shortcut::Type::ChangeTool &&
        shortcut->tool == tool) {
      return shortcut;
    }
  }

  return NULL;
}

static Shortcut* get_keyboard_shortcut_for_quicktool(tools::Tool* tool)
{
  for (Shortcut* shortcut : *shortcuts) {
    if (shortcut->type == Shortcut::Type::EditorQuicktool &&
        shortcut->tool == tool) {
      return shortcut;
    }
  }

  return NULL;
}

static Shortcut* get_keyboard_shortcut_for_spriteeditor(const char* action_name)
{
  for (Shortcut* shortcut : *shortcuts) {
    if (shortcut->type == Shortcut::Type::SpriteEditor &&
        shortcut->action == action_name) {
      return shortcut;
    }
  }

  return NULL;
}

// Manager event handler.
bool CustomizedGuiManager::onProcessMessage(Message* msg)
{
  switch (msg->type()) {

    case kCloseAppMessage:
      {
        // Execute the "Exit" command.
        Command* command = CommandsModule::instance()->getCommandByName(CommandId::Exit);
        UIContext::instance()->executeCommand(command);
      }
      break;

    case kDropFilesMessage:
      {
        // If the main window is not the current foreground one. We
        // discard the drop-files event.
        if (getForegroundWindow() != App::instance()->getMainWindow())
          break;

        const DropFilesMessage::Files& files = static_cast<DropFilesMessage*>(msg)->files();

        // Open all files
        Command* cmd_open_file =
          CommandsModule::instance()->getCommandByName(CommandId::OpenFile);
        Params params;

        for (DropFilesMessage::Files::const_iterator
               it = files.begin(); it != files.end(); ++it) {
          params.set("filename", it->c_str());
          UIContext::instance()->executeCommand(cmd_open_file, &params);
        }
      }
      break;

    case kQueueProcessingMessage:
      gui_feedback();
      break;

    case kKeyDownMessage: {
      Window* toplevel_window = getTopWindow();

      // If there is a foreground window as top level...
      if (toplevel_window &&
          toplevel_window != App::instance()->getMainWindow() &&
          toplevel_window->isForeground()) {
        // We just do not process keyboard shortcuts for menus and tools
        break;
      }

      for (Shortcut* shortcut : *shortcuts) {
        if (shortcut->is_pressed(msg)) {
          // Cancel menu-bar loops (to close any popup menu)
          App::instance()->getMainWindow()->getMenuBar()->cancelMenuLoop();

          switch (shortcut->type) {

            case Shortcut::Type::ChangeTool: {
              tools::Tool* current_tool = UIContext::instance()->settings()->getCurrentTool();
              tools::Tool* select_this_tool = shortcut->tool;
              tools::ToolBox* toolbox = App::instance()->getToolBox();
              std::vector<tools::Tool*> possibles;

              // Iterate over all tools
              for (tools::ToolIterator it = toolbox->begin(); it != toolbox->end(); ++it) {
                Shortcut* shortcut = get_keyboard_shortcut_for_tool(*it);

                // Collect all tools with the pressed keyboard-shortcut
                if (shortcut && shortcut->is_pressed(msg))
                  possibles.push_back(*it);
              }

              if (possibles.size() >= 2) {
                bool done = false;

                for (size_t i=0; i<possibles.size(); ++i) {
                  if (possibles[i] != current_tool &&
                      ToolBar::instance()->isToolVisible(possibles[i])) {
                    select_this_tool = possibles[i];
                    done = true;
                    break;
                  }
                }

                if (!done) {
                  for (size_t i=0; i<possibles.size(); ++i) {
                    // If one of the possibilities is the current tool
                    if (possibles[i] == current_tool) {
                      // We select the next tool in the possibilities
                      select_this_tool = possibles[(i+1) % possibles.size()];
                      break;
                    }
                  }
                }
              }

              ToolBar::instance()->selectTool(select_this_tool);
              return true;
            }

            case Shortcut::Type::ExecuteCommand: {
              Command* command = shortcut->command;

              // Commands are executed only when the main window is
              // the current window running at foreground.
              UI_FOREACH_WIDGET(getChildren(), it) {
                Window* child = static_cast<Window*>(*it);

                // There are a foreground window executing?
                if (child->isForeground()) {
                  break;
                }
                // Is it the desktop and the top-window=
                else if (child->isDesktop() && child == App::instance()->getMainWindow()) {
                  // OK, so we can execute the command represented
                  // by the pressed-key in the message...
                  UIContext::instance()->executeCommand(command, shortcut->params);
                  return true;
                }
              }
              break;
            }

            case Shortcut::Type::EditorQuicktool: {
              // Do nothing, it is used in the editor through the
              // get_selected_quicktool() function.
              break;
            }

          }
          break;
        }
      }
      break;
    }

  }

  return Manager::onProcessMessage(msg);
}

std::string CustomizedGuiManager::loadLayout(Widget* widget)
{
  if (widget->getRoot() == NULL)
    return "";

  std::string rootId = widget->getRoot()->getId();
  std::string widgetId = widget->getId();

  return get_config_string(("layout:"+rootId).c_str(), widgetId.c_str(), "");
}

void CustomizedGuiManager::saveLayout(Widget* widget, const std::string& str)
{
  if (widget->getRoot() == NULL)
    return;

  std::string rootId = widget->getRoot()->getId();
  std::string widgetId = widget->getId();

  set_config_string(("layout:"+rootId).c_str(), widgetId.c_str(), str.c_str());
}

} // namespace app
