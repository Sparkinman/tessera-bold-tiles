#include "bold_tiles.h"
#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <map>
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
    const std::string tap = c.options["tap"] | "none";
    tap_card_ = tap == "card";
    forecast_ = c.options["forecast"] | "";
    tile_ = c.tile;
    value_ = ui::label(c.parent, Font::TITLE, theme::INK);
    unit_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
  }

  void on_state(JsonObjectConst data) override {
    show_name(data);
    seen_name_ = name_of(data);
    const std::string text = format(data["state"] | "");
    const std::string unit = unit_on_ ? std::string(data["attributes"]["unit_of_measurement"] | "") : "";
    // What the history card shows of this tile: its weather entity, unit and reading.
    plugin_->set_link(entity_, BoldTiles::Link{forecast_, std::string(data["attributes"]["unit_of_measurement"] | ""), text});
    if (text == text_ && unit == unit_text_) return;
    text_ = text;
    unit_text_ = unit;
    layout();
  }

  // With Tap: Graph and forecast, a tap opens the history card for this sensor.
  void on_tap() override {
    if (tap_card_ && !entity_.empty()) tessera::open_card(plugin_->plugin_id(), "history", entity_, tile_, seen_name_);
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

  int decimals_ = -1, tile_ = -1;
  bool unit_on_ = true, tap_card_ = false;
  std::string text_, unit_text_, forecast_, seen_name_;
  lv_obj_t *value_{}, *unit_{};
};

// What a Bold state tile can do to its entity, and the words for it: a lock locks or unlocks, a cover opens or closes,
// a switch, light or input boolean turns on or off. NONE for an entity it cannot change, or a state in between.
enum class Act : uint8_t { NONE, LOCK, UNLOCK, OPEN, CLOSE, ON, OFF };

static Act act_for(const std::string &domain, const std::string &state) {
  if (domain == "lock") {
    if (state == "locked") return Act::UNLOCK;
    if (state == "unlocked" || state == "open" || state == "jammed") return Act::LOCK;
  } else if (domain == "cover") {
    if (state == "closed") return Act::OPEN;
    if (state == "open") return Act::CLOSE;
  } else if (domain == "switch" || domain == "light" || domain == "input_boolean") {
    if (state == "on") return Act::OFF;
    if (state == "off") return Act::ON;
  }
  return Act::NONE;
}

// The popup a Bold state tile opens on a tap: the name, the state in the largest bold letters that fit, and one big key
// under it that does the opposite (Unlock while it is locked, Lock while it is not). As wide and as tall as the screen
// lets a popup be. A tap on the key sends the action; the key then says what is happening ("Locking") until Home
// Assistant reports the new state, and the popup closes itself a moment after.
class ActionPopup : public tessera::Popup {
 public:
  ActionPopup(const BoldTiles *plugin, std::vector<const lv_font_t *> fonts, std::string entity, std::string name,
              std::function<void()> gone)
      : plugin_(plugin), fonts_(std::move(fonts)), entity_(std::move(entity)), name_text_(std::move(name)),
        gone_(std::move(gone)) {}

  void open(const tessera::PopupContext &c) override {
    w_ = c.width;
    h_ = c.height;
    const int gap = ui::px(ui::large() ? 14 : 10);
    // The name on the first row, short of the close key.
    name_ = ui::label(c.parent, Font::HEADLINE, theme::INK_SOFT);
    const int name_h = ui::line_height(Font::HEADLINE);
    lv_obj_set_size(name_, std::max(1, w_ - c.corner), name_h);
    lv_obj_set_pos(name_, 0, std::max(0, (c.key - name_h) / 2));
    ui::set_text(name_, name_text_);
    top_ = std::max(c.key, name_h) + gap;
    // The key: the lower 40% of the room, a rounded block with its word in the middle.
    key_h_ = h_ * 40 / 100;
    key_y_ = h_ - key_h_;
    key_ = ui::block(c.parent, theme::ACCENT);
    lv_obj_set_size(key_, w_, key_h_);
    lv_obj_set_pos(key_, 0, key_y_);
    lv_obj_set_style_radius(key_, ui::px(18), 0);
    key_word_ = ui::label(key_, Font::TITLE, theme::ON_ACCENT);
    word_ = ui::label(c.parent, Font::TITLE, theme::INK);
    word_h_ = key_y_ - gap - top_;
    opened_ = true;
    paint();
  }

