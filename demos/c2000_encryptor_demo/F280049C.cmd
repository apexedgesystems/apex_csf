/* ============================================================================
 * F280049C.cmd - Linker command file for TMS320F280049C (flash execution)
 *
 * Based on C2000Ware 28004x_generic_flash_lnk.cmd with minimal changes
 * for the c2000_encryptor_demo application.
 *
 * Reference: TMS320F28004x Technical Reference Manual (SPRUI33)
 * ============================================================================ */

MEMORY
{
PAGE 0 :
   BEGIN             : origin = 0x080000, length = 0x000002
   RAMM0             : origin = 0x0000F6, length = 0x00030A
   RAMLS0            : origin = 0x008000, length = 0x000800
   RAMLS1            : origin = 0x008800, length = 0x000800
   RAMLS2            : origin = 0x009000, length = 0x000800
   RAMLS3            : origin = 0x009800, length = 0x000800
   RAMLS4            : origin = 0x00A000, length = 0x000800
   RESET             : origin = 0x3FFFC0, length = 0x000002

   /* Flash Bank 0 (16 x 4 KB sectors) */
   FLASH_BANK0_SEC0  : origin = 0x080002, length = 0x000FFE
   FLASH_BANK0_SEC1  : origin = 0x081000, length = 0x001000
   FLASH_BANK0_SEC2  : origin = 0x082000, length = 0x001000
   FLASH_BANK0_SEC3  : origin = 0x083000, length = 0x001000
   FLASH_BANK0_SEC4  : origin = 0x084000, length = 0x001000
   FLASH_BANK0_SEC5  : origin = 0x085000, length = 0x001000
   FLASH_BANK0_SEC6  : origin = 0x086000, length = 0x001000
   FLASH_BANK0_SEC7  : origin = 0x087000, length = 0x001000
   FLASH_BANK0_SEC8  : origin = 0x088000, length = 0x001000
   FLASH_BANK0_SEC9  : origin = 0x089000, length = 0x001000
   FLASH_BANK0_SEC10 : origin = 0x08A000, length = 0x001000
   FLASH_BANK0_SEC11 : origin = 0x08B000, length = 0x001000
   FLASH_BANK0_SEC12 : origin = 0x08C000, length = 0x001000
   FLASH_BANK0_SEC13 : origin = 0x08D000, length = 0x001000
   FLASH_BANK0_SEC14 : origin = 0x08E000, length = 0x001000
   FLASH_BANK0_SEC15 : origin = 0x08F000, length = 0x001000

   /* Flash Bank 1 (16 x 4 KB sectors) */
   FLASH_BANK1_SEC0  : origin = 0x090000, length = 0x001000
   FLASH_BANK1_SEC1  : origin = 0x091000, length = 0x001000
   FLASH_BANK1_SEC2  : origin = 0x092000, length = 0x001000
   FLASH_BANK1_SEC3  : origin = 0x093000, length = 0x001000
   FLASH_BANK1_SEC4  : origin = 0x094000, length = 0x001000
   FLASH_BANK1_SEC5  : origin = 0x095000, length = 0x001000
   FLASH_BANK1_SEC6  : origin = 0x096000, length = 0x001000
   FLASH_BANK1_SEC7  : origin = 0x097000, length = 0x001000
   FLASH_BANK1_SEC8  : origin = 0x098000, length = 0x001000
   FLASH_BANK1_SEC9  : origin = 0x099000, length = 0x001000
   FLASH_BANK1_SEC10 : origin = 0x09A000, length = 0x001000
   FLASH_BANK1_SEC11 : origin = 0x09B000, length = 0x001000
   FLASH_BANK1_SEC12 : origin = 0x09C000, length = 0x001000
   FLASH_BANK1_SEC13 : origin = 0x09D000, length = 0x001000
   FLASH_BANK1_SEC14 : origin = 0x09E000, length = 0x001000
   FLASH_BANK1_SEC15 : origin = 0x09F000, length = 0x000FF0

PAGE 1 :
   BOOT_RSVD         : origin = 0x000002, length = 0x0000F1
   RAMM1             : origin = 0x000400, length = 0x0003F8
   RAMLS5            : origin = 0x00A800, length = 0x000800
   RAMLS6            : origin = 0x00B000, length = 0x000800
   RAMLS7            : origin = 0x00B800, length = 0x000800
   RAMGS0            : origin = 0x00C000, length = 0x002000
   RAMGS1            : origin = 0x00E000, length = 0x002000
   RAMGS2            : origin = 0x010000, length = 0x002000
   RAMGS3            : origin = 0x012000, length = 0x001FF8
}

SECTIONS
{
   codestart    : > BEGIN,            PAGE = 0, ALIGN(4)
   .text        : >> FLASH_BANK0_SEC2 | FLASH_BANK0_SEC3 | FLASH_BANK0_SEC4 | FLASH_BANK0_SEC5,
                                      PAGE = 0, ALIGN(4)
   .cinit       : > FLASH_BANK0_SEC1, PAGE = 0, ALIGN(4)
   .switch      : > FLASH_BANK0_SEC1, PAGE = 0, ALIGN(4)
   .reset       : > RESET,            PAGE = 0, TYPE = DSECT

   .stack       : > RAMM1,            PAGE = 1

   .pinit       : > FLASH_BANK0_SEC1, PAGE = 0, ALIGN(4)
   .ebss        : > RAMLS5,           PAGE = 1
   .esysmem     : > RAMLS5,           PAGE = 1
   .cio         : > RAMLS0,           PAGE = 0
   .econst      : > FLASH_BANK0_SEC6, PAGE = 0, ALIGN(4)

   ramgs0       : > RAMGS0,           PAGE = 1
   ramgs1       : > RAMGS1,           PAGE = 1

   .TI.ramfunc  : LOAD = FLASH_BANK0_SEC1,
                  RUN = RAMLS0,
                  LOAD_START(_RamfuncsLoadStart),
                  LOAD_SIZE(_RamfuncsLoadSize),
                  LOAD_END(_RamfuncsLoadEnd),
                  RUN_START(_RamfuncsRunStart),
                  RUN_SIZE(_RamfuncsRunSize),
                  RUN_END(_RamfuncsRunEnd),
                  PAGE = 0, ALIGN(4)
}
