#include "board_model.h"
#include <algorithm>
namespace mcu {
void BoardModel::update(const std::string &board, const std::string &field,
                        const std::string &value, std::int64_t now_ms) {
  if (std::find(boards.begin(), boards.end(), board) != boards.end() &&
      std::find(fields.begin(), fields.end(), field) != fields.end())
    samples_[{board, field}] = {value, now_ms};
}
Sample BoardModel::sample(const std::string &board,
                          const std::string &field) const {
  auto it = samples_.find({board, field});
  return it == samples_.end() ? Sample{} : it->second;
}
bool BoardModel::online(const std::string &board, std::int64_t now_ms) const {
  auto heartbeat = sample(board, "heartbeat");
  return heartbeat.seen_ms != 0 && now_ms - heartbeat.seen_ms < 3500;
}
} // namespace mcu
