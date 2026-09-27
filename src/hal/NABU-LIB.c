// ****************************************************************************************
// NABU-LIB C Library
// DJ Sures (c) 2024
// https://nabu.ca
// https://github.com/DJSures/NABU-LIB
//
// TRIMMED for rick-dangerous (unity-built via NABU-LIB.h): only what src/
// actually calls is left -- init, DI/EI, AY write/read, keyboard/joystick
// queue, the HCCA RX + keyboard ISRs, vdp register/address setters,
// vdp_clearVRAM(), and a fixed-parameter vdp_initG2Mode(). Everything else
// (HCCA read/write helpers -- hal/RetroNET-FileRead-only.c has its own
// hcca_Di* set --, VDP vsync interrupt helpers, generic vdp_init() for
// text/multicolor modes, RNG seeding, RightShift, nop()) was unreferenced.
// **********************************************************************************************

void initNABULib(void) {

  // Turn off the rom
  IO_CONTROL = CONTROL_ROMSEL | CONTROL_VDOBUF;

  NABU_DisableInterrupts();

  __asm

    IM 2;

    ld a, INTERUPT_VECTOR_MAP_MSB;
    ld i, a;

    // HCCA Receive
    ld hl, _isrHCCARX;
    ld (INTERUPT_VECTOR_MAP_ADDRESS), hl;

    // HCCA Keyboard
    ld hl, _isrKeyboard;
    ld (INTERUPT_VECTOR_MAP_ADDRESS + 4), hl;

  __endasm;

  // A homebrew has no existing interrupt state, so start from a clean mask
  ayWrite(IOPORTA, INT_MASK_HCCARX | INT_MASK_KEYBOARD);

  NABU_EnableInterrupts();
}

void NABU_DisableInterrupts(void) __naked {

  __asm

    di

    ret

  __endasm;
}

void NABU_EnableInterrupts(void) __naked {

  __asm

    ei

    ret

  __endasm;
}

void isrHCCARX(void) __naked {

  __asm
    push bc;
    push de;
    push hl;
    push af;
  __endasm;

  // _rxBuffer[_rxBufferWritePos] = IO_HCCA;
  // _rxBufferWritePos++;

  __asm
    ld	bc, __rxBuffer+0
    ld	hl, (__rxBufferWritePos)
    ld	h, 0x00
    add	hl, bc
    in	a, (_IO_HCCA)
    ld	(hl), a
    ld	a, (__rxBufferWritePos+0)
    inc	a
    ld	(__rxBufferWritePos+0), a
  __endasm;

  __asm
    pop af;
    pop hl;
    pop de;
    pop bc;
    ei;
    reti;
  __endasm;
}

