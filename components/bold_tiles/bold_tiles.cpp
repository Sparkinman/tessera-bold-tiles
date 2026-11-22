#include "bold_tiles.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace esphome::bold_tiles {

using tessera::Font;
namespace ui = tessera::ui;

static int text_w(const std::string &text, const lv_font_t *font) {
  lv_point_t size;
  lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}
// The height of a text wrapped to `width`.
static int text_h(const std::string &text, const lv_font_t *font, int width) {
  lv_point_t size;
  lv_text_get_size(&size, text.c_str(), font, 0, 0, width, LV_TEXT_FLAG_NONE);
  return size.y;
}
static int ascent(const lv_font_t *font) { return font->line_height - font->base_line; }
// How far the top of a capital or a digit is below the line's top, and how tall it is: the layout centres what shows,
// not the empty space a line keeps above and below.
static void cap_box(const lv_font_t *font, int &top, int &h) {
  lv_font_glyph_dsc_t g{};
  if (lv_font_get_glyph_dsc(font, &g, 'H', 0) && g.box_h > 0) {
    h = g.box_h;
    top = ascent(font) - g.box_h - g.ofs_y;
  } else {
    h = font->line_height;
    top = 0;
  }
}
static int cap_h(const lv_font_t *font) {
  int top, h;
  cap_box(font, top, h);
  return h;
}

// What all three tiles share: the name small at the top left, the fonts largest first, and the size the tile is set to
// (how much of the tile's height the large text may take).
class BoldTile : public tessera::Tile {
 public:
  explicit BoldTile(const BoldTiles *plugin) : plugin_(plugin) {}

 protected:
  void begin(const tessera::TileContext &c, bool name_line) {
    parent_ = c.parent;
    width_ = c.width;
    height_ = c.height;
    entity_ = c.entity ? c.entity : "";
    // The label: the tile's own Label option first. Tessera makes a new tile when an option changes, so a new label
    // shows at once; the editor's tile name only arrives when the tile is made, so a rename waits for a restart.
    const std::string label = c.options["label"] | "";
    custom_name_ = !label.empty() ? label : (c.name ? c.name : "");
    const std::string size = c.options["size"] | "large";
    share_ = size == "normal" ? 45 : size == "huge" ? 85 : 65;
    gap_ = ui::px(ui::large() ? 8 : 5);
    for (auto it = plugin_->fonts().rbegin(); it != plugin_->fonts().rend(); ++it) fonts_.push_back((*it)->get_lv_font());
    fonts_.push_back(ui::font(Font::TITLE));  // the screen's own bold name font, for the smallest tiles
    if (name_line) {
      // The name in the screen's bold name font, about the size of its second line.
      name_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
      name_h_ = ui::line_height(Font::TITLE);
      lv_obj_set_size(name_, width_, name_h_);
      lv_obj_set_pos(name_, 0, 0);
      if (!custom_name_.empty()) ui::set_text(name_, custom_name_);
    }
  }

  // The name the editor gave the tile, else Home Assistant's.
  void show_name(JsonObjectConst data) {
    if (name_ && custom_name_.empty()) ui::set_text(name_, data["name"] | "");
  }
  std::string name_of(JsonObjectConst data) const {
    return custom_name_.empty() ? std::string(data["name"] | "") : custom_name_;
  }

  // The largest font whose capitals fit `max_h` and whose text fits `max_w` on one line (`extra_w` beside it).
  const lv_font_t *fit_line(const std::string &text, int max_w, int max_h, int extra_w = 0) const {
    for (const lv_font_t *f : fonts_)
      if (cap_h(f) <= max_h && text_w(text, f) + extra_w <= max_w) return f;
    return fonts_.back();
  }
  // The largest font whose text, wrapped to `max_w` with no word broken, fits `max_h`.
  const lv_font_t *fit_wrapped(const std::string &text, int max_w, int max_h) const {
    for (const lv_font_t *f : fonts_) {
      bool words_fit = true;
      size_t start = 0;
      while (start <= text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        if (text_w(text.substr(start, end - start), f) > max_w) words_fit = false;
        start = end + 1;
      }
      if (words_fit && text_h(text, f, max_w) <= max_h) return f;
    }
    return fonts_.back();
  }

  const BoldTiles *plugin_;
  lv_obj_t *parent_{}, *name_{};
  int width_ = 0, height_ = 0, share_ = 65, gap_ = 5, name_h_ = 0;
  std::string entity_, custom_name_;
  std::vector<const lv_font_t *> fonts_;
};

// A sensor's number: large and bold in the middle of the tile, its unit in a smaller bold beside it.
class ValueTile : public BoldTile {
 public:
  using BoldTile::BoldTile;

  void create(const tessera::TileContext &c) override {
    begin(c, true);
    const std::string decimals = c.options["decimals"] | "auto";
    decimals_ = decimals == "auto" ? -1 : atoi(decimals.c_str());
    unit_on_ = c.options["unit"] | true;
    value_ = ui::label(c.parent, Font::TITLE, theme::INK);
    unit_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
  }

