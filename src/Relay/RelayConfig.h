#pragma once

#include <memory>
#include <string>

namespace mediakit {
namespace relay {

bool matches(const std::string &vhost, const std::string &app, const std::string &stream);
std::shared_ptr<void> holdPendingReader(const std::string &vhost, const std::string &app, const std::string &stream);
bool hasPendingReader(const std::string &vhost, const std::string &app, const std::string &stream);
std::string sourceUrl(const std::string &schema, const std::string &stream, const std::string &params);
std::string setting(const char *key, const char *legacy_key, const char *fallback = "");

} // namespace relay
} // namespace mediakit
