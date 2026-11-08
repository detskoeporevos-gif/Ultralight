#pragma once

#include <AppCore/AppCore.h>
#include <Ultralight/Listener.h>

class UI;
using namespace ultralight;

/**
 * One browser tab: owns the View that renders the page plus an optional
 * inspector overlay docked to the bottom of the window.
 */
class Tab : public ViewListener,
            public LoadListener {
public:
  Tab(UI* ui, uint64_t id, uint32_t width, uint32_t height, int x, int y);
  ~Tab();

  uint64_t id() const { return id_; }
  RefPtr<View> view() { return overlay_->view(); }

  void Show();
  void Hide();

  void ToggleInspector();
  bool IsInspectorShowing() const;
  IntRect GetInspectorResizeDragHandle() const;
  int GetInspectorHeight() const;
  void SetInspectorHeight(int height);

  void Resize(uint32_t width, uint32_t height);

  // Inherited from Listener::View
  virtual void OnChangeTitle(View* caller, const String& title) override;
  virtual void OnChangeURL(View* caller, const String& url) override;
  virtual void OnChangeTooltip(View* caller, const String& tooltip) override;
  virtual void OnChangeCursor(View* caller, Cursor cursor) override;
  virtual void OnAddConsoleMessage(View* caller, MessageSource source,
    MessageLevel level, const String& message, uint32_t line_number,
    uint32_t column_number, const String& source_id) override;
  virtual RefPtr<View> OnCreateChildView(ultralight::View* caller,
    const String& opener_url, const String& target_url,
    bool is_popup, const IntRect& popup_rect) override;
  virtual RefPtr<View> OnCreateInspectorView(ultralight::View* caller, bool is_local,
      const String& inspected_url) override;

  // Inherited from Listener::Load
  virtual void OnDOMReady(View* caller, uint64_t frame_id,
    bool is_main_frame, const String& url) override;
  virtual void OnBeginLoading(View* caller, uint64_t frame_id,
    bool is_main_frame, const String& url) override;
  virtual void OnFinishLoading(View* caller, uint64_t frame_id,
    bool is_main_frame, const String& url) override;
  virtual void OnFailLoading(View* caller, uint64_t frame_id,
    bool is_main_frame, const String& url, const String& description,
    const String& error_domain, int error_code) override;
  virtual void OnUpdateHistory(View* caller) override;

protected:
  UI* ui_;
  RefPtr<Overlay> overlay_;
  RefPtr<Overlay> inspector_overlay_;
  uint64_t id_;
  uint32_t container_width_, container_height_;
};
