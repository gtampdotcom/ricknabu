/*
 * engine/sounds.c -- NOT a port of xrick/src/sounds.c's own
 * sounds_play() (it dispatches to StartSong(SOUNDBANK, idx), a
 * TI-cartridge-specific player reading soundbank.c's compressed
 * effect/music bank -- no NABU equivalent).
 *
 * SIZE PASS: every sound EFFECT (DIE_SND/JUMP_SND/BULLET_SND/WALK_SND/
 * CRAWL_SND/STICK_SND/BOMBSHHT_SND/EXPLODE_SND/BOX_SND/BONUS_SND/etc, all
 * dropped on request) is gone now -- this file only plays MUSIC (the
 * title theme and each map-intro screen's own theme, engine/scr_imap.c),
 * needed to fund ENT_NBR_ENTDATA/SPRSEQ/MVSTEP's real-size fix (ents.h's
 * own header: mark.ent encodes real entity-type information via its
 * numeric range, so those tables can't be trimmed/rebased the way
 * map_blocks could -- costs real resident bytes with no smaller safe
 * cut). Every sounds_play() call site throughout e_rick.c/e_bomb.c/
 * e_box.c/e_bonus.c/e_sbonus.c/e_them.c is harmless either way: that
 * function is a no-op now, matching the "silent, safe" precedent already
 * established for every previously-dropped effect id. Full effect
 * machinery (walk_afx[]/afx_tick()/play_die_pcm() and its two hard-won
 * hardware-bug fixes) is in git history if effects come back once a
 * future round frees enough room a different way (e.g. real per-map
 * entdata streaming that doesn't break ents.c's type-range dispatch).
 */

#include "config.h"
#include "sounds.h"
#include "NABU-LIB.h"
#include "tiles.h"          /* tiles_banks_shared -- music_vgm[]'s shared
                             * scratch space, see that comment below */
#include "sprites.h"        /* ecm_slot_cache_slots/
                             * sprites_ecmSlotCacheInvalidate() --
                             * sounds_playWaaaaa()'s own borrowed
                             * scratch space when SPRITE_ECM_ENABLED,
                             * see that function's own header. */
#include "sys_nabu_load.h"  /* sys_nabu_loadRes(), RES_xx */

/* SOUND_ENABLED (config.h), on request ("Sound disabled... cache as much
 * as you can to reduce HCCA use during gameplay"): the real
 * implementation below (music player, the shared FX-effect slot, every
 * resident buffer and streamed .DAT) only compiles in when this is 1.
 * When it's 0, every function sounds.h declares still exists -- just as
 * a trivial empty-body stub, at the very end of this file, past the
 * matching #else below -- so every call site throughout e_rick.c/
 * e_bomb.c/e_box.c/e_them.c/main.c/etc keeps compiling completely
 * unchanged, same "harmless either way" contract this file's own header
 * already established for the old ENABLE_SOUND/dropped-effects era.
 * Disabling this frees real resident RAM (jump_data/bullet_data/
 * explode_data/walk_data/stick_data/pad_data, ~464 bytes) AND real code
 * (the whole FX-tick player, music_tick(), every per-effect wrapper) AND
 * removes every network round-trip sound ever made (every streamed
 * effect's own sys_nabu_loadAsset*() call, plus MUSIC1.DAT/INTRMUS1-5.DAT/
 * GAMEOVER.DAT) -- exactly the "less HCCA traffic" this was asked for. */
#if SOUND_ENABLED

/* BUG FIXED, real hardware CRASH: bits 7:6 of AY register 7 aren't sound
 * bits at all -- they're the AY-3-8910's two general-purpose I/O port
 * direction bits, and per eej's own NABU hardware writeup
 * (github.com/eej/Nabu-Deep-Dungeon-Adventure), NABU and MSX require
 * *opposite* settings there (NABU needs 01, MSX needs 10) -- unlike bits
 * 0-5, the ordinary tone/noise mixer bits, which are identical on every
 * AY-3-8910. A real-TI-decoded register-7 write only ever carries mixer
 * bits (the original player's own encoder masks its output to
 * `(x>>2)&0x3C`, always zeroing bits 0/1/6/7) -- so writing one of those
 * bytes to hardware verbatim sets the I/O port bits to 00, neither
 * NABU's required 01 nor MSX's 10. That's what crashed real hardware/
 * Marduk on firing (back when this file still had AY sound effects,
 * since dropped -- see this file's own top header): eej's writeup notes
 * no confirmed damage reports but says this should still be avoided for
 * stability. Fixed by routing every register-7 write in this file
 * through ay_write_mixer() below, which always forces the correct NABU
 * I/O bits into the byte regardless of what the mixer bits say.
 *
 * SIZE PASS: dropped the ay_mixer_shadow byte this used to also
 * maintain -- it existed so multiple concurrent effects could each
 * read-modify-write register 7 without touching each other's bits
 * (real hardware readback wasn't trustworthy enough to use directly).
 * With every effect gone, music is the only remaining register-7 writer
 * left, and it always writes a full replacement value anyway -- nothing
 * left to preserve, so nothing left to shadow. */
#define AY_REG7_NABU_IOBITS 0x40 /* bit6=1, bit7=0 -- NABU's required 01 */

static void ay_write_mixer(U8 val) {
  ayWrite(7, (val & 0x3F) | AY_REG7_NABU_IOBITS);
}

/* Every effect (JUMP_SND/BULLET_SND/WALK_SND/CRAWL_SND/STICK_SND/
 * BOMBSHHT_SND/EXPLODE_SND/BOX_SND/BONUS_SND/SBONUS_SND/SBONUS2_SND/
 * PAD_SND -- walk_afx[]'s own decoded data and afx_tick()'s whole ticked-
 * playback-slot machinery, and sound_afx_tick_all() itself, its main.c
 * call site removed too) dropped on request to fund ENT_NBR_ENTDATA/
 * SPRSEQ/MVSTEP's real-size fix -- see this file's own top header. Every
 * one of their sounds_play() call sites throughout e_rick.c/e_bullet.c/
 * e_bomb.c/e_box.c/e_bonus.c/e_sbonus.c is harmless either way, since
 * sounds_play() (bottom of this file) is a no-op now. Full data/code in
 * git history if effects come back later. */

/* NULL means idle -- same "data pointer doubles as the is-it-playing
 * flag" convention this file's own dropped misc_pos_data used. Declared
 * up here (not next to sounds_playFx()/sounds_fx_tick() further down,
 * where they're actually used day to day) so sounds_reset_all() below
 * can reach them too -- C needs the declaration before any use in the
 * same translation unit. */
static const U8 *fx_data = 0;
static U16 fx_size = 0;
static U16 fx_pos = 0;
static U8 fx_wait = 0;

/* SOUND PRIORITY (SND_PRIORITY[29]/currentPri, ported from xrick/src/
 * sounds.c) DROPPED again (SIZE PASS, on request "you can drop sound
 * priority list, that's not that important" -- closing part of the
 * sound-enable budget gap): frees the 29-byte table plus the nPri/
 * currentPri comparison logic in sounds_playFx()/sounds_playFxStreamed()
 * below. Reverts to this file's own pre-priority behavior -- whichever
 * effect fires most recently always takes the shared slot, no notion of
 * one effect outranking another (so e.g. an enemy wakeup sound CAN cut
 * off an explosion again, the exact thing the priority system was added
 * to prevent -- acceptable per this request). Full table + gating logic
 * in git history/this session's own transcript if it's worth the bytes
 * again later.
 *
 * currentIdx (kept, NOT part of this cut) is a different, unrelated fix:
 * BUG FIXED, reported as "blew up the final missile on MBASE and many
 * explosions happened, there was silence": EXPLODE_SND's own opening
 * tuple (assets/EXPLODE.DAT's first bytes, checked directly) sets
 * both amplitude registers to 0 before any later tuple ever raises them
 * -- priming the tone-period registers silently before the actual "boom"
 * ramps in. A burst of several EXPLODE_SND calls in quick succession
 * (many entities exploding within the same handful of frames, as MBASE's
 * ending does) kept restarting fx_pos back to 0 before playback ever
 * reached past that silent opening -- audible result: nothing, for the
 * whole barrage. Fixed by rejecting a retrigger of the SAME sound id
 * while it's still mid-playback (see sounds_playFx()/
 * sounds_playFxStreamed() below) -- any OTHER sound id still cuts in
 * immediately now that priority is gone, but two overlapping explosions
 * no longer keep restarting each other's silent lead-in; the first one
 * gets to finish, and the next queued explosion starts cleanly once it
 * does. 0xFFFF (never a real SND id; they're all 0-28) means idle. */
static U16 currentIdx = 0xFFFF;

