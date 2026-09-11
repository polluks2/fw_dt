/*
 * FinalWriter Text DataType for AmigaOS
 *
 * A BOOPSI DataType subclass of text.datatype that reads
 * FinalWriter document files (.fw) and renders them with
 * basic formatting support.
 *
 * Copyright (C) 2026  Stefan Haubenthal
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Compile with SAS/C:  sc link finalwriter_datatype.c TO finalwriter.datatype
 * Compile with GCC:    m68k-amigaos-gcc -noixemul -o finalwriter.datatype finalwriter_datatype.c
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/textclass.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <graphics/gfx.h>
#include <graphics/text.h>
#include <graphics/rastport.h>

#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/intuition_protos.h>
#include <clib/datatypes_protos.h>
#include <clib/graphics_protos.h>
#include <clib/alib_protos.h>

/* ------------------------------------------------------------------ */
/* Library definitions                                                 */
/* ------------------------------------------------------------------ */

#define LIBVERSION   1
#define LIBREVISION  0

static const char libName[]    = "finalwriter.datatype";
static const char libIdString[] = "finalwriter.datatype 1.0 (13 May 2026)";

struct FWLibBase {
    struct Library        lb;
    BPTR                  segList;
    struct Class         *fwClass;
    UWORD                 openCnt;
};

/* ------------------------------------------------------------------ */
/* Per-object instance data (for BOOPSI instance)                       */
/* ------------------------------------------------------------------ */

struct FWAttr {
    struct MinNode  node;
    ULONG           offset;
    UWORD           fontIdx;
    UBYTE           fontSize;
    UBYTE           style;      /* bit 0 = underline, bit 2 = bold, bit 3 = italic */
    UBYTE           flags;      /* 0 = normal, 1 = super, 17 = endnote */
    ULONG           charCount;
};

struct FWFont {
    struct MinNode  node;
    STRPTR          name;
};

struct FWInstance {
    struct MinNode  node;
    APTR            memPool;
    STRPTR          buffer;
    ULONG           bufferLen;
    struct List     attrList;
    struct List     fontList;
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static ULONG SwapLong(ULONG x)
{
    return (x >> 24) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24);
}

/* ------------------------------------------------------------------ */
/* File parser: reads SWRT chunks and fills TDTA_Buffer + attributes   */
/* ------------------------------------------------------------------ */