  void on_state(JsonObjectConst data) override {
    show_name(data);
    const std::string text = format(data["state"] | "");
    const std::string unit = unit_on_ ? std::string(data["attributes"]["unit_of_measurement"] | "") : "";
    if (text == text_ && unit == unit_text_) return;
    text_ = text;
    unit_text_ = unit;
    layout();
  }

  void on_theme() override {
    ui::set_color(value_, theme::INK);
    ui::set_color(unit_, theme::INK_SOFT);
    if (name_) ui::set_color(name_, theme::INK_SOFT);
  }

 private:
  // The state as a number with the decimals asked for ("auto": as Home Assistant has it, at most one), or the state
  // itself where it is no number; a dash while it is unavailable.
  std::string format(const std::string &state) const {
    if (state.empty() || state == "unavailable" || state == "unknown") return "-";
    char *end = nullptr;
    const float v = strtof(state.c_str(), &end);
    if (end == state.c_str() || *end) return state;
    int places = decimals_;
    if (places < 0) {
      const size_t dot = state.find('.');
      places = dot == std::string::npos ? 0 : std::min<int>(1, state.size() - dot - 1);
    }
    char buf[24];
    snprintf(buf, sizeof buf, "%.*f", places, v);
    return std::string(strcmp(buf, "-0") == 0 ? "0" : buf);
  }

  void layout() {
    const int top = name_h_ + gap_;
    const int room_h = std::min(height_ - top - gap_, height_ * share_ / 100);
    // The unit at about 45% of the value's size: try each value size with the unit font that goes with it.
    const lv_font_t *vf = fonts_.back(), *uf = fonts_.back();
    for (size_t i = 0; i < fonts_.size(); ++i) {
      const lv_font_t *u = fonts_[std::min(fonts_.size() - 1, i + 2)];
      const int extra = unit_text_.empty() ? 0 : gap_ / 2 + text_w(unit_text_, u);
      if (cap_h(fonts_[i]) <= room_h && text_w(text_, fonts_[i]) + extra <= width_) {
        vf = fonts_[i];
        uf = u;
        break;
      }
      vf = fonts_[i];
      uf = u;
    }
    lv_obj_set_style_text_font(value_, vf, 0);
    lv_obj_set_style_text_font(unit_, uf, 0);
    ui::set_text(value_, text_);
    ui::set_text(unit_, unit_text_);
    int ctop, ch;
    cap_box(vf, ctop, ch);
    const int vw = text_w(text_, vf);
    const int uw = unit_text_.empty() ? 0 : gap_ / 2 + text_w(unit_text_, uf);
    const int x = std::max(0, (width_ - vw - uw) / 2);
    // The capitals in the middle of the room under the name.
    const int y = top + std::max(0, (height_ - top - ch) / 2) - ctop;
    lv_obj_set_size(value_, vw + 2, vf->line_height);
    lv_obj_set_pos(value_, x, y);
    // The unit on the value's baseline, at the top of its digits (as °F stands beside a temperature).
    lv_obj_set_size(unit_, uw + 2, uf->line_height);
    int utop, uh;
    cap_box(uf, utop, uh);
    lv_obj_set_pos(unit_, x + vw + gap_ / 2, y + ctop - utop);
  }

  int decimals_ = -1;
  bool unit_on_ = true;
  std::string text_, unit_text_;
  lv_obj_t *value_{}, *unit_{};
};

// An entity's state as a large bold word, in the alert colour when it stands open or unlocked.
class StateTile : public BoldTile {
 public:
  using BoldTile::BoldTile;

  void create(const tessera::TileContext &c) override {
    begin(c, true);
    alert_on_ = c.options["alert"] | true;
    word_ = ui::label(c.parent, Font::TITLE, theme::INK);
  }

  void on_state(JsonObjectConst data) override {
    show_name(data);
    const std::string state = data["state"] | "";
    const std::string dc = data["attributes"]["device_class"] | "";
    const std::string domain = entity_.substr(0, entity_.find('.'));
    bool alert = false;
    const std::string word = word_for(domain, dc, state, alert);
    alert_ = alert_on_ && alert;
    if (word != text_) {
      text_ = word;
      layout();
    }
    paint();
  }

  void on_theme() override {
    paint();
    if (name_) ui::set_color(name_, theme::INK_SOFT);
  }

 private:
  // Home Assistant's state in words: doors and windows open or closed, locks locked or unlocked, the rest as it comes
  // with a capital.
  std::string word_for(const std::string &domain, const std::string &dc, const std::string &state, bool &alert) const {
    if (state.empty() || state == "unavailable" || state == "unknown") return plugin_->text("unavailable");
    const bool opening = dc == "door" || dc == "garage_door" || dc == "window" || dc == "opening";
    if (domain == "binary_sensor" && opening) {
      alert = state == "on";
      return plugin_->text(alert ? "open" : "closed");
    }
    if (domain == "lock") {
      alert = state == "unlocked" || state == "open" || state == "jammed";
      if (state == "locked" || state == "unlocked" || state == "jammed" || state == "locking" || state == "unlocking")
        return plugin_->text(state.c_str());
    }
    if (domain == "cover") {
      alert = state != "closed";
      if (state == "open" || state == "closed" || state == "opening" || state == "closing")
        return plugin_->text(state.c_str());
    }
    if (state == "on" || state == "off") {
      alert = state == "on" && (domain == "binary_sensor" || domain == "alarm_control_panel");
      return plugin_->text(state.c_str());
    }
    std::string word = state;
    std::replace(word.begin(), word.end(), '_', ' ');
    word[0] = static_cast<char>(toupper(static_cast<unsigned char>(word[0])));
    return word;
  }

