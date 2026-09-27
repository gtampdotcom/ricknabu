/*
 * engine/scr_hof.c -- hall of fame, brought back on request ("is there
 * room to bring back the hall of fame?" -> screen + name entry, banner
 * streamed from RICK.DAT).
 *
 *   - screen_hallOfFame(): the "hall of fame" banner (the TI art,
 *     assets/HAFPAT.DAT/HAFCOL.DAT) and the top-8 table. Shown in the title screen's
 *     attract cycle (main.c's screen_titlepage(): title -> hall of fame ->
 *     title ...) and after a new high score is entered.
 *   - screen_enterName(): after game over, if the score makes the top 8
 *     (hof_qualifies()), asks for a name and inserts it (hof_insert()).
 *     Scores live in RAM only -- they reset when the NABU restarts.
 *
 * VRAM/RAM: the title music occupies tiles_banks_shared (the usual font
 * buffer) while the attract cycle runs, and at first boot the font hasn't
 * even been loaded yet -- so these screens stream the font and banner
 * straight into VRAM through an idle sprite-cache buffer (scratch_begin(),
 * the same trick the title picture uses), no RAM of their own. The banner
 * goes into tile slots 128-223 (rows 0-2), which the font's ASCII text
 * never uses -- same layout as xrick's draw_hof_title().
 *
 * Replaces the earlier scr_imain.c/game.c versions (since deleted, which
 * ran on the old software clock and resident TI picture data);
 * hof_paintScoreLine() and game_hscores[] were carried over from them.
 */

/* ---------------------------------------------------------------------
 * Scratch buffer: a sprite cache that is idle outside gameplay -- the ECM
 * slot cache, or on a 9918A-only build its full-size page-0 cache (both at
 * least 6144 bytes). scratch_end() invalidates whatever sprites it held so
 * they simply re-stream when gameplay next needs them. Shared with main.c's
 * title picture.
 * --------------------------------------------------------------------- */
#if SPRITE_ECM_ENABLED
#define SCRATCH_BUF (&ecm_slot_cache_slots[0][0])
#else
#define SCRATCH_BUF (&page0_cache_slots[0][0])
#endif

static void scratch_end(void) {
#if SPRITE_ECM_ENABLED
  sprites_ecmSlotCacheInvalidate(); /* on stock also rebuilds the stock caches */
#else
  sprites_page0CacheInit();
#endif
}

/* ---------------------------------------------------------------------
 * stream_to_vram(): resource `res` (`size` bytes) from RICK.DAT into VRAM
 * at `vram`, through SCRATCH_BUF -- copied with bitmapcharcopy() (font:
 * replicated into every screen third) if `replicate`, else vdpmemcpy2().
 *
 * MUSIC_SMOOTH_LOADS (build.ps1), on request ("try it, just make sure you
 * can revert"): 1 = read in STREAM_CHUNK pieces and keep the title music
 * going between them (sounds_music_playing() -- never at first boot or
 * after game over, when the music buffer holds other data), since the
 * music can't be ticked DURING a read (sounds.c's sounds_music_hold()
 * comment). A read can't count vblanks either, so each chunk counts as
 * STREAM_VBLANKS_PER_CHUNK -- raise it if the music drags during screen
 * changes, lower it if it rushes. 0 (the default) = one read per resource
 * with the music silenced and resumed around the whole load
 * (sounds_music_hold()/_resume(), the callers' job) -- fewer reads, faster
 * screens. The attract cycle switches screens when the music reaches its
 * end (sounds_music_looped()), so that pause falls between play-throughs.
 * --------------------------------------------------------------------- */
#ifndef MUSIC_SMOOTH_LOADS
#define MUSIC_SMOOTH_LOADS 0
#endif
#define STREAM_CHUNK 512
#define STREAM_VBLANKS_PER_CHUNK 3

static void stream_to_vram(U16 res, U16 vram, U16 size, U8 replicate) {
#if MUSIC_SMOOTH_LOADS
  U16 off, n;
  U8 k;

  for (off = 0; off < size; off += n) {
    n = size - off;
    if (n > STREAM_CHUNK) {
      n = STREAM_CHUNK;
    }
    sys_nabu_loadRes(res + (off >> 2), SCRATCH_BUF, n);
    if (replicate) {
      bitmapcharcopy(vram + off, SCRATCH_BUF, n);
    } else {
      vdpmemcpy2(vram + off, SCRATCH_BUF, n);
    }
    if (sounds_music_playing()) {
      for (k = 0; k < STREAM_VBLANKS_PER_CHUNK; k++) {
        sounds_music_tick(MUSIC_RATE_TITLE);
      }
    }
  }
#else
  sys_nabu_loadRes(res, SCRATCH_BUF, size);
  if (replicate) {
    bitmapcharcopy(vram, SCRATCH_BUF, size);
  } else {
    vdpmemcpy2(vram, SCRATCH_BUF, size);
  }
#endif
}

/* ---------------------------------------------------------------------
 * Table
 * --------------------------------------------------------------------- */

