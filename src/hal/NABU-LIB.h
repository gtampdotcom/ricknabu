// ****************************************************************************************
// NABU-LIB C Library - MAKE SOMETHING
// DJ Sures (c) 2024
// https://nabu.ca
// https://github.com/DJSures/NABU-LIB
//
// TRIMMED for rick-dangerous: this is a header-only library (NABU-LIB.c is
// #included at the bottom), so include it from EXACTLY ONE translation unit
// (main.c) with BIN_TYPE defined. Only what src/ uses is left -- see the
// header comment in NABU-LIB.c. Build-time DISABLE_* / DEBUG_VDP_INT / cursor
// options, the VDP text-mode state, the HCCA read/write helpers and the
// generic vdp_init() were all removed as unreferenced.
// **********************************************************************************************

#ifndef NABU_H
#define NABU_H
#define BIN_HOMEBREW 100
#define BIN_CPM 200
#define byte uint8_t

#ifndef BIN_TYPE
  #error A BIN_TYPE has not been specified (#define BIN_TYPE BIN_HOMEBREW before including NABU-LIB.h).
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

// I/O ports
__sfr __at 0xA0 IO_VDPDATA;
__sfr __at 0xA1 IO_VDPLATCH;

__sfr __at 0x40 IO_AYDATA;
__sfr __at 0x41 IO_AYLATCH;

__sfr __at 0x80 IO_HCCA;

__sfr __at 0x90 IO_KEYBOARD;
__sfr __at 0x91 IO_KEYBOARD_STATUS;

__sfr __at 0x00 IO_CONTROL;

#define CONTROL_ROMSEL     0x01
#define CONTROL_VDOBUF     0x02
#define CONTROL_STROBE     0x04
#define CONTROL_LED_CHECK  0x08
#define CONTROL_LED_ALERT  0x10
#define CONTROL_LED_PAUSE  0x20

// Shared with CP/M -- do not change
#define INTERUPT_VECTOR_MAP_MSB 0xff
#define INTERUPT_VECTOR_MAP_ADDRESS 0xff00

#define INT_MASK_HCCARX   0x80
#define INT_MASK_HCCATX   0x40
#define INT_MASK_KEYBOARD 0x20
#define INT_MASK_VDP      0x10

#define IOPORTA  0x0e
#define IOPORTB  0x0f

// HCCA RX ring buffer, written by isrHCCARX(). Only the write position is
// kept -- nothing in this project reads through NABU-LIB's hcca_read*().
volatile uint8_t _rxBuffer[256] = {0};
volatile uint8_t _rxBufferWritePos = 0;

// Keyboard ring buffer + joystick status, written by isrKeyboard()
volatile uint8_t _kbdBuffer[256]= {0};
volatile uint8_t _kbdBufferReadPos = 0;
volatile uint8_t _kbdBufferWritePos = 0;
volatile uint8_t _lastKeyboardIntVal = 0;
volatile uint8_t _joyStatus[4] = {0};

typedef enum JOYSTICKENUM {
  Joy_Left = 0b00000001,
  Joy_Down = 0b00000010,
  Joy_Right = 0b00000100,
  Joy_Up = 0b00001000,
  Joy_Button = 0b00010000,
};

enum VDP_COLORS {
  VDP_TRANSPARENT = 0,
  VDP_BLACK = 1,
  VDP_MED_GREEN = 2,
  VDP_LIGHT_GREEN = 3,
  VDP_DARK_BLUE = 4,
  VDP_LIGHT_BLUE = 5,
  VDP_DARK_RED = 6,
  VDP_CYAN = 7,
  VDP_MED_RED = 8,
  VDP_LIGHT_RED = 9,
  VDP_DARK_YELLOW = 10,
  VDP_LIGHT_YELLOW = 11,
  VDP_DARK_GREEN = 12,
  VDP_MAGENTA = 13,
  VDP_GRAY = 14,
  VDP_WHITE = 15
};

// System: ROM off, IM2 vectors for HCCA RX + keyboard, enable interrupts
void initNABULib(void);
inline void NABU_DisableInterrupts(void);
inline void NABU_EnableInterrupts(void);

// AY-3-8910
inline void ayWrite(uint8_t reg, uint8_t val);
inline uint8_t ayRead(uint8_t reg);

// Keyboard (also the joysticks, index 0 - 3)
uint8_t isKeyPressed(void);
uint8_t getChar(void);
inline uint8_t getJoyStatus(uint8_t joyNum);

// VDP
inline void vdp_setRegister(uint8_t registerIndex, uint8_t value);
inline void vdp_setWriteAddress(uint16_t address);
inline void vdp_setReadAddress(uint16_t address);
void vdp_clearVRAM(void);
// Fixed: Graphics II, 16x16 sprites, thirds split, black backdrop
void vdp_initG2Mode(void);

#include "NABU-LIB.c"

#endif
