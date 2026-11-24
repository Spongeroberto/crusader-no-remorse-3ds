#pragma once
// A screen the game view opens and runs until it closes (Gump_run_modal,
// 1328:1588: only the display and the events, so the game is frozen). The
// game hands it gump key codes (gump_keys.hpp), mouse events in screen
// coordinates and the timer ticks.

#include "core/palette.hpp"
#include "core/surface.hpp"

#include <cstdint>
#include <optional>

namespace crusader {

class ModalGump {
public:
    virtual ~ModalGump() = default;

    virtual void draw(Surface8& screen) const = 0;
    virtual void key(std::uint16_t code) = 0;
    virtual void mouse_down(int, int) {}
    virtual void mouse_move(int, int) {}
    virtual void mouse_up(int, int) {}
    // Timer ticks, at every pass of the modal loop.
    virtual void update(std::uint32_t) {}
    // Port addition: a controller button (PadButton) pressed while
    // waiting_for_pad(), which the platform layer then keeps from the game.
    virtual bool waiting_for_pad() const { return false; }
    virtual void pad_button(int) {}

    virtual bool closed() const = 0;
    // The gump's result once closed (Gump_run_modal's return value).
    virtual int result() const { return 0; }
    // Drawn over the frozen game view, rather than over a cleared screen.
    virtual bool over_view() const { return false; }
    // Port addition: the rectangle it draws in, in screen coordinates, for
    // platforms that show it larger than the view (Game::set_overlay_zoom).
    struct Area {
        int x = 0, y = 0, w = 0, h = 0;
    };
    virtual std::optional<Area> area() const { return std::nullopt; }
    // Port addition: for an area() larger than such a platform's screen, the
    // part of it to keep in view (the selection); the rest is cut off.
    virtual std::optional<Area> focus() const { return std::nullopt; }
    // The palette it writes to the DAC as it goes, if any.
    virtual const Palette* palette() const { return nullptr; }
};

}  // namespace crusader
