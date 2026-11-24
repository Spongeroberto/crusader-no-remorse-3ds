#include "engine/ui/load_screen.hpp"

#include "core/flex.hpp"
#include "core/language.hpp"
#include "engine/ui/gump_keys.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string_view>

namespace crusader {

namespace {

constexpr int kLeft = 111, kTop = 66;
constexpr int kQuarterW = 208, kQuarterH = 174;
constexpr int kSlotX = 183, kSlotY = 109, kSlotStep = 27;
constexpr std::size_t kNameEntry = 9;
// Compact layout (fewer save slots): the frame under the last row, the
// label column painted over (from the frame's inner edge to the slots) and
// the gap between a label and its slot.
constexpr int kPanelMargin = 18;
constexpr int kLabelLeft = kLeft + 6, kLabelTop = kSlotY - 4, kLabelGap = 6;
constexpr int kLabelFont = 13;
constexpr std::uint8_t kLabelColour = 13;  // MISC.PAL
constexpr std::size_t kNameSize = 32;

}  // namespace

// @orig 1058:000D
LoadScreen::Names LoadScreen::read_names(const std::filesystem::path& savegame_dir) {
    Names names;
    for (int i = 0; i < kNames; ++i) {
        const auto path = savegame_dir / ("SAVEGAME." + std::to_string(i + 1));
        if (!std::filesystem::exists(path)) continue;
        try {
            const auto flex = FlexArchive::open(path);
            if (flex.count() <= kNameEntry) continue;
            const auto e = flex.entry(kNameEntry);
            const std::size_t n = std::min(e.size(), kNameSize);
            std::string name(e.begin(), e.begin() + n);
            names[i] = name.substr(0, name.find('\0'));
        } catch (const std::exception&) {
            // unreadable: empty name, as for a missing file
        }
    }
    return names;
}

// @orig 1008:0088
LoadScreen::LoadScreen(ShapeCache& gumps, const FontSet& fonts, const Names& names, int& selected, Mode mode,
                       int save_slots)
    : gumps_(&gumps), fonts_(&fonts), names_(&names), selected_(&selected), mode_(mode),
      save_slots_(std::clamp(save_slots, 1, kSaveSlots)) {
    // The rows, by slot: the original's eleven, then the autosave on the
    // load screen; with fewer, the autosave first.
    const bool autosave = mode_ == Mode::kLoad;
    if (autosave && compact()) rows_.push_back(kSlots - 1);
    for (int i = 0; i < save_slots_; ++i) rows_.push_back(i);
    if (autosave && !compact()) rows_.push_back(kSlots - 1);
    if (!compact()) *selected_ = std::clamp(*selected_, 0, visible_slots() - 1);
    else if (row_of(*selected_) < 0) *selected_ = rows_.front();
    const Shape* s = gumps_->get(kSlotShape);
    const int w = s && !s->frames().empty() ? s->frames()[0].width : 1;
    const int h = s && !s->frames().empty() ? s->frames()[0].height : 1;
    for (int i = 0; i < kSlots; ++i) {
        const int row = row_of(i) >= 0 ? row_of(i) : i;
        const int y = kSlotY + kSlotStep * row;
        slots_[i] = {kSlotX, y, kSlotX + w - 1, y + h - 1};
    }
    // Compact: the frame 18 pixels under the load screen's last row, about as
    // under the save screen's eleventh (the save screen, a row shorter, the
    // same size).
    panel_height_ = 2 * kQuarterH;
    if (compact()) {
        const int last = kSlotY + kSlotStep * save_slots_ + h;
        panel_height_ = std::min(panel_height_, last + kPanelMargin - kTop);
    }
}

int LoadScreen::row_of(int slot) const {
    const auto it = std::find(rows_.begin(), rows_.end(), slot);
    return it == rows_.end() ? -1 : static_cast<int>(it - rows_.begin());
}

// @orig none: port addition (Game::set_overlay_zoom)
LoadScreen::Rect LoadScreen::area() const {
    return {kLeft, kTop, kLeft + 2 * kQuarterW - 1, kTop + panel_height_ - 1};
}

// @orig 1008:05A6 1008:0C9A
void LoadScreen::draw(Surface8& screen) const {
    screen.clear(0);
    for (int q = 0; q < 4; ++q) {
        const auto shape = static_cast<std::uint16_t>(64 + 5 * static_cast<int>(mode_) + q);
        // Compact: the bottom half drawn higher, over the top one.
        const int y = q < 2 ? kTop : kTop + panel_height_ - kQuarterH;
        if (const Shape* s = gumps_->get(shape); s && !s->frames().empty()) {
            s->frames()[0].draw(screen, kLeft + (q & 1) * kQuarterW, y);
        }
    }
    if (compact()) {
        // The rows' "MISSION:" painted over, then numbered.
        for (int y = kLabelTop; y < kTop + panel_height_ - kPanelMargin; ++y) {
            std::fill(screen.row(y) + kLabelLeft, screen.row(y) + kSlotX - 1, std::uint8_t{0});
        }
        // In the small font, in the red of "MISSION:" (one colour, so its
        // pixels are recoloured), right-aligned against the slot.
        TextWriter writer(*fonts_, kLabelFont);
        for (int row = 0; row < visible_slots(); ++row) {
            const int slot = rows_[static_cast<std::size_t>(row)];
            const std::string label =
                slot == kSlots - 1 ? tr("AUTOSAVE") : std::string(tr("SLOT")) + ' ' + std::to_string(slot + 1);
            const Rect& r = slots_[slot];
            const auto size = writer.measure(label);
            Surface8 text(size.width + 2, size.height + 2, 0);
            writer.print(text, 0, 0, label);
            const int x0 = std::max(kLabelLeft, kSlotX - kLabelGap - size.width);
            const int y0 = (r.y0 + r.y1 + 1 - size.height) / 2;
            for (int y = 0; y < text.height; ++y) {
                for (int x = 0; x < text.width && x0 + x < kSlotX - 1; ++x) {
                    if (text.row(y)[x]) screen.row(y0 + y)[x0 + x] = kLabelColour;
                }
            }
        }
    }
    const Shape* slot_shape = gumps_->get(kSlotShape);
    for (const int i : rows_) {
        const Rect& r = slots_[i];
        if (slot_shape && !slot_shape->frames().empty()) slot_shape->frames()[0].draw(screen, r.x0, r.y0);
        if (i == *selected_) outline(screen, r.x0, r.y0, r.x1, r.y1, kSelectedColour);
        const bool edited = editing_ && i == *selected_;
        TextWriter(*fonts_, kFont).print(screen, r.x0 + 4, r.y0 + 12, edited ? name_ + '_' : (*names_)[i]);
    }
}

// @orig 1008:0312
void LoadScreen::key(std::uint16_t code) {
    if (result_) return;
    if (editing_) {
        edit_key(code);
        return;
    }
    const int sel = row_of(*selected_);
    switch (code) {
    case gump_key::kEscape:
        result_ = 0;
        break;
    case gump_key::kUp:
    case '8':
        if (sel != 0) *selected_ = rows_[static_cast<std::size_t>(sel - 1)];
        break;
    case gump_key::kDown:
    case '2':
        if (sel < visible_slots() - 1) *selected_ = rows_[static_cast<std::size_t>(sel + 1)];
        break;
    case gump_key::kEnter:
        choose(*selected_);
        break;
    default:
        break;
    }
}

// @orig 1008:0AEA 1008:0B63
void LoadScreen::mouse_down(int x, int y) {
    if (result_ || editing_) return;
    for (const int i : rows_) {
        if (slots_[i].contains(x, y)) {
            *selected_ = i;
            choose(i);
            return;
        }
    }
}

// @orig 1008:0B63
void LoadScreen::choose(int i) {
    if (mode_ == Mode::kLoad) {
        if (!(*names_)[i].empty()) result_ = 1;
        return;
    }
    editing_ = true;
    name_ = auto_name_ ? auto_name_() : (*names_)[i];
}

// @orig none: port addition (save names without a keyboard)
std::string LoadScreen::dated_name(const std::tm& local) {
    if (local.tm_year + 1900 < 2020) return tr("SAVED GAME");
    const int a = us_version() ? local.tm_mon + 1 : local.tm_mday;
    const int b = us_version() ? local.tm_mday : local.tm_mon + 1;
    char text[32];
    std::snprintf(text, sizeof text, "%02d/%02d/%04d %02d:%02d", a, b, local.tm_year + 1900, local.tm_hour,
                  local.tm_min);
    // German dates: 18.11.2026.
    if (german_version()) text[2] = text[5] = '.';
    return text;
}

// @orig 1008:089C 1008:041A
void LoadScreen::edit_key(std::uint16_t code) {
    // Escape, Enter and Backspace are told by the code's low byte.
    const auto c = static_cast<std::uint8_t>(code);
    if (c == 0x1B) {
        editing_ = false;
        return;
    }
    if (c == 0x0D) {
        if (!name_.empty()) {
            editing_ = false;
            result_ = 1;
        }
        return;
    }
    if (c == 0x08) {
        if (!name_.empty()) name_.pop_back();
        return;
    }
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    // The US executable no longer takes < : and ; (13D8:09F3). The German one
    // (1008:09B7) turns the keys of a US keyboard into a German one's
    // (FUN_1008_0000): " ' : ; [ { give the capital umlauts A O U, and < is
    // not taken. The port's codes follow the player's layout: the umlaut keys
    // give their code page 850 letters (sdl_input.cpp), small or capital.
    const std::string_view punctuation = us_version()       ? " .?!,/\"'"
                                         : german_version() ? " .?!,/"
                                                            : "< :;.?!,/\"'";
    unsigned char umlaut = 0;
    if (german_version()) {
        if (c == 0x84 || c == 0x8E) umlaut = 0x8E;       // a, A umlaut
        else if (c == 0x94 || c == 0x99) umlaut = 0x99;  // o, O umlaut
        else if (c == 0x81 || c == 0x9A) umlaut = 0x9A;  // u, U umlaut
    }
    if (!(alnum || umlaut || punctuation.find(static_cast<char>(c)) != std::string_view::npos)) return;
    if (name_.size() >= kMaxNameLength || code >= 0x100) return;
    name_ += static_cast<char>(umlaut ? umlaut : c >= 'a' && c <= 'z' ? c - 0x20 : c);
    const Rect& r = slots_[*selected_];
    if (TextWriter(*fonts_, kFont).measure(name_ + '_').width > (r.x1 - r.x0 + 1) - 4) name_.pop_back();
}


}  // namespace crusader
