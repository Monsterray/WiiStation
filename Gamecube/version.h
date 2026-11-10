/* WiiStation's version, Major.Minor.Patch.
 *   Major: a change that old settings files, save states or memory cards do not survive.
 *   Minor: new features; files from the previous minor version still work.
 *   Patch: fixes only.
 * Gamecube/release/apps/WiiStation/meta.xml (<version>) carries the same number; change both
 * (scripts/build.sh warns when they differ). 4.0.0 = the "Beta 4.0" of 2024-04-18.
 * 6.0.0 is a major release for the new menu and achievements, not a format break: settings,
 * save states and memory cards from 5.x still work (a 5.19.0 state loads, 2026-11-10). */
#ifndef WIISTATION_VERSION_H
#define WIISTATION_VERSION_H
#define WIISTATION_VERSION "6.0.0"
#endif
