#include "UI.h"
#include "AppSettings.h"
#include "StringUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define CHROME_HEIGHT_CSS 88

namespace {

// Ultralight key modifiers: Alt = 1, Ctrl = 2, Meta = 4, Shift = 8
const uint32_t kModAlt = 1;
const uint32_t kModCtrl = 2;
const uint32_t kModShift = 8;

// Virtual key codes (shared across the AppCore backends)
const int kVK_Escape = 0x1B;
const int kVK_Left = 0x25;
const int kVK_Right = 0x27;
const int kVK_F5 = 0x74;
const int kVK_F12 = 0x7B;

const int kVK_I = 0x49;
const int kVK_L = 0x4C;
const int kVK_Q = 0x51;
const int kVK_R = 0x52;
const int kVK_T = 0x54;
const int kVK_W = 0x57;
const int kVK_OEM_COMMA = 0xBC;

std::string EscapeJSString(const std::string& input) {
  std::string out;
  out.reserve(input.size() + 8);
  for (char c : input) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace

UI::UI(RefPtr<Window> window) : window_(window), cur_cursor_(Cursor::kCursor_Pointer) {
  scale_ = (float)window_->scale();
  ui_height_ = (uint32_t)std::round(CHROME_HEIGHT_CSS * scale_);
  tab_height_ = window_->height() - (int)ui_height_;
  if (tab_height_ < 1)
    tab_height_ = 1;

  assets_path_ = escfr::ToStd(App::instance()->settings().file_system_path);
  settings_json_ = escfr::LoadSettingsJSON(assets_path_);

  // Optional: ESCFR_START_URL=esc://settings makes the app open that page on
  // launch (used by the screenshot steps in CI).
  const char* start = std::getenv("ESCFR_START_URL");
  if (start && *start)
    start_url_ = start;

  overlay_ = Overlay::Create(window_, window_->width(), ui_height_, 0, 0);
  view()->set_load_listener(this);
  view()->set_view_listener(this);
  view()->LoadURL("file:///ui.html");
}

UI::~UI() {
  tabs_.clear();

  if (view()) {
    view()->set_load_listener(nullptr);
    view()->set_view_listener(nullptr);
  }

  overlay_ = nullptr;
}

// ---------------------------------------------------------------------------
// Chrome → native
// ---------------------------------------------------------------------------

void UI::OnAddConsoleMessage(View* caller, MessageSource source, MessageLevel level,
  const String& message, uint32_t line_number, uint32_t column_number, const String& source_id) {
  std::string text = escfr::ToStd(message);
  std::printf("[chrome] %s (line %u)\n", text.c_str(), line_number);
  std::fflush(stdout);
}

void UI::OnDOMReady(View* caller, uint64_t frame_id, bool is_main_frame, const String& url) {
  if (caller != view())
    return;

  RefPtr<JSContext> locked_context = view()->LockJSContext();
  SetJSContext(locked_context->ctx());

  JSObject global = JSGlobalObject();

  addTab = global["addTab"];
  updateTab = global["updateTab"];
  updateTabURL = global["updateTabURL"];
  closeTab = global["closeTab"];
  setActiveTab = global["setActiveTab"];
  updateLoading = global["updateLoading"];
  updateURL = global["updateURL"];
  setNavigationState = global["setNavigationState"];
  applySettings = global["applySettings"];
  showToast = global["showToast"];
  focusAddress = global["focusAddress"];

  global["OnBack"] = BindJSCallback(&UI::OnBack);
  global["OnForward"] = BindJSCallback(&UI::OnForward);
  global["OnRefresh"] = BindJSCallback(&UI::OnRefresh);
  global["OnStop"] = BindJSCallback(&UI::OnStop);
  global["OnHome"] = BindJSCallback(&UI::OnHome);
  global["OnToggleTools"] = BindJSCallback(&UI::OnToggleTools);
  global["OnRequestNewTab"] = BindJSCallback(&UI::OnRequestNewTab);
  global["OnRequestTabClose"] = BindJSCallback(&UI::OnRequestTabClose);
  global["OnActiveTabChange"] = BindJSCallback(&UI::OnActiveTabChange);
  global["OnRequestChangeURL"] = BindJSCallback(&UI::OnRequestChangeURL);
  global["OnQueryNavigationState"] = BindJSCallback(&UI::OnQueryNavigationState);
  global["OnFocusContent"] = BindJSCallback(&UI::OnFocusContent);
  global["OnRequestSettings"] = BindJSCallback(&UI::OnRequestSettings);
  global["OnQuit"] = BindJSCallback(&UI::OnQuit);

  chrome_ready_ = true;

  ApplyChromeSettings();

  if (tabs_.empty())
    CreateNewTab(start_url_.empty() ? String("esc://newtab") : String(start_url_.c_str()));
}

void UI::OnBack(const JSObject& obj, const JSArgs& args) {
  if (active_tab())
    active_tab()->view()->GoBack();
}

void UI::OnForward(const JSObject& obj, const JSArgs& args) {
  if (active_tab())
    active_tab()->view()->GoForward();
}

void UI::OnRefresh(const JSObject& obj, const JSArgs& args) {
  if (active_tab())
    active_tab()->view()->Reload();
}

void UI::OnStop(const JSObject& obj, const JSArgs& args) {
  if (active_tab())
    active_tab()->view()->Stop();
}

void UI::OnHome(const JSObject& obj, const JSArgs& args) {
  std::string home = escfr::FindJSONString(settings_json_, "homePage");
  if (home.empty())
    home = "esc://newtab";
  NavigateActiveTab(home.c_str());
}

void UI::OnToggleTools(const JSObject& obj, const JSArgs& args) {
  if (active_tab())
    active_tab()->ToggleInspector();
}

void UI::OnRequestNewTab(const JSObject& obj, const JSArgs& args) {
  CreateNewTab("esc://newtab");
}

void UI::OnRequestTabClose(const JSObject& obj, const JSArgs& args) {
  if (args.size() != 1 || tabs_.empty())
    return;

  CloseTab((uint64_t)args[0]);
}

void UI::CloseTab(uint64_t id) {
  if (tabs_.empty() || tabs_.find(id) == tabs_.end())
    return;

  if (tabs_.size() == 1) {
    if (App::instance())
      App::instance()->Quit();
    return;
  }

  std::vector<uint64_t> ids;
  for (auto& entry : tabs_)
    ids.push_back(entry.first);

  auto pos = std::find(ids.begin(), ids.end(), id);
  size_t index = (size_t)std::distance(ids.begin(), pos);
  uint64_t neighbour = ids[(index + 1) % ids.size()];
  if (neighbour == id)
    neighbour = ids[0];

  if (chrome_ready_)
    closeTab({ id });

  if (id == active_tab_id_) {
    if (tabs_[id])
      tabs_[id]->Hide();
    tabs_[id].reset();
    tabs_.erase(id);

    active_tab_id_ = neighbour;
    if (tabs_[active_tab_id_]) {
      tabs_[active_tab_id_]->Show();
      if (chrome_ready_)
        setActiveTab({ active_tab_id_ });
      UpdateNavigationState();
      SetURL(tabs_[active_tab_id_]->view()->url());
    }
  } else {
    tabs_[id].reset();
    tabs_.erase(id);
  }
}

void UI::OnActiveTabChange(const JSObject& obj, const JSArgs& args) {
  if (args.size() != 1)
    return;

  uint64_t id = args[0];

  if (id == active_tab_id_ || tabs_.find(id) == tabs_.end())
    return;

  if (active_tab())
    active_tab()->Hide();

  active_tab_id_ = id;
  tabs_[active_tab_id_]->Show();

  if (chrome_ready_)
    setActiveTab({ active_tab_id_ });

  RefPtr<View> tab_view = tabs_[active_tab_id_]->view();
  SetLoading(tab_view->is_loading());
  SetURL(tab_view->url());
  UpdateNavigationState();
}

void UI::OnRequestChangeURL(const JSObject& obj, const JSArgs& args) {
  if (args.size() != 1)
    return;

  String url = args[0];
  std::string url_text = escfr::ToStd(url);
  NavigateActiveTab(url_text.c_str());
}

void UI::OnQueryNavigationState(const JSObject& obj, const JSArgs& args) {
  UpdateNavigationState();
}

void UI::OnFocusContent(const JSObject& obj, const JSArgs& args) {
  FocusContent();
}

void UI::OnRequestSettings(const JSObject& obj, const JSArgs& args) {
  ApplyChromeSettings();
}

void UI::OnQuit(const JSObject& obj, const JSArgs& args) {
  if (App::instance())
    App::instance()->Quit();
}

// ---------------------------------------------------------------------------
// Internal pages → native bridge
// ---------------------------------------------------------------------------

void UI::BindInternalPage(Tab* tab, const String& url) {
  if (!tab || !IsInternalURL(url))
    return;

  RefPtr<View> page_view = tab->view();
  if (!page_view)
    return;

  RefPtr<JSContext> locked_context = page_view->LockJSContext();
  SetJSContext(locked_context->ctx());

  JSObject global = JSGlobalObject();
  global["escFRSaveSettings"] = BindJSCallback(&UI::OnPageSaveSettings);
  global["escFRNavigate"] = BindJSCallback(&UI::OnPageNavigate);
  global["escFRNewTab"] = BindJSCallback(&UI::OnPageNewTab);
  global["escFRCloseTab"] = BindJSCallback(&UI::OnPageCloseTab);
  global["escFRToggleDevtools"] = BindJSCallback(&UI::OnPageToggleDevtools);
  global["escFRToast"] = BindJSCallback(&UI::OnPageToast);
  global["escFRQuit"] = BindJSCallback(&UI::OnPageQuit);

  std::string page = escfr::ToStd(url);
  bool wants_init = page.find("new_tab_page.html") != std::string::npos ||
                    page.find("settings.html") != std::string::npos;
  if (!wants_init)
    return;

  std::string script = "if (typeof __escFRInit === 'function') { __escFRInit(\"" +
                       EscapeJSString(settings_json_) + "\"); }";
  page_view->EvaluateScript(escfr::FromStd(script));
}

void UI::OnPageSaveSettings(const JSObject& obj, const JSArgs& args) {
  if (args.size() != 1)
    return;

  String json_value = args[0];
  std::string json = escfr::ToStd(json_value);

  if (!escfr::SaveSettingsJSON(assets_path_, json)) {
    ShowToast("Не удалось сохранить настройки");
    return;
  }

  settings_json_ = json;
  ApplyChromeSettings();
  ShowToast("Настройки сохранены");
}

void UI::OnPageNavigate(const JSObject& obj, const JSArgs& args) {
  if (args.size() != 1)
    return;

  String url = args[0];
  std::string url_text = escfr::ToStd(url);
  NavigateActiveTab(url_text.c_str());
}

void UI::OnPageNewTab(const JSObject& obj, const JSArgs& args) {
  if (args.size() == 1) {
    String url = args[0];
    CreateNewTab(url);
  } else {
    CreateNewTab("esc://newtab");
  }
}

void UI::OnPageCloseTab(const JSObject& obj, const JSArgs& args) {
  if (tabs_.size() <= 1) {
    if (App::instance())
      App::instance()->Quit();
    return;
  }

  CloseTab(active_tab_id_);
}

void UI::OnPageToggleDevtools(const JSObject& obj, const JSArgs& args) {
  if (active_tab())
    active_tab()->ToggleInspector();
}

void UI::OnPageToast(const JSObject& obj, const JSArgs& args) {
  if (args.size() == 1) {
    String message = args[0];
    ShowToast(message);
  }
}

void UI::OnPageQuit(const JSObject& obj, const JSArgs& args) {
  if (App::instance())
    App::instance()->Quit();
}

// ---------------------------------------------------------------------------
// Tabs
// ---------------------------------------------------------------------------

void UI::CreateNewTab(const String& url) {
  uint64_t id = tab_id_counter_++;

  if (active_tab())
    active_tab()->Hide();

  tab_height_ = window_->height() - (int)ui_height_;
  if (tab_height_ < 1)
    tab_height_ = 1;

  tabs_[id].reset(new Tab(this, id, window_->width(), (uint32_t)tab_height_, 0, (int)ui_height_));
  active_tab_id_ = id;

  String target = url;
  if (escfr::ToStd(target).empty())
    target = String("esc://newtab");

  String resolved = ResolveURL(target);
  if (escfr::ToStd(resolved).empty())
    resolved = target;

  // Tell the chrome about the tab *before* loading: local pages can finish
  // loading synchronously, and those callbacks must find the tab in JS.
  if (chrome_ready_) {
    addTab({ id, String("Новая вкладка"), resolved, true });
    setActiveTab({ id });
  }

  tabs_[id]->view()->LoadURL(resolved);
  tabs_[id]->Show();

  SetLoading(true);
  SetURL(resolved);
  UpdateNavigationState();
}

RefPtr<View> UI::CreateNewTabForChildView(const String& url) {
  uint64_t id = tab_id_counter_++;

  if (active_tab())
    active_tab()->Hide();

  tab_height_ = window_->height() - (int)ui_height_;
  if (tab_height_ < 1)
    tab_height_ = 1;

  tabs_[id].reset(new Tab(this, id, window_->width(), (uint32_t)tab_height_, 0, (int)ui_height_));
  active_tab_id_ = id;

  String resolved = ResolveURL(url);
  if (escfr::ToStd(resolved).empty())
    resolved = url;

  if (chrome_ready_) {
    addTab({ id, String("Новая вкладка"), resolved, true });
    setActiveTab({ id });
  }

  tabs_[id]->view()->LoadURL(resolved);
  tabs_[id]->Show();

  return tabs_[id]->view();
}

void UI::UpdateTabTitle(uint64_t id, const String& title) {
  if (tabs_.find(id) == tabs_.end())
    return;

  if (chrome_ready_)
    updateTab({ id, title, JSValue(), tabs_[id]->view()->is_loading() });

  if (id == active_tab_id_) {
    std::string window_title = escfr::ToStd(title);
    window_title = window_title.empty() ? "EscFR" : window_title + " — EscFR";
    window_->SetTitle(window_title.c_str());
  }
}

void UI::UpdateTabURL(uint64_t id, const String& url) {
  if (tabs_.find(id) == tabs_.end())
    return;

  if (chrome_ready_)
    updateTabURL({ id, url });

  if (id == active_tab_id_)
    SetURL(url);
}

void UI::UpdateTabHistory(uint64_t id, bool can_go_back, bool can_go_forward) {
  if (tabs_.find(id) == tabs_.end())
    return;

  if (id != active_tab_id_)
    return;

  if (chrome_ready_)
    setNavigationState({ can_go_back, can_go_forward });
}

void UI::UpdateTabNavigation(uint64_t id, bool is_loading, bool can_go_back, bool can_go_forward) {
  if (tabs_.find(id) == tabs_.end())
    return;

  if (chrome_ready_)
    updateTab({ id, tabs_[id]->view()->title(), JSValue(), is_loading });

  if (id != active_tab_id_)
    return;

  SetLoading(is_loading);
  if (chrome_ready_)
    setNavigationState({ can_go_back, can_go_forward });
}

// ---------------------------------------------------------------------------
// Chrome helpers
// ---------------------------------------------------------------------------

void UI::SetLoading(bool is_loading) {
  if (!chrome_ready_)
    return;
  RefPtr<JSContext> lock(view()->LockJSContext());
  updateLoading({ is_loading });
}

void UI::SetURL(const String& url) {
  if (!chrome_ready_)
    return;
  RefPtr<JSContext> lock(view()->LockJSContext());
  updateURL({ url });
}

void UI::UpdateNavigationState() {
  if (!chrome_ready_ || !active_tab())
    return;
  RefPtr<View> tab_view = active_tab()->view();
  RefPtr<JSContext> lock(view()->LockJSContext());
  setNavigationState({ tab_view->CanGoBack(), tab_view->CanGoForward() });
}

void UI::ApplyChromeSettings() {
  if (!chrome_ready_)
    return;
  RefPtr<JSContext> lock(view()->LockJSContext());
  applySettings({ String(settings_json_.c_str()) });
}

void UI::ShowToast(const String& message) {
  if (!chrome_ready_)
    return;
  RefPtr<JSContext> lock(view()->LockJSContext());
  showToast({ message });
}

void UI::FocusAddress() {
  if (!chrome_ready_)
    return;
  overlay_->Focus();
  RefPtr<JSContext> lock(view()->LockJSContext());
  JSArgs args;
  focusAddress(args);
}

void UI::FocusContent() {
  if (active_tab())
    active_tab()->Show();
}

void UI::NavigateActiveTab(const char* url) {
  if (!url || !*url || !active_tab())
    return;

  String target(url);
  String resolved = ResolveURL(target);
  if (escfr::ToStd(resolved).empty())
    resolved = target;

  active_tab()->view()->LoadURL(resolved);
  active_tab()->Show();
}

String UI::ResolveURL(const String& url) {
  std::string resolved = escfr::ResolveInternalURL(escfr::ToStd(url));
  return escfr::FromStd(resolved);
}

bool UI::IsInternalURL(const String& url) const {
  std::string value = escfr::ToStd(url);
  return value.find("file:///") == 0;
}

void UI::SetCursor(Cursor cursor) {
  cur_cursor_ = cursor;
  if (window_)
    window_->SetCursor(cursor);
}

// ---------------------------------------------------------------------------
// Window events
// ---------------------------------------------------------------------------

bool UI::OnKeyEvent(const ultralight::KeyEvent& evt) {
  if (evt.type != KeyEvent::kType_RawKeyDown)
    return true;

  const bool ctrl = (evt.modifiers & kModCtrl) != 0;
  const bool alt = (evt.modifiers & kModAlt) != 0;
  const bool shift = (evt.modifiers & kModShift) != 0;
  const int key = evt.virtual_key_code;

  if (ctrl && shift && key == kVK_I) {
    if (active_tab())
      active_tab()->ToggleInspector();
    return false;
  }

  if (ctrl && !alt) {
    switch (key) {
      case kVK_T: CreateNewTab("esc://newtab"); return false;
      case kVK_W:
        if (!tabs_.empty())
          CloseTab(active_tab_id_);
        return false;
      case kVK_L: FocusAddress(); return false;
      case kVK_R:
        if (active_tab())
          active_tab()->view()->Reload();
        return false;
      case kVK_OEM_COMMA: NavigateActiveTab("esc://settings"); return false;
      case kVK_Q:
        if (App::instance())
          App::instance()->Quit();
        return false;
      default: break;
    }
  }

  if (!ctrl && !alt && key == kVK_F5) {
    if (active_tab())
      active_tab()->view()->Reload();
    return false;
  }

  if (ctrl && key == kVK_F12) {
    if (active_tab())
      active_tab()->ToggleInspector();
    return false;
  }

  if (alt && key == kVK_Left) {
    if (active_tab())
      active_tab()->view()->GoBack();
    return false;
  }

  if (alt && key == kVK_Right) {
    if (active_tab())
      active_tab()->view()->GoForward();
    return false;
  }

  if (!ctrl && !alt && key == kVK_Escape) {
    if (active_tab() && active_tab()->view()->is_loading())
      active_tab()->view()->Stop();
    return false;
  }

  return true;
}

bool UI::OnMouseEvent(const ultralight::MouseEvent& evt) {
  if (active_tab() && active_tab()->IsInspectorShowing()) {
    float x_px = (float)std::round(evt.x * window()->scale());
    float y_px = (float)std::round(evt.y * window()->scale());

    if (is_resizing_inspector_) {
      int resize_delta = inspector_resize_begin_mouse_y_ - (int)y_px;
      int new_inspector_height = inspector_resize_begin_height_ + resize_delta;
      active_tab()->SetInspectorHeight(new_inspector_height);

      if (evt.type == MouseEvent::kType_MouseUp)
        is_resizing_inspector_ = false;

      return false;
    }

    IntRect drag_handle = active_tab()->GetInspectorResizeDragHandle();
    bool over_drag_handle = drag_handle.Contains(Point(x_px, y_px));

    if (over_drag_handle && !is_over_inspector_resize_drag_handle_) {
      window()->SetCursor(Cursor::kCursor_NorthSouthResize);
      is_over_inspector_resize_drag_handle_ = true;
    } else if (!over_drag_handle && is_over_inspector_resize_drag_handle_) {
      window()->SetCursor(cur_cursor_);
      is_over_inspector_resize_drag_handle_ = false;
    }

    if (over_drag_handle && evt.type == MouseEvent::kType_MouseDown && !is_resizing_inspector_) {
      is_resizing_inspector_ = true;
      inspector_resize_begin_mouse_y_ = (int)y_px;
      inspector_resize_begin_height_ = active_tab()->GetInspectorHeight();
    }

    return !over_drag_handle;
  }

  return true;
}

void UI::OnClose(ultralight::Window* window) {
  if (App::instance())
    App::instance()->Quit();
}

void UI::OnResize(ultralight::Window* window, uint32_t width, uint32_t height) {
  int tab_height = (int)height - (int)ui_height_;
  if (tab_height < 1)
    tab_height = 1;

  overlay_->Resize(window->width(), ui_height_);

  for (auto& entry : tabs_) {
    if (entry.second)
      entry.second->Resize(window->width(), (uint32_t)tab_height);
  }
}
