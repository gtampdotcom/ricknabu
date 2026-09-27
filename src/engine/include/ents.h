/*
 * engine/include/ents.h -- ported from xrick/include/ents.h, unchanged.
 * Entity struct/table definitions; no TI-specific content. The "TI only has
 * 32 sprites" comment below stays accurate on NABU too -- the TMS9918A
 * family's 32-sprite limit is the same chip either way.
 */

#ifndef _ENTS_H
#define _ENTS_H

#include "ricksystem.h"

extern void ents_paintAll(void);

// this is only used in e_sbonus.c. Everywhere else uses ent_ents[1] literally
#define ENT_XRICK ent_ents[1]

/* BUG FIXED, reported as "no visible boulder, Rick dies instantly on
 * level 1 without moving": a previous round tried rebasing mark_t.ent
 * and ent_entdata[] to a dense, per-map-local 0-based numbering (same
 * trick as maps.h's own MAP_NBR_BLOCKS) to avoid keeping the whole
 * global table resident. That's UNSAFE here in a way it isn't for
 * blocks/bnums/submaps/connect: ents.c's entity-creation code doesn't
 * treat mark.ent/ent_ents[].n as an opaque index -- it's real xrick
 * design that the NUMBER ITSELF encodes entity type via range checks
 * (`map_marks_ent[m] >= 0x10`, `>= 0x18`, and a mod-3 pattern
 * distinguishing e_them type 1a/1b/2 -- see ents.c's own big comment
 * starting "the type of an entity is determined by its .n"). Compacting
 * to a small local range (0-21 for SAMERICA, 0-26 for EGYPT) makes
 * `>= 0x18` never true and `>= 0x10` true far less often than it should
 * be, silently misrouting entity creation/behavior for nearly everything
 * -- not a data-corruption bug, a broken implicit contract this session
 * didn't know about until it broke real gameplay.
 *
 * The real fix: mark.ent and ent_entdata[] keep their REAL global xrick
 * values, unrebased, exactly like the original reference -- entdata is
 * genuinely shared, whole-game, not-per-map data (back to this file's
 * older reasoning: no smaller safe cut exists here). ENT_NBR_ENTDATA is
 * the full real 74. ENT_NBR_SPRSEQ/MVSTEP are still trimmed from their
 * own full real sizes (136/784) -- that part IS safe, since it's just
 * truncating unused tail entries, not renumbering anything -- sized to
 * the UNION of every entdata row any currently-ported map's marks
 * reference (SAMERICA+EGYPT+CASTLE+MBASE combined). Widened for CASTLE:
 * its own reachable .spr/.sni chains walk up to sprseq index 108 and
 * mvstep index 147 respectively before hitting each chain's own real
 * 0xff terminator (verified by walking every chain, not just widening to
 * a bare index cutoff -- one entry, ent 0x47/zombie's own step_no_i, is
 * provably dead code on every live path but was widened for anyway
 * rather than assumed unreachable).
 *
 * Widened again for MBASE, SPRSEQ only (MVSTEP's own deepest real chain,
 * 243, already fits under 261 -- no change needed there): two of its own
 * type-3 marks (ent 0x43/0x44, sharing entdata.spr=128) chain-walk out to
 * sprseq index 134 before terminating -- confirmed by walking the REAL
 * awake-animation semantics (e_them_t3_action2()'s own wakeup: label sets
 * sproffs=1, so the awake chain starts at sprbase+1, not sprbase+0 --
 * getting this off-by-one wrong looks like an empty/degenerate chain
 * instead of the real 5-frame one). ENT_NBR_SPRSEQ widened to the full
 * real reference size (136) rather than the bare 135 needed, same
 * "don't just widen to a cutoff" policy as CASTLE's own MVSTEP widening
 * above -- there's no meaningfully smaller safe size once past 134 needs
 * to be included anyway.
 * ent_entdata[]/ent_sprseq[]/ent_mvstep[] are loaded ONCE at boot again
 * (main.c's loadAssets()), not per-map -- they can't safely swap per map
 * the way rebased-and-packed data could, since every map's marks must
 * resolve correctly through the SAME shared tables simultaneously. */
#define ENT_NBR_ENTDATA 0x4a
#define ENT_NBR_SPRSEQ 0x88
#define ENT_NBR_MVSTEP 0x105