  // The tile's latest state: what it shows and what the key does now.
  void set_state(const std::string &state, const std::string &word, bool alert, Act act) {
    const bool changed = !state_.empty() && state != state_;
    state_ = state;
    word_text_ = word;
    alert_ = alert;
    act_ = act;
    if (busy_ && changed) {
      busy_ = false;
      close_at_ = now_ + 2;  // done: let it be seen, then go
    }
    if (opened_) paint();
  }

  void on_tick(uint32_t epoch) override {
    now_ = epoch;
    if (close_at_ && epoch >= close_at_) {
      close_at_ = 0;
      if (gone_) gone_();
      tessera::close_popup(plugin_);
    }
  }

  void on_tap() override {
    if (busy_ || act_ == Act::NONE) return;
    const tessera::Point p = tessera::tap_point();
    // The key, whether the point comes in the room's pixels or the glass's.
    lv_area_t a;
    lv_obj_get_coords(key_, &a);
    const bool in_room = p.y >= key_y_ && p.y <= key_y_ + key_h_;
    const bool on_glass = p.x >= a.x1 && p.x <= a.x2 && p.y >= a.y1 && p.y <= a.y2;
    if (!in_room && !on_glass) return;
    switch (act_) {
      case Act::LOCK: tessera::action("lock.lock", entity_); break;
      case Act::UNLOCK: tessera::action("lock.unlock", entity_); break;
      case Act::OPEN: tessera::action("cover.open_cover", entity_); break;
      case Act::CLOSE: tessera::action("cover.close_cover", entity_); break;
      case Act::ON: tessera::wish(entity_, tessera::Wish::ON_OFF, "on"); break;
      case Act::OFF: tessera::wish(entity_, tessera::Wish::ON_OFF, "off"); break;
      default: return;
    }
    busy_ = true;
    close_at_ = now_ ? now_ + 20 : 0;  // no answer in 20 s: it goes anyway
    paint();
  }

  void on_theme() override { paint(); }

  void on_dismiss() override {
    if (gone_) gone_();
  }

 private:
  const char *key_text() const {
    if (busy_) {
      switch (act_) {
        case Act::LOCK: return "locking";
        case Act::UNLOCK: return "unlocking";
        case Act::OPEN: return "opening";
        case Act::CLOSE: return "closing";
        default: return "working";
      }
    }
    switch (act_) {
      case Act::LOCK: return "act_lock";
      case Act::UNLOCK: return "act_unlock";
      case Act::OPEN: return "act_open";
      case Act::CLOSE: return "act_close";
      case Act::ON: return "act_on";
      case Act::OFF: return "act_off";
      default: return "unavailable";
    }
  }

  // The largest font whose capitals fit `max_h` and whose text fits `max_w`.
  const lv_font_t *fit(const std::string &text, int max_w, int max_h) const {
    for (const lv_font_t *f : fonts_)
      if (cap_h(f) <= max_h && text_w(text, f) <= max_w) return f;
    return fonts_.back();
  }

  // Draws one line of words centred in a band, by their capitals.
  static void place(lv_obj_t *label, const std::string &text, const lv_font_t *f, int x0, int w, int y0, int h) {
    lv_obj_set_style_text_font(label, f, 0);
    ui::set_text(label, text);
    int ctop, ch;
    cap_box(f, ctop, ch);
    const int tw = text_w(text, f);
    lv_obj_set_size(label, tw + 2, f->line_height);
    lv_obj_set_pos(label, x0 + std::max(0, (w - tw) / 2), y0 + std::max(0, (h - ch) / 2) - ctop);
  }

