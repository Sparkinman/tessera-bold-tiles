#pragma once
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

  // The bold fonts from plugin.yaml, smallest first.
  void add_font(font::Font *f) { fonts_.push_back(f); }
  const std::vector<font::Font *> &fonts() const { return fonts_; }

 protected:
  std::vector<font::Font *> fonts_;
};

}  // namespace esphome::bold_tiles
