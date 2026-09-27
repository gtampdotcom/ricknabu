/*
 * engine/e_them.c -- port of xrick/src/e_them.c
 *
 * Ported: fetch_mvstep()/e_them_t3_action2()/e_them_t3_action() (type 3,
 * the scripted/triggered entity -- the boulder), u_themtest(),
 * e_them_gozombie(), e_them_t1_action2()/e_them_t1_action()/
 * e_them_t1a_action()/e_them_t1b_action() (types 1a/1b -- same shared
 * movement function, just a `type` parameter apart), e_them_z_action()
 * (the death-bounce state entities of any of these types fall into when
 * killed), and now e_them_t2_action2()/e_them_t2_action() -- the
 * remaining enemy type, sharing u_themtest()/e_bomb_lethal/E_BULLET_ENT
 * with type 1 but adding its own much larger "Black Magic" RNG-driven
 * climbing/chasing logic. All four e_them types (1a/1b/2/3) are real now
 * -- box/bonus/speed-bonus (e_box.c/e_bonus.c/e_sbonus.c) are the
 * remaining stub_ebehavior.c no-ops, deliberately: those are pickups, not
 * enemies, a separate port on its own.
 *
 * ents.c's ent_actvis() already creates type 1a/1b/2 entities correctly
 * (ent_creat2(), the .sprbase/.w/.h/.trig_x/.trig_y/.latency/.c1=offsx=2
 * setup) -- it just used to skip creating them at all (map_marks_ent[m] <
 * 0x18 check, added when these were no-ops so unported entities wouldn't
 * sit frozen on screen). That skip is now narrowed to still exclude box/
 * bonus/speed-bonus (still no-ops) but let 1a/1b/2 through -- see that
 * file's own comment at the check.
 *
 * Map 1/submap 0 already has a real type-1a mark in its own level data
 * (row 0x38, ent=0x04, no flags) -- porting this needed no new map/mark
 * data at all, just the missing behavior code. No type-2 mark exists in
 * submap 0's own (still truncated to that one submap, maps.h's
 * MAP_NBR_MARKS comment) 5-entry mark list, so e_them_t2_action() is
 * correct but currently unreachable from anything spawned yet -- it'll
 * start mattering the moment MAP_NBR_MARKS widens to cover a submap that
 * actually has one.
 *
 * Real changes from the TI source (both old and new code in this file):
 *   - Dropped `unsigned int nOldBank = nBank;` / SWITCH_IN_BANK* /
 *     SWITCH_IN_BANK(nOldBank) throughout -- same convention as every
 *     other file in this port; no cartridge banking on NABU.
 *   - e_them_t3_action2()'s own wakeup sound and e_them_gozombie()'s own
 *     death sound are both real now -- see each function's own comment.
 *     Both used to be dropped `#ifdef ENABLE_SOUND` blocks (no sound
 *     engine ported yet); e_them_gozombie()'s DIE_SND was also briefly
 *     restored and reverted once in between, back when DIE_SND was still
 *     the old ~0.7s CPU-blocking play_die_pcm() (too disruptive on every
 *     single enemy kill) -- not an issue any more since every effect
 *     since is non-blocking (sounds.c's own header).
 *   - Dropped the `#ifdef CLASSIC99` spin-wait block in t3's wakeup path
 *     -- CLASSIC99 is never defined outside the TI build.
 *   - e_them_t2_action2()'s own sys_printf() debug-trace calls (commented
 *     out even in the reference) dropped rather than carried over as dead
 *     comments -- same reasoning as every other debug sys_printf() call
 *     site this port drops (config.h's DEBUG isn't defined).
 */

#include "config.h"
#include "env.h"
#include "game.h"
#include "ents.h"
#include "e_them.h"
#include "e_rick.h"
#include "e_bomb.h"
#include "e_bullet.h"
#include "util.h"
#include "sounds.h"

/*
 * public vars
 */
U32 e_them_rndseed = 0;

/*
 * local vars -- e_them_t2_action2()'s own "Black Magic" RNG state, see
 * that function's own comment.
 */
static U16 e_them_rndnbr = 0;

static void fetch_mvstep(int step, mvstep_t *pmv) {
  *pmv = ent_mvstep[step];
}

