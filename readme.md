20260926

https://nabu.ca/software/176
https://www.youtube.com/watch?v=ISQDIoS5I18

This is a port of [rickti](https://github.com/tursilion/rickti) to the NABU PC
rickti is a port of [xrick](https://github.com/tursilion/rickti)

It supports F18A/PICO9918/TN-VDP for enhanced colours but also runs on a stock NABU.

The [rickti](https://github.com/tursilion/rickti) version requires a 256KB ROM and 32KB of RAM. The NABU has 64KB of RAM but it has no cartridge port or bank switching, so it has to download resources as required from the HCCA. I managed to squeeze rick.nabu into 32KB, leaving the other 32KB for scratch and download space. It uses a rolling sprite cache to store sprites on demand, you will notice some delays when there are more sprites on screen than can fit in RAM. This could be optimised further but I'm happy with the current playability.

You can use the joystick or arrows+YES key

- Fire+Up - fire gun 
- Fire+Down - dynamite
- Fire+Left/Right - poke with stick

"In case you are not familiar with Rick Dangerous - expect to die A LOT. This is a memorization game for the most part, and a lot of traps give little to no warning."

The stock TMS9918a sprites in this were made by ti99iuc

The original notes are below.

# xrick
Remember Rick Dangerous?

Way before Lara Croft, back in the 1980's and early 1990's, Rick Dangerous was the Indiana Jones of computer games,
running away from rolling rocks, avoiding traps, from South America to a futuristic missile base via Egypt and the
Schwarzendumpf castle.

**xrick** is a clone of Rick Dangerous, produced by carefully reverse-engineering the PC and Atari versions of the
game, and re-coding in C. It has been ported to Windows, Linux, but also BeOs, Amiga, QNX, and all sorts
of gaming console.

You can read more about Rick Dangerous straight from his creator, [Simon Phipps](https://www.simonphipps.com/games/rickdangerous/),
and more about xrick at the original [xrick page](http://www.bigorno.net/xrick).
