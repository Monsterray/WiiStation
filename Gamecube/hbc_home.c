/* hbc_home.c -- the Homebrew Channel's in-app agent (deps/hbc_agent, hbc-reborn's sdk).
 *
 * While WiiStation runs, `tools/hbc.py` on the PC (in hbc-reborn) still reaches the Wii on
 * TCP 4299: status, the SD card's files, `exit`, `run new.dol`, and after a crash `crash`
 * (the exception, registers and a backtrace, recorded in MEM2 before the crash screen,
 * which then returns to HBC by itself). In WiiStation's menu, HOME on a Wii Remote opens
 * HBC's HOME overlay over the menu: Exit (to HBC, the System Menu, restart, power off),
 * a screenshot, DEV and WiiMote. In a game, HOME is still WiiStation's own menu button.
 *
 * Exits go WiiStation's own way: a game is stopped (psxcounters.c sees stop), comes back
 * to the menu, which saves its memory cards as after any game, closes it, and leaves
 * through the same fade as Exit to Loader (Gui.cpp, shutdown = 2). */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include "../deps/hbc_agent/sdk/hbc_agent.h"
#include "hbc_home.h"
#include "lab_net.h"
#include "ws_crash.h"

extern char shutdown;             /* GamecubeMain.cpp: 1 power off, 2 back to the loader */
extern int stop;                  /* GamecubeMain.cpp: leave the CPU loop */
extern GXRModeObj *vmode;

static int agent_up;

void hbc_home_start(void)
{
	static hbc_agent_config cfg;

	cfg.name = "WiiStation";
	cfg.version = __DATE__ " " __TIME__;
	cfg.app_polls_exit = true;               /* hbc_home_retrace / hbc_home_menu_frame */
	cfg.crash_reload_s = lab_active() ? 10 : 0;   /* 0: the agent's 3 s */
	cfg.gc_pads = true;                       /* the menu called PAD_Init() */
	/* Lab mode: no listener. With one, WiiStation answers TCP 4299 as HBC's menu does, so
	 * another workstation's bench queue took the Wii for idle and its `hbc.py run` made the
	 * agent exit a chain in the middle (2026-09-30, twice, no results). The crash handler
	 * stays; HBC shows its report after the reload. */
	cfg.no_network = lab_active();
	/* HBC 1.10's safety tools, but not the buttons: WiiStation reads Reset itself (the
	 * retrace callback: Reset = back to the menu) and sets its own Power callback, so a
	 * Reset the agent took went to HBC instead. Its frame pacing wraps WiiStation's
	 * retrace callback: GamecubeMain.cpp starts the agent after setting it. */
	cfg.no_safety = HBC_AGENT_NO_BUTTONS;
	agent_up = hbc_agent_init(&cfg) == 0;
}

/* The retrace callback (every frame, a game running or not): an exit asked for from the
 * PC stops the game, so it comes back to the menu the usual way. */
void hbc_home_retrace(void)
{
	if (agent_up && hbc_agent_exit_requested())
		stop = 1;
}

/* Each menu frame. Returns 1 when the menu should close the loaded game and leave. */
int hbc_home_menu_frame(void)
{
	int chan;

	if (!agent_up)
		return 0;
	if (hbc_agent_exit_requested())
		return !shutdown;
	for (chan = 0; chan < WPAD_MAX_WIIMOTES; chan++)
		if (WPAD_ButtonsDown(chan) & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME))
			break;
	if (chan < WPAD_MAX_WIIMOTES || hbc_agent_home_pending())
		hbc_agent_home(vmode);   /* the agent pauses its hang watchdog while this is open */
	return 0;
}

/* Before WiiStation starts the network itself (the SMB thread): the agent's start-up must
 * be over first, or net_init() can hang (hbc_agent.h). */
void hbc_home_net_wait(void)
{
	if (agent_up)
		hbc_agent_net_wait(20000);
}