/*
 * Action sub-function for e_them _t3
 *
 * Waits until triggered by something, then execute move steps from
 * ent_mvstep with sprite from ent_sprseq. When done, either restart
 * or disappear.
 *
 * Not always lethal ... but if lethal, kills rick.
 */
void e_them_t3_action2(U16 e) {
#define sproffs c1
#define step_count c2
  U16 i;
  U16 x, y;
  mvstep_t tmp_mvstep;

  while (1) {
    /* calc new sprite */
    i = ent_sprseq[ent_ents[e].sprbase + ent_ents[e].sproffs];
    if (i == 0xff) {
      i = ent_sprseq[ent_ents[e].sprbase];
    }
    ent_ents[e].sprite = i;
    /* 0xff is meant to be 'repeat' but seems the last screen
     * can land there, which is screwing up the cache logic */
    if ((ent_ents[e].sprite == 0) || (ent_ents[e].sprite == 0xff)) {
      /* we need to clear the sprite cache */
      U16 n = ent_ents[e].n;
      delete_ent(e);
      ent_ents[e].n = n;
      ent_ents[e].sprite = 0;
    }

    if (ent_ents[e].sproffs != 0) { /* awake */

      /* rotate sprseq */
      if (ent_sprseq[ent_ents[e].sprbase + ent_ents[e].sproffs] != 0xff) {
        ent_ents[e].sproffs++;
      }
      if (ent_sprseq[ent_ents[e].sprbase + ent_ents[e].sproffs] == 0xff) {
        ent_ents[e].sproffs = 1;
      }

      fetch_mvstep(ent_ents[e].step_no, &tmp_mvstep);
      if (ent_ents[e].step_count < tmp_mvstep.count) {
        /*
         * still running this step: try to increment x and y while
         * checking that they remain within boundaries. if so, return.
         * else switch to next step.
         */
        ent_ents[e].step_count++;
        x = ent_ents[e].x + tmp_mvstep.dx;

        /* check'n save */
        if (x > 0 && x < 0xe8) {
          ent_ents[e].x = x;
          y = ent_ents[e].y + tmp_mvstep.dy;
          if (y > 0 && y < 0xf1) { /* was 0xdc */
            ent_ents[e].y = y;
            return;
          }
        }
      }

      /*
       * step is done, or x or y is outside boundaries. try to
       * switch to next step
       */
      ent_ents[e].step_no++;
      fetch_mvstep(ent_ents[e].step_no, &tmp_mvstep);
      if (tmp_mvstep.count != 0xff) {
        /* there is a next step: init and loop */
        ent_ents[e].step_count = 0;
      } else {
        /* there is no next step: restart or deactivate */
        if (!E_RICK_STTST(E_RICK_STZOMBIE) && !(ent_ents[e].flags & ENT_FLG_ONCE)) {
          /* loop this entity */
          ent_ents[e].sproffs = 0;
          ent_ents[e].n &= ~ENT_LETHAL;
          if (ent_ents[e].flags & ENT_FLG_LETHALR) {
            ent_ents[e].n |= ENT_LETHAL;
          }
          ent_ents[e].x = ent_ents[e].xsave;
          ent_ents[e].y = ent_ents[e].ysave;
          if (ent_ents[e].y < 0 || ent_ents[e].y > 0x140) {
            delete_ent(e);
            return;
          } else {
            /* reset the sprite in case we don't retrigger */
            U16 n = ent_ents[e].n;
            delete_ent(e);
            ent_ents[e].n = n;
          }
        } else {
          /* deactivate this entity */
          delete_ent(e);
          return;
        }
      }
    } else {
      /* ent_ents[e].sproffs == 0 -- waiting */

      if (ent_ents[e].flags & ENT_FLG_TRIGRICK) { /* reacts to rick */
        /* wake up if triggered by rick */
        if (u_trigbox(e, E_RICK_ENT.x + 0x0C, E_RICK_ENT.y + 0x0A)) {
          goto wakeup;
        }
      }

      if (ent_ents[e].flags & ENT_FLG_TRIGSTOP) { /* reacts to rick "stop" */
        /* wake up if triggered by rick "stop" */
        if (E_RICK_STTST(E_RICK_STSTOP) && u_trigbox(e, e_rick_stop_x, e_rick_stop_y)) {
          goto wakeup;
        }
      }

      if (ent_ents[e].flags & ENT_FLG_TRIGBULLET) { /* reacts to bullets */
        /* wake up if triggered by bullet */
        if (E_BULLET_ENT.n && u_trigbox(e, e_bullet_xc, e_bullet_yc)) {
          delete_ent(E_BULLET_NO);
          goto wakeup;
        }
      }

      if (ent_ents[e].flags & ENT_FLG_TRIGBOMB) { /* reacts to bombs */
        /* wake up if triggered by bomb */
        if (e_bomb_lethal) {
          if (u_trigbox(e, e_bomb_xc, e_bomb_yc)) {
            goto wakeup;
          }
        }
      }

      /* not triggered: keep waiting */
      return;

      /* something triggered the entity: wake up */
      /* initialize step counter */
    wakeup:
      if (E_RICK_STTST(E_RICK_STZOMBIE)) {
        return;
      }
      /* Real per-type sound on request ("is the dart/arrow meant to make
       * a sound") -- the reference (xrick/src/e_them.c) plays
       * `sounds_play(WAV_ENTITY[(trigsnd & 0x1F) - 0x14])` right here,
       * every time a type-3 scripted/triggered entity (boulder, dart/
       * arrow trap, any other prop using this same mechanism) wakes up --
       * this port used to drop it outright (this file's own header, "no
       * sound engine ported yet"). ENT0_SND..ENT8_SND are real chiptune
       * effects now (see sounds_playEnt()'s own header, engine/sounds.c,
       * for why they stream instead of staying resident) -- this is
       * their real, correct call site; e_them_gozombie() above plays the
       * one generic DIE_SND for actual enemy kills, unrelated to this.
       *
       * BUG FIXED, reported via the IA's own console log ("Invalid
       * filename 'ENT.DAT': non-printable ASCII at pos 3=0x1C"): some
       * real type-3 entity's own .trigsnd value has its low 5 bits below
       * 0x14, making this subtraction go negative -- as the U16
       * sounds_playEnt() takes, that wraps to a huge index (0xFFEC for a
       * masked value of 0), which sounds_playEnt() then both indexes
       * ent_snd_sizes[]/ent_snd_ids[] with (undefined behavior, reading
       * whatever bytes happen to sit far past those 9-entry tables) and
       * folds into its filename template's digit byte -- 0x30+0xFFEC
       * truncates to 0x1C, exactly the non-printable byte the IA
       * rejected. The REFERENCE never actually fixed this either: its
       * own equivalent call site is guarded by a `#ifdef CLASSIC99`
       * block that just busy-spins forever on this exact condition
       * ("trying to catch a bug here... spin - I think I can abuse i",
       * xrick/src/e_them.c) -- debugging instrumentation, not a real
       * clamp, and not even compiled into this port (CLASSIC99 isn't
       * defined) -- plus a FIXME right above it admitting the whole
       * table was never fully nailed down upstream ("I dont have the
       * table yet... is it 8 of them, not 10?"). Real fix here: skip the
       * sound entirely when the computed index isn't a real ENT0-8
       * entry, same "known gap, not a crash" spirit as this file's other
       * not-fully-mapped cases -- silence beats a wild filename/array
       * index. */
      if ((ent_ents[e].trigsnd & 0x1F) >= 0x14 && (ent_ents[e].trigsnd & 0x1F) <= 0x1C) {
        sounds_playEnt((ent_ents[e].trigsnd & 0x1F) - 0x14);
      }
      ent_ents[e].n &= ~ENT_LETHAL;
      if (ent_ents[e].flags & ENT_FLG_LETHALI) {
        ent_ents[e].n |= ENT_LETHAL;
      }
      ent_ents[e].sproffs = 1;
      ent_ents[e].step_count = 0;
      ent_ents[e].step_no = ent_ents[e].step_no_i;
      return;
    }
  }
#undef sproffs
#undef step_count
}

/*
 * Action function for e_them _t3 type
 */
void e_them_t3_action(U16 e) {
  e_them_t3_action2(e);

  /* if lethal, can kill rick */
  if ((ent_ents[e].n & ENT_LETHAL) && !E_RICK_STTST(E_RICK_STZOMBIE) && e_rick_boxtest(e)) {
    e_rick_gozombie();
  }
}

#define TYPE_1A (0x00)
#define TYPE_1B (0xff)

/*
 * Check if entity boxtests with a lethal e_them, i.e. something lethal
 * in slot 0 and 4 to 8.
 *
 * e: entity slot number.
 * ret: TRUE/boxtests, FALSE/not
 */
U8 u_themtest(U16 e) {
  U16 i;

  if ((ent_ents[0].n & ENT_LETHAL) && u_boxtest(e, 0)) {
    return TRUE;
  }

  for (i = 4; i < 9; i++) {
    if ((ent_ents[i].n & ENT_LETHAL) && u_boxtest(e, i)) {
      return TRUE;
    }
  }

  return FALSE;
}

/*
 * Go zombie (death bounce) for an e_them entity.
 */
void e_them_gozombie(U16 e) {
#define offsx c1
  ent_ents[e].n = 0x47; /* zombie entity */
  ent_ents[e].offsy = -0x0400;
  /* DIE_SND is back on request, every enemy kill, not just Rick's own
   * death -- a previous round tried this with the OLD play_die_pcm()
   * (sounds.c's own header), a ~0.7s CPU-blocking busy-wait that read as
   * too disruptive on every single kill and was reverted. The real
   * chiptune effect that replaced it (sounds_playDie(), a converted
   * SN76489 capture -- see sounds.c's own header) is non-blocking,
   * ticked through the same shared fx slot as JUMP_SND/BULLET_SND/
   * EXPLODE_SND/BONUS_SND -- that specific objection doesn't apply here
   * any more.
   *
   * BUG FIXED, on request ("is the dart/arrow meant to make a sound"):
   * this used to call sounds_playEnt((trigsnd & 0x1F) - 0x14) here
   * instead of sounds_playDie() -- wrong call site, my own mistake.
   * Checking the reference directly (xrick/src/e_them.c) shows
   * `sounds_play(WAV_ENTITY[(trigsnd & 0x1F) - 0x14])` lives in
   * e_them_t3_action2()'s own `wakeup:` label (the type-3 scripted/
   * triggered entity -- boulders, dart/arrow traps, any other prop that
   * "wakes up" when triggered), not in this function at all -- this
   * function has ALWAYS just played the one generic DIE_SND for every
   * enemy kill, verbatim from the reference. Moved the real fix to
   * e_them_t3_action2() below, where it actually belongs. */
  sounds_playDie();
  addscore(50);
  if (ent_ents[e].flags & ENT_FLG_ONCE) {
    /* make sure entity won't be activated again */
    map_marks_ent[ent_ents[e].mark] |= MAP_MARK_NACT;
  }
  ent_ents[e].offsx = (ent_ents[e].x >= 0x80 ? -0x02 : 0x02);
#undef offsx
}

