#ifndef SWRT_INTERN_H
#define SWRT_INTERN_H

#include <exec/types.h>

#define G(o) ((struct Gadget *)(o))

/* IFF chunk IDs packed as 32-bit big-endian */
#define ID_FORM MAKE_ID('F','O','R','M')
#define ID_SWRT MAKE_ID('S','W','R','T')
#define ID_HYPH MAKE_ID('H','Y','P','H')
#define ID_ASCI MAKE_ID('A','S','C','I')
#define ID_DISP MAKE_ID('D','I','S','P')
#define ID_SPEL MAKE_ID('S','P','E','L')
#define ID_PICP MAKE_ID('P','I','C','P')
#define ID_LINP MAKE_ID('L','I','N','P')
#define ID_BOXP MAKE_ID('B','O','X','P')
#define ID_OVLP MAKE_ID('O','V','L','P')
#define ID_SWCL MAKE_ID('S','W','C','L')
#define ID_CHRS MAKE_ID('C','H','R','S')
#define ID_ATTR MAKE_ID('A','T','T','R')
#define ID_DOC  MAKE_ID('D','O','C',' ')
#define ID_SEC1 MAKE_ID('S','E','C','1')
#define ID_PAG3 MAKE_ID('P','A','G','3')
#define ID_PRN3 MAKE_ID('P','R','N','3')
#define ID_TBLK MAKE_ID('T','B','L','K')
#define ID_TXOB MAKE_ID('T','X','O','B')
#define ID_TABS MAKE_ID('T','A','B','S')
#define ID_RULE MAKE_ID('R','U','L','E')
#define ID_SHD1 MAKE_ID('S','H','D','1')
#define ID_TSSD MAKE_ID('T','S','S','D')
#define ID_FNTD MAKE_ID('F','N','T','D')
#define ID_FDTA MAKE_ID('F','D','T','A')
#define ID_PASD MAKE_ID('P','A','S','D')
#define ID_TOCD MAKE_ID('T','O','C','D')
#define ID_GINF MAKE_ID('G','I','N','F')
#define ID_BUTD MAKE_ID('B','U','T','D')
#define ID_LMST MAKE_ID('L','M','S','T')
#define ID_RMST MAKE_ID('R','M','S','T')
#define ID_SAVP MAKE_ID('S','A','V','P')
#define ID_ENDP MAKE_ID('E','N','D','P')
#define ID_PROV MAKE_ID('P','R','O','V')
#define ID_ILBM MAKE_ID('I','L','B','M')

#define SWRT_CHRS_ALLOC_INC 4096

struct SwrtData
{
    APTR    Pool;
    ULONG   Flags;
    STRPTR  TextBuffer;
    ULONG   TextBufferLen;
    ULONG   TextBufferAlloced;
};

#endif /* SWRT_INTERN_H */