  void paint() {
    if (!opened_) return;
    place(word_, word_text_, fit(word_text_, w_, word_h_ * 80 / 100), 0, w_, top_, word_h_);
    ui::set_tone(word_, alert_ ? tessera::Tone::ALERT : tessera::Tone::NORMAL, theme::INK);
    const std::string kw = plugin_->text(key_text());
    place(key_word_, kw, fit(kw, w_ * 85 / 100, key_h_ * 45 / 100), 0, w_, 0, key_h_);
    // Unlocking and opening in the alert colour, the rest in the accent; a key with nothing to do goes grey.
    const bool opens = act_ == Act::UNLOCK || act_ == Act::OPEN;
    ui::set_fill(key_, act_ == Act::NONE ? tessera::Tone::NORMAL : opens ? tessera::Tone::ALERT : tessera::Tone::ACCENT,
                 theme::ACCENT);
    lv_obj_set_style_bg_opa(key_, busy_ ? LV_OPA_60 : LV_OPA_COVER, 0);
    ui::set_color(key_word_, theme::ON_ACCENT);
  }

  const BoldTiles *plugin_;
  std::vector<const lv_font_t *> fonts_;
  std::string entity_, name_text_, state_, word_text_;
  std::function<void()> gone_;
  bool opened_ = false, alert_ = false, busy_ = false;
  Act act_ = Act::NONE;
  int w_ = 0, h_ = 0, top_ = 0, word_h_ = 0, key_y_ = 0, key_h_ = 0;
  uint32_t now_ = 0, close_at_ = 0;
  lv_obj_t *name_{}, *word_{}, *key_{}, *key_word_{};
};

// An entity's state as a large bold word, in the alert colour when it stands open or unlocked. With Tap: Popup, a tap
// opens the ActionPopup above to lock or unlock it (or open or close, or turn on or off).
class StateTile : public BoldTile {
 public:
  using BoldTile::BoldTile;

  ~StateTile() override {
    // A popup of this tile outlives it only on the glass: close it, so it never calls back into a deleted tile.
    if (popup_) tessera::close_popup(plugin_);
  }

  void create(const tessera::TileContext &c) override {
    begin(c, true);
    alert_on_ = c.options["alert"] | true;
    const std::string tap = c.options["tap"] | "none";
    tap_popup_ = tap == "popup";
    word_ = ui::label(c.parent, Font::TITLE, theme::INK);
  }

  void on_tap() override {
    if (!tap_popup_ || entity_.empty()) return;
    const std::string domain = entity_.substr(0, entity_.find('.'));
    tessera::show_popup(
        plugin_,
        [this, domain]() {
          popup_ = new ActionPopup(plugin_, fonts_, entity_, shown_name_, [this]() { popup_ = nullptr; });
          popup_->set_state(state_, state_word_, alert_, act_for(domain, state_));
          return popup_;
        },
        tessera::PopupWidth::WIDE);
  }