/* Default content, verbatim from xrick/src/game.c. '@' is this font's
 * blank glyph (' ' is not blank in bank 0 -- see fb.c's header). */
hscore_t game_hscores[8] = {
  { 0, 8000, "TURSILION@" },
  { 0, 7000, "GTAMP@@@@@" },
  { 0, 6000, "DANGERSTU@" },
  { 0, 5000, "VISREALM@@" },
  { 0, 4000, "LEO@AND@DJ" },
  { 0, 3000, "MARK@@@@@@" },
  { 0, 2000, "DAVE@@@@@@" },
  { 0, 1000, "TI99IUC@@@" }
};

/* TRUE if the current score (env_score_hi/lo) beats entry e. */
static U8 hof_beats(const hscore_t *e) {
  return env_score_hi > e->score_hi ||
         (env_score_hi == e->score_hi && env_score_lo > e->score_lo);
}

/* TRUE if the current score makes the top 8. */
U8 hof_qualifies(void) {
  return hof_beats(&game_hscores[7]);
}

/* Inserts the current score with `name` (10 chars) below any equal score. */
static void hof_insert(const U8 *name) {
  hscore_t *e = &game_hscores[7];
  U8 k;

  while (e != game_hscores && hof_beats(e - 1)) {
    *e = *(e - 1);
    e--;
  }
  e->score_hi = env_score_hi;
  e->score_lo = env_score_lo;
  for (k = 0; k < 10; k++) {
    e->name[k] = name[k];
  }
  e->name[10] = 0;

  /* entered once: don't let another route to the end-of-game screens
   * (e.g. level select's ENDING preview) enter the same score again */
  env_score_hi = 0;
  env_score_lo = 0;
}

/* ---------------------------------------------------------------------
 * Drawing
 * --------------------------------------------------------------------- */

/* Two decimal digits of v (0-99) into p; returns the end. */
static U8 *hof_2digits(U8 *p, U8 v) {
  *p++ = (U8)('0' + v / 10);
  *p++ = (U8)('0' + v % 10);
  return p;
}

/* Formats hi (2 digits, 0-99) + lo (4 digits, 0-9999) into p. */
static U8 *hof_digits(U8 *p, U16 hi, U16 lo) {
  p = hof_2digits(p, (U8)hi);
  p = hof_2digits(p, (U8)(lo / 100));
  return hof_2digits(p, (U8)(lo % 100));
}

/* One "NNXXXX@@@....@@@NAME" row at tile column 4 -- xrick's
 * gotoxy(4, 5+i*2) layout. */
static void hof_paintScoreLine(const hscore_t *entry, U16 row) {
  static const char sep[] = "@@@....@@@";
  U8 line[6 + 10 + 10 + 1];
  U8 *p = hof_digits(line, entry->score_hi, entry->score_lo);
  U8 k;

  for (k = 0; k < 10; k++) {
    *p++ = (U8)sep[k];
  }
  for (k = 0; k < 10 && entry->name[k]; k++) {
    *p++ = entry->name[k];
  }
  *p = TILES_NULL;
  tiles_paintListAt(line, 4 * 8, row * 8);
}

/* One font half (pattern or colour table) streamed to VRAM, then the
 * banner's matching half into tiles 128-223 (3 tile rows, contiguous in
 * the data and in VRAM, top screen third -- no thirds replication). */
static void hof_streamTable(U16 fontRes, U16 bannerRes, U16 vram) {
  stream_to_vram(fontRes, vram, 0x800, TRUE);
  stream_to_vram(bannerRes, vram + 128 * 8, 768, FALSE);
}

/* Blank screen with the font and the hall-of-fame banner, streamed into
 * VRAM through the scratch buffer (see this file's header). The font is
 * the one tiles_setBank(0) would push: F18A or stock art. */
static void hof_setupScreen(void) {
  U8 i;

  hof_streamTable(sysvid_nabu_hasF18A ? RES_TF18PATA : RES_TILEPATA, RES_HAFPAT, gPattern);
  hof_streamTable(sysvid_nabu_hasF18A ? RES_TF18COLA : RES_TILECOLA, RES_HAFCOL, gColor);
#if !VDP_TARGET_9918A_ONLY
  if (sysvid_nabu_hasF18A) {
    f18a_unlock();
    f18a_loadPalette((const U16 *)tilesf18_pal, 0, 16);
  }
#endif
  scratch_end();
  tiles_setBank(0xff); /* VRAM font changed behind tiles_setBank()'s back */

  fb_clear();
  NABU_DisableInterrupts();
  vdp_setWriteAddress(gImage);
  for (i = 0; i < 96; i++) {
    IO_VDPDATA = (U8)(128 + i);
  }
  NABU_EnableInterrupts();
}

/* Ticks the title music until it reaches its end (sounds_music_looped()),
 * or a key or new joystick press. Returns the key code, 1 for joystick
 * input, or 0 when the music wrapped. */
