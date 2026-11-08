#include "Tab.h"
#include "UI.h"
#include "StringUtil.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

#define INSPECTOR_DRAG_HANDLE_HEIGHT 10

namespace {

std::string EscapeHTML(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c; break;
    }
  }
  return out;
}

std::string ErrorPageHTML(const std::string& url, const std::string& description,
                          const std::string& error_domain, int error_code) {
  std::ostringstream html;
  html << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
          "<title>Связь прервана</title><style>"
          "@font-face{font-family:'EscFR Mono';src:url('fonts/jetbrains-mono-latin-400-normal.woff2') "
          "format('woff2');}"
          "*{box-sizing:border-box}"
          "html,body{height:100%;margin:0}"
          "body{display:flex;align-items:center;justify-content:center;background:#070b11;"
          "color:#d5deec;font-family:'Segoe UI',Arial,sans-serif;"
          "background-image:linear-gradient(rgba(51,227,207,.05) 1px,transparent 1px),"
          "linear-gradient(90deg,rgba(51,227,207,.04) 1px,transparent 1px);"
          "background-size:52px 52px,52px 52px}"
          ".card{width:min(680px,86vw);padding:34px 38px;border:1px solid #1c2b3e;border-radius:14px;"
          "background:linear-gradient(180deg,#0f1926,#0a1018);"
          "box-shadow:0 20px 50px rgba(0,0,0,.6),inset 0 1px 0 rgba(255,255,255,.03)}"
          ".kicker{font-family:'EscFR Mono',Consolas,monospace;font-size:10px;letter-spacing:2.6px;"
          "text-transform:uppercase;color:#5a6a80}"
          "h1{margin:14px 0 6px;font-size:27px;letter-spacing:-.5px;color:#f2f7ff}"
          "h1 b{color:#ff2e57;font-weight:600}"
          ".lead{color:#8b9bb1;font-size:13.5px;line-height:1.7;margin:0 0 22px}"
          "dl{display:grid;grid-template-columns:auto 1fr;gap:8px 18px;margin:0 0 26px;"
          "font-family:'EscFR Mono',Consolas,monospace;font-size:12px}"
          "dt{color:#5a6a80;letter-spacing:1px;text-transform:uppercase;font-size:10px;"
          "padding-top:2px}"
          "dd{margin:0;color:#cfd8e6;word-break:break-all}"
          ".actions{display:flex;gap:10px;flex-wrap:wrap}"
          "a{display:inline-flex;align-items:center;height:32px;padding:0 14px;border-radius:8px;"
          "border:1px solid #24384f;background:linear-gradient(180deg,#162333,#101a27);"
          "color:#d5deec;text-decoration:none;font-size:12.5px;font-weight:600}"
          "a.primary{border-color:rgba(255,46,87,.5);background:linear-gradient(180deg,#d32344,#a81733);"
          "color:#fff}"
          ".foot{margin-top:26px;font-family:'EscFR Mono',Consolas,monospace;font-size:9.5px;"
          "letter-spacing:2px;text-transform:uppercase;color:#3d4d63}"
          "</style></head><body><div class=\"card\">"
          "<div class=\"kicker\">Wired // link lost</div>"
          "<h1>Связь <b>прервана</b></h1>"
          "<p class=\"lead\">EscFR не смог подключиться к узлу. Проверьте адрес и сеть — "
          "Wired отвечает не всем.</p><dl>"
       << "<dt>url</dt><dd>" << EscapeHTML(url) << "</dd>"
       << "<dt>причина</dt><dd>" << EscapeHTML(description) << "</dd>"
       << "<dt>домен</dt><dd>" << EscapeHTML(error_domain) << "</dd>"
       << "<dt>код</dt><dd>" << error_code << "</dd>"
       << "</dl><div class=\"actions\">"
          "<a class=\"primary\" href=\"esc://newtab\">Новая вкладка</a>"
          "<a href=\"esc://settings\">Настройки</a>"
          "</div><div class=\"foot\">present day, present time</div></div></body></html>";
  return html.str();
}

}  // namespace

Tab::Tab(UI* ui, uint64_t id, uint32_t width, uint32_t height, int x, int y)
  : ui_(ui), id_(id), container_width_(width), container_height_(height) {
  overlay_ = Overlay::Create(ui->window(), width, height, x, y);
  view()->set_view_listener(this);
  view()->set_load_listener(this);
}

Tab::~Tab() {
  if (view()) {
    view()->set_view_listener(nullptr);
    view()->set_load_listener(nullptr);
  }
}

void Tab::Show() {
  overlay_->Show();
  overlay_->Focus();

  if (inspector_overlay_)
    inspector_overlay_->Show();
}

void Tab::Hide() {
  overlay_->Hide();
  overlay_->Unfocus();

  if (inspector_overlay_)
    inspector_overlay_->Hide();
}

