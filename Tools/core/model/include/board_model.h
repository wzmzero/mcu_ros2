#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace mcu {
inline constexpr std::array<const char *, 2> boards{"esp32s3", "stm32"};
inline constexpr std::array<const char *, 8> fields{
    "heartbeat",     "echo",           "peer_received",
    "roundtrip",     "service_result", "action_feedback",
    "action_result", "action_status"};
struct Sample {
  std::string value;
  std::int64_t seen_ms = 0;
};
class BoardModel {
public:
  void update(const std::string &board, const std::string &field,
              const std::string &value, std::int64_t now_ms);
  Sample sample(const std::string &board, const std::string &field) const;
  bool online(const std::string &board, std::int64_t now_ms) const;
  void clear() { samples_.clear(); }

private:
  std::map<std::pair<std::string, std::string>, Sample> samples_;
};
} // namespace mcu