static U8 hof_wait(void) {
  U16 last;
  U8 key;

  sysevt_poll();
  last = control_status;
  sounds_music_looped(); /* forget a wrap from before this screen */
  for (;;) {
    sysevt_poll();
    sounds_music_tick(MUSIC_RATE_TITLE);
    if (sounds_music_looped()) {
      return 0;
    }
    if (isKeyPressed()) {
      key = getChar();
      return key ? key : 1;
    }
    if (control_status & ~last) {
      return 1;
    }
    last = control_status;
    sys_nabu_waitFrame(16);
  }
}

/* Hall-of-fame screen: banner + top 8, shown (with the title music
 * playing) until the music reaches its end. See hof_wait() for the
 * return. */
U8 screen_hallOfFame(void) {
  U8 i;

#if !MUSIC_SMOOTH_LOADS
  sounds_music_hold(); /* no droning chord during the loads */
#endif
  hof_setupScreen();
#if !MUSIC_SMOOTH_LOADS
  sounds_music_resume();
#endif
  for (i = 0; i < 8; i++) {
    hof_paintScoreLine(&game_hscores[i], (U16)(5 + i * 2));
  }
  return hof_wait();
}

/* ---------------------------------------------------------------------
 * Name entry (after game over, when hof_qualifies())
 *
 * Keyboard: letters/digits type, SPACE = blank, DEL or left arrow = back,
 * RETURN or YES = done. Joystick: up/down pick the letter at the cursor,
 * right or FIRE accept it and move on, left goes back; FIRE on a blank
 * (or after the 10th letter) finishes.
 * --------------------------------------------------------------------- */

static const char enterNameText[] = "ENTER@YOUR@NAME" TILES_NULLCHAR;

static void name_paint(const U8 *name, U8 pos, U8 blink) {
  U8 line[11];
  U8 k;

  for (k = 0; k < 10; k++) {
    line[k] = name[k];
  }
  if (pos < 10 && blink && line[pos] == '@') {
    line[pos] = '.'; /* show the cursor on a blank */
  }
  line[10] = TILES_NULL;
  tiles_paintListAt(line, 11 * 8, 14 * 8);
}

/* next/previous letter for the joystick: '@' (blank) then A..Z -- '@' is
 * ASCII 0x40, right before 'A', so this is just +/-1 wrapping at the ends.
 * (Digits can still be typed on the keyboard.) */
static U8 name_step(U8 c, U8 up) {
  if (up) {
    return c == 'Z' ? '@' : (U8)(c + 1);
  }
  return c == '@' ? 'Z' : (U8)(c - 1);
}

void screen_enterName(void) {
  U8 name[10];
  U8 line[7 + 6 + 1];
  U8 pos = 0, k, key, blink = 0;
  U16 last, pressed;

  for (k = 0; k < 10; k++) {
    name[k] = '@';
  }
  hof_setupScreen();
  tiles_paintListAt((const U8 *)enterNameText, 8 * 8, 8 * 8);
  {
    static const char scoreText[] = "SCORE@@";
    U8 *p = line;
    for (k = 0; k < 7; k++) {
      *p++ = (U8)scoreText[k];
    }
    p = hof_digits(p, env_score_hi, env_score_lo);
    *p = TILES_NULL;
    tiles_paintListAt(line, 9 * 8, 11 * 8);
  }

  while (isKeyPressed()) { /* nothing left over from game over */
    getChar();
  }
  sysevt_poll();
  last = control_status;

  for (;;) {
    name_paint(name, pos, (U8)(blink & 0x10));
    blink++;
    if (sounds_music_playing()) {
      sounds_music_tick(MUSIC_RATE_TITLE);
    }
    sys_nabu_waitFrame(16);

    sysevt_poll();
    pressed = control_status & ~last;
    last = control_status;
    key = isKeyPressed() ? getChar() : 0;
    if (key >= 'a' && key <= 'z') {
      key = (U8)(key - 'a' + 'A');
    }

    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') || key == ' ') {
      if (pos < 10) {
        name[pos++] = (key == ' ') ? '@' : key;
      }
    } else if (key == 0x7F || key == 0x08 || key == 0xE1 || (pressed & CONTROL_LEFT)) {
      if (pos > 0) {
        pos--;
        if (key) {
          name[pos] = '@'; /* keyboard back = delete */
        }
      }
    } else if (key == 0x0D || key == 0xE7) {
      break;
    } else if (pos < 10 && (pressed & (CONTROL_UP | CONTROL_DOWN))) {
      if (name[pos] < '@' || name[pos] > 'Z') {
        name[pos] = '@'; /* a typed digit: restart the cycle at blank */
      }
      name[pos] = name_step(name[pos], (U8)(pressed & CONTROL_UP));
    } else if (pressed & (CONTROL_FIRE | CONTROL_RIGHT)) {
      if (pos >= 10 || ((pressed & CONTROL_FIRE) && name[pos] == '@')) {
        break;
      }
      pos++;
    }
  }

  hof_insert(name);
}

/* eof */