void Tab::ToggleInspector() {
  if (!inspector_overlay_) {
    view()->CreateLocalInspectorView();
  } else if (inspector_overlay_->is_hidden()) {
    inspector_overlay_->Show();
  } else {
    inspector_overlay_->Hide();
  }
}

bool Tab::IsInspectorShowing() const {
  if (!inspector_overlay_)
    return false;

  return !inspector_overlay_->is_hidden();
}

IntRect Tab::GetInspectorResizeDragHandle() const {
  if (!IsInspectorShowing())
    return IntRect::MakeEmpty();

  int drag_handle_height_px =
    (int)std::round(INSPECTOR_DRAG_HANDLE_HEIGHT * ui_->window()->scale());

  int drag_handle_x = (int)inspector_overlay_->x();
  int drag_handle_y = (int)inspector_overlay_->y() - drag_handle_height_px / 2;

  return { drag_handle_x, drag_handle_y, drag_handle_x + (int)inspector_overlay_->width(),
           drag_handle_y + drag_handle_height_px };
}

int Tab::GetInspectorHeight() const {
  if (inspector_overlay_)
    return inspector_overlay_->height();

  return 0;
}

void Tab::SetInspectorHeight(int height) {
  if (height > 2) {
    inspector_overlay_->Resize(inspector_overlay_->width(), height);

    // Trigger a resize to perform re-layout / re-size of content overlay
    Resize(container_width_, container_height_);
  }
}

void Tab::Resize(uint32_t width, uint32_t height) {
  container_width_ = width;
  container_height_ = height;

  uint32_t content_height = container_height_;
  if (inspector_overlay_ && !inspector_overlay_->is_hidden())
    content_height -= inspector_overlay_->height();

  if (content_height < 1)
    content_height = 1;

  overlay_->Resize(container_width_, content_height);

  if (inspector_overlay_ && !inspector_overlay_->is_hidden()) {
    inspector_overlay_->MoveTo(0, overlay_->y() + overlay_->height());
    inspector_overlay_->Resize(container_width_, inspector_overlay_->height());
  }
}

void Tab::OnChangeTitle(View* caller, const String& title) {
  ui_->UpdateTabTitle(id_, title);
}

void Tab::OnChangeURL(View* caller, const String& url) {
  ui_->UpdateTabURL(id_, url);
}

void Tab::OnChangeTooltip(View* caller, const String& tooltip) {}

void Tab::OnChangeCursor(View* caller, Cursor cursor) {
  if (id_ == ui_->active_tab_id())
    ui_->SetCursor(cursor);
}

void Tab::OnAddConsoleMessage(View* caller, MessageSource source, MessageLevel level,
  const String& message, uint32_t line_number, uint32_t column_number, const String& source_id) {
}

RefPtr<View> Tab::OnCreateChildView(ultralight::View* caller, const String& opener_url,
  const String& target_url, bool is_popup, const IntRect& popup_rect) {
  return ui_->CreateNewTabForChildView(target_url);
}

RefPtr<View> Tab::OnCreateInspectorView(ultralight::View* caller, bool is_local,
                                        const String& inspected_url) {
  if (inspector_overlay_)
    return nullptr;

  inspector_overlay_ = Overlay::Create(ui_->window(), container_width_, container_height_ / 2, 0, 0);

  // Force resize to update layout
  Resize(container_width_, container_height_);
  inspector_overlay_->Show();

  return inspector_overlay_->view();
}

void Tab::OnDOMReady(View* caller, uint64_t frame_id, bool is_main_frame, const String& url) {
  if (is_main_frame)
    ui_->BindInternalPage(this, url);
}

void Tab::OnBeginLoading(View* caller, uint64_t frame_id, bool is_main_frame, const String& url) {
  ui_->UpdateTabNavigation(id_, caller->is_loading(), caller->CanGoBack(), caller->CanGoForward());
}

void Tab::OnFinishLoading(View* caller, uint64_t frame_id, bool is_main_frame, const String& url) {
  ui_->UpdateTabNavigation(id_, caller->is_loading(), caller->CanGoBack(), caller->CanGoForward());
}

void Tab::OnFailLoading(View* caller, uint64_t frame_id, bool is_main_frame, const String& url,
  const String& description, const String& error_domain, int error_code) {
  if (!is_main_frame)
    return;

  std::string html = ErrorPageHTML(escfr::ToStd(url), escfr::ToStd(description),
                                   escfr::ToStd(error_domain), error_code);
  view()->LoadHTML(escfr::FromStd(html));
}

void Tab::OnUpdateHistory(View* caller) {
  ui_->UpdateTabNavigation(id_, caller->is_loading(), caller->CanGoBack(), caller->CanGoForward());
}
