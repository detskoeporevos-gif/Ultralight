#pragma once

#include <AppCore/AppCore.h>
#include "Tab.h"
#include <map>
#include <memory>
#include <string>

using ultralight::JSArgs;
using ultralight::JSFunction;
using ultralight::JSObject;
using namespace ultralight;

/**
 * EscFR browser chrome: tab strip, address bar, app menu and the native
 * bridge used by the internal pages (esc://newtab, esc://settings, esc://about).
 */
class UI : public WindowListener,
           public LoadListener,
           public ViewListener {
public:
  UI(RefPtr<Window> window);
  ~UI();

  // Inherited from WindowListener
  virtual bool OnKeyEvent(const ultralight::KeyEvent& evt) override;
  virtual bool OnMouseEvent(const ultralight::MouseEvent& evt) override;
  virtual void OnClose(ultralight::Window* window) override;
  virtual void OnResize(ultralight::Window* window, uint32_t width, uint32_t height) override;

  // Inherited from LoadListener
  virtual void OnDOMReady(View* caller, uint64_t frame_id,
    bool is_main_frame, const String& url) override;

  // Inherited from ViewListener
  virtual void OnChangeCursor(ultralight::View* caller, Cursor cursor) override { SetCursor(cursor); }
  virtual void OnAddConsoleMessage(View* caller, MessageSource source, MessageLevel level,
    const String& message, uint32_t line_number, uint32_t column_number,
    const String& source_id) override;

  // Called from the chrome's JavaScript
  void OnBack(const JSObject& obj, const JSArgs& args);
  void OnForward(const JSObject& obj, const JSArgs& args);
  void OnRefresh(const JSObject& obj, const JSArgs& args);
  void OnStop(const JSObject& obj, const JSArgs& args);
  void OnHome(const JSObject& obj, const JSArgs& args);
  void OnToggleTools(const JSObject& obj, const JSArgs& args);
  void OnRequestNewTab(const JSObject& obj, const JSArgs& args);
  void OnRequestTabClose(const JSObject& obj, const JSArgs& args);
  void OnActiveTabChange(const JSObject& obj, const JSArgs& args);
  void OnRequestChangeURL(const JSObject& obj, const JSArgs& args);
  void OnQueryNavigationState(const JSObject& obj, const JSArgs& args);
  void OnFocusContent(const JSObject& obj, const JSArgs& args);
  void OnRequestSettings(const JSObject& obj, const JSArgs& args);
  void OnQuit(const JSObject& obj, const JSArgs& args);

  // Called from internal pages (esc://*) through the native bridge
  void OnPageSaveSettings(const JSObject& obj, const JSArgs& args);
  void OnPageNavigate(const JSObject& obj, const JSArgs& args);
  void OnPageNewTab(const JSObject& obj, const JSArgs& args);
  void OnPageCloseTab(const JSObject& obj, const JSArgs& args);
  void OnPageToggleDevtools(const JSObject& obj, const JSArgs& args);
  void OnPageToast(const JSObject& obj, const JSArgs& args);
  void OnPageQuit(const JSObject& obj, const JSArgs& args);

  RefPtr<Window> window() { return window_; }
  uint64_t active_tab_id() const { return active_tab_id_; }
  Tab* active_tab() { return tabs_.empty() ? nullptr : tabs_[active_tab_id_].get(); }

  // Shared with Tab
  void UpdateTabTitle(uint64_t id, const String& title);
  void UpdateTabURL(uint64_t id, const String& url);
  void UpdateTabNavigation(uint64_t id, bool is_loading, bool can_go_back, bool can_go_forward);
  void BindInternalPage(Tab* tab, const String& url);
  RefPtr<View> CreateNewTabForChildView(const String& url);
  void SetCursor(Cursor cursor);

protected:
  void CreateNewTab(const String& url = String());
  void CloseTab(uint64_t id);
  void SetLoading(bool is_loading);
  void SetURL(const String& url);
  void FocusAddress();
  void FocusContent();
  void ShowToast(const String& message);
  void NavigateActiveTab(const char* url);
  void UpdateNavigationState();
  void ApplyChromeSettings();

  String ResolveURL(const String& url);
  bool IsInternalURL(const String& url) const;

  RefPtr<View> view() { return overlay_->view(); }

  RefPtr<Window> window_;
  RefPtr<Overlay> overlay_;
  uint32_t ui_height_;
  int tab_height_;
  float scale_;

  std::map<uint64_t, std::unique_ptr<Tab>> tabs_;
  uint64_t active_tab_id_ = 0;
  uint64_t tab_id_counter_ = 0;
  Cursor cur_cursor_;

  bool is_resizing_inspector_ = false;
  bool is_over_inspector_resize_drag_handle_ = false;
  int inspector_resize_begin_height_ = 0;
  int inspector_resize_begin_mouse_y_ = 0;

  std::string assets_path_;
  std::string settings_json_;
  std::string start_url_;
  bool chrome_ready_ = false;

  JSFunction addTab;
  JSFunction updateTab;
  JSFunction updateTabURL;
  JSFunction closeTab;
  JSFunction setActiveTab;
  JSFunction updateLoading;
  JSFunction updateURL;
  JSFunction setNavigationState;
  JSFunction applySettings;
  JSFunction showToast;
  JSFunction focusAddress;

  friend class Tab;
};