static BOOL ParseFile(struct FWLibBase *base, Class *cl, Object *o, BPTR fh)
{
    struct FWInstance *inst;
    UBYTE hdr4[4];
    BOOL success = FALSE;
    UBYTE *textBuf = NULL;
    ULONG textAlloc = 0, textPos = 0, totalChars = 0;
    struct TextFont *font = NULL;
    ULONG estLines, poolSize;

    inst = (struct FWInstance *)INST_DATA(cl, o);

    NewList(&inst->attrList);
    NewList(&inst->fontList);

    if (Read(fh, hdr4, 4) != 4)
    {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }

    /* Accept FORM/SWRT (real FinalWriter documents) as well as raw
     * SWRT chunk streams (fmt stays at first chunk header). */
    if (hdr4[0] == 'F' && hdr4[1] == 'O' && hdr4[2] == 'R' && hdr4[3] == 'M')
    {
        UBYTE sz[4];
        if (Read(fh, sz, 4) != 4 || Read(fh, hdr4, 4) != 4 ||
            hdr4[0] != 'S' || hdr4[1] != 'W' || hdr4[2] != 'R' || hdr4[3] != 'T')
        {
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            return FALSE;
        }
    }
    else if (!(hdr4[0] == 'S' && hdr4[1] == 'W' && hdr4[2] == 'R' && hdr4[3] == 'T'))
    {
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return FALSE;
    }

    /* parse chunks (tag:4 + size:4 + data:size) */
    while (1)
    {
        UBYTE hdr[8];
        LONG r;
        ULONG chkSize;

        r = Read(fh, hdr, 8);
        if (r != 8) break;

        chkSize = SwapLong(*(ULONG *)(hdr + 4));

        /* ATTR + CHRS pair (ATTR is 22 bytes, big-endian) */
        if (hdr[0] == 'A' && hdr[1] == 'T' && hdr[2] == 'T' && hdr[3] == 'R')
        {
            UBYTE ad[22];
            UBYTE chk2[8];
            ULONG charCnt;
            UWORD fontIdx;
            UBYTE fontSize;
            UBYTE style;
            UBYTE flags;
            ULONG chrsSize;

            if (Read(fh, ad, 22) != 22) break;

            charCnt  = ((ULONG)ad[0]  << 24) | ((ULONG)ad[1]  << 16) |
                       ((ULONG)ad[2]  <<  8) | (ULONG)ad[3];
            fontIdx  = ((UWORD)ad[4]  <<  8) | (UWORD)ad[5];
            fontSize = ad[7];
            style    = ad[9];
            flags    = ad[11];

            /* read CHRS tag + size */
            if (Read(fh, chk2, 8) != 8) break;
            chrsSize = SwapLong(*(ULONG *)(chk2 + 4));

            if (chk2[0] != 'C' || chk2[1] != 'H' || chk2[2] != 'R' || chk2[3] != 'S')
                break;

            if (charCnt == 0) charCnt = chrsSize;
            if (charCnt > chrsSize) charCnt = chrsSize;

            if (charCnt > 0)
            {
                struct FWAttr *attr;

                /* grow text buffer */
                if (textPos + charCnt + 1 > textAlloc)
                {
                    ULONG newSize = textPos + charCnt + 1024;
                    UBYTE *nb = AllocVec(newSize, MEMF_CLEAR);
                    if (!nb) { SetIoErr(ERROR_NO_FREE_STORE); break; }
                    if (textBuf) { CopyMem(textBuf, nb, textPos); FreeVec(textBuf); }
                    textBuf = nb;
                    textAlloc = newSize;
                }

                if (Read(fh, textBuf + textPos, charCnt) != (LONG)charCnt) break;
                textPos += charCnt;

                /* store attribute */
                attr = AllocVec(sizeof(struct FWAttr), MEMF_CLEAR);
                if (attr)
                {
                    attr->offset    = totalChars;
                    attr->fontIdx   = fontIdx;
                    attr->fontSize  = fontSize;
                    attr->style     = style;
                    attr->flags     = flags;
                    attr->charCount = charCnt;
                    AddTail(&inst->attrList, (struct Node *)attr);
                }

                totalChars += charCnt;
            }

            /* pad */
            if (chrsSize & 1) Seek(fh, 1, OFFSET_CURRENT);
        }
        /* FDTA - font name */
        else if (hdr[0] == 'F' && hdr[1] == 'D' && hdr[2] == 'T' && hdr[3] == 'A')
        {
            if (chkSize > 0 && chkSize < 256)
            {
                struct FWFont *fnt = AllocVec(sizeof(struct FWFont), MEMF_CLEAR);
                if (fnt)
                {
                    fnt->name = AllocVec(chkSize + 1, MEMF_CLEAR);
                    if (fnt->name)
                    {
                        Read(fh, fnt->name, chkSize);
                        fnt->name[chkSize] = 0;
                    }
                    AddTail(&inst->fontList, (struct Node *)fnt);
                }
                if (chkSize & 1) Seek(fh, 1, OFFSET_CURRENT);
            }
            else
                Seek(fh, chkSize + (chkSize & 1), OFFSET_CURRENT);
        }
        else
        {
            /* skip any other chunk, incl. RULE */
            Seek(fh, chkSize + (chkSize & 1), OFFSET_CURRENT);
        }
    }

    if (textBuf && totalChars > 0)
    {
        inst->buffer    = (STRPTR)textBuf;
        inst->bufferLen = totalChars;

        estLines = (totalChars / 80) + 1;
        if (estLines > 500) estLines = 500;
        poolSize = sizeof(struct Line) * estLines + 256;

        inst->memPool = CreatePool(MEMF_SHARED | MEMF_CLEAR, poolSize, poolSize);
        if (!inst->memPool)
        {
            SetIoErr(ERROR_NO_FREE_STORE);
            FreeVec(textBuf);
            return FALSE;
        }

        SetDTAttrs(o,
            TDTA_Buffer,    (ULONG)inst->buffer,
            TDTA_BufferLen, inst->bufferLen,
            DTA_ObjName,    (ULONG)"FinalWriter Document",
            TAG_END);

        GetDTAttrs(o, DTA_TextFont, &font, TAG_END);
        if (!font)
        {
            struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
            font = OpenFont(&ta);
            if (font)
            {
                SetDTAttrs(o, DTA_TextFont, (ULONG)font, TAG_END);
                CloseFont(font);
            }
        }

        GetDTAttrs(o, DTA_TextFont, &font, TAG_END);
        if (font)
        {
            ULONG nomH = ((totalChars / 80) + 1) * font->tf_YSize;
            if (nomH < 100) nomH = 100;
            if (nomH > 32767) nomH = 32767;
            SetDTAttrs(o,
                DTA_NominalHoriz, 640,
                DTA_NominalVert,  nomH,
                TAG_END);
        }

        success = TRUE;
    }
    else
    {
        if (textBuf) FreeVec(textBuf);
        SetIoErr(ERROR_BAD_DATA);
    }

    return success;
}