/* Silences every channel and restores the mixer's normal default, AND
 * (BUG FIXED, reported as "the explode sound lags/extends after Rick
 * falls off screen and respawns") stops whichever JUMP_SND/BULLET_SND/
 * EXPLODE_SND effect might still be mid-playback in the shared fx slot
 * (see that section's own header further down) -- clearing the AY
 * registers alone isn't enough on its own: sounds_fx_tick() runs
 * unconditionally every frame regardless of what just happened
 * elsewhere, so leaving fx_data non-NULL here just means the very next
 * frame's own tick writes the effect's next tuple right back over
 * whatever this function just silenced, reviving it a moment later.
 * EXPLODE.DAT (originally the Spectrum version, ~3.8 real seconds long --
 * now a much shorter TI-99 conversion instead, see this file's own
 * JUMP_SND/BULLET_SND/EXPLODE_SND/BONUS_SND header) was the original
 * report's own trigger case: long enough to still be mid-playback when
 * the respawn below reset the submap, audibly bleeding into the new
 * scene without this fix. The underlying race (any effect still mid-
 * playback at a submap reset, not specifically EXPLODE's own duration)
 * is what this function actually guards against, so the fix stays
 * correct regardless of any one effect's length. This was already the one
 * place every submap transition (main.c) forces a known-clean AY state
 * regardless of what music or an effect left it in; main.c's death/
 * respawn handling didn't call this at all before -- see that call
 * site's own comment. */
/* SIZE PASS, on request ("code trims to close the sound-enable budget
 * gap"): this exact "clear the shared fx slot, silence channels 8/9/10"
 * sequence used to be duplicated three times in this file (here, plus
 * sounds_fx_tick()'s own natural-end and out-of-range-register bail
 * points further down) -- SDCC doesn't merge identical blocks across
 * different call sites/branches on its own, so writing it once and
 * calling it from all three real savings, not just style. Split from
 * ay_write_mixer(0x78) below (sounds_reset_all()'s own extra step, not
 * shared by sounds_fx_tick()'s bail points) rather than folding that in
 * too -- keeping this helper's contract identical everywhere it's used. */
static void fx_stop(void) {
  fx_data = 0;
  currentIdx = 0xFFFF;
  ayWrite(8, 0);
  ayWrite(9, 0);
  ayWrite(10, 0);
}

/* ONE-DEEP QUEUE, on request ("test a way to play repeated explosions"):
 * a level-1+ effect (explosions, enemy/trigger sounds) that fx_reject()
 * (below) turns away is remembered here and started as soon as the
 * current effect ends, instead of being lost -- so MBASE's ending plays
 * the bomb's boom and then the barrage (ENT7), and back-to-back explosions
 * play back to back. Newest request wins; level-0 effects are never
 * queued. Cleared by sounds_reset_all() and by a non-priority
 * sounds_pause_fx(). */
static struct {
  U8 kind;                 /* 0 none, 1 resident, 2 streamed */
  U16 idx, bit, offset, res, size;
} fx_pending;

static U8 music_on; /* see sounds_music_playing() */

void sounds_reset_all(void) {
  fx_stop();
  fx_pending.kind = 0;
  music_on = FALSE;
  ay_write_mixer(0x78);
}

/* PRIORITY EFFECTS, on request ("there's a sound that plays when you
 * leave the first submap, sometimes when entering a new submap, can you
 * make that sound have priority"): the speed-bonus pair, SBONUS_SND
 * (start marker crossed) and SBONUS2_SND (stop marker reached in time,
 * bonus awarded) -- both fire right at a submap edge. While either is
 * playing, no other effect may take the shared slot (sounds_playFx()/
 * _Resident()/_Streamed() check this before even loading), and a submap
 * change or scroll (sounds_pause_fx()) only pauses it -- it carries on
 * from where it was afterwards (an earlier rewind-to-start version played
 * it twice, reported).
 *
 * EXPLODE_SND sits one level below them, on request ("explosion sounds
 * should get a higher priority"): it can't be cut off by ordinary effects,
 * and doesn't cut off the bonus sounds. */
static U8 fx_isPriority(U16 idx) {
  return idx == SBONUS_SND || idx == SBONUS2_SND;
}

/* Level of a resident effect -- the enemy sounds (ENT0-8, the only
 * streamed effects) are level 1 too, passed by sounds_playFxStreamed(). */
static U8 fx_level(U16 idx) {
  return fx_isPriority(idx) ? 2 : (idx == EXPLODE_SND) ? 1 : 0;
}

static U8 currentLevel;

/* TRUE if a new effect can't take the slot: the same effect is still
 * playing (restarting it would replay EXPLODE's silent opening forever
 * during a barrage -- currentIdx's own comment), or a higher level is. */
static U8 fx_reject(U16 idx, U8 level) {
  return fx_data && (idx == currentIdx || currentLevel > level);
}

/* For submap changes and scrolls (main.c): same AY silence as
 * sounds_reset_all() -- nothing ticks during the blocking load/scroll, so
 * a live tone would otherwise drone through it -- but a priority effect is
 * kept (paused, not stopped): ticking resumes it from the same spot once
 * the new room is up. Anything else is stopped as before. Deaths, level
 * changes and screen exits still use sounds_reset_all(), which stops
 * everything. */
void sounds_pause_fx(void) {
  if (fx_data && fx_isPriority(currentIdx)) {
    ayWrite(8, 0);
    ayWrite(9, 0);
    ayWrite(10, 0);
  } else {
    fx_stop();
    fx_pending.kind = 0;
  }
  ay_write_mixer(0x78);
}

/*
 * music_vgm: NOT decoded from soundbank.c -- an earlier version played
 * the real TI-99 cartridge's own RICK1_SND title theme that way (same
 * pipeline as jump_afx[] above), but that track uses
 * channel C as a near-constant tone+noise buzz at max volume, which read
 * as harsh static rather than a musical texture no matter how it was
 * balanced (tried masking the noise out entirely, then just capping its
 * volume -- neither sounded right). Replaced with a real hardware rip
 * instead: assets/rick-theme.vgm (VGM format, AY8910/YM2149 register
 * writes captured live, most likely from a ZX Spectrum or MSX version of
 * the game), parsed directly (no CPU emulation needed -- VGM commands
 * are already literal register writes with explicit wait counts, unlike
 * soundbank.c's compressed stream).
 *
 * The raw VGM has 32292 AY register writes over its 67.07s length (a
 * longer, more complete rip than an earlier version of this same file --
 * that one cut off mid-reprise around 24s, with no real fade, and got
 * trimmed short here to hide it; this one has a real fade-out, volumes
 * ramping 5->4->3->2->1->0 from ~58.8s to ~59.6s before a final mixer
 * mute at ~59.76s, so it's encoded in full instead), but the vast
 * majority of those writes are exact no-op re-writes of a value already
 * set -- a common trait of live hardware captures/tracker exports that
 * refresh every register every row regardless of whether it changed.
 * Deduplicating (only keep a write when the value actually differs from
 * the last one written to that register) drops that to 732 real writes,
 * encoded below up through that final mute -- the ~7s of pure trailing
 * silence after it isn't worth encoding.
 *
 * NOT compiled in -- streamed from assets/MUSIC1.DAT into
 * tiles_banks_shared instead (same trick engine/scr_imap.c's
 * map0_title/map0_body use), on request once this file's own 712
 * bytes stopped being worth spending on one fixed song: this scratch
 * space is 8192 bytes, so MUSIC1_SIZE can grow (or a second/third song
 * can be swapped in via sounds_music_load(), see below) without costing
 * this build another byte. Safe to reuse for the *entire* time the title
 * screen's wait loop runs, not just after some other call consumes it
 * first: main.c's screen_titlepage() only ever uses tiles_banks_shared
 * (via tiles_banks_col) for the splash image's color data, and that's
 * already been blitted to VRAM by the time sounds_music_load() runs --
 * see that call site's own comment. loadAssets() (main.c, right after
 * screen_titlepage() returns) is the next thing to touch this memory,
 * loading real tile-bank data -- by then the title screen (and this
 * song) is long done.
 *
 * Same repeating (count, (reg,val)*count, delta) tuple encoding as
 * music_afx[] used to (see git history / prior version), but quantized
 * to this build's own ~66ms game tick directly (GAME_PERIOD, game.h)
 * instead of the 60Hz-native encoding jump_afx[]
 * uses -- so unlike sound_afx_tick_all()'s AFX_FRAMES_PER_TICK catch-up
 * loop, music_tick() below advances exactly one encoded tick per call,
 * one call per screen_titlepage() wait-loop iteration. delta is how many
 * ticks to wait after this event before the next fires; the first
 * tuple's count=0 would encode the track's own lead-in silence, zeroed
 * out on earlier request to start immediately instead. The last tuple's
 * delta loops back to that same (zero) gap once playback wraps -- by
 * then the real fade-out has already muted every channel and the mixer,
 * so the loop restarts from real silence either way; music_tick()'s own
 * hard-mute-on-wrap is just a defensive backstop now, not doing the real
 * work it was for the earlier, abruptly-cut version. Uses channels A/B/C
 * (registers 0-5/8-10) plus the
 * envelope generator (11-13) and mixer (register 7) -- real 3-voice
 * music, not the 2 the soundbank.c version had. Only ever played on the
 * title screen's own wait loop, never alongside DIE_SND/JUMP_SND.
 */
