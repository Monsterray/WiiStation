/* Hold-to-repeat for Up and Down in lists: the feel is these four numbers (milliseconds). The first
 * repeat comes REPEAT_DELAY_MS after the press; then one every repeatInterval(held), where held is
 * the time since the press: REPEAT_START_MS at the first repeat, falling in a straight line to
 * REPEAT_FAST_MS once REPEAT_RAMP_MS have passed since the press, and staying there.
 * Its own header so libgui (FocusManager) can use it without MenuEngine.h's layout names. */
#ifndef LISTREPEAT_H
#define LISTREPEAT_H

namespace menu {
namespace engine {

enum {
	REPEAT_DELAY_MS = 400,
	REPEAT_START_MS = 120,
	REPEAT_FAST_MS  = 30,
	REPEAT_RAMP_MS  = 1500
};
int repeatInterval(int heldMs);		/* MenuEngine.cpp */

} // namespace engine
} // namespace menu

#endif
