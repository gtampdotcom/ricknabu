;
;	Startup for Nabu, a circa 1981 Z80 based computer.
;

    module  nabu_crt0


;--------
; Include zcc_opt.def to access defines etc, dynamically made at build time.
;--------

    defc    crt0 = 1
    INCLUDE "zcc_opt.def"

;--------
; Some scope definitions
;--------

    EXTERN  _main           ;main() is always external to crt0 code
    PUBLIC  __Exit          ;jp'd to by exit()
    PUBLIC  l_dcal          ;jp(hl) - used by compiler to jump indirect.

IFNDEF NABU_BARE_ASM
    ; Subtype "Default" and non-bare others have console output and input.
    defc    CONSOLE_COLUMNS = 32
IF !DEFINED_CONSOLE_ROWS
    defc    CONSOLE_ROWS = 24
ENDIF
    defc    CRT_KEY_DEL = 127

    ; Define the audio chip ports for some reason, not used, likely leftover
    ; copy paste from the lm80c target.
    PUBLIC  PSG_AY_REG
    PUBLIC  PSG_AY_DATA
    defc    PSG_AY_REG = $40
    defc    PSG_AY_DATA = $41

    EXTERN  cpm_platform_init
    EXTERN  vdp_set_mode

    defc    TAR__clib_exit_stack_size = 0 ; Disable atexit() functionality.
    defc    TAR__fputc_cons_generic = 1 ; Has console text output library.
ENDIF ; NDEF NABU_BARE_ASM

    ; Put the stack below $ff00, interrupt table will be at $ff00 and above.
    defc    TAR__register_sp = $ff00