  void layout() {
    const int top = name_h_ + gap_;
    const int room_h = std::min(height_ - top - gap_, height_ * share_ / 100);
    const lv_font_t *f = fit_line(text_, width_, room_h);
    lv_obj_set_style_text_font(word_, f, 0);
    ui::set_text(word_, text_);
    int ctop, ch;
    cap_box(f, ctop, ch);
    const int w = text_w(text_, f);
    lv_obj_set_size(word_, w + 2, f->line_height);
    lv_obj_set_pos(word_, std::max(0, (width_ - w) / 2), top + std::max(0, (height_ - top - ch) / 2) - ctop);
  }

  void paint() {
    if (word_) ui::set_tone(word_, alert_ ? tessera::Tone::ALERT : tessera::Tone::NORMAL, theme::INK);
  }

  bool alert_on_ = true, alert_ = false;
  std::string text_;
  lv_obj_t *word_{};
};

// A light or switch: its name large and bold over two lines if need be, On or Off under it, a soft colour behind it
// while it is on. A tap turns it on or off through wish(), so it shows at once.
class SwitchTile : public BoldTile {
 public:
  using BoldTile::BoldTile;

  void create(const tessera::TileContext &c) override {
    begin(c, false);
    const std::string color = c.options["color"] | "yellow";
    glow_on_ = color != "none";
    glow_role_ = color == "blue" ? theme::MARK_BLUE : color == "green" ? theme::MARK_GREEN : theme::MARK_AMBER;
    // The colour while it is on: a rounded panel over the whole card area, behind the words.
    glow_ = ui::block(c.parent, glow_role_);
    lv_obj_set_size(glow_, width_, height_);
    lv_obj_set_pos(glow_, 0, 0);
    lv_obj_set_style_radius(glow_, ui::px(10), 0);
    lv_obj_set_style_bg_opa(glow_, LV_OPA_40, 0);
    lv_obj_add_flag(glow_, LV_OBJ_FLAG_HIDDEN);
    title_ = ui::text(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    state_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_style_text_align(state_, LV_TEXT_ALIGN_CENTER, 0);
    state_h_ = ui::line_height(Font::BODY_LARGE);
    lv_obj_set_size(state_, width_, state_h_);
    if (!custom_name_.empty()) set_title(custom_name_);
  }

  void on_state(JsonObjectConst data) override {
    const std::string name = name_of(data);
    if (name != title_text_) set_title(name);
    const std::string state = data["state"] | "";
    on_ = state == "on";
    const bool unavailable = state.empty() || state == "unavailable" || state == "unknown";
    ui::set_text(state_, plugin_->text(unavailable ? "unavailable" : on_ ? "on" : "off"));
    if (glow_on_ && on_) lv_obj_remove_flag(glow_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(glow_, LV_OBJ_FLAG_HIDDEN);
  }

  void on_tap() override {
    if (!entity_.empty()) tessera::wish(entity_, tessera::Wish::ON_OFF, on_ ? "off" : "on");
  }

  void on_theme() override {
    lv_obj_set_style_bg_color(glow_, ui::color(glow_role_), 0);
    ui::set_color(title_, theme::INK);
    ui::set_color(state_, theme::MUTED);
  }

 private:
  void set_title(const std::string &name) {
    title_text_ = name;
    const int room_h = std::min(height_ - state_h_ - gap_, height_ * share_ / 100 + height_ / 6);
    const lv_font_t *f = fit_wrapped(name, width_, room_h);
    lv_obj_set_style_text_font(title_, f, 0);
    ui::set_text(title_, name);
    const int th = text_h(name, f, width_);
    lv_obj_set_size(title_, width_, th);
    // The name and the state together in the middle of the tile.
    const int block = th + gap_ + state_h_;
    const int y = std::max(0, (height_ - block) / 2);
    lv_obj_set_pos(title_, 0, y);
    lv_obj_set_pos(state_, 0, y + th + gap_);
  }

  theme::Role glow_role_ = theme::MARK_AMBER;
  bool on_ = false, glow_on_ = true;
  int state_h_ = 0;
  std::string title_text_;
  lv_obj_t *glow_{}, *title_{}, *state_{};
};

void BoldTiles::setup() {
  add_tile("value", [this]() { return new ValueTile(this); });
  add_tile("state", [this]() { return new StateTile(this); });
  add_tile("switch", [this]() { return new SwitchTile(this); });
}

}  // namespace esphome::bold_tiles