// Note: TI only has 32 sprites, and 4 per line, but each ent is 2x2 sprites
#define ENT_ENTSNUM 0x0c

/*
 * flags for ent_ents[e].n  ("yes" when set)
 *
 * ENT_LETHAL: is entity lethal?
 */
#define ENT_LETHAL 0x80

/*
 * flags for ent_ents[e].flag  ("yes" when set)
 *
 * ENT_FLG_ONCE: should the entity run once only?
 * ENT_FLG_STOPRICK: does the entity stops rick (and goes to slot zero)?
 * ENT_FLG_LETHALR: is entity lethal when restarting?
 * ENT_FLG_LETHALI: is entity initially lethal?
 * ENT_FLG_TRIGBOMB: can entity be triggered by a bomb?
 * ENT_FLG_TRIGBULLET: can entity be triggered by a bullet?
 * ENT_FLG_TRIGSTOP: can entity be triggered by rick stop?
 * ENT_FLG_TRIGRICK: can entity be triggered by rick?
 */
#define ENT_FLG_ONCE 0x01
#define ENT_FLG_STOPRICK 0x02
#define ENT_FLG_LETHALR 0x04
#define ENT_FLG_LETHALI 0x08
#define ENT_FLG_TRIGBOMB 0x10
#define ENT_FLG_TRIGBULLET 0x20
#define ENT_FLG_TRIGSTOP 0x40
#define ENT_FLG_TRIGRICK 0x80

typedef struct {
  U16 n;          /* b00 */
  /*U16 b01;*/    /* b01 in ASM code but never used */
  S16 x;         /* b02 - position */
  S16 y;         /* w04 - position */
  U16 sprite;     /* b08 - sprite number */
  /*U16 w0C;*/   /* w0C in ASM code but never used */
  U16 w;          /* b0E - width */
  U16 h;          /* b10 - height */
  U16 mark;      /* w12 - number of the mark that created the entity */
  U16 flags;      /* b14 */
  U16 trig_x;    /* b16 - position of trigger box */
  U16 trig_y;    /* w18 - position of trigger box */
  U16 xsave;     /* b1C */
  U16 ysave;     /* w1E */
  U16 sprbase;   /* w20 */
  U16 step_no_i; /* w22 */
  U16 step_no;   /* w24 */
  S16 c1;        /* b26 */
  S16 c2;        /* b28 */
  U16 ylow;       /* b2A */
  S16 offsy;     /* w2C */
  U16 latency;    /* b2E */
  U16 prev_n;     /* new */
  U16 prev_x;    /* new */
  U16 prev_y;    /* new */
  U16 prev_s;     /* new */
  U16 trigsnd;    /* new */
  U16 spriteIndex;  /* psuedo sprite index assigned (0xff = not assigned) --
                     * owns this entity's VRAM PATTERN slot only (names
                     * spriteIndex*4 .. +15), kept while the entity lives */
  U16 lastSpriteDrawn; /* sprite number last uploaded to VRAM */
  U8 hwIndex;       /* first of the 4 HARDWARE sprites (attribute table
                     * entries) it was drawn with this frame, 0xff = none --
                     * re-packed every frame by ents_paintAll(), see
                     * sprites.c's sprites_beginFrame() */
} ent_t;

typedef struct {
  U16 w, h;
  U16 spr, sni;
  U16 trig_w, trig_h;
  U16 snd;
} entdata_t;

typedef struct {
  U16 count;
  S16 dx, dy;
} mvstep_t;

extern ent_t ent_ents[ENT_ENTSNUM + 1];
extern entdata_t ent_entdata[ENT_NBR_ENTDATA]; /* fileload: BSS, see dat_ents.c */
extern U16 ent_sprseq[ENT_NBR_SPRSEQ]; /* fileload: BSS, see dat_ents.c */
extern mvstep_t ent_mvstep[ENT_NBR_MVSTEP]; /* fileload: BSS, see dat_ents2.c */

extern void delete_ent(U16);
extern void ent_hideSprite(U16); /* not in the TI original -- see ents.c */
extern void ent_reset(void);
extern void ent_actvis(U16, U16);
extern void ent_clprev(void);
extern void ent_action(void);

#endif

/* eof */