#define music_vgm ((U8 *)&tiles_banks_shared)
#define MUSIC1_SIZE 2108

/* BUG FIXED, real hardware/Marduk CRASH ("PSG reg address > 0x1f when
 * writing"): music_tick()'s wraparound check used to compare music_pos
 * against the MUSIC1_SIZE constant unconditionally -- fine as long as
 * MUSIC1 was the only song that ever existed, but once engine/scr_imap.c
 * started loading MUSIC2.DAT (2018 bytes) through this same function and
 * player, playing it against MUSIC1_SIZE's 2108 let music_pos run 90
 * bytes past MUSIC2's real end before ever wrapping -- reading whatever
 * stale bytes happened to be sitting in tiles_banks_shared past that
 * point as if they were valid (count,(reg,val)*count,delta) data. Fixed
 * by recording *this* load's real size here instead of trusting a
 * constant tied to one specific song. */
static U16 music_size = MUSIC1_SIZE;

/* Loads a song into music_vgm's scratch space -- call once, before
 * sounds_music_start(), while tiles_banks_shared is safe to overwrite
 * (see music_vgm's own header for exactly when that is). Separate
 * from sounds_music_start() so a future second/third song is just
 * another sys_nabu_loadAsset() filename+size away, no new plumbing. */
#if !SOUND_TRIM_MUSIC
void sounds_music_load(U16 res, U16 size) {
  music_size = size;
  sys_nabu_loadRes(res, music_vgm, size);
}
#else
void sounds_music_load(U16 res, U16 size) {
  (void)res; (void)size;
}
#endif

/* BUG FIXED, reported as "there are some repeated notes at the end of
 * the song": assets/rick-theme.vgm's own capture wasn't a clean rip --
 * comparing its last ~3 real seconds against its own intro shows the
 * tail is a partial, cut-off repeat of the opening passage (the capture
 * simply ran a little past the tune's real end rather than stopping on
 * a clean boundary). Re-encoded above with the cutoff moved from the
 * file's full 24.19s down to 20.99s, right before that repeat starts --
 * 118 bytes smaller too. Looping now restarts from a clean ending
 * instead of mid-reprise. */

static U16 music_pos = 0;
static U8 music_wait = 0;
static U16 music_acc = 0; /* fractional steps, 1/256ths -- sounds_music_tick() */
static U8 music_looped;   /* see sounds_music_looped() */

/* music_step() advances the music by exactly one encoded tick -- see
 * music_vgm[]'s own header for the encoding and why this doesn't need a
 * multi-frame catch-up loop the way ticked sound effects used to: it's
 * already quantized to this build's own game tick. Register 7 writes go
 * through ay_write_mixer(). SIZE PASS: this used to be a separate
 * static music_tick() with sounds_music_tick() below as a one-line
 * wrapper calling it -- merged, since nothing else ever called
 * music_tick() directly. */

/* SIZE PASS, same reasoning as fx_stop() above: this bare
 * "ayWrite(8,0); ayWrite(9,0); ayWrite(10,0);" trio was duplicated three
 * times inside sounds_music_tick() below (both out-of-range-register bail
 * points, plus the wraparound-mute case) -- one shared helper instead. */
#if !SOUND_TRIM_MUSIC
/* The music's own last-written channel volumes (registers 8-10), so a
 * screen change can silence it during a blocking load and put the same
 * chord back afterwards -- sounds_music_hold()/_resume() below. */
static U8 music_vol[3];

static void music_mute(void) {
  ayWrite(8, 0);
  ayWrite(9, 0);
  ayWrite(10, 0);
  music_vol[0] = music_vol[1] = music_vol[2] = 0;
}

/* HOLD/RESUME, on request ("the music stutters when the screens change"):
 * a screen change streams its graphics (up to 12KB for the title picture)
 * with the music not ticking, and the AY kept droning whatever chord was
 * sounding, then the music jumped back in. Ticking the music DURING a
 * read isn't safe (rn_FileRead() polls the HCCA through the AY's I/O port
 * and relies on the AY register latch staying put; a tick would also take
 * longer than the gap between incoming bytes). Instead: silence the
 * channels for the load, then restore the same volumes and carry on from
 * the same spot -- a short clean pause instead of a stutter. */
void sounds_music_hold(void) {
  ayWrite(8, 0);
  ayWrite(9, 0);
  ayWrite(10, 0);
}

void sounds_music_resume(void) {
  ayWrite(8, music_vol[0]);
  ayWrite(9, music_vol[1]);
  ayWrite(10, music_vol[2]);
}

static void music_step(void) {
  U8 count, i, reg, val, delta;

  if (music_wait > 0) {
    music_wait--;
    return;
  }

  /* BUG FIXED, reported as "PSG reg address > 0x1f" -- confirmed to
   * happen re-entering this player mid-gameplay (screen_introMap() on
   * ordinary level advance, screen_levelSelect() via the ESC key), never
   * on the original boot->title->select path. Root cause not yet
   * isolated (file sizes checked correct, not F18A-specific -- confirmed
   * on stock MAME too), but wherever it comes from, `reg` ending up
   * outside the AY's own real 0-13 register range means music_pos has
   * desynced from a true tuple boundary and everything read from here on
   * is not real (reg,val) data any more, just whatever bytes happen to
   * follow. Bail out to a clean, silent, known-good state the instant
   * that's detected instead of handing hardware a bogus register/address
   * -- same "guarantee silence at the seam" fix this function's own
   * wraparound-mute code below already uses for a different case,
   * applied earlier, before a bad write can reach the chip at all. This
   * doesn't explain the desync, only stops it from crashing real
   * hardware/Marduk/MAME while the real cause is still being tracked
   * down. */
  if (music_pos >= music_size) {
    music_pos = 0;
  }

  count = music_vgm[music_pos++];
  for (i = 0; i < count; i++) {
    if ((U16)(music_pos + 1) >= music_size) {
      music_pos = 0;
      music_mute();
      return;
    }
    reg = music_vgm[music_pos++];
    val = music_vgm[music_pos++];
    if (reg > 13) {
      music_pos = 0;
      music_mute();
      return;
    }
    if (reg == 7) {
      ay_write_mixer(val);
    } else {
      ayWrite(reg, val);
      if (reg >= 8 && reg <= 10) {
        music_vol[reg - 8] = val; /* for sounds_music_resume() */
      }
    }
  }
  delta = music_vgm[music_pos++];
  music_wait = delta;

  if (music_pos >= music_size) { /* was sizeof(music_vgm) when this was a
                                   * real compiled-in array, then the
                                   * MUSIC1_SIZE constant once it moved to
                                   * scratch space -- see music_size's own
                                   * BUG FIXED comment for why a fixed
                                   * constant broke once a second song
                                   * (MUSIC2) needed this same player. */
    music_pos = 0;
    music_looped = TRUE;
    /* BUG FIXED, reported as "a beep at the end": trimming music_vgm[]
     * to a clean-sounding cut is finicky by ear -- the true fix isn't
     * finding a perfectly silent instant in the data (there may not be
     * one), it's guaranteeing silence at the seam regardless of what was
     * still sounding the instant the trimmed data ran out. Hard-mute all
     * three channels the moment playback wraps, before the loop's own
     * leading silence (its own count=0 entry) even begins -- whatever
     * note was cut off mid-sustain stops cleanly instead of ringing on,
     * clicking, or jumping straight to a different pitch. */
    music_mute();
  }
}

/* Called once per vblank by every music screen: advances the music by
 * `rate`/256 encoded ticks (fractions carry over), so each screen's tempo
 * is one constant (sounds.h's MUSIC_RATE_xx) instead of being stuck to
 * whole-vblank multiples. */
void sounds_music_tick(U16 rate) {
  music_acc += rate;
  while (music_acc >= 256) {
    music_acc -= 256;
    music_step();
  }
}

void sounds_music_start(void) {
  music_pos = 0;
  music_wait = 0;
  music_acc = 0;
  music_looped = FALSE;
  music_vol[0] = music_vol[1] = music_vol[2] = 0;
  music_on = TRUE;
}

U8 sounds_music_looped(void) {
  U8 r = music_looped;
  music_looped = FALSE;
  return r;
}

/* TRUE between sounds_music_start() and the next sounds_reset_all()/
 * sounds_music_stop() -- i.e. the music buffer really holds a song. Lets
 * engine/scr_hof.c's stream_to_vram() keep the music going during loads
 * only when there is music to keep going. */
U8 sounds_music_playing(void) {
  return music_on;
}