/*
 * Action sub-function for e_them _t1a and _t1b.
 *
 * Those two types move horizontally, and fall if they have to. Type 1a
 * moves horizontally over a given distance and then u-turns and repeats;
 * type 1b does u-turns in order to move horizontally towards rick.
 */
void e_them_t1_action2(U16 e, U16 type) {
#define offsx c1
#define step_count c2
  S32 i;
  U16 x, y;
  U16 env0, env1;

  /* by default, try vertical move. calculate new y */
  i = ((S32)ent_ents[e].y << 8) + ent_ents[e].offsy + ent_ents[e].ylow;
  y = (U16)(i >> 8);

  /* deactivate if outside vertical boundaries */
  if (y > 0x140) {
    delete_ent(e);
    return;
  }

  /* test environment */
  u_envtest(ent_ents[e].x, y, FALSE, &env0, &env1);

  if (!(env1 & (MAP_EFLG_VERT | MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP))) {
    /* vertical move possible: falling */
    if (env1 & MAP_EFLG_LETHAL) {
      /* lethal entities kill e_them */
      e_them_gozombie(e);
      return;
    }
    /* save, cleanup and return */
    ent_ents[e].y = y;
    ent_ents[e].ylow = (U8)i;
    ent_ents[e].offsy += 0x0080;
    if (ent_ents[e].offsy > 0x0800) {
      ent_ents[e].offsy = 0x0800;
    }
    return;
  }

  /* vertical move not possible. calculate new sprite */
  ent_ents[e].sprite = ent_ents[e].sprbase + ent_sprseq[(ent_ents[e].x & 0x1c) >> 3] +
                       (ent_ents[e].offsx < 0 ? 0x03 : 0x00);

  /* reset offsy */
  ent_ents[e].offsy = 0x0080;

  /* align to ground */
  ent_ents[e].y &= 0xfff8;
  ent_ents[e].y |= 0x0003;

  /* latency: if not zero then decrease and return */
  if (ent_ents[e].latency > 0) {
    ent_ents[e].latency--;
    return;
  }

  /* horizontal move. calculate new x */
  if (ent_ents[e].offsx == 0) { /* not supposed to move -> don't */
    return;
  }

  x = ent_ents[e].x + ent_ents[e].offsx;
  if (ent_ents[e].x < 0 || ent_ents[e].x > 0xe8) {
    /* U-turn and return if reaching horizontal boundaries */
    ent_ents[e].step_count = 0;
    ent_ents[e].offsx = -ent_ents[e].offsx;
    return;
  }

  /* test environment */
  u_envtest(x, ent_ents[e].y, FALSE, &env0, &env1);

  if (env1 & (MAP_EFLG_VERT | MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP)) {
    /* horizontal move not possible: u-turn and return */
    ent_ents[e].step_count = 0;
    ent_ents[e].offsx = -ent_ents[e].offsx;
    return;
  }

  /* horizontal move possible */
  if (env1 & MAP_EFLG_LETHAL) {
    /* lethal entities kill e_them */
    e_them_gozombie(e);
    return;
  }

  /* save */
  ent_ents[e].x = x;

  /* depending on type, */
  if (type == TYPE_1B) {
    /* set direction to move horizontally towards rick */
    if ((ent_ents[e].x & 0x1e) != 0x10) { /* prevents too frequent u-turns */
      return;
    }
    ent_ents[e].offsx = (ent_ents[e].x < E_RICK_ENT.x) ? 0x02 : -0x02;
    return;
  } else {
    /* set direction according to step counter */
    ent_ents[e].step_count++;
    if ((ent_ents[e].trig_x >> 1) > ent_ents[e].step_count) {
      return;
    }
  }

  /* type is 1A and step counter reached its limit: u-turn */
  ent_ents[e].step_count = 0;
  ent_ents[e].offsx = -ent_ents[e].offsx;
#undef offsx
#undef step_count
}

