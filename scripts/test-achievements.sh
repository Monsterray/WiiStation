#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_dir=$(mktemp -d)
trap 'rm -rf "$task_dir"' EXIT
# Upstream's parser uses null-based member-offset expressions which Clang UBSan
# diagnoses. Keep it unmodified; ASan covers upstream, both sanitizers cover our glue.
for source in deps/rcheevos/src/runtime/*.c deps/rcheevos/src/util/*.c; do
  "${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address -fno-omit-frame-pointer -Ideps/rcheevos/include \
    -c "$source" -o "$task_dir/$(basename "${source%.c}").o"
done
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ideps/rcheevos/include achievements.c tests/achievements_test.c \
  "$task_dir"/*.o -lm -o "$task_dir/test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/test" "$task_dir"
# Miis as players: who earns, the journal's key column, the union, states of other players, the cap
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ideps/rcheevos/include achievements.c tests/achievements_players_test.c \
  "$task_dir"/*.o -lm -o "$task_dir/players_test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/players_test" "$task_dir"
# Official mode with Local players: the Miis besides the Official one earn rc_client's set into the Local journal
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Ideps/rcheevos/include achievements.c tests/achievements_ra_test.c \
  "$task_dir"/*.o -lm -o "$task_dir/ra_test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/ra_test" "$task_dir"
# which account file a game load logs in with, and the names of the RetroAchievements Local journal
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/ach_account_test.c -o "$task_dir/account_test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/account_test"
# the progress pop-up rule (only a new 10 % step, the first reading is the baseline) and the unlock pictures' file names
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined   tests/ach_progress_test.c -o "$task_dir/progress_test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/progress_test"
# badge pictures: the name check and the RGBA8 -> RGB5A3 tiling (Gamecube/ach_badge_conv.h)
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/ach_badge_test.c -o "$task_dir/badge_test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/badge_test"
# the clock: an HTTP Date header and the time zone learnt from it (tests/achievement_clock.c)
"${CC:-cc}" -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/achievement_clock.c tests/achievement_clock_learn_test.c -o "$task_dir/clock_test"
UBSAN_OPTIONS=halt_on_error=1 "$task_dir/clock_test"