/* ------------------------------------------------------------------ */
/* Layout method: break text buffer into Line structures               */
/* ------------------------------------------------------------------ */

static ULONG LayoutMethod(struct FWLibBase *base, Class *cl, Object *o,
                           struct gpLayout *gpl)
{
    struct DTSpecialInfo *si = (struct DTSpecialInfo *)GADGET(o)->SpecialInfo;
    struct FWInstance *inst = (struct FWInstance *)INST_DATA(cl, o);
    struct List *lineList = NULL;
    struct IBox *domain = NULL;
    struct TextFont *font = NULL;
    struct TextAttr *tattr = NULL;
    STRPTR buffer = NULL;
    ULONG bufferLen = 0;
    STRPTR title = NULL;
    BOOL wordWrap = FALSE;
    ULONG visible = 0, total = 0;
    ULONG offset = 0, yoffset = 0;
    ULONG tabSpace;
    BOOL lineFeed = FALSE, newSeg = FALSE, abortFlag = FALSE;
    struct Line *line;
    ULONG bsig = 0;
    ULONG nomWidth = 640, nomHeight = 200;
    UBYTE fgPen = 1, bgPen = 0;
    struct RastPort trp;

    if (!inst || !inst->buffer) return 0;

    if (GetDTAttrs(o,
        DTA_TextAttr,   (ULONG)&tattr,
        DTA_TextFont,   (ULONG)&font,
        DTA_Domain,     (ULONG)&domain,
        DTA_ObjName,    (ULONG)&title,
        TDTA_Buffer,    (ULONG)&buffer,
        TDTA_BufferLen, (ULONG)&bufferLen,
        TDTA_LineList,  (ULONG)&lineList,
        TDTA_WordWrap,  (ULONG)&wordWrap,
        TAG_END) < 8)
        return 0;

    ObtainSemaphore(&si->si_Lock);