void e_them_t1_action(U16 e, U16 type) {
  e_them_t1_action2(e, type);

  /* lethal entities kill them */
  if (u_themtest(e)) {
    e_them_gozombie(e);
    return;
  }

  /* bullet kills them */
  if (E_BULLET_ENT.n &&
      u_fboxtest(e, E_BULLET_ENT.x + (e_bullet_offsx < 0 ? 0 : 0x18), E_BULLET_ENT.y)) {
    delete_ent(E_BULLET_NO);
    e_them_gozombie(e);
    return;
  }

  /* bomb kills them */
  if (e_bomb_lethal && e_bomb_hit(e)) {
    e_them_gozombie(e);
    return;
  }

  /* rick stops them */
  if (E_RICK_STTST(E_RICK_STSTOP) && u_fboxtest(e, e_rick_stop_x, e_rick_stop_y)) {
    ent_ents[e].latency = 0x14;
  }

  /* they kill rick */
  if (e_rick_boxtest(e)) {
    e_rick_gozombie();
  }
}

/*
 * Action function for e_them _t1a type (stays within boundaries)
 */
void e_them_t1a_action(U16 e) {
  e_them_t1_action(e, TYPE_1A);
}

/*
 * Action function for e_them _t1b type (runs for rick)
 */
void e_them_t1b_action(U16 e) {
  e_them_t1_action(e, TYPE_1B);
}

/*
 * Action function for e_them _z (zombie/death-bounce) type.
 */
