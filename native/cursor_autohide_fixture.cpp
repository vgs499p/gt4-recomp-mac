// TASK-060: controls for the cursor auto-hide rule (native/cursor_autohide.hpp). Pure: no window.

#include "cursor_autohide.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>

namespace {

using namespace gt4::cursor;

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

void policy_parsing() {
  require(parse_policy(nullptr).auto_hide, "unset GT4_CURSOR_AUTOHIDE means on");
  require(parse_policy("1").auto_hide, "GT4_CURSOR_AUTOHIDE=1 is on");
  require(!parse_policy("0").auto_hide, "GT4_CURSOR_AUTOHIDE=0 is off");
  require(parse_policy(nullptr).hide_delay_ms == 3000, "the default delay is 3 s");
  for (const char *bad : {"", "2", "true", "off", " 0"}) {
    bool refused = false;
    try {
      parse_policy(bad);
    } catch (const std::invalid_argument &) {
      refused = true;
    }
    require(refused, "other GT4_CURSOR_AUTOHIDE values are refused");
  }
}

void hides_after_the_delay_over_the_picture() {
  AutoHide cursor;
  require(!cursor.update(100, 100, 1000, true), "the first sample counts as a movement");
  require(!cursor.update(100, 100, 3999, true), "still visible just before 3 s");
  require(cursor.update(100, 100, 4000, true), "hidden after 3 s without movement over the picture");
  require(cursor.update(100, 100, 9000, true), "stays hidden while the mouse rests");
  require(!cursor.update(101, 100, 9001, true), "any movement shows it at once");
  require(!cursor.update(101, 100, 12000, true), "the delay restarts at the last movement");
  require(cursor.update(101, 100, 12001, true), "hidden again 3 s after the last movement");
}

void never_touched_outside_the_picture() {
  AutoHide cursor;
  cursor.update(10, 10, 0, false);
  require(!cursor.update(10, 10, 60000, false), "outside the picture the cursor is never hidden");
  // Resting outside, then the window moves under the resting cursor (no mouse movement): the delay already passed.
  require(cursor.update(10, 10, 60001, true), "a resting cursor over the picture hides once the delay passed");
  require(!cursor.update(10, 10, 60002, false), "leaving the picture releases the cursor");
}

void switched_off() {
  AutoHide cursor(Policy{false, 3000});
  cursor.update(5, 5, 0, true);
  require(!cursor.update(5, 5, 100000, true), "GT4_CURSOR_AUTOHIDE=0 never hides");
}

void short_delay_and_clock_order() {
  AutoHide cursor(Policy{true, 500});
  cursor.update(0, 0, 1000, true);
  require(cursor.update(0, 0, 1500, true), "a 500 ms delay (fixture policy) hides after 500 ms");
  AutoHide backward;
  backward.update(0, 0, 5000, true);
  require(!backward.update(0, 0, 4000, true), "a clock that goes back never hides");
}

} // namespace

int main() {
  policy_parsing();
  hides_after_the_delay_over_the_picture();
  never_touched_outside_the_picture();
  switched_off();
  short_delay_and_clock_order();
  std::puts("cursor auto-hide fixture passed");
  return 0;
}