    if (buffer && bufferLen && font)
    {
        InitRastPort(&trp);
        SetFont(&trp, font);

        nomHeight = 24 * font->tf_YSize;
        nomWidth  = 80  * font->tf_XSize;
        tabSpace  = font->tf_XSize * 8;

        if (wordWrap || gpl->gpl_Initial)
        {
            ULONG i;
            ULONG attrEnd;
            ULONG segLen;
            ULONG anchor;
            UBYTE style;
            UBYTE flags;
            ULONG j;

            /* clear old line list */
            if (lineList)
            {
                while ((line = (struct Line *)RemHead(lineList)))
                    FreePooled(inst->memPool, line, sizeof(struct Line));
            }

            for (i = 0; i < bufferLen && bsig == 0 && !abortFlag; )
            {
                /* find attribute for current position */
                attrEnd = bufferLen;
                style = 0;
                flags = 0;

                {
                    struct FWAttr *fa = (struct FWAttr *)inst->attrList.lh_Head;
                    while (fa)
                    {
                        if (fa->offset <= i && (fa->offset + fa->charCount) > i)
                        {
                            style   = fa->style;
                            flags   = fa->flags;
                            attrEnd = fa->offset + fa->charCount;
                            break;
                        }
                        fa = (struct FWAttr *)fa->node.ln_Succ;
                    }
                }

                segLen = 0;
                anchor = i;

                for (j = i; j < bufferLen && j < attrEnd; j++)
                {
                    /* CR / LF / CR+LF */
                    if (buffer[j] == 13 || buffer[j] == 10)
                    {
                        newSeg = lineFeed = TRUE;
                        segLen = j - anchor;
                        i = j + 1;
                        if (buffer[j] == 13 && j + 1 < bufferLen && buffer[j + 1] == 10)
                            i = j + 2;  /* consume CRLF pair */
                        break;
                    }
                    /* form feed / page break */
                    else if (buffer[j] == 12)
                    {
                        newSeg = lineFeed = TRUE;
                        segLen = j - anchor;
                        i = j + 1;
                        break;
                    }
                    /* tab */
                    else if (buffer[j] == 9)
                    {
                        if (!newSeg) segLen = j - anchor;
                        newSeg = TRUE;
                        i = j + 1;
                        break;
                    }
                    else
                    {
                        ULONG sw = TextLength(&trp, &buffer[anchor], (j - anchor) + 1);
                        if (domain && wordWrap && (offset + sw > (ULONG)domain->Width))
                        {
                            /* word break */
                            ULONG k;
                            for (k = j; k > anchor; k--)
                            {
                                if (buffer[k] == ' ')
                                {
                                    segLen = k - anchor;
                                    newSeg = lineFeed = TRUE;
                                    i = k + 1;
                                    break;
                                }
                            }
                            if (!newSeg)
                            {
                                segLen = j - anchor;
                                newSeg = lineFeed = TRUE;
                                i = j;
                            }
                            break;
                        }
                    }
                }

                if (!newSeg)
                {
                    segLen = attrEnd - anchor;
                    i = attrEnd;
                }

                if (segLen > 0 || lineFeed)
                {
                    line = AllocPooled(inst->memPool, sizeof(struct Line));
                    if (line)
                    {
                        ULONG sw;
                        ULONG ls;

                        sw = TextLength(&trp, &buffer[anchor], segLen);
                        ls = FS_NORMAL;
                        if (style & 1) ls |= FSF_UNDERLINED;
                        if (style & 4) ls |= FSF_BOLD;
                        if (style & 8) ls |= FSF_ITALIC;
                        if (flags == 1 || flags == 17) ls |= FSF_SUPERSCRIPT;

                        line->ln_Text    = &buffer[anchor];
                        line->ln_TextLen = segLen;
                        line->ln_XOffset = offset;
                        line->ln_YOffset = yoffset + font->tf_Baseline;
                        line->ln_Width   = sw;
                        line->ln_Height  = font->tf_YSize;
                        line->ln_Flags   = (lineFeed) ? LNF_LF : 0;
                        line->ln_FgPen   = fgPen;
                        line->ln_BgPen   = bgPen;
                        line->ln_Style   = ls;
                        line->ln_Data    = NULL;

                        AddTail(lineList, (struct Node *)line);

                        if (lineFeed)
                        {
                            yoffset += font->tf_YSize;
                            offset = 0;
                            total++;
                        }
                        else
                        {
                            offset += sw;
                        }
                    }
                    else
                    {
                        abortFlag = TRUE;
                    }
                }

                newSeg = lineFeed = FALSE;
                bsig = CheckSignal(SIGBREAKF_CTRL_C);
            }
        }
        else
        {
            total = si->si_TotVert;
        }

        visible = (domain && domain->Height > 0) ? domain->Height / font->tf_YSize : 10;

        si->si_VertUnit  = font->tf_YSize;
        si->si_VisVert   = visible;
        si->si_TotVert   = total;
        si->si_HorizUnit = 1;
        si->si_VisHoriz  = (domain) ? domain->Width : 640;
        si->si_TotHoriz  = (domain) ? domain->Width : 640;
    }