void e_them_z_action(U16 e) {
#define offsx c1
  S32 i;

  /* calc new sprite -- sprbase+6/+7, the same formula every other type's
   * zombie-bounce uses. The "spear guy" enemy's own death frames (sprbase
   * 142 -> 148/149) used to fall back to its normal walk pose (142) here
   * because 148/149 weren't loaded (see sprites_map1_extra[]'s own
   * comment on sprite 148) -- they're real data now, so this no longer
   * needs a special case. */
  ent_ents[e].sprite = ent_ents[e].sprbase + ((ent_ents[e].x & 0x04) ? 0x07 : 0x06);

  /* calc new y */
  i = ((S32)ent_ents[e].y << 8) + ent_ents[e].offsy + ent_ents[e].ylow;

  /* deactivate if out of vertical boundaries */
  if (ent_ents[e].y < 0 || ent_ents[e].y > 0xdc) {
    delete_ent(e);
    return;
  }

  /* save */
  ent_ents[e].offsy += 0x0080;
  ent_ents[e].ylow = (U8)i;
  ent_ents[e].y = (U16)(i >> 8);

  /* calc new x */
  ent_ents[e].x += ent_ents[e].offsx;

  /* must stay within horizontal boundaries */
  if (ent_ents[e].x < 0) {
    ent_ents[e].x = 0;
  }
  if (ent_ents[e].x > 0xe8) {
    ent_ents[e].x = 0xe8;
  }
#undef offsx
}

/*
 * Action sub-function for e_them _t2.
 *
 * Must document what it does.
 */
void e_them_t2_action2(U16 e) {
#define flgclmb c1
#define offsx c2
  S32 i;
  U16 x, y;
  S16 yd;
  U16 env0, env1;

  /*
   * vars required by the Black Magic (tm) performance at the
   * end of this function.
   */
  static U16 bx;
  static U8* bl = (U8*)&bx;
  static U8* bh = (U8*)&bx + 1;
  static U16 cx;
  static U8* cl = (U8*)&cx;
  static U8* ch = (U8*)&cx + 1;
  static U16* sh = (U16*)&e_them_rndseed;             // MSW of rndseed
  static U16* sl = (U16*)((U8*)&e_them_rndseed + 2);  // LSW of rndseed

  /* latency: if not zero then decrease */
  if (ent_ents[e].latency > 0) {
    ent_ents[e].latency--;
  }

  /* climbing? */
  if (ent_ents[e].flgclmb != TRUE) {
    goto climbing_not;
  }

  /* CLIMBING */

  /* latency: if not zero then return */
  if (ent_ents[e].latency > 0) {
    return;
  }

  /* calc new sprite */
  ent_ents[e].sprite = ent_ents[e].sprbase + 0x08 +
      (((ent_ents[e].x ^ ent_ents[e].y) & 0x04) ? 1 : 0);

  /* reached rick's level? */
  /* 0x1fe not 0xfe: y reaches 0x140, comparing only the low byte made
   * climbers below the screen think they were level with rick */
  if ((ent_ents[e].y & 0x1fe) != (E_RICK_ENT.y & 0x1fe)) {
    goto ymove;
  }

xmove:
  /* calc new x and test environment */
  ent_ents[e].offsx = (ent_ents[e].x < E_RICK_ENT.x) ? 0x02 : -0x02;
  x = ent_ents[e].x + ent_ents[e].offsx;
  u_envtest(x, ent_ents[e].y, FALSE, &env0, &env1);
  if (env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP)) {
    return;
  }
  if (env1 & MAP_EFLG_LETHAL) {
    e_them_gozombie(e);
    return;
  }
  ent_ents[e].x = x;
  if (env1 & (MAP_EFLG_VERT | MAP_EFLG_CLIMB)) { /* still climbing */
    return;
  }
  goto climbing_not;  /* not climbing anymore */

ymove:
  /* calc new y and test environment */
  yd = ent_ents[e].y < E_RICK_ENT.y ? 0x02 : -0x02;
  y = ent_ents[e].y + yd;
  if (y > 0x140) { /* y is U16: moving up past 0 wraps to 0xFFFx, caught here too */
    delete_ent(e);
    return;
  }
  u_envtest(ent_ents[e].x, y, FALSE, &env0, &env1);
  if (env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP)) {
    if (yd < 0) {
      goto xmove;  /* can't go up */
    } else {
      goto climbing_not;  /* can't go down */
    }
  }
  /* can move */
  ent_ents[e].y = y;
  if (env1 & (MAP_EFLG_VERT | MAP_EFLG_CLIMB)) {  /* still climbing */
    return;
  }

  /* NOT CLIMBING */