/* SIZE PASS: identical body to sounds_reset_all() above (silence all
 * three channels, restore the mixer default) -- was its own separate
 * copy back when effects existed and needed their own reset semantics;
 * with those gone, sharing one implementation is free. Call once, right
 * after the title screen's wait loop ends. */
void sounds_music_stop(void) {
  sounds_reset_all();
}
#else
void sounds_music_tick(U16 rate) {
  (void)rate;
}

void sounds_music_start(void) {
}

void sounds_music_stop(void) {
}

void sounds_music_hold(void) {
}

void sounds_music_resume(void) {
}

U8 sounds_music_playing(void) {
  return 0;
}

U8 sounds_music_looped(void) {
  static U16 calls;
  if (++calls < MUSIC_LOOP_FALLBACK_CALLS) {
    return FALSE;
  }
  calls = 0;
  return TRUE;
}
#endif

/* play_die_pcm() (DIE_SND -- the bit-banged AY-amplitude PCM playback,
 * two hard-won hardware-bug fixes and all) dropped along with every
 * other effect -- see this file's own top header. sounds_play() itself
 * is gone too now (SIZE PASS): config.h's ENABLE_SOUND being undefined
 * means every real call site is preprocessed out, so this had zero
 * callers left anywhere in the build -- same "no live callers, drop it"
 * reasoning sound_afx_tick_all() got above. Full code (and
 * DEATH_PCM_SIZE/assets/DIESND.PCM) in git history if DIE_SND (or
 * sounds_play() generally) comes back later. */

#define WAAAAA_PCM_SIZE 8159

/* BORROWED SCRATCH SPACE, on request ("would it be possible for the PCM
 * cache to share the same location as ECM sprites? I don't mind if the
 * ECM sprites have to refresh after the PCM is played, it doesn't
 * happen often"): when SPRITE_ECM_ENABLED, sounds_playWaaaaa() below
 * streams+plays straight out of engine/sprites.c's own
 * ecm_slot_cache_slots (engine/include/sprites.h) instead of
 * tiles_banks_shared -- same "one resident buffer, two mutually-
 * exclusive-in-time users" pattern tiles_banks_shared_u itself already
 * uses (tiles.h), just a second instance of it, and it means
 * tiles_banks_shared_u's own rawWatermark member (tiles.h) no longer
 * has to force that union's size up to WAAAAA_PCM_SIZE on that build --
 * the tile-bank members can be however small they really are. REVERTED
 * to a conditional (#if SPRITE_ECM_ENABLED / #else tiles_banks_shared),
 * on the later "split the ECM plane cache back out from page0" change
 * (engine/include/sprites.h's own SPRITE_PLANES_SIZE comment): the
 * unconditional graphics_cache_slots this used to borrow only existed
 * because that cache covered the stock page0 fallback too and so had
 * to exist on every build; ecm_slot_cache_slots is ECM-only again, so a
 * build with no ECM at all has nothing here to borrow, same as before
 * that merge. Safe on the same terms as the original reuse: PCM
 * playback is a rare, deliberate, CPU-blocking one-off, never
 * concurrent with a sprite actually being painted; sounds_playWaaaaa()
 * invalidates the whole ECM slot cache right after playback
 * (sprites_ecmSlotCacheInvalidate()) so nothing downstream ever reads
 * stale/overwritten plane data, at the cost of every cached ECM slot
 * being a fresh cache miss again just once after each play -- accepted
 * as fine since this "doesn't happen often". Compile-time guard below
 * catches ECM_SLOT_CACHE_SLOTS ever being tuned down far enough that
 * WAAAAA no longer fits. */
#if SPRITE_ECM_ENABLED && !SOUND_TRIM_PCM /* nothing borrows the cache when PCM is trimmed */
#if WAAAAA_PCM_SIZE > ECM_SLOT_CACHE_TOTAL_BYTES
#error "WAAAAA_PCM_SIZE no longer fits ecm_slot_cache_slots -- either raise ECM_SLOT_CACHE_SLOTS (engine/include/sprites.h) or fall back to tiles_banks_shared here."
#endif
#endif

/* SIZE PASS, on request ("reduce waaaaa"), then REVERTED, on request
 * ("accept it and drop chunking"): tried loading WAAAAA_PCM_SIZE (8159)
 * in two smaller pieces instead of one shot, to let tiles_banks_shared's
 * own declared size shrink below WAAAAA's need once engine/tiles.c's
 * gameplay tile bank went sparse (see tiles.h's own header) -- WAAAAA
 * was quietly the real thing keeping this shared buffer large, not the
 * tile data. Split into two fetch+play passes the same way engine/
 * sprites.c streams one sprite frame at a time.
 *
 * BUG FOUND, extensively bisected (test_pcm/main.c's own header has the
 * full test matrix -- 15 variants tried): the SECOND half's own
 * playback came out corrupted every time a real HCCA fetch sat between
 * the two playback passes, regardless of: settle delay before the
 * second fetch (~225us and ~50ms both tried), settle delay after it
 * (same two durations), the fetch mechanism itself (offset-based,
 * separate-file, and handle-based rn_fileOpen()/rn_fileHandleReadSeq()
 * all tried), or re-asserting ay_write_mixer(0x79) fresh before the
 * second pass. None of it made any difference -- whatever the real
 * hardware/toolchain interaction is, it wasn't timing, wasn't the fetch
 * API, and wasn't mixer/port-direction state, at least not in any way a
 * settle delay or explicit re-assert could paper over. Splitting into
 * TWO fetches with NO playback between them (i.e. fetch, fetch, THEN
 * play once continuously) does work correctly -- but that needs the
 * full WAAAAA_PCM_SIZE resident at once anyway, so it doesn't actually
 * solve the original problem this chunking was trying to solve.
 *
 * REVERTED to one fetch + one continuous playback pass (below) -- the
 * version that predates this whole WAAAAA-shrinking effort, proven
 * correct by the same bisection (test_pcm's own '1'). At the time this
 * needed WAAAAA_PCM_SIZE contiguous bytes of tiles_banks_shared again;
 * SUPERSEDED, on request ("would it be possible for the PCM cache to
 * share the same location as ECM sprites?") -- it streams into engine/
 * sprites.c's own ecm_slot_cache_slots now instead when SPRITE_ECM_
 * ENABLED, and tiles_banks_shared_u's own rawWatermark member (tiles.h)
 * that used to force its size up to fit this is gone on THAT build
 * config -- still present (and still needed) on a build with no ECM at
 * all, where this function still falls back to tiles_banks_shared, same
 * as before.
 * SOUND_TRIM_PCM (config.h) is the escape hatch for builds that can't
 * afford this -- off by default would mean WAAAAA doesn't play; the
 * default build.bat setting reflects whatever the current byte-budget
 * situation allows, see that file's own comment block. */

/*
 * Plays assets/WAAAAA.PCM, streamed into engine/sprites.c's own
 * ecm_slot_cache_slots (when SPRITE_ECM_ENABLED) or tiles_banks_shared
 * (otherwise) from the NABU network adapter (same sys_nabu_loadAsset()
 * mechanism music_vgm[] above uses) -- safe borrowed scratch space
 * either way, on the same "never both live at once" terms as
 * music_vgm[]'s own tiles_banks_shared reuse (this function's own later
 * header, below, has the full account).
 *
 * Bit-bangs channel A's 4-bit
 * amplitude register with each sample byte's top nibble (`srl a` x4),
 * paced by a fixed delay loop tuned for ~11kHz -- the AY-3-8910 has no
 * real DAC. ay_write_mixer(0x79) disables channel A's tone (bit0) and
 * noise (bit3) first -- both already-proven-necessary hardware fixes from
 * the original: nibble extraction must be the TOP nibble (not fujinet-
 * battleship's own low-nibble sequence, which only worked there because
 * its assets were pre-quantized into the low nibble already), and channel
 * A's tone/noise generators must be disabled before playback or they
 * amplitude-modulate the "DAC" signal instead of passing it through
 * cleanly. Hardcoded 0x79 instead of this file's old `ay_mixer_shadow |
 * 0x09` -- that shadow byte is gone now (this file's own top header:
 * nothing left to preserve once every ticked AY effect was dropped), and
 * the only other register-7 writers left (sounds_reset_all()/music, both
 * gameplay-adjacent not gameplay-concurrent) always leave it at the same
 * known 0x78 default this cheat key would see anyway.
 */