    ReleaseSemaphore(&si->si_Lock);

    if (bsig == 0 && si->si_TotVert > 0)
    {
        SetDTAttrs(o,
            GA_ID,              GADGET(o)->GadgetID,
            DTA_VisibleVert,    visible,
            DTA_TotalVert,      total,
            DTA_NominalVert,    nomHeight,
            DTA_VertUnit,       font ? font->tf_YSize : 8,
            DTA_VisibleHoriz,   domain ? domain->Width : 640,
            DTA_TotalHoriz,     domain ? domain->Width : 640,
            DTA_NominalHoriz,   nomWidth,
            DTA_HorizUnit,      1,
            DTA_Title,          (ULONG)title,
            DTA_Busy,           FALSE,
            DTA_Sync,           TRUE,
            TAG_END);
    }

    return total;
}

/* ------------------------------------------------------------------ */
/* BOOPSI dispatcher                                                   */
/* ------------------------------------------------------------------ */

static ULONG Dispatcher(Class *cl, Object *o, Msg msg)
{
    struct FWLibBase *base = (struct FWLibBase *)cl->cl_UserData;
    ULONG retval = 0;

    switch (msg->MethodID)
    {
        case OM_NEW:
        {
            BPTR fh;

            retval = DoSuperMethodA(cl, o, msg);
            if (!retval) break;

            GetDTAttrs((Object *)retval, DTA_Handle, &fh, TAG_END);
            if (fh && ParseFile(base, cl, (Object *)retval, fh))
                break;

            /* failed - coerce dispose */
            CoerceMethod(cl, (Object *)retval, OM_DISPOSE);
            retval = 0;
            break;
        }

        case OM_UPDATE:
        case OM_SET:
        {
            retval = DoSuperMethodA(cl, o, msg);
            if (retval && OCLASS(o) == cl)
            {
                struct RastPort *rp;
                rp = ObtainGIRPort(((struct opSet *)msg)->ops_GInfo);
                if (rp)
                {
                    DoRender(o, ((struct opSet *)msg)->ops_GInfo, GREDRAW_UPDATE);
                    ReleaseGIRPort(rp);
                }
                retval = 0;
            }
            break;
        }

        case GM_LAYOUT:
        {
            SetDTAttrs(o,
                GA_ID, GADGET(o)->GadgetID,
                DTA_Busy, TRUE,
                TAG_END);
            retval = DoSuperMethodA(cl, o, msg);
            retval += DoAsyncLayout(o, (struct gpLayout *)msg);
            break;
        }

        case DTM_PROCLAYOUT:
            SetDTAttrs(o,
                GA_ID, GADGET(o)->GadgetID,
                DTA_Busy, TRUE,
                TAG_END);
            retval = DoSuperMethodA(cl, o, msg);
            break;

        case DTM_ASYNCLAYOUT:
            retval = LayoutMethod(base, cl, o, (struct gpLayout *)msg);
            break;

        case OM_DISPOSE:
        {
            struct FWInstance *inst = (struct FWInstance *)INST_DATA(cl, o);
            struct FWAttr *fa;
            struct FWFont *ff;
            struct List *ll;

            if (inst)
            {
                while ((fa = (struct FWAttr *)RemHead(&inst->attrList)))
                    FreeVec(fa);
                while ((ff = (struct FWFont *)RemHead(&inst->fontList)))
                {
                    if (ff->name) FreeVec(ff->name);
                    FreeVec(ff);
                }
                if (GetDTAttrs(o, TDTA_LineList, &ll, TAG_END) && ll)
                    NewList(ll);
                if (inst->buffer) FreeVec(inst->buffer);
                if (inst->memPool) DeletePool(inst->memPool);
            }
            retval = DoSuperMethodA(cl, o, msg);
            break;
        }

        default:
            retval = DoSuperMethodA(cl, o, msg);
            break;
    }

    return retval;
}