climbing_not:
  ent_ents[e].flgclmb = FALSE;  /* not climbing */

  /* calc new y (falling) and test environment */
  i = ((S32)ent_ents[e].y << 8) + ent_ents[e].offsy + ent_ents[e].ylow;
  y = (U16)(i >> 8);
  u_envtest(ent_ents[e].x, y, FALSE, &env0, &env1);
  if (!(env1 & (MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP))) {
    /* can go there */
    if (env1 & MAP_EFLG_LETHAL) {
      e_them_gozombie(e);
      return;
    }
    if (y > 0x140) {  /* deactivate if outside */
      delete_ent(e);
      return;
    }
    if (!(env1 & MAP_EFLG_VERT)) {
      /* save */
      ent_ents[e].y = y;
      ent_ents[e].ylow = (U8)i;
      ent_ents[e].offsy += 0x0080;
      if (ent_ents[e].offsy > 0x0800) {
        ent_ents[e].offsy = 0x0800;
      }
      return;
    }
    if (((ent_ents[e].x & 0x07) == 0x04) && (y < E_RICK_ENT.y)) {
      ent_ents[e].flgclmb = TRUE;  /* climbing */
      return;
    }
  }

  /* can't go there, or ... */
  /* align to ground -- must keep bit 8: y goes up to 0x140 and masking
   * it off teleported enemies from the hidden bottom rows up 256px */
  ent_ents[e].y = (ent_ents[e].y & 0xfff8) | 0x03;
  ent_ents[e].offsy = 0x0100;
  if (ent_ents[e].latency != 00) {
    return;
  }

  if ((env1 & MAP_EFLG_CLIMB) &&
      ((ent_ents[e].x & 0x0e) == 0x04) &&
      (ent_ents[e].y > E_RICK_ENT.y)) {
    ent_ents[e].flgclmb = TRUE;  /* climbing */
    return;
  }

  /* calc new sprite */
  ent_ents[e].sprite = ent_ents[e].sprbase +
      ent_sprseq[(ent_ents[e].offsx < 0 ? 4 : 0) +
      ((ent_ents[e].x & 0x0e) >> 3)];

  if (ent_ents[e].offsx == 0) {
    ent_ents[e].offsx = 2;
  }
  x = ent_ents[e].x + ent_ents[e].offsx;
  if (x < 0xe8) {
    u_envtest(x, ent_ents[e].y, FALSE, &env0, &env1);
    if (!(env1 & (MAP_EFLG_VERT | MAP_EFLG_SOLID | MAP_EFLG_SPAD | MAP_EFLG_WAYUP))) {
      ent_ents[e].x = x;
      if ((x & 0x1e) != 0x08) {
        return;
      }

      /*
       * Black Magic (tm)
       *
       * this is obviously some sort of randomizer to define a direction
       * for the entity. it is an exact copy of what the assembler code
       * does but I can't explain.
       */
      bx = e_them_rndnbr + *sh + *sl + 0x0d;
      cx = *sh;
      *bl ^= *ch;
      *bl ^= *cl;
      *bl ^= *bh;
      e_them_rndnbr = bx;

      ent_ents[e].offsx = (*bl & 0x01) ? -0x02 : 0x02;

      /* back to normal */
      return;
    }
  }

  /* U-turn */
  if (ent_ents[e].offsx == 0) {
    ent_ents[e].offsx = 2;
  } else {
    ent_ents[e].offsx = -ent_ents[e].offsx;
  }
#undef offsx
#undef flgclmb
}

/*
 * Action function for e_them _t2 type
 */
void e_them_t2_action(U16 e) {
  e_them_t2_action2(e);

  /* they kill rick */
  if (e_rick_boxtest(e)) {
    e_rick_gozombie();
  }

  /* lethal entities kill them */
  if (u_themtest(e)) {
    e_them_gozombie(e);
    return;
  }

  /* bullet kills them */
  if (E_BULLET_ENT.n && u_fboxtest(e, E_BULLET_ENT.x + (e_bullet_offsx < 0 ? 0 : 0x18), E_BULLET_ENT.y)) {
    delete_ent(E_BULLET_NO);
    e_them_gozombie(e);
    return;
  }

  /* bomb kills them */
  if (e_bomb_lethal && e_bomb_hit(e)) {
    e_them_gozombie(e);
    return;
  }

  /* rick stops them with stick */
  if (E_RICK_STTST(E_RICK_STSTOP) && u_fboxtest(e, e_rick_stop_x, e_rick_stop_y)) {
    ent_ents[e].latency = 40;
  }
}

/* eof */
