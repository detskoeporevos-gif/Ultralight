#include "Browser.h"

#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/Renderer.h>

#include <cstdlib>

namespace {

bool EnvFlag(const char* name) {
  const char* value = std::getenv(name);
  if (!value || !*value)
    return false;
  return value[0] != '0';
}

}  // namespace

Browser::Browser() {
  Settings settings;
  settings.developer_name = "EscFR";
  settings.app_name = "EscFR";
  settings.file_system_path = "./assets/";

  // Headless / software-rendering friendly: ESCFR_FORCE_CPU=1 makes the app
  // usable on machines without a GPU (and inside CI under Xvfb).
  settings.force_cpu_renderer = EnvFlag("ESCFR_FORCE_CPU");

  Config config;
  config.scroll_timer_delay = 1.0 / 90.0;

  app_ = App::Create(settings, config);

  window_ = Window::Create(app_->main_monitor(), 1280, 820, false,
    kWindowFlags_Resizable | kWindowFlags_Titled | kWindowFlags_Maximizable);

  window_->SetTitle("EscFR — Wired Browser");
  window_->MoveToCenter();

  ui_.reset(new UI(window_));
  window_->set_listener(ui_.get());
}

Browser::~Browser() {
  window_->set_listener(nullptr);

  ui_.reset();

  window_ = nullptr;
  app_ = nullptr;
}

void Browser::Run() {
  app_->Run();
}
