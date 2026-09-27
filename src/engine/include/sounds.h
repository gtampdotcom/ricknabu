/*
 * engine/include/sounds.h -- NOT a port of xrick/include/sounds.h's real
 * body. The SND index constants below are verbatim (harmless, pure
 * data), but the real header's declarations (sounds_play() backed by
 * TISNPlay.h's StartSong()/soundbank.c, a TI-cartridge-specific
 * compressed music-and-effects player) have no NABU equivalent -- see
 * engine/sounds.c's own header for why this port's sounds_play() is a
 * new, minimal implementation instead of a port.
 */

#ifndef _SOUNDS_H
#define _SOUNDS_H

#include "ricksystem.h"

#define RICK1_SND 28
#define SAMERICA_SND 27
#define EGYPT_SND 0
#define SCHWARZ_SND 19
#define MBASE_SND 25
#define GAMEOVER_SND 16
#define RICK1VICTORY_SND 22

/* sound effects */

#define WALK_SND 1
#define CRAWL_SND 3
#define JUMP_SND 13
#define STICK_SND 5
#define BULLET_SND 7
#define BOMBSHHT_SND 4
#define EXPLODE_SND 8
#define DIE_SND 23

#define PAD_SND 15
#define BOX_SND 10
#define BONUS_SND 12
#define SBONUS_SND 11
#define SBONUS2_SND 14

#define ENT0_SND 9
#define ENT1_SND 20
#define ENT2_SND 24
#define ENT3_SND 17
#define ENT4_SND 18
#define ENT5_SND 21
#define ENT6_SND 2
#define ENT7_SND 26
#define ENT8_SND 6

/* sounds_play() itself is gone now (SIZE PASS, engine/sounds.c's own
 * header): every real call site throughout e_rick.c/e_bullet.c/e_bomb.c/
 * e_box.c/e_bonus.c/e_sbonus.c is wrapped in `#ifdef ENABLE_SOUND`
 * (config.h, now undefined), so none of them compile in -- nothing left
 * to declare here. Restore both (and re-port real effect data/code from
 * git history) if effects come back later. */

/* Silences all three AY channels outright and restores the mixer's
 * normal default -- call on every submap transition (main.c's own
 * map_chain() handling) so music/a future effect can never carry a stuck
 * tone across a room change. See engine/sounds.c's own comment. */
extern void sounds_reset_all(void);

/* Like sounds_reset_all(), for submap changes and scrolls: silences the
 * AY, but a priority effect (the speed-bonus sounds) is rewound to replay
 * afterwards instead of being stopped. See sounds.c. */
extern void sounds_pause_fx(void);

/* Title-screen music -- separate from the sounds_play()/
 * sound_afx_tick_all() effect pair above since it only ever plays on the
 * title screen's own wait loop, never during gameplay, and needs an
 * explicit load/start/stop rather than an SND index. See engine/
 * sounds.c's music_vgm/music_tick() headers.
 *
 * sounds_music_load() streams a song into music_vgm's scratch space --
 * call it once, before sounds_music_start(), while that memory is safe
 * to overwrite (see music_vgm's own header for exactly when). `size`
 * must match the .DAT file's real byte length exactly (sys_nabu_
 * loadAsset() panics otherwise) -- see MUSIC1_SIZE in sounds.c for the
 * one song this build currently ships. */
extern void sounds_music_load(U16 res, U16 size); /* res: a RES_xx (res.h) */
extern void sounds_music_start(void);
extern void sounds_music_stop(void);
/* Silence / restore the music around a blocking screen-change load --
 * see sounds.c. */
extern void sounds_music_hold(void);
extern void sounds_music_resume(void);
/* TRUE while a song is loaded and started (not after a reset/stop). */
extern U8 sounds_music_playing(void);
/* TRUE (once) if the song has reached its end and wrapped back to the start
 * since the last call or sounds_music_start() -- the title/hall-of-fame
 * attract cycle switches screens there, while the music is silent anyway.
 * Builds without music count calls instead (one per vblank):
 * MUSIC_LOOP_FALLBACK_CALLS stands in for one play-through. */
extern U8 sounds_music_looped(void);
#define MUSIC_LOOP_FALLBACK_CALLS 1200 /* 20 seconds */

/* Call once per vblank: advances the music by `rate`/256 encoded ticks.
 * Tempo per screen -- raise to speed a tune up, lower to slow it down:
 *   MUSIC_RATE_TITLE: title + level select. 102/256 = 1 tick per ~2.5
 *     vblanks (~42ms), 20% faster than the old 50ms-per-tick busy-wait.
 *   MUSIC_RATE_INTRO: map intro screens. The data is encoded at one tick
 *     per vblank (256); 307 = 1.2 per vblank, 20% faster, near the pace
 *     the old busy-wait loop actually ran it at. */
#define MUSIC_RATE_TITLE 102
#define MUSIC_RATE_INTRO 307
extern void sounds_music_tick(U16 rate);

/* Bit-banged AY-amplitude PCM playback of assets/WAAAAA.PCM (~0.7s,
 * CPU-blocking) wired to main.c's in-game '1' cheat-toggle key 
 * See engine/sounds.c's own header. */
extern void sounds_playWaaaaa(void);

/* JUMP_SND/BULLET_SND/EXPLODE_SND/BONUS_SND/DIE_SND -- real chiptune
 * effects (assets/JUMP.DAT, BULLET.DAT, EXPLODE.DAT, TREASURE.DAT,
 * DIE.DAT), resident at boot, non-blocking, sharing ONE playback slot
 * between all five (see engine/sounds.c's own header for why: each only
 * ever sounds on one AY channel, so there is no second/third channel to
 * give truly independent effects -- whichever plays more recently just
 * takes over). sounds_playJump()/sounds_playBullet()/sounds_playExplode()/
 * sounds_playBonus()/sounds_playDie() each start their own effect in that
 * shared slot (e_rick.c's own two JUMP_SND call sites and its own DIE_SND
 * call site, e_bullet.c's own BULLET_SND call site, e_bomb.c's/e_box.c's
 * own EXPLODE_SND call sites, e_bonus.c's own BONUS_SND call site,
 * e_them.c's own DIE_SND call site on every enemy kill); sounds_fx_tick()
 * must be called once per real game frame (main.c's main loop) to
 * actually advance whichever one is currently playing -- see engine/
 * sounds.c's own header for the whole mechanism and why it needs ticking
 * unlike sounds_playWaaaaa() above. */
extern void sounds_playJump(void);
extern void sounds_playBullet(void);
extern void sounds_playExplode(void);
extern void sounds_playBonus(void);
extern void sounds_playDie(void);
extern void sounds_playWalk(void);
extern void sounds_playCrawl(void);
extern void sounds_playStick(void);
extern void sounds_playPad(void);
extern void sounds_playBombshht(void); /* fuse ticking, e_bomb.c */
extern void sounds_playBox(void);
extern void sounds_playSbonus(void);
extern void sounds_playSbonus2(void);
extern void sounds_playEnt(U16 idx);
extern void sounds_playGameover(void);
/* Advances the playing effect by `passes` of its 16.7ms encoded ticks. */
extern void sounds_fx_tick(U8 passes);

#endif /* _SOUNDS_H */

/* eof */