void isrKeyboard(void) __naked {

  __asm
    push bc;
    push de;
    push hl;
    push af;
    push iy;
  __endasm;

  uint8_t inKey = IO_KEYBOARD;

  /* 0x90-0x95 are keyboard status/error codes, never keys, and are dropped
   * here -- including 0x94, the "watchdog" byte the keyboard sends about
   * every 3.7s when idle (NABU Technical Manual, keyboard encoding). */
  if (inKey >= 0x80 && inKey <= 0x83) {

    _lastKeyboardIntVal = inKey;
  } else if (inKey < 0x90 || inKey > 0x95) {

    /* BUG FIXED, on request ("add keyboard support... arrows to move
     * and YES key to fire"): this used to dispatch purely on
     * _lastKeyboardIntVal, with no check on inKey itself -- so ANY
     * byte arriving right after a 0x80-0x83 joystick-select prefix
     * got treated as that joystick's status value, even if it was
     * actually an unrelated keyboard byte (e.g. one of the 0xE0-0xFF
     * special-function-key codes the NABU Technical Manual's own
     * Keyboard Encoding Chart documents for arrows/YES/NO/etc).
     * A real joystick status value is always in the 0xA0-0xBF range
     * (same chart, the "'JS'" column) -- added that check here so an
     * out-of-range byte falls through to the keyboard queue instead,
     * even with a stale/pending _lastKeyboardIntVal. */
    if (_lastKeyboardIntVal != 0 && inKey >= 0xA0 && inKey <= 0xBF) {
      switch (_lastKeyboardIntVal) {
        case 0x80:
          _lastKeyboardIntVal = 0;
          _joyStatus[0] = inKey;
          break;
        case 0x81:
          _lastKeyboardIntVal = 0;
          _joyStatus[1] = inKey;
          break;
        case 0x82:
          _lastKeyboardIntVal = 0;
          _joyStatus[2] = inKey;
          break;
        case 0x83:
          _lastKeyboardIntVal = 0;
          _joyStatus[3] = inKey;
          break;
      }
    } else if (inKey < 0xA0 || inKey > 0xBF) {
      _lastKeyboardIntVal = 0;

      _kbdBuffer[_kbdBufferWritePos] = inKey;

      _kbdBufferWritePos++;
    } else {
      /* BUG FIXED, reported as "sometimes the joystick doesn't skip the
       * intro screens": a joystick status byte (0xA0-0xBF, joystick-only
       * in the encoding chart) arriving without its 0x80-0x83 prefix used
       * to fall through into the keyboard queue as a fake keypress -- so a
       * joystick only "worked" on screens that check the keyboard when a
       * prefix happened to get lost. Dropped now; the screens check the
       * joystick itself instead. */
      _lastKeyboardIntVal = 0;
    }
  }

  __asm
    pop iy;
    pop af;
    pop hl;
    pop de;
    pop bc;
    ei;
    reti;
  __endasm;
}

void ayWrite(uint8_t reg, uint8_t val) {

  IO_AYLATCH = reg;

  IO_AYDATA = val;
}

uint8_t ayRead(uint8_t reg) {

  IO_AYLATCH = reg;

  return IO_AYDATA;
}

uint8_t isKeyPressed(void) {

  return (_kbdBufferWritePos != _kbdBufferReadPos);
}

uint8_t getChar(void) {

  while (_kbdBufferWritePos == _kbdBufferReadPos);

  uint8_t key = _kbdBuffer[_kbdBufferReadPos];

  _kbdBufferReadPos++;

  return key;
}

uint8_t getJoyStatus(uint8_t joyNum) {

  return _joyStatus[joyNum];
}

void vdp_setRegister(uint8_t registerIndex, uint8_t value) {

  IO_VDPLATCH = value;

  IO_VDPLATCH = 0x80 | registerIndex;
}

void vdp_setWriteAddress(uint16_t address) {

  IO_VDPLATCH = address;

  IO_VDPLATCH = (address >> 8) | 0x40;
}

void vdp_setReadAddress(uint16_t address) {

  IO_VDPLATCH = address;

  IO_VDPLATCH = (address >> 8);
}

void vdp_clearVRAM(void) {

  // 16 KB of VRAM (0x0000 - 0x3FFF).
  vdp_setWriteAddress(0x00);

  for (uint16_t i = 0; i < 0x4000; i++)
    IO_VDPDATA = 0;
}

// Graphics II, 16x16 sprites, no magnify, thirds split, black backdrop --
// the only vdp_initG2Mode() argument set this project ever passed
// (VDP_BLACK, true, false, false, true), so the generic vdp_init() mode
// switch collapses to this fixed register sequence. Resulting layout:
// name table 0x1800, pattern generator 0x0000, color table 0x2000, sprite
// attribute table 0x1b00, sprite generator 0x3800.
void vdp_initG2Mode(void) {

  vdp_setRegister(0, 0b00000010);
  vdp_setRegister(1, 0b11000010);  // 16K, display on, INT on, 16x16 sprites
  vdp_setRegister(2, 0x06);
  vdp_setRegister(4, 0x03);
  vdp_setRegister(3, 0xff);
  vdp_setRegister(5, 0x36);
  vdp_setRegister(6, 0x07);
  vdp_setRegister(7, VDP_BLACK);   // fg 0, bg black
}