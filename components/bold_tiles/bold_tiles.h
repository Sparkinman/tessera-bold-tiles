#pragma once
#include <map>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/font/font.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::bold_tiles {

// The plugin: three tile types (value, state, switch) that show an entity in large bold Roboto.
class BoldTiles : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  // What a Bold value tile tells its history card: the weather entity chosen for its forecast, its unit and reading.
  struct Link {
    std::string weather, unit, value;
  };
  void set_link(const std::string &entity, const Link &link) const { links_[entity] = link; }
  Link link(const std::string &entity) const {
    auto it = links_.find(entity);
    return it == links_.end() ? Link{} : it->second;
  }
  // The open card that waits for answers of the app (tessera::send).
  struct Answerer {
    virtual ~Answerer() = default;
    virtual void answer(JsonObjectConst message) = 0;
  };
  Answerer *answerer() const { return answerer_; }
  void set_answerer(Answerer *a) const { answerer_ = a; }

  // The bold fonts from plugin.yaml, smallest first.
  void add_font(font::Font *f) { fonts_.push_back(f); }
  const std::vector<font::Font *> &fonts() const { return fonts_; }

 protected:
  std::vector<font::Font *> fonts_;
  mutable std::map<std::string, Link> links_;
  mutable Answerer *answerer_{nullptr};
};

}  // namespace esphome::bold_tiles