/* ------------------------------------------------------------------ */
/* Library functions                                                    */
/* ------------------------------------------------------------------ */

static struct Library *LibNull(struct FWLibBase *base)    { return (struct Library *)base; }
static struct Library *LibOpen(struct FWLibBase *base)
{
    base->openCnt++;
    base->lb.lib_OpenCnt++;
    return (struct Library *)base;
}
static struct Library *LibClose(struct FWLibBase *base)
{
    if (base->openCnt > 0) base->openCnt--;
    if (base->lb.lib_OpenCnt > 0) base->lb.lib_OpenCnt--;
    return (base->openCnt == 0) ? NULL : (struct Library *)base;
}
static struct Library *LibExpunge(struct FWLibBase *base)
{
    if (base->openCnt) return (struct Library *)base->segList;
    if (base->fwClass)
    {
        CloseClass(base->fwClass);
        base->fwClass = NULL;
    }
    Remove((struct Node *)base);
    FreeMem(base, sizeof(struct FWLibBase));
    return NULL;
}

static ULONG LibDispatch(struct FWLibBase *base)
{
    return 0;
}

/* ------------------------------------------------------------------ */
/* Auto-init: called once when library is first loaded                  */
/* ------------------------------------------------------------------ */

static struct Library *LibInit(struct FWLibBase *base, BPTR segList,
                                struct ExecBase *sysBase)
{
    struct Class *superClass;

    base->lb.lib_Node.ln_Type  = NT_LIBRARY;
    base->lb.lib_Node.ln_Name  = (STRPTR)libName;
    base->lb.lib_Flags         = LIBF_SUMUSED | LIBF_CHANGED;
    base->lb.lib_Version       = LIBVERSION;
    base->lb.lib_Revision      = LIBREVISION;
    base->lb.lib_IdString      = (STRPTR)libIdString;
    base->segList              = segList;
    base->openCnt              = 0;
    base->fwClass              = NULL;

    superClass = OpenClass((STRPTR)"text.datatype", 1, NULL);
    if (!superClass) return NULL;

    base->fwClass = MakeClass(1, 0,
        (STRPTR)"finalwriter.datatype",
        superClass,
        sizeof(struct FWInstance),
        (void *)Dispatcher,
        NULL, NULL, NULL);

    CloseClass(superClass);

    if (!base->fwClass) return NULL;

    base->fwClass->cl_UserData = (APTR)base;

    return (struct Library *)base;
}

/* ------------------------------------------------------------------ */
/* Resident structure + function table for auto-init                    */
/* ------------------------------------------------------------------ */

static const ULONG funcTable[] =
{
    (ULONG)LibNull,
    (ULONG)LibOpen,
    (ULONG)LibClose,
    (ULONG)LibExpunge,
    (ULONG)LibNull,
    0,
    (ULONG)LibDispatch,
    0
};

#ifdef __GNUC__
#ifdef __GNUC__
static const struct Resident resident __attribute__((section("resident"))) =
#else
#pragma section resident, __far
static const struct Resident resident =
#endif
{
    RTC_MATCHWORD,
    (struct Resident *)&resident,
    (struct Resident *)&resident + 1,
    RTF_AUTOINIT | RTF_NATIVE,
    LIBVERSION,
    NT_LIBRARY,
    -60,
    (STRPTR)libName,
    (STRPTR)libIdString,
    (APTR)funcTable,
    (APTR)LibInit
};
#ifdef __SASC
#pragma section
#endif