  void on_state(JsonObjectConst data) override {
    show_name(data);
    shown_name_ = name_of(data);
    const std::string state = data["state"] | "";
    const std::string dc = data["attributes"]["device_class"] | "";
    const std::string domain = entity_.substr(0, entity_.find('.'));
    bool alert = false;
    const std::string word = word_for(domain, dc, state, alert);
    alert_ = alert_on_ && alert;
    state_ = state;
    state_word_ = word;
    if (word != text_) {
      text_ = word;
      layout();
    }
    paint();
    if (popup_) popup_->set_state(state_, state_word_, alert_, act_for(domain, state_));
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

  bool alert_on_ = true, alert_ = false, tap_popup_ = false;
  std::string text_, state_, state_word_, shown_name_;
  ActionPopup *popup_{};
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

// ---- The history card: the last week as bars and the forecast for the place ----

static int32_t civil_days(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
// A moment as ISO 8601 in UTC ("2026-11-15T05:00:00Z"), for the recorder's start_time.
static std::string iso_utc(uint32_t epoch) {
  int32_t z = static_cast<int32_t>(epoch / 86400) + 719468;
  const int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = static_cast<int32_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
  const uint32_t s = epoch % 86400;
  char buf[24];
  snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02u:%02u:00Z", static_cast<int>(y), m, d, s / 3600, (s / 60) % 60);
  return buf;
}
static uint32_t weather_icon(const std::string &c) {
  if (c == "sunny") return 0xF0599;
  if (c == "clear-night") return 0xF0594;
  if (c == "cloudy") return 0xF0590;
  if (c == "partlycloudy") return 0xF0595;
  if (c == "rainy") return 0xF0597;
  if (c == "pouring") return 0xF0596;
  if (c == "snowy") return 0xF0598;
  if (c == "snowy-rainy") return 0xF067F;
  if (c == "fog") return 0xF0591;
  if (c == "hail") return 0xF0592;
  if (c == "lightning") return 0xF0593;
  if (c == "lightning-rainy") return 0xF067E;
  if (c == "windy" || c == "windy-variant") return 0xF059D;
  return 0xF05D6;
}
static std::string whole(float v, const char *suffix = "\xC2\xB0") {
  if (std::isnan(v)) return "-";
  char buf[16];
  snprintf(buf, sizeof buf, "%d%s", static_cast<int>(std::lround(v)), suffix);
  return buf;
}
// One line of words centred in a band by their capitals, from x0 (or ending at x0 with `right`, or centred in band_w).
static void put(lv_obj_t *label, const std::string &text, const lv_font_t *f, int x0, int y0, int h, int align = 0,
                int band_w = 0) {
  lv_obj_set_style_text_font(label, f, 0);
  ui::set_text(label, text);
  int ctop, ch;
  cap_box(f, ctop, ch);
  const int tw = text_w(text, f);
  lv_obj_set_size(label, tw + 2, f->line_height);
  const int x = align < 0 ? x0 - tw : align > 0 ? x0 + std::max(0, (band_w - tw) / 2) : x0;
  lv_obj_set_pos(label, x, y0 + std::max(0, (h - ch) / 2) - ctop);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
}
static void gone(lv_obj_t *o) {
  if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// The card a Bold value tile opens with Tap: Graph and forecast. At the top the reading now, large, and the week's low
// and high; under it the last seven days as bars (one per three hours, from the recorder's hourly means) with the
// days marked; at the bottom the daily forecast of the weather entity chosen on the tile, a bold column per day.
class HistoryCard : public tessera::Card, public BoldTiles::Answerer {
 public:
  explicit HistoryCard(const BoldTiles *plugin) : plugin_(plugin) {}
  ~HistoryCard() override {
    if (plugin_->answerer() == this) plugin_->set_answerer(nullptr);
  }

  void open(const tessera::CardContext &c) override {
    w_ = c.width;
    h_ = c.height;
    entity_ = c.entity ? c.entity : "";
    const BoldTiles::Link link = plugin_->link(entity_);
    weather_ = link.weather;
    unit_ = link.unit;
    now_text_ = link.value;
    for (auto it = plugin_->fonts().rbegin(); it != plugin_->fonts().rend(); ++it) fonts_.push_back((*it)->get_lv_font());
    fonts_.push_back(ui::font(Font::TITLE));
    pad_ = ui::px(14);
    now_ = ui::label(c.parent, Font::TITLE, theme::INK);
    range_ = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
    hi_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lo_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    for (auto &b : bars_) {
      b = ui::block(c.parent, theme::ACCENT);
      lv_obj_set_style_radius(b, ui::px(2), 0);
      gone(b);
    }
    for (auto &m : marks_) {
      m = ui::label(c.parent, Font::BODY, theme::MUTED);
      gone(m);
    }
    for (auto &col : cols_) {
      col.day = ui::label(c.parent, Font::TITLE, theme::INK_SOFT);
      col.icon = ui::label(c.parent, Font::ICON, theme::INK);
      col.hi = ui::label(c.parent, Font::TITLE, theme::INK);
      col.lo = ui::label(c.parent, Font::TITLE, theme::MUTED);
      gone(col.day), gone(col.icon), gone(col.hi), gone(col.lo);
    }
    note_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    // The bands: the reading 18%, the graph 47%, the forecast the rest.
    top_h_ = h_ * 18 / 100;
    graph_y_ = top_h_;
    graph_h_ = h_ * 47 / 100;
    fc_y_ = graph_y_ + graph_h_;
    fc_h_ = h_ - fc_y_;
    put(note_, plugin_->text("loading"), ui::font(Font::BODY), pad_, graph_y_, graph_h_);
    plugin_->set_answerer(this);
    if (!weather_.empty()) {
      JsonDocument request;
      request["ask"] = "call_service:weather.get_forecasts";
      request["data"]["entity_id"] = weather_;
      request["data"]["type"] = "daily";
      forecast_re_ = tessera::send(const_cast<BoldTiles *>(plugin_), request.as<JsonObjectConst>());
    }
    paint_top();
  }

  void on_state(JsonObjectConst data) override {
    const std::string state = data["state"] | "";
    if (!state.empty() && state != "unavailable" && state != "unknown") {
      char *end = nullptr;
      const float v = strtof(state.c_str(), &end);
      if (end != state.c_str()) {
        char buf[16];
        snprintf(buf, sizeof buf, "%.1f", v);
        now_text_ = buf;
        paint_top();
      }
    }
  }

  void on_tick(uint32_t epoch) override {
    if (!epoch) return;
    now_epoch_ = epoch;
    if (!history_re_ && !history_asked_) {
      // The last seven days of hourly means, from the recorder.
      history_asked_ = true;
      JsonDocument request;
      request["ask"] = "recorder/statistics_during_period";
      request["data"]["start_time"] = iso_utc(epoch - 7 * 86400);
      request["data"]["statistic_ids"].add(entity_);
      request["data"]["period"] = "hour";
      request["data"]["types"].add("mean");
      history_re_ = tessera::send(const_cast<BoldTiles *>(plugin_), request.as<JsonObjectConst>());
      if (!history_re_) put(note_, plugin_->text("failed"), ui::font(Font::BODY), pad_, graph_y_, graph_h_);
    }
  }

  void answer(JsonObjectConst message) override {
    const uint32_t re = message["re"] | 0u;
    if (!re) return;
    if (re == history_re_) {
      if (!(message["ok"] | false)) {
        put(note_, plugin_->text("failed"), ui::font(Font::BODY), pad_, graph_y_, graph_h_);
        return;
      }
      values_.clear();
      // With the manifest's answers: {"mean": [..]}; without: {"<id>": [{"mean": ..}, ..]}.
      JsonVariantConst mean = message["result"]["mean"];
      if (mean.is<JsonArrayConst>()) {
        for (JsonVariantConst v : mean.as<JsonArrayConst>()) values_.push_back(v | NAN);
      } else {
        for (JsonObjectConst p : message["result"][entity_].as<JsonArrayConst>()) values_.push_back(p["mean"] | NAN);
      }
      values_.erase(std::remove_if(values_.begin(), values_.end(), [](float v) { return std::isnan(v); }), values_.end());
      paint_graph();
      paint_top();
    } else if (re == forecast_re_) {
      days_.clear();
      if (message["ok"] | false) {
        for (JsonObjectConst f : message["result"][weather_]["forecast"].as<JsonArrayConst>()) {
          Day d;
          int y = 0, m = 0, day = 0;
          if (sscanf(f["datetime"] | "", "%d-%d-%d", &y, &m, &day) != 3) continue;
          d.civil = civil_days(y, m, day);
          d.condition = f["condition"] | "";
          d.high = f["temperature"] | NAN;
          d.low = f["templow"] | NAN;
          days_.push_back(d);
          if (days_.size() >= 7) break;
        }
      }
      paint_forecast();
    }
    tessera::refresh();
  }

  void on_theme() override {
    for (auto &b : bars_) lv_obj_set_style_bg_color(b, ui::color(theme::ACCENT), 0);
  }

 private:
  struct Day {
    int32_t civil = 0;
    std::string condition;
    float high = NAN, low = NAN;
  };
  struct Col {
    lv_obj_t *day{}, *icon{}, *hi{}, *lo{};
  };
  static constexpr int BARS = 56;

  const lv_font_t *fit(const std::string &text, int max_w, int max_h) const {
    for (const lv_font_t *f : fonts_)
      if (cap_h(f) <= max_h && text_w(text, f) <= max_w) return f;
    return fonts_.back();
  }

  void paint_top() {
    const std::string now = now_text_.empty() ? "-" : now_text_ + unit_;
    put(now_, now, fit(now, w_ * 50 / 100, top_h_ * 70 / 100), pad_, 0, top_h_);
    if (!values_.empty()) {
      const auto mm = std::minmax_element(values_.begin(), values_.end());
      std::string r = plugin_->text("week") + std::string("  ") + whole(*mm.first) + " / " + whole(*mm.second);
      put(range_, r, fit(r, w_ * 45 / 100, top_h_ * 40 / 100), w_ - pad_, 0, top_h_, -1);
    }
  }

  void paint_graph() {
    gone(note_);
    for (auto &b : bars_) gone(b);
    for (auto &m : marks_) gone(m);
    if (values_.size() < 2) {
      put(note_, plugin_->text("no_history"), ui::font(Font::BODY), pad_, graph_y_, graph_h_);
      return;
    }
    // Three hours a bar, the newest at the right.
    const int n = std::min<int>(BARS, (values_.size() + 2) / 3);
    const int per = (values_.size() + n - 1) / n;
    std::vector<float> bucket;
    for (int i = 0; i < n; ++i) {
      float sum = 0;
      int count = 0;
      for (int k = i * per; k < std::min<int>(values_.size(), (i + 1) * per); ++k) sum += values_[k], ++count;
      if (count) bucket.push_back(sum / count);
    }
    const auto mm = std::minmax_element(values_.begin(), values_.end());
    const float lo = *mm.first, hi = *mm.second, span = std::max(1.0f, hi - lo);
    const int label_w = ui::px(60);
    const int marks_h = ui::line_height(Font::BODY);
    const int x0 = pad_ + label_w, gw = w_ - x0 - pad_;
    const int gh = graph_h_ - marks_h - ui::px(8);
    const int slot = std::max(2, gw / static_cast<int>(bucket.size()));
    const int bar_w = std::max(1, slot * 70 / 100);
    for (size_t i = 0; i < bucket.size(); ++i) {
      const int bh = std::max(ui::px(3), static_cast<int>((bucket[i] - lo) / span * gh * 0.9f) + gh / 10);
      lv_obj_set_size(bars_[i], bar_w, bh);
      lv_obj_set_pos(bars_[i], x0 + static_cast<int>(i) * slot, graph_y_ + gh - bh);
      lv_obj_remove_flag(bars_[i], LV_OBJ_FLAG_HIDDEN);
    }
    // The week's high and low beside the bars.
    put(hi_, whole(hi), ui::font(Font::BODY), pad_, graph_y_, marks_h);
    put(lo_, whole(lo), ui::font(Font::BODY), pad_, graph_y_ + gh - marks_h, marks_h);
    // The days under the bars, where a local day begins (the newest bar is the last hour).
    if (!now_epoch_) return;
    static const char *const SHORT[7] = {"s0", "s1", "s2", "s3", "s4", "s5", "s6"};
    int marks = 0, last_day = -1;
    for (size_t i = 0; i < bucket.size() && marks < 8; ++i) {
      const uint32_t back = static_cast<uint32_t>((values_.size() - 1 - std::min<size_t>(values_.size() - 1, i * per)) * 3600u);
      const tessera::LocalTime t = tessera::local_time(now_epoch_ - back);
      if (t.day != last_day) {
        if (last_day != -1) {
          const int wd = static_cast<int>(((civil_days(t.year, t.month, t.day) % 7) + 11) % 7);
          put(marks_[marks++], plugin_->text(SHORT[wd]), ui::font(Font::BODY), x0 + static_cast<int>(i) * slot,
              graph_y_ + gh + ui::px(4), marks_h);
        }
        last_day = t.day;
      }
    }
  }

  void paint_forecast() {
    for (auto &col : cols_) gone(col.day), gone(col.icon), gone(col.hi), gone(col.lo);
    if (days_.empty()) return;
    static const char *const SHORT[7] = {"s0", "s1", "s2", "s3", "s4", "s5", "s6"};
    int32_t today = 0;
    if (now_epoch_) {
      const tessera::LocalTime t = tessera::local_time(now_epoch_);
      today = civil_days(t.year, t.month, t.day);
    }
    std::vector<const Day *> shown;
    for (const Day &d : days_)
      if (!today || d.civil >= today) shown.push_back(&d);
    const int n = std::min<int>(shown.size(), 7);
    if (!n) return;
    const int col_w = (w_ - 2 * pad_) / n;
    const int day_h = fc_h_ * 22 / 100, icon_h = ui::line_height(Font::ICON), rest = fc_h_ - day_h - icon_h;
    for (int i = 0; i < n; ++i) {
      const Day &d = *shown[i];
      Col &col = cols_[i];
      const int x = pad_ + i * col_w;
      const std::string name = d.civil == today ? plugin_->text("today") : plugin_->text(SHORT[((d.civil % 7) + 11) % 7]);
      put(col.day, name, fit(name, col_w * 90 / 100, day_h * 60 / 100), x, fc_y_, day_h, 1, col_w);
      ui::set_text(col.icon, ui::icon(weather_icon(d.condition)));
      lv_obj_set_size(col.icon, col_w, icon_h);
      lv_obj_set_style_text_align(col.icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(col.icon, x, fc_y_ + day_h);
      lv_obj_remove_flag(col.icon, LV_OBJ_FLAG_HIDDEN);
      const std::string hi = whole(d.high), lo = whole(d.low);
      put(col.hi, hi, fit(hi, col_w * 85 / 100, rest * 40 / 100), x, fc_y_ + day_h + icon_h, rest * 58 / 100, 1, col_w);
      put(col.lo, lo, fit(lo, col_w * 70 / 100, rest * 25 / 100), x, fc_y_ + day_h + icon_h + rest * 58 / 100,
          rest * 42 / 100, 1, col_w);
    }
  }

  const BoldTiles *plugin_;
  int w_ = 0, h_ = 0, pad_ = 14, top_h_ = 0, graph_y_ = 0, graph_h_ = 0, fc_y_ = 0, fc_h_ = 0;
  uint32_t now_epoch_ = 0, history_re_ = 0, forecast_re_ = 0;
  bool history_asked_ = false;
  std::string entity_, weather_, unit_, now_text_;
  std::vector<const lv_font_t *> fonts_;
  std::vector<float> values_;
  std::vector<Day> days_;
  lv_obj_t *now_{}, *range_{}, *hi_{}, *lo_{}, *bars_[BARS]{}, *marks_[8]{}, *note_{};
  Col cols_[7];
};

void BoldTiles::on_message(JsonObjectConst message) {
  if (answerer_) answerer_->answer(message);
}

void BoldTiles::setup() {
  add_tile("value", [this]() { return new ValueTile(this); });
  add_tile("state", [this]() { return new StateTile(this); });
  add_tile("switch", [this]() { return new SwitchTile(this); });
  add_card("history", [this]() { return new HistoryCard(this); }, true);
}

}  // namespace esphome::bold_tiles