IFNDEF CRT_ORG_CODE
    ; No longer have to start where we get loaded into memory by the boot ROM
    ; (usually $140D, but other ROMs vary) since we now turn off the ROM and
    ; relocate the program to lower memory (won't work for moving higher).
    ; For programs that expect to start higher, add something like this to your
    ; makefile (use decimal numbers to avoid problems) when invoking zcc:
    ;   -pragma-define:CRT_ORG_CODE=4096
    defc    CRT_ORG_CODE = 0x0000
ENDIF ; CRT_ORG_CODE

IFNDEF NABU_BARE_ASM
IFNDEF CLIB_DEFAULT_SCREEN_MODE
    ; Sets a VDP screen mode.
    defc    CLIB_DEFAULT_SCREEN_MODE = 2
ENDIF
ENDIF ; NDEF NABU_BARE_ASM

    INCLUDE "crt/classic/crt_rules.inc"

    org     CRT_ORG_CODE

    ; Three bytes of unused stuff, Nabu's ROM loader jumps into code just past
    ; them, so put in 0 = NOP.  May have been used for a 24 bit size of the
    ; segment or something else NABU Networky.
    defb    0,0,0

    ; Relocate the loaded program and data to the actual origin location, since
    ; the NABU ROMs load the segment somewhere after the ROMs, and it varies
    ; from one ROM version to another!  The most common ROM loads at $140d.
    ; So our code here needs to be position independent.  It works by writing
    ; some code to near the end of RAM ($FFE0) to get the program counter, and
    ; do an ldir to move the program.

    defc    IO_CONTROL = $0 ; I/O address of the control register.
    defc    CONTROL_ROMSEL = $01 ; Bit which controls ROM enable.
    defc    CONTROL_VDOBUF = $02 ; Bit which controls video output choice.
    defc    NABU_RELOC_STUB_DESTINATION = $FFE0 ; Should be above temp stack.
    defc    NABU_RELOC_TEMP_STACK = $FF00
    EXTERN  __BSS_END_tail
    EXTERN  __DATA_END_tail

nabu_relocate:
    di	; We're not ready to handle interrupts, need an interrupt table etc.
    ld  sp, NABU_RELOC_TEMP_STACK ; Temporary stack, away from loaded code.

    ; Switch out boot ROM so RAM is visible, connect VDP to video output.
    ld  a, CONTROL_ROMSEL | CONTROL_VDOBUF
    out (IO_CONTROL), a

    ; Get the current program counter, where in memory are we?
    ld  hl, NABU_RELOC_STUB_DESTINATION
    ld  (hl), $e1 ; pop hl opcode.
    inc hl
    ld  (hl), $e9 ; jp (hl) opcode.
    call NABU_RELOC_STUB_DESTINATION ; Returns with program counter in HL.
nabu_reloc_pc: ; HL points to nabu_reloc_pc address as loaded in memory.

    ; Write a little program stub (to move the main program around) into RAM at
    ; a known memory addresses, hopefully above where our program was loaded
    ; into memory.  Note that it can be loaded up to 8K higher, based on maximum
    ; 8K ROM size starting at location zero assuming the ROM loader isn't silly.

    ld   bc, nabu_reloc_pc-CRT_ORG_CODE
    and  a, a ; Clear the carry flag.
    sbc  hl, bc ; HL set to address of origin of this program, after loading.
    push hl ; Useful, save load origin for later.
    ld   bc, nabu_reloc_stub_start-CRT_ORG_CODE
    add  hl, bc ; Get address of our stub code.
    ld   de, NABU_RELOC_STUB_DESTINATION ; Desired beginning of program in RAM.
    ld   bc, nabu_reloc_stub_end-nabu_reloc_stub_start ; Size of our stub code.
    ldir

    ; Set up the registers for an ldir to move the program down in memory.
    ; Doesn't work for moving it up in memory.  When done, jumps to start: in
    ; the moved code.
    pop  hl ; Address of origin of this program, as loaded in RAM.
    ld   de, CRT_ORG_CODE ; Desired beginning of program in RAM.
    ld   bc, __DATA_END_tail-CRT_ORG_CODE ; Size of the real (non-BSS) program content -- BSS is zeroed separately by crt0_init, and moving only this much lets the file leave out the BSS placeholder bytes (CRT_TRIM_BSS=1) without the relocation copy reading past what was actually loaded.
    jp   NABU_RELOC_STUB_DESTINATION+nabu_reloc_stub_ldir-nabu_reloc_stub_start

nabu_reloc_stub_start:
nabu_reloc_stub_ldir:
    ldir
    jp  start ; Continue on with the rest of the C runtime initialisation.
nabu_reloc_stub_reset:
    ld  a, 0 ; The control register is zero on reset.
    out (IO_CONTROL), a
    rst  0 ; Start at location 0 in the ROM, likely the boot code.
nabu_reloc_stub_end:

start:
    INCLUDE "crt/classic/crt_init_sp.inc" ; Sets the stack pointer.
    ; Set interrupt system mode 2, with interrupt table at $ff00.
    di
    ld      a,$ff
    ld      i,a
    im      2

    ; Setup BSS memory and perform other initialisation
    call    crt0_init

IFNDEF NABU_BARE_ASM
    ; Code is shared with CP/M. This is a noop, but pulls in code
    ; into crt0_init and crt0_exit
    ; REVERTED (rick-dangerous): tried dropping this call to save 253
    ; bytes ("genuinely dead code" -- WRONG, see TOOLCHAIN.md fix 3's own
    ; corrected writeup) -- broke boot outright, never reached the title
    ; screen. cpm_platform_init's pulled-in asm_interrupt_init module is
    ; NOT dead weight despite the "noop" comment above; something in it
    ; (most likely real IM2 interrupt-vector-table setup, since removing
    ; it also dropped nabu_set_interrupts/nabu_enable_interrupt/
    ; asm_interrupt_handler from the link -- exactly what IM2 mode, set
    ; a few lines above this call, needs a real handler installed for)
    ; is load-bearing. Restored.
    call    cpm_platform_init
ENDIF ; NDEF NABU_BARE_ASM

    INCLUDE "crt/classic/crt_init_atexit.inc"

IFNDEF NABU_BARE_ASM
    INCLUDE "crt/classic/tms99x8/tms99x8_mode_init.inc"
ENDIF ; NDEF NABU_BARE_ASM

    INCLUDE "crt/classic/crt_init_heap.inc"

IFNDEF NABU_BARE_ASM
    ; Turn on or off interrupts as specified by __crt_enable_eidi flags.
    INCLUDE "crt/classic/crt_init_eidi.inc"
ENDIF ; NDEF NABU_BARE_ASM

    call    _main

__Exit: ; Re-enters here if user program called exit() rather than returning.
    push    hl ; Save exit code.
    call    crt0_exit

IFNDEF NABU_BARE_ASM
    INCLUDE "crt/classic/tms99x8/tms99x8_mode_exit.inc"
ENDIF ; NDEF NABU_BARE_ASM

    pop     bc ; Exit code.

IFNDEF NABU_BARE_ASM
    INCLUDE "crt/classic/crt_exit_eidi.inc"
ENDIF ; NDEF NABU_BARE_ASM

    ; Switch the ROM bank in and jump to the location zero reset code.  Since
    ; this code may be in the RAM area used by the ROM, use a stub in high
    ; memory to do the work.
NabuReboot:
    di
    im  0  ; Use stock interrupt mode like a hardware reset would do.
    ld  hl, nabu_reloc_stub_start
    ld  de, NABU_RELOC_STUB_DESTINATION
    ld  bc, nabu_reloc_stub_end-nabu_reloc_stub_start
    ldir
    jp  NABU_RELOC_STUB_DESTINATION+nabu_reloc_stub_reset-nabu_reloc_stub_start

l_dcal:
    jp      (hl)

IFNDEF NABU_BARE_ASM
    ; Selects print formats and stdio functions to use.  But not in bare mode!
    INCLUDE "crt/classic/crt_runtime_selection.inc"

    ; And include handling disabling screenmodes
    INCLUDE "crt/classic/tms99x8/tms99x8_mode_disable.inc"

    ; Include code to access the HCCA network storage device.
    INCLUDE "target/nabu/classic/nabu_hccabuf.asm"
ENDIF ; NDEF NABU_BARE_ASM

    INCLUDE "crt/classic/crt_section.inc"

