/*
 * hal/sysevt_nabu.c -- matches xrick/include/sysevt.h's contract.
 *
 * Real implementation: reads NABU-LIB's joystick 0 status and translates
 * it into control.h's CONTROL_* bits in control_status, which
 * engine/e_rick.c reads every frame.
 *
 * NABU-LIB's joystick API isn't in this repo to check directly (same
 * caveat as hal/RetroNET-FileStore.h before it was uploaded -- see that
 * file's own header), so this is written against DJ Sures' own published
 * joystick-test example (forums.nabu.ca, "Polling the Joystick statuses
 * crash. (fixed)"), which polls exactly this way:
 *
 *   uint8_t status = getJoyStatus(0);
 *   if (status & Joy_Left) ...
 *   else if (status & Joy_Right) ...
 *   else if (status & Joy_Up) ...
 *   else if (status & Joy_Down) ...
 *   else if (status & Joy_Button) ...
 *
 * That example only checks one direction at a time (else-if chain); this
 * reads all five bits independently instead, since Rick Dangerous needs
 * diagonals (e.g. UP+LEFT while climbing) and FIRE at the same time as a
 * direction (e.g. FIRE+UP to shoot) -- e_rick.c's control_status checks
 * assume bits can combine. If NABU-LIB's real Joy_* bit values turn out to
 * be mutually-exclusive positions rather than independent bits (i.e. a
 * single "which of these 9 positions" enum, not a bitmask), this needs
 * revisiting -- the forum example's own else-if chain would be consistent
 * with either interpretation, so it doesn't settle the question on its
 * own. Worth confirming once this actually runs against real NABU-LIB.h.
 *
 * NOT implemented:
 *   - Keyboard input -- config.h's ENABLE_KEYBOARD is deliberately left
 *     undefined (see that file's header), and CONTROL_PAUSE/CONTROL_END/
 *     CONTROL_EXIT have no input source at all yet -- NABU's standard
 *     joystick is a 4-direction-plus-1-button controller with nothing to
 *     map those three to.
 *   - Joystick 1 (2-player) -- only getJoyStatus(0) is read.
 *   - Any debouncing/edge-detection -- control_status is a live level
 *     snapshot each poll, matching what e_rick.c's control_status checks
 *     already expect (they test "is this held", not "was this just
 *     pressed").
 *
 * sysevt_wait() is a plain call-through to sysevt_poll() -- there's no
 * blocking/event-queue primitive to wait on yet (see hal/sys_nabu.c's own
 * timing caveat), so callers that want pacing between polls handle that
 * themselves (main.c's game loop does, via sys_nabu_tick()).
 */

#include "sysevt_nabu.h"
#include "NABU-LIB.h"
#include "control.h"

void sysevt_poll(void) {
  U8 joy = getJoyStatus(0);
  U16 status = 0;

  if (joy & Joy_Up)     status |= CONTROL_UP;
  if (joy & Joy_Down)   status |= CONTROL_DOWN;
  if (joy & Joy_Left)   status |= CONTROL_LEFT;
  if (joy & Joy_Right)  status |= CONTROL_RIGHT;
  if (joy & Joy_Button) status |= CONTROL_FIRE;

  control_status = status;
}

/* SIZE PASS: wrapped out -- zero callers project-wide (main.c's game loop
 * calls sysevt_poll() directly). */
#if 0
void sysevt_wait(void) {
  sysevt_poll();
}
#endif