#if !SOUND_TRIM_PCM
void sounds_playWaaaaa(void) {
  /* On request: silence everything first -- any effect still playing (or
   * queued) is stopped, so nothing drones on channels B/C during the load
   * or plays over the sample. */
  sounds_reset_all();

#if SPRITE_ECM_ENABLED
  sys_nabu_loadRes(RES_WAAAAA, (U8 *)ecm_slot_cache_slots, WAAAAA_PCM_SIZE);
#else
  sys_nabu_loadRes(RES_WAAAAA, (U8 *)&tiles_banks_shared, WAAAAA_PCM_SIZE);
#endif

  ay_write_mixer(0x79);

  __asm
#if SPRITE_ECM_ENABLED
    ld hl, #_ecm_slot_cache_slots
#else
    ld hl, #_tiles_banks_shared
#endif
    ld de, #WAAAAA_PCM_SIZE
_pcm_play:
    ld a, (hl)
    inc hl

    srl a           ; sample shifted right 4 -- top nibble, see the
    srl a           ; header comment above
    srl a
    srl a

    ld c, a
    ld a, #8        ; AY register 8 = channel A amplitude
    out (#0x41), a  ; AY register-select/latch port
    ld a, c
    out (#0x40), a  ; AY data port

    ld b, #19       ; fixed delay, tuned for ~11kHz playback
_pcm_wait:
    djnz _pcm_wait

    dec de
    ld a, d
    or e
    jr nz, _pcm_play
  __endasm;

  ayWrite(8, 0); /* silence channel A -- don't leave the last sample's
                  * volume level stuck on after playback ends. */

#if SPRITE_ECM_ENABLED
  sprites_ecmSlotCacheInvalidate(); /* see this function's own header --
                                      * ecm_slot_cache_slots' real
                                      * contents just got overwritten
                                      * with PCM samples above. */
#endif
}
#else
void sounds_playWaaaaa(void) {
}
#endif

/* TEMPORARY, OFF BY DEFAULT (config.h's own SOUND_TRIM_FX header) -- on
 * request ("add one for skipping sound effects"), same "split music and
 * pcm streaming defines" spirit as SOUND_TRIM_MUSIC/SOUND_TRIM_PCM
 * above: gates every gameplay sound EFFECT (JUMP/BULLET/EXPLODE/WALK/
 * CRAWL/STICK/BOX/SBONUS/SBONUS2/GAMEOVER/PAD/BONUS/DIE/ENT0-8) from
 * sounds_playFx() through sounds_fx_tick() at the bottom of this section
 * -- one flag, not fourteen, since every one of these functions and
 * their two shared helpers (sounds_playFx()/sounds_playFxStreamed())
 * either drop out together (nothing left to call the helpers with) or
 * not at all. ALL of them are streamed now (this section's own later
 * header -- SIZE PASS, on request "find enough ram... even if they have
 * to be streamed"), so this flag no longer frees any resident data --
 * just the code itself (the two shared helpers plus fourteen thin
 * wrappers). Music (title theme, map intro themes) and the WAAAAA
 * cheat-key PCM effect above are unaffected -- see their own independent
 * flags. Set via build.bat's -DSOUND_TRIM_FX=1; flip back to 0 (or
 * delete that -D) once real trims close the gap for good -- not meant
 * to stay permanently. */
#if !SOUND_TRIM_FX

/* JUMP_SND/BULLET_SND/EXPLODE_SND, back on request -- real chiptune
 * effects, not a PCM clip: assets/JUMP.DAT/BULLET.DAT/EXPLODE.DAT,
 * same (count,(reg,val)*count,delta) tuple format as music_vgm[] above.
 * BONUS_SND/DIE_SND (TREASURE.DAT/DIE.DAT) used to be resident right here
 * too -- moved down to the STREAMED effects section further below (SIZE
 * PASS, on request "find more ram without removing any features"): both
 * are one-shot moments (a treasure pickup, an enemy kill/Rick's own
 * death), not an every-frame-possible one like the three still resident
 * here, so they fit the exact "fine to stream" category this file's own
 * sounds_playFxStreamed() header already describes -- freed 690 bytes of
 * BSS (244+446) for zero feature loss. See sounds_playBonus()/
 * sounds_playDie() further down, next to sounds_playBombshht()/etc.
 *
 * ALL FOUR (now three resident, two streamed) SWAPPED to rickti-main's
 * own TI-99 .psg sources, on request ("use jump.psg from the TI-99
 * version" for JUMP, then "swap them all for consistency" for the
 * remaining three) -- originally all four were extracted from a ZX
 * Spectrum demo-reel capture rick-sound-effects.vgm, already
 * native AY8910 register writes, no chip conversion needed) via
 * tools/vgm_to_dat.py directly; now all four go through the same
 * psg2vgm -sn + tools/sn_to_ay.py pipeline as WALK_SND/CRAWL_SND/
 * STICK_SND/PAD_SND/BOMBSHHT_SND/etc (see WALK_DATA_SIZE's own comment).
 * Old Spectrum versions backed up as assets/JUMP-spectrum.DAT.bak/
 * BULLET-spectrum.DAT.bak/EXPLODE-spectrum.DAT.bak/TREASURE-spectrum.DAT.bak
 * if any of these ever need reverting. Smaller across the board -- JUMP
 * 144->76, BULLET 150->82, TREASURE 312->244, and EXPLODE most
 * dramatically 632->94 (the TI-99 explosion is a genuinely different,
 * much shorter ~0.57s sound, not just a smaller encoding of the same
 * ~3.8s one -- listened and compared before swapping, on request).
 *
 * JUMP_SND/BULLET_SND/EXPLODE_SND USED TO stay loaded resident at boot
 * (main.c's loadAssets()) rather than streamed on demand like
 * tiles_banks_shared's own occupants -- unlike WAAAAA.PCM above (a
 * deliberate one-off cheat keypress that can afford a network round-trip
 * first), these sound on every jump/shot/explosion during real gameplay,
 * e_rick.c's/e_bullet.c's/e_bomb.c's own call sites all mid-physics-
 * update, not idle moments to hide a fetch behind -- that guarantee was
 * the whole reason they were the last ones still resident. ALL THREE
 * (plus WALK_SND/CRAWL_SND/STICK_SND, this section's own later header)
 * moved to streamed on request ("find enough ram... even if they have to
 * be streamed") once every other option was exhausted: freed the last
 * 194 bytes of BSS this section had left (76+82+36), enough real margin
 * to restore music + the WAAAAA cheat effect at the same time. REAL
 * TRADEOFF, accepted on request: every jump/shot/step now pays a network
 * round-trip before the sound starts, the exact class of latency this
 * section used to specifically avoid for these three -- see
 * sounds_playFxStreamed()'s own header for the general "brief,
 * likely-barely-noticeable pause" expectation, genuinely untested here
 * at this trigger frequency though (nothing else streamed is anywhere
 * near this frequent). BONUS_SND (treasure pickup, e_bonus.c) and
 * DIE_SND (enemy kill/death, e_them.c/e_rick.c) never needed the
 * zero-latency guarantee -- see their own streamed wrappers further down
 * for why.
 *
 * ONE SHARED playback slot for every effect in this file, not one each --
 * same reasoning as every other effect in this section (this file's own
 * header). The old Spectrum-rip versions of BULLET/EXPLODE/TREASURE were
 * channel-C-only (JUMP's own comment already noted this for the old JUMP
 * too); every one of the new TI-99 versions (checked directly against
 * their own extracted tuple data) uses all three tone channels plus noise
 * instead, same "fights whatever else is in the shared slot" tradeoff
 * already accepted for DIE_SND/PAD_SND -- this swap changes that overlap
 * behavior across the board, not just which recordings play.
 *
 * NON-blocking, ticked once per game frame (sounds_fx_tick(), call site:
 * main.c's main loop) -- unlike WAAAAA.PCM's hard busy-wait, these have
 * to play WHILE Rick keeps falling/moving/firing; freezing gameplay for
 * either would feel far worse than for a deliberate cheat-toggle
 * keypress.
 *
 * Ticks per call (was the fixed FX_FRAMES_PER_TICK = 2, now the caller's
 * `passes` argument): gameplay passes one tick per vblank actually
 * elapsed (main.c's main loop) -- the real encoded rate, independent of
 * gameplay speed. FIXED, reported as "much slower than the TI99 version":
 * it was one tick per 2 vblanks, i.e. half speed. History of the old
 * value, for reference: the data is encoded at 16ms/tick (tools/
 * vgm_to_dat.py's own default, matching the AY chip's native ~60Hz
 * capture rate -- see that tool's header on why a short effect needs the
 * native tick, not GAME_PERIOD's coarser one), but sounds_fx_tick() below
 * is only ever called once per real ~66ms game frame -- advancing
 * exactly one encoded tick per call would stretch the effect out to
 * roughly 4x its real length, the exact "sounds nothing like the
 * original" bug the old afx_tick() already hit and fixed the same way
 * (catch up by more than one encoded tick per real frame). 2 is that same
 * function's own previously-tuned-by-ear value for this identical
 * real-frame-vs-encoded-tick mismatch (GAME_PERIOD's real duration is a
 * busy-wait approximation, not confirmed -- see game_period's own
 * comment) -- a starting point, not a measurement; retune by ear the same
 * way if either effect sounds off. */
#define JUMP_DATA_SIZE 76
#define BULLET_DATA_SIZE 82
#define TREASURE_DATA_SIZE 244
#define DIE_DATA_SIZE 446
/* JUMP_DATA_SIZE/BULLET_DATA_SIZE/EXPLODE_DATA_SIZE/TREASURE_DATA_SIZE/
 * DIE_DATA_SIZE stay defined here
 * (sounds_playExplode()/sounds_playBonus()/sounds_playDie() further down
 * need the byte count to stream), but their old resident explode_data[]/
 * treasure_data[]/die_data[] BSS buffers are gone -- see this section's
 * own header (SIZE PASS: moved to streamed, 690 bytes back for TREASURE/
 * DIE, another 94 for EXPLODE on request "find enough ram... even if
 * they have to be streamed"). BONUS_SND is still the TI-99 bonus.psg
 * conversion (see this section's own header on the Spectrum->TI-99 swap)
 * and DIE_SND/EXPLODE_SND are still the same real chiptune data as
 * always -- only the resident-vs-streamed delivery changed, not the
 * actual sound data. EXPLODE_SND fires on bomb/missile detonation and
 * box breakage (e_bomb.c/e_box.c) -- rare, discrete events, not the
 * every-frame-possible category WALK/CRAWL are still resident for
 * (below), so the same brief-pause tradeoff this file's own STREAMED
 * section header already accepts for BOX_SND/GAMEOVER_SND/etc applies
 * here too. */
#define EXPLODE_DATA_SIZE 94
/* WALK_SND/CRAWL_SND/STICK_SND/PAD_SND -- ALL streamed now (SIZE PASS,
 * on request "find enough ram... even if they have to be streamed"):
 * WALK_SND fired every single footstep (e_rick.c), the most every-frame-
 * possible trigger in this whole file, so it was the very last one moved
 * off resident delivery, once JUMP/BULLET/EXPLODE's own move still
 * wasn't enough margin on its own. CRAWL_SND is WALK.DAT's own bytes
 * again (BUG FIXED, on request "we need to find savings": CRAWL.DAT and
 * WALK.DAT came out byte-for-byte identical, unsurprising since
 * crawl.psg/walk.psg's own source-tool difference is a noise-period
 * offset too small to survive quantization at this project's own
 * 16ms/tick) -- streamed from the same WALK.DAT file under its own
 * CRAWL_SND id, same "no redundant second copy" saving this had as a
 * resident buffer, now for the network fetch instead. STICK_SND/PAD_SND
 * were already streamed before this round (edge-triggered/rare events,
 * see git history for their own original moves). Converted from
 * rickti-main's own walk.psg/crawl.psg (github.com/tursilion/vgmcomp2's
 * psg2vgm -sn, then tools/sn_to_ay.py -- see that tool's own header). */
#define WALK_DATA_SIZE 36
#define STICK_DATA_SIZE 44

/* Starts `data` (a scratch buffer -- every effect is streamed now, see
 * this section's own header) from the top, taking over the one shared
 * slot regardless of what was playing before -- see this section's own
 * header for why sharing one slot instead of giving each effect its own
 * is correct here, not just cheaper. Actual playback happens in
 * sounds_fx_tick() below, not here -- this only resets the read
 * position, same "start()/tick() are separate calls" split as
 * sounds_music_start()/sounds_music_tick() above. */
/* `idx` is one of sounds.h's SND constants -- only checked against
 * currentIdx now (SND_PRIORITY[] dropped, see this section's own header),
 * so the only thing that can reject a new effect is that exact same
 * effect already being mid-playback (the MBASE-explosion-barrage fix,
 * currentIdx's own comment) -- anything else always takes the slot. */
/* Callers (_Streamed/_Resident) have already checked fx_reject(). */
static void sounds_playFx(U16 idx, const U8 *data, U16 size, U8 level) {
  currentIdx = idx;
  currentLevel = level;
  fx_data = data;
  fx_size = size;
  fx_pos = 0;
  fx_wait = 0;
}

/* STREAMED effects -- BOX_SND/SBONUS_SND/SBONUS2_SND, ENT0_SND..ENT8_SND
 * (this section's own header just below), and now BONUS_SND/DIE_SND too
 * (moved down from the resident section above on a later request -- see
 * that section's own header), on request ("convert all the sounds that
 * haven't been converted yet, would the ones that can't fit be possible
 * to stream"). BOMBSHHT_SND (fuse-ticking) used to be here too --
 * DROPPED entirely, not just un-streamed, on a later request (SIZE PASS,
 * closing part of the sound-enable budget gap): sounds_playBombshht()
 * doesn't exist any more, see e_bomb.c's own header for its one call
 * site. Same rickti-main .psg -> psg2vgm -sn -> tools/sn_to_ay.py ->
 * tools/vgm_to_dat.py pipeline as every resident effect above, but NOT
 * kept resident: these effects together would cost multiple KB of BSS if
 * made resident, nowhere close to affordable (this build's own real
 * ceiling has under 300 bytes of headroom at a time -- see rick.map's
 * own __BSS_END_tail vs TAR__register_sp, and this project's own memory
 * notes on what happens past it). Streamed instead,
 * the same sys_nabu_loadAsset()-into-scratch-space trick
 * sounds_playWaaaaa() above and sounds_music_load() (both this file) and
 * engine/scr_imap.c's own map0_title/map0_body already use --
 * tiles_banks_shared is genuinely idle for the ENTIRE span between one
 * map_loadMap()/tiles_setBank() call and the next (nothing else reads or
 * writes it while real gameplay is running -- verified directly: every
 * caller of tiles_setBank(), the only function that ever reads FROM this
 * memory, is a level-start/screen-transition call site, never a per-frame
 * gameplay one), so it's free real estate for exactly this.
 *
 * REAL TRADEOFF, not free: sys_nabu_loadAsset() is a synchronous network
 * round-trip -- unlike every resident effect above, which starts on the
 * very next sounds_fx_tick() call with zero delay, a streamed effect
 * blocks gameplay for however long that read takes before playback (via
 * the same non-blocking sounds_fx_tick() player as everything else) even
 * starts. Each of these 13 files is small (34-1564 bytes, nowhere near
 * WAAAAA.PCM's 8159-byte load, which is itself only tolerated because
 * it's a deliberate cheat-key press, not a routine gameplay event) --
 * expected to be a brief, likely barely-noticeable pause rather than a
 * real freeze, but genuinely untested on real hardware/network timing.
 * Fine for a one-shot enemy-kill/pickup/reveal moment; would NOT be fine
 * for something as frequent as JUMP_SND/WALK_SND, which is exactly why
 * those stayed resident instead. */
/* Same-effect gated the same way as sounds_playFx() above (SND_PRIORITY[]
 * dropped, see this section's own header), but checked BEFORE the network
 * load rather than after -- if a rejected streamed effect's load ran
 * anyway, it would have overwritten tiles_banks_shared out from under
 * whatever effect is still mid-playback from that same scratch buffer
 * right now. Checking first avoids that corruption as well as the wasted
 * round-trip. */
static void sounds_playFxStreamed(U16 idx, U16 res, U16 size) {
  if (fx_reject(idx, 1)) {
    /* checked before the load: no wasted network read. Enemy sounds are
     * level 1 -- queued to play next (fx_pending's own comment). */
    fx_pending.kind = 2;
    fx_pending.idx = idx;
    fx_pending.res = res;
    fx_pending.size = size;
    return;
  }

  sys_nabu_loadRes(res, (U8 *)&tiles_banks_shared, size);
  sounds_playFx(idx, (const U8 *)&tiles_banks_shared, size, 1);
}

/* RESIDENT FX CACHE, on request ("is there enough ram to store all the
 * sfx at least the most common ones") -- ONE small permanent buffer
 * (FX_RESIDENT_TOTAL_SIZE, below) holding every one of this section's
 * own 13 streamed effects at once, except ENT0_SND..ENT8_SND (see
 * sounds_playEnt()'s own header just below this block): those 9 sum to
 * 3836 bytes on their own, ENT7 alone 1564 of it -- this project's own
 * prior analysis already ruled out making that whole set resident
 * ("would badly blow this build's real memory ceiling"), and nothing
 * about the graphics-cache merge changes that math, so they're left
 * streamed exactly as before. The OTHER 13 (12 distinct files -- CRAWL
 * reuses WALK's own bytes, this file's own sounds_playCrawl() comment)
 * sum to a genuinely small 1744 bytes total -- worth comparing against
 * ECM_SLOT_CACHE_SLOTS' own ~387 bytes/slot or PAGE0_CACHE_SLOTS' own
 * ~131 bytes/slot (engine/include/sprites.h) to see how cheap this
 * really is. Small and FIXED enough (unlike either of those, whose own
 * working set is much larger and much more varied) that no eviction
 * policy is needed at all: each effect gets its own permanent,
 * fixed-offset slot in fx_resident_data[], loaded ONCE on first play and
 * never evicted, so JUMP_SND/WALK_SND/CRAWL_SND -- the genuinely
 * every-frame-possible ones this section's own older header still
 * (STALE, left as found) describes as "the last ones still resident"
 * before an earlier byte-budget crisis streamed them -- go back to
 * paying that HCCA round-trip only ONCE per effect for the whole game,
 * not on every single jump/footstep.
 *
 * fx_resident_loaded's own bits track which slots have been streamed in
 * yet (0 = not yet, stream on this call; 1 = already resident, play
 * directly) -- a U16 bitmask is enough for 13 flags. CRAWL_SND doesn't
 * get its own bit: it shares WALK_SND's exact offset+bit, since loading
 * either one satisfies both (same underlying bytes, this file's own
 * sounds_playCrawl() comment). */
#define FX_RESIDENT_TOTAL_SIZE 1744
static U8 fx_resident_data[FX_RESIDENT_TOTAL_SIZE];
static U16 fx_resident_loaded;

#define FX_OFF_BOMBSHHT 0
#define FX_BIT_BOMBSHHT 0x0001
#define FX_OFF_WALK     34   /* CRAWL_SND shares this offset+bit */
#define FX_BIT_WALK     0x0002
#define FX_OFF_JUMP     70
#define FX_BIT_JUMP     0x0004
#define FX_OFF_BULLET   146
#define FX_BIT_BULLET   0x0008
#define FX_OFF_EXPLODE  228
#define FX_BIT_EXPLODE  0x0010
#define FX_OFF_SBONUS   322
#define FX_BIT_SBONUS   0x0020
#define FX_OFF_STICK    416
#define FX_BIT_STICK    0x0040
#define FX_OFF_PAD      460
#define FX_BIT_PAD      0x0080
#define FX_OFF_SBONUS2  592
#define FX_BIT_SBONUS2  0x0100
#define FX_OFF_BOX      726
#define FX_BIT_BOX      0x0200
#define FX_OFF_GAMEOVER 876
#define FX_BIT_GAMEOVER 0x0400
#define FX_OFF_TREASURE 1054 /* BONUS_SND */
#define FX_BIT_TREASURE 0x0800
#define FX_OFF_DIE      1298
#define FX_BIT_DIE      0x1000
/* 1298 + DIE_DATA_SIZE(446) = 1744 = FX_RESIDENT_TOTAL_SIZE -- every
 * byte accounted for, no padding. */

/* Same same-idx-mid-playback reject as sounds_playFxStreamed() (the
 * MBASE-explosion-barrage fix, currentIdx's own comment) -- still needed
 * for BEHAVIOR (don't restart an already-playing effect from position 0),
 * even though the memory-corruption half of that function's own header
 * (a rejected load overwriting a shared scratch buffer mid-playback)
 * flat out can't happen here any more: every resident effect has its own
 * permanent slot, never shared with another effect's still-playing data.
 * `offset`/`bit` are one of this section's own FX_OFF_xxx and
 * FX_BIT_xxx constant pairs. */
static void sounds_playFxResident(U16 idx, U16 bit, U16 offset, U16 res, U16 size) {
  U8 level = fx_level(idx);

  if (fx_reject(idx, level)) {
    if (level) { /* explosion/bonus: play next instead of losing it */
      fx_pending.kind = 1;
      fx_pending.idx = idx;
      fx_pending.bit = bit;
      fx_pending.offset = offset;
      fx_pending.res = res;
      fx_pending.size = size;
    }
    return;
  }
  if (!(fx_resident_loaded & bit)) {
    sys_nabu_loadRes(res, &fx_resident_data[offset], size);
    fx_resident_loaded |= bit;
  }
  sounds_playFx(idx, &fx_resident_data[offset], size, level);
}

/* Starts the queued effect, if any -- called when the current one ends. */
static void fx_playPending(void) {
  U8 kind = fx_pending.kind;

  fx_pending.kind = 0;
  if (kind == 1) {
    sounds_playFxResident(fx_pending.idx, fx_pending.bit, fx_pending.offset,
                          fx_pending.res, fx_pending.size);
  } else if (kind == 2) {
    sounds_playFxStreamed(fx_pending.idx, fx_pending.res, fx_pending.size);
  }
}

#define BOX_DATA_SIZE 150
#define SBONUS_DATA_SIZE 94
#define SBONUS2_DATA_SIZE 134
#define GAMEOVER_DATA_SIZE 178
#define PAD_DATA_SIZE 132
#define BOMBSHHT_DATA_SIZE 34

void sounds_playJump(void) {
  sounds_playFxResident(JUMP_SND, FX_BIT_JUMP, FX_OFF_JUMP, RES_JUMP, JUMP_DATA_SIZE);
}

void sounds_playBullet(void) {
  sounds_playFxResident(BULLET_SND, FX_BIT_BULLET, FX_OFF_BULLET, RES_BULLET, BULLET_DATA_SIZE);
}

void sounds_playExplode(void) {
  sounds_playFxResident(EXPLODE_SND, FX_BIT_EXPLODE, FX_OFF_EXPLODE, RES_EXPLODE, EXPLODE_DATA_SIZE);
}

/* Footsteps never interrupt, on request ("make it so it doesn't cut off
 * any other playing sounds"): every effect shares one slot and a new one
 * normally takes it over, so a footstep used to cut off whatever jump,
 * shot or explosion was still playing. WALK/CRAWL now only start when the
 * slot is free or already holding a footstep; otherwise that step is
 * silent. (Louder, same request: assets/WALK.DAT's channel-C volume write
 * raised from 3 to 10 out of 15.) */
static U8 fx_footstepBlocked(void) {
  return fx_data && currentIdx != WALK_SND && currentIdx != CRAWL_SND;
}

void sounds_playWalk(void) {
  if (fx_footstepBlocked()) {
    return;
  }
  sounds_playFxResident(WALK_SND, FX_BIT_WALK, FX_OFF_WALK, RES_WALK, WALK_DATA_SIZE);
}

void sounds_playCrawl(void) {
  if (fx_footstepBlocked()) {
    return;
  }
  sounds_playFxResident(CRAWL_SND, FX_BIT_WALK, FX_OFF_WALK, RES_WALK, WALK_DATA_SIZE); /*
    CRAWL.DAT and WALK.DAT are byte-identical, see this file's own header --
    reuses the same asset AND the same resident slot/bit under CRAWL_SND's own id */
}

void sounds_playStick(void) {
  sounds_playFxResident(STICK_SND, FX_BIT_STICK, FX_OFF_STICK, RES_STICK, STICK_DATA_SIZE);
}

void sounds_playBox(void) {
  sounds_playFxResident(BOX_SND, FX_BIT_BOX, FX_OFF_BOX, RES_BOX, BOX_DATA_SIZE);
}

void sounds_playSbonus(void) {
  sounds_playFxResident(SBONUS_SND, FX_BIT_SBONUS, FX_OFF_SBONUS, RES_SBONUS, SBONUS_DATA_SIZE);
}

void sounds_playPad(void) {
  sounds_playFxResident(PAD_SND, FX_BIT_PAD, FX_OFF_PAD, RES_PAD, PAD_DATA_SIZE);
}

/* RESTORED, on request ("bring back the fuse sound"): fuse ticking, called
 * from e_bomb.c's ticking branch. 34-byte file assets/BOMBSHHT.DAT. */
void sounds_playBombshht(void) {
  sounds_playFxResident(BOMBSHHT_SND, FX_BIT_BOMBSHHT, FX_OFF_BOMBSHHT, RES_BOMBSHHT, BOMBSHHT_DATA_SIZE);
}

/* BONUS_SND (treasure pickup, e_bonus.c) and DIE_SND (enemy kill/Rick's
 * own death, e_them.c/e_rick.c) -- moved here from the resident section
 * above (SIZE PASS, freed 690 bytes of BSS with no feature loss, back
 * when these were pure streamed -- now resident again via the small
 * shared FX cache above, not the old per-effect dedicated buffers that
 * SIZE PASS removed). DIE_SND in particular could in principle retrigger
 * in a fast burst (several enemies killed in quick succession) the same
 * way EXPLODE_SND's own MBASE-finale bug did -- but that class of bug is
 * already closed off here: sounds_playFxResident()'s own same-idx reject
 * (see that function's own comment) means a second DIE_SND while one is
 * already playing is dropped instead of restarting, so a kill streak
 * can't stack up redundant restarts or re-trigger the silent-lead-in bug
 * on itself. */
void sounds_playBonus(void) {
  sounds_playFxResident(BONUS_SND, FX_BIT_TREASURE, FX_OFF_TREASURE, RES_TREASURE, TREASURE_DATA_SIZE);
}

void sounds_playDie(void) {
  sounds_playFxResident(DIE_SND, FX_BIT_DIE, FX_OFF_DIE, RES_DIE, DIE_DATA_SIZE);
}

void sounds_playSbonus2(void) {
  sounds_playFxResident(SBONUS2_SND, FX_BIT_SBONUS2, FX_OFF_SBONUS2, RES_SBONUS2, SBONUS2_DATA_SIZE);
}

void sounds_playGameover(void) {
  sounds_playFxResident(GAMEOVER_SND, FX_BIT_GAMEOVER, FX_OFF_GAMEOVER, RES_GAMEOVER, GAMEOVER_DATA_SIZE);
}

/* ENT0_SND..ENT8_SND -- the reference's real per-enemy-type kill sounds
 * (xrick/src/sounds.c's own WAV_ENTITY[9] table, xrick/src/e_them.c:783
 * `sounds_play(WAV_ENTITY[(ent_ents[e].trigsnd & 0x1F) - 0x14])`) --
 * ent_ents[].trigsnd is already a ported field (engine/include/ents.h),
 * just never read anywhere in this build until now. e_them_gozombie()
 * (engine/e_them.c) used to play the same generic DIE_SND for every
 * single enemy kill (the novawaa.vgm capture, back on request earlier
 * this session) -- that stays Rick's own death sound; enemy kills now
 * play their real per-type sound instead via sounds_playEnt() below,
 * same trigsnd-indexed lookup as the reference. All nine stream (this
 * section's own header) -- ENT7 alone is 1564 bytes, and the full set
 * together would badly blow this build's real memory ceiling resident.
 *
 * SIZE PASS: filename built from one mutable template (poking the digit
 * into place) instead of 9 separate string literals, same trick engine/
 * scr_imap.c's titleFile/bodyFile and engine/maps.c's map_loadMap() own
 * templates use -- one 9-byte template plus a 9-entry U16 size table
 * beats 9 full 9-byte literals plus a 9-pointer lookup array; this build
 * was already checked against the real memory ceiling and came in 70
 * bytes over before this change. */
static const U16 ent_snd_sizes[9] = {
  74, 294, 572, 338, 238, 670, 34, 1564, 52,
};

/* Maps the same 0-8 idx to its real sounds.h SND constant -- STILL
 * NEEDED even with SND_PRIORITY[] dropped (this section's own header):
 * sounds_playFxStreamed()'s currentIdx same-sound-reject check needs each
 * enemy type's own real, globally-unique SND id, not the raw 0-8 loop
 * idx -- several OTHER real SND constants already sit in that same 0-8
 * range (WALK_SND=1, ENT6_SND=2, CRAWL_SND=3, ...), so comparing raw idx
 * against currentIdx directly would misidentify, say, an ENT1 kill as
 * "the same sound" as WALK_SND and silently drop it. NOT sequential
 * (ENT0_SND=9, ENT1_SND=20, ENT6_SND=2, ...), so idx can't be used to
 * index into sounds.h's own constants directly the way it indexes
 * ent_snd_sizes[]/the filename digit above -- this table does that
 * remapping. */
static const U16 ent_snd_ids[9] = {
  ENT0_SND, ENT1_SND, ENT2_SND, ENT3_SND, ENT4_SND,
  ENT5_SND, ENT6_SND, ENT7_SND, ENT8_SND,
};

/* `idx` is 0-8 (already resolved from trigsnd the same way the
 * reference's WAV_ENTITY[] lookup is) -- see e_them.c's own call site. */
void sounds_playEnt(U16 idx) {
  /* ENT0.DAT..ENT8.DAT, packed into RICK.DAT one fixed stride apart
   * (engine/include/res.h). */
  sounds_playFxStreamed(ent_snd_ids[idx], RES_ENT_BASE + idx * RES_ENT_STRIDE, ent_snd_sizes[idx]);
}

/* Advances whichever effect is in the shared slot by up to
 * `passes` encoded ticks -- see this section's own header for
 * why more than one. Cheap no-op whenever nothing is playing. Register 7
 * writes go through ay_write_mixer(), same NABU-vs-MSX I/O-bit reason as
 * music_tick(). */
void sounds_fx_tick(U8 passes) {
  U8 pass, count, i, reg, val;

  if (!fx_data) {
    return;
  }

  for (pass = 0; pass < passes; pass++) {
    if (fx_wait > 0) {
      fx_wait--;
      continue;
    }
    if (fx_pos >= fx_size) {
      /* BUG FIXED, reported as "the jump sound leaves a tone going after
       * it ends": this used to only silence channel A (ayWrite(8,0)) --
       * a leftover copy from WAAAAA.PCM's own cleanup above, which really
       * does use channel A. JUMP_SND/BULLET_SND both only ever sound on
       * channel C (registers 6/10, this section's own header), and
       * BULLET.DAT's own last tuple happens to already write reg10=0
       * itself (masking this), but JUMP.DAT's last tuple doesn't -- its
       * real capture just fades to a quiet-but-nonzero amplitude (1) and
       * stops issuing updates, so channel C kept ringing at that level
       * forever once fx_data was cleared and nothing ever wrote reg10
       * again. fx_stop() (SIZE PASS'd up near sounds_reset_all()) silences
       * all three amplitude registers here regardless of which channel a
       * given effect actually used -- correct for whichever one is
       * playing, and safe for one that's already 0; it also clears
       * currentIdx/fx_data, freeing the slot for whatever fires next. */
      fx_stop();
      fx_playPending(); /* a queued explosion/enemy sound, if any */
      return;
    }
    count = fx_data[fx_pos++];
    for (i = 0; i < count; i++) {
      /* BUG FIXED, defensive: same out-of-range-register guard as
       * sounds_music_tick() just got, for the same reason -- see that
       * function's own comment. Not confirmed reachable here (this
       * player's own state -- fx_data/fx_pos/fx_size -- isn't shared
       * with the music player's tiles_banks_shared reuse the way that
       * bug involves), but cheap enough to add for the same class of
       * failure regardless. */
      if ((U16)(fx_pos + 1) >= fx_size) {
        fx_stop();
        return;
      }
      reg = fx_data[fx_pos++];
      val = fx_data[fx_pos++];
      if (reg > 13) {
        fx_stop();
        return;
      }
      if (reg == 7) {
        ay_write_mixer(val);
      } else {
        ayWrite(reg, val);
      }
    }
    fx_wait = fx_data[fx_pos++];
  }
}

#else /* !SOUND_TRIM_FX */

void sounds_playJump(void) {
}

void sounds_playBullet(void) {
}

void sounds_playExplode(void) {
}

void sounds_playWalk(void) {
}

void sounds_playCrawl(void) {
}

void sounds_playStick(void) {
}

void sounds_playBox(void) {
}

void sounds_playSbonus(void) {
}

void sounds_playPad(void) {
}

void sounds_playBombshht(void) {
}

void sounds_playBonus(void) {
}

void sounds_playDie(void) {
}

void sounds_playSbonus2(void) {
}

void sounds_playGameover(void) {
}

void sounds_playEnt(U16 idx) {
  (void)idx;
}

void sounds_fx_tick(U8 passes) {
  (void)passes;
}

#endif /* SOUND_TRIM_FX */

#else /* !SOUND_ENABLED */

/* Trivial stubs, one per sounds.h declaration -- see this file's own
 * SOUND_ENABLED comment (top of file) for why these exist instead of
 * removing the declarations/call sites. Every call site elsewhere in
 * this project passes real arguments (filenames, sizes, SND indices);
 * these just ignore them and return, matching the exact same "no-op,
 * safe either way" contract this project already used for the old
 * dropped-effects era. */
void sounds_reset_all(void) {
}

void sounds_pause_fx(void) {
}

void sounds_music_load(U16 res, U16 size) {
}

void sounds_music_start(void) {
}

void sounds_music_tick(U16 rate) {
  (void)rate;
}

void sounds_music_stop(void) {
}

void sounds_music_hold(void) {
}

void sounds_music_resume(void) {
}

U8 sounds_music_playing(void) {
  return 0;
}

U8 sounds_music_looped(void) {
  static U16 calls;
  if (++calls < MUSIC_LOOP_FALLBACK_CALLS) {
    return FALSE;
  }
  calls = 0;
  return TRUE;
}

void sounds_playWaaaaa(void) {
}

void sounds_playJump(void) {
}

void sounds_playBullet(void) {
}

void sounds_playExplode(void) {
}

void sounds_playBonus(void) {
}

void sounds_playDie(void) {
}

void sounds_playWalk(void) {
}

void sounds_playCrawl(void) {
}

void sounds_playStick(void) {
}

void sounds_playPad(void) {
}

void sounds_playBombshht(void) {
}

void sounds_playBox(void) {
}

void sounds_playSbonus(void) {
}

void sounds_playSbonus2(void) {
}

void sounds_playEnt(U16 idx) {
}

void sounds_playGameover(void) {
}

void sounds_fx_tick(U8 passes) {
  (void)passes;
}

#endif /* SOUND_ENABLED */

/* eof */
