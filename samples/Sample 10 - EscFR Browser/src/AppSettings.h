#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace escfr {

/// Settings used when nothing has been saved yet. Kept in sync with the
/// defaults inside assets/settings.html and assets/ui.js.
inline const char* DefaultSettingsJSON() {
  return "{\"searchEngine\":\"duckduckgo\",\"homePage\":\"esc://newtab\","
         "\"scanlines\":true,\"glitch\":true,\"compact\":false,\"shield\":true}";
}

inline std::vector<std::string> SettingsPathCandidates(const std::string& file_system_path) {
  std::vector<std::string> paths;

  if (!file_system_path.empty()) {
    std::string hint = file_system_path;
    if (hint.back() != '/' && hint.back() != '\\')
      hint += "/";
    paths.push_back(hint + "escfr-settings.json");
  }

  paths.push_back("./assets/escfr-settings.json");
  paths.push_back("escfr-settings.json");
  return paths;
}

inline std::string LoadSettingsJSON(const std::string& file_system_path) {
  std::vector<std::string> paths = SettingsPathCandidates(file_system_path);

  for (const std::string& path : paths) {
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.good())
      continue;

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string json = buffer.str();

    // Guard against truncated / corrupt files rather than feeding garbage to JS.
    if (json.find('{') == 0 && json.find('}') != std::string::npos)
      return json;
  }

  return DefaultSettingsJSON();
}

inline bool SaveSettingsJSON(const std::string& file_system_path, const std::string& json) {
  if (json.size() < 2 || json.front() != '{' || json.back() != '}')
    return false;

  std::vector<std::string> paths = SettingsPathCandidates(file_system_path);

  for (const std::string& path : paths) {
    std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.good())
      continue;

    out << json;
    out.close();
    if (out.good())
      return true;
  }

  return false;
}

/// Extremely small helper: pull the value of a string key out of the flat
/// settings object above (used for the Home button, which lives in C++).
inline std::string FindJSONString(const std::string& json, const std::string& key) {
  std::string needle = "\"" + key + "\"";
  size_t pos = json.find(needle);
  if (pos == std::string::npos)
    return std::string();

  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos)
    return std::string();

  pos = json.find('"', pos);
  if (pos == std::string::npos)
    return std::string();

  size_t end = json.find('"', pos + 1);
  if (end == std::string::npos)
    return std::string();

  return json.substr(pos + 1, end - pos - 1);
}

/// Map esc:// URLs onto the local asset files that implement them.
inline std::string ResolveInternalURL(const std::string& url) {
  std::string base = url;
  size_t hash = base.find('#');
  std::string fragment = hash == std::string::npos ? "" : base.substr(hash);
  if (hash != std::string::npos)
    base = base.substr(0, hash);

  if (base == "esc://newtab" || base == "esc://" || base == "esc://home")
    return "file:///new_tab_page.html";

  if (base == "esc://settings")
    return "file:///settings.html" + fragment;

  if (base == "esc://about" || base == "esc://version")
    return "file:///about.html" + fragment;

  return std::string();
}

}  // namespace escfr
