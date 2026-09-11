/*
    Copyright (C) 1995-2025, The AROS Development Team. All rights reserved.

    FinalWriter Document (FORM/SWRT) datatype for AROS.
    Decodes FinalWriter IFF files and renders them as text.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/dosh.h>
#include <graphics/gfxbase.h>
#include <graphics/rpattr.h>
#include <intuition/imageclass.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/textclass.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/iffparse.h>
#include <proto/datatypes.h>

#include "swrt_intern.h"

#undef DEBUG
#define DEBUG 0
#include <aros/debug.h>

#include <aros/symbolsets.h>
ADD2LIBS("datatypes/text.datatype", 0, struct Library *, TextBase)

/*****************************************************************************/

static IPTR NotifyAttrChanges(Object *o, VOID *ginfo, ULONG flags, Tag tag1, ...)
{
    AROS_SLOWSTACKTAGS_PRE(tag1)
    retval = DoMethod(o, OM_NOTIFY, AROS_SLOWSTACKTAGS_ARG(tag1), (IPTR)ginfo, flags);
    AROS_SLOWSTACKTAGS_POST
}

/*****************************************************************************/

/* IFF chunk header: 4-byte ID + 4-byte size (big-endian) */
struct IFFChunk
{
    UBYTE id[4];
    UBYTE size[4];
};

static ULONG ReadIFFLong(UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
           ((ULONG)p[2] << 8)  | (ULONG)p[3];
}

static void WriteIFFLong(UBYTE *p, ULONG v)
{
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8)  & 0xff;
    p[3] = v & 0xff;
}

/*****************************************************************************/
/* Append text to the buffer, expanding as needed */

static BOOL SwrtAppendText(struct SwrtData *data, UBYTE *text, ULONG len)
{
    ULONG needed = data->TextBufferLen + len + 1;

    if (needed > data->TextBufferAlloced)
    {
        ULONG newalloc = data->TextBufferAlloced;
        UBYTE *newbuf;

        while (newalloc < needed)
            newalloc += SWRT_CHRS_ALLOC_INC;

        newbuf = AllocPooled(data->Pool, newalloc);
        if (!newbuf)
            return FALSE;

        if (data->TextBuffer && data->TextBufferLen > 0)
        {
            CopyMem(data->TextBuffer, newbuf, data->TextBufferLen);
            FreePooled(data->Pool, data->TextBuffer, data->TextBufferAlloced);
        }

        data->TextBuffer = newbuf;
        data->TextBufferAlloced = newalloc;
    }

    CopyMem(text, data->TextBuffer + data->TextBufferLen, len);
    data->TextBufferLen += len;
    data->TextBuffer[data->TextBufferLen] = '\0';

    return TRUE;
}

static BOOL SwrtAppendChar(struct SwrtData *data, UBYTE ch)
{
    return SwrtAppendText(data, &ch, 1);
}

/*****************************************************************************/
/* DTM_PROCVAL - probe the file to see if it's a FinalWriter document */

static IPTR Swrt__DTM_PROCVAL(Class *cl, Object *o, struct dtProbe *msg)
{
    struct IFFChunk form;
    ULONG chunk0;

    D(bug("Swrt_DTM_PROCVAL: probing\n"));

    /* Must have at least a 4-byte ID to probe */
    if (msg->dtp_Size < 4)
        return DTPROBE_MAYBE;

    /* Check first word: FORM/SWRT, raw SWRT, or unknown */
    CopyMem(msg->dtp_Buffer, &form, 8);
    chunk0 = ReadIFFLong(form.id);
    if (chunk0 == ID_SWRT)
        return DTPROBE_MAYBE | DTPROBE_SPECIFIC;

    if (chunk0 == ID_FORM)
    {
        /* FORM with type SWRT at offset 8 */
        if (msg->dtp_Size < 12)
            return DTPROBE_MAYBE;
        if (ReadIFFLong((UBYTE *)msg->dtp_Buffer + 8) != ID_SWRT)
            return DTPROBE_MAYBE;

        D(bug("Swrt_DTM_PROCVAL: matched FORM/SWRT\n"));

        /* Also check for a HYPH chunk at the expected position */
        if (msg->dtp_Size >= 20)
        {
            ULONG hyphid = ReadIFFLong((UBYTE *)msg->dtp_Buffer + 12);
            if (hyphid == ID_HYPH)
                return DTPROBE_MAYBE | DTPROBE_SPECIFIC;
        }
    }

    return DTPROBE_MAYBE;
}

/*****************************************************************************/
/* OM_NEW - open and parse the FinalWriter file */

static IPTR Swrt__OM_NEW(Class *cl, Object *o, struct opSet *msg)
{
    IPTR retval;

    if ((retval = DoSuperMethodA(cl, o, (Msg)msg)))
    {
        struct SwrtData *data;
        BPTR fh = ZERO;
        ULONG filesize;
        UBYTE *filebuf = NULL;
        ULONG pos;
        BOOL success = FALSE;
        IPTR estlines, poolsize;
        BOOL prev_was_empty = FALSE;
        BOOL last_was_nl = TRUE;

        data = INST_DATA(cl, (Object *)retval);
        data->Pool = NULL;
        data->TextBuffer = NULL;
        data->TextBufferLen = 0;
        data->TextBufferAlloced = 0;

        /* Get the file handle from attributes */
        GetDTAttrs((Object *)retval,
                   DTA_FileHandle, (IPTR)&fh,
                   TAG_DONE);

        if (fh == ZERO)
        {
            SetIoErr(ERROR_REQUIRED_ARG_MISSING);
            goto fail;
        }

        /* Get file size */
        filesize = (ULONG)Seek(fh, 0, OFFSET_END);
        Seek(fh, 0, OFFSET_BEGINNING);
        if (filesize < 12)
        {
            SetIoErr(ERROR_READ_PROTECTED);
            goto fail;
        }

        /* Read entire file into memory */
        filebuf = AllocVec(filesize, MEMF_ANY);
        if (!filebuf)
        {
            SetIoErr(ERROR_NO_FREE_STORE);
            goto fail;
        }

        if (Read(fh, filebuf, filesize) != (LONG)filesize)
        {
            SetIoErr(ERROR_READ_PROTECTED);
            goto fail;
        }

        /* Verify header: either FORM/SWRT or a raw SWRT stream. */
        if (ReadIFFLong(filebuf) == ID_FORM)
        {
            if (filesize < 12 || ReadIFFLong(filebuf + 8) != ID_SWRT)
            {
                SetIoErr(ERROR_OBJECT_WRONG_TYPE);
                goto fail;
            }
            pos = 12;
        }
        else if (ReadIFFLong(filebuf) == ID_SWRT)
        {
            pos = 4;
        }
        else
        {
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            goto fail;
        }

        /* Create memory pool */
        estlines = (filesize / 80) + 1;
        estlines = (estlines > 500) ? 500 : estlines;
        poolsize = sizeof(struct Line) * estlines;
        poolsize += filesize + 4096;

        data->Pool = CreatePool(MEMF_CLEAR | MEMF_PUBLIC, poolsize, poolsize);
        if (!data->Pool)
        {
            SetIoErr(ERROR_NO_FREE_STORE);
            goto fail;
        }

        /* Parse IFF chunks (FORM/SWRT starts at 12, raw SWRT at 4) */
        while (pos + 8 <= filesize)
        {
            ULONG chunk_id = ReadIFFLong(filebuf + pos);
            ULONG chunk_size = ReadIFFLong(filebuf + pos + 4);
            ULONG padded_size;

            if (chunk_size > filesize - pos - 8)
                break;

            if (chunk_id == ID_ATTR)
            {
                prev_was_empty = FALSE;
            }
            else if (chunk_id == ID_CHRS)
            {
                if (chunk_size > 0)
                {
                    UBYTE *chrs_data = filebuf + pos + 8;
                    ULONG i;

                    if (prev_was_empty && !last_was_nl)
                    {
                        SwrtAppendChar(data, '\n');
                        last_was_nl = TRUE;
                    }

                    for (i = 0; i < chunk_size; i++)
                    {
                        UBYTE c = chrs_data[i];

                        if (c == '\r')
                        {
                            SwrtAppendChar(data, '\n');
                            last_was_nl = TRUE;
                            if (i + 1 < chunk_size && chrs_data[i + 1] == '\n')
                                i++;
                        }
                        else if (c == '\n')
                        {
                            SwrtAppendChar(data, '\n');
                            last_was_nl = TRUE;
                        }
                        else
                        {
                            if (c == '\t')
                                SwrtAppendChar(data, '\t');
                            else if (c >= 32 && c < 127)
                                SwrtAppendChar(data, c);
                            else if (c == 0x0C)
                                SwrtAppendChar(data, '\f');
                            else if (c == ' ')
                                SwrtAppendChar(data, ' ');
                            last_was_nl = FALSE;
                        }
                    }
                    prev_was_empty = FALSE;
                }
                else
                {
                    prev_was_empty = TRUE;
                }
            }
            else if (chunk_id == ID_ILBM)
            {
                SwrtAppendText(data, (UBYTE *)"\n[IMAGE]\n", 9);
                prev_was_empty = FALSE; last_was_nl = TRUE;
            }
            else if (chunk_id == ID_GINF)
            {
                if (!last_was_nl)
                {
                    SwrtAppendChar(data, '\n');
                    last_was_nl = TRUE;
                }
                prev_was_empty = FALSE;
            }
            else if (chunk_id == ID_FORM)
            {
                ULONG nested_form_size = ReadIFFLong(filebuf + pos + 4);
                padded_size = nested_form_size + 8;
                if (nested_form_size % 2)
                    padded_size++;
                pos += padded_size;
                continue;
            }
            else
            {
                /* Skip other chunks */
            }

            padded_size = 8 + chunk_size;
            if (padded_size % 2)
                padded_size++;
            pos += padded_size;
        }

        D(bug("Swrt_OM_NEW: extracted %ld bytes of text\n", data->TextBufferLen));

        /* Set the text buffer attributes so the superclass can display it */
        if (data->TextBuffer && data->TextBufferLen > 0)
        {
            SetDTAttrs((Object *)retval,
                       TDTA_Buffer,    (IPTR)data->TextBuffer,
                       TDTA_BufferLen, (IPTR)data->TextBufferLen,
                       TAG_DONE);
            success = TRUE;
        }
        else
        {
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            success = FALSE;
        }

        FreeVec(filebuf);
        filebuf = NULL;

        if (!success)
        {
            fail:
            if (filebuf)
            {
                FreeVec(filebuf);
                filebuf = NULL;
            }
            /* OM_DISPOSE releases the text buffer and pool */
            CoerceMethod(cl, (Object *)retval, OM_DISPOSE);
            retval = 0;
        }
    }

    return retval;
}

/*****************************************************************************/

static IPTR Swrt__OM_DISPOSE(Class *cl, Object *o, Msg msg)
{
    struct SwrtData *data;
    struct List *linelist = NULL;
    IPTR retval;

    data = INST_DATA(cl, o);

    /* Clear line list so super doesn't try to free our pool memory */
    if (GetDTAttrs(o, TDTA_LineList, (IPTR)&linelist, TAG_DONE) && linelist)
        NewList(linelist);

    /* Free our resources */
    if (data->TextBuffer && data->Pool)
        FreePooled(data->Pool, data->TextBuffer, data->TextBufferAlloced);
    if (data->Pool)
        DeletePool(data->Pool);

    retval = DoSuperMethodA(cl, o, msg);
    return retval;
}

/*****************************************************************************/

static IPTR Swrt__OM_SET(Class *cl, Object *o, struct opSet *msg)
{
    IPTR retval;

    if ((retval = DoSuperMethodA(cl, o, (Msg)msg)) && (OCLASS(o) == cl))
    {
        struct RastPort *rp;

        if ((rp = ObtainGIRPort(msg->ops_GInfo)))
        {
            struct gpRender gpr;

            gpr.MethodID   = GM_RENDER;
            gpr.gpr_GInfo  = msg->ops_GInfo;
            gpr.gpr_RPort  = rp;
            gpr.gpr_Redraw = GREDRAW_UPDATE;
            DoMethodA(o, (Msg)&gpr);

            ReleaseGIRPort(rp);
        }
        retval = 0;
    }

    return retval;
}

/*****************************************************************************/

static IPTR Swrt__GM_LAYOUT(Class *cl, Object *o, struct gpLayout *msg)
{
    IPTR retval;

    NotifyAttrChanges(o, msg->gpl_GInfo, 0,
                      GA_ID   , G(o)->GadgetID,
                      DTA_Busy, TRUE,
                      TAG_DONE);

    retval = (IPTR)DoSuperMethodA(cl, o, (Msg)msg);
    retval += DoAsyncLayout(o, msg);

    return retval;
}

/*****************************************************************************/

static IPTR Swrt__DTM_PROCLAYOUT(Class *cl, Object *o, struct gpLayout *msg)
{
    IPTR retval;

    NotifyAttrChanges(o, ((struct gpLayout *)msg)->gpl_GInfo, 0,
                      GA_ID   , G(o)->GadgetID,
                      DTA_Busy, TRUE,
                      TAG_DONE);

    retval = (IPTR)DoSuperMethodA(cl, o, (Msg)msg);
    return retval;
}

/*****************************************************************************/

static IPTR Swrt__DTM_READ(Class *cl, Object *o, struct dtRead *msg)
{
    /*
     * Support reading the document data.
     * We just pass the text buffer back.
     */
    struct SwrtData *data = INST_DATA(cl, o);

    if (data->TextBuffer && data->TextBufferLen > 0)
    {
        ULONG readlen = data->TextBufferLen;

        if (msg->dtr_Offset >= readlen)
            return 0;
        readlen -= msg->dtr_Offset;

        if (msg->dtr_Length < readlen)
            readlen = msg->dtr_Length;

        CopyMem(data->TextBuffer + msg->dtr_Offset,
                msg->dtr_Buffer, readlen);

        return (IPTR)readlen;
    }

    return 0;
}

/*****************************************************************************/

static IPTR Swrt__DTM_PRINT(Class *cl, Object *o, struct dtPrint *dtp)
{
    union printerIO *pio = dtp->dtp_PIO;
    struct SwrtData *data = INST_DATA(cl, o);

    if (data->TextBuffer && data->TextBufferLen > 0)
    {
        pio->ios.io_Command = CMD_WRITE;
        pio->ios.io_Data    = (APTR)data->TextBuffer;
        pio->ios.io_Length  = (LONG)data->TextBufferLen;
        return DoIO((struct IORequest *)pio);
    }

    return PDERR_CANCEL;
}

/*****************************************************************************/

static IPTR Swrt__DTM_ASYNCLAYOUT(Class *cl, Object *o, struct gpLayout *gpl)
{
    struct DTSpecialInfo        *si = (struct DTSpecialInfo *)G(o)->SpecialInfo;
    struct SwrtData             *data = INST_DATA(cl, o);
    ULONG                       visible = 0, total = 0;
    struct RastPort             trp;
    ULONG                       hunit = 1;
    ULONG                       bsig = 0;

    BOOL                        linefeed = FALSE;
    BOOL                        newseg = FALSE;
    BOOL                        abort = FALSE;

    struct TextAttr             *tattr;
    struct TextFont             *font;
    struct List                 *linelist;
    struct IBox                 *domain;
    IPTR                        wrap = FALSE;
    STRPTR                      title;

    ULONG                       num, offset, swidth;
    ULONG                       anchor = 0, newanchor = 0;
    struct Line                 *line;
    ULONG                       yoffset = 0;
    ULONG                       nomwidth = 0, nomheight = 0;
    ULONG                       max_linelength = 0;
    UBYTE                       *buffer;
    IPTR                        bufferlen;

    D(bug("Swrt_DTM_ASYNCLAYOUT\n"));

    if (GetDTAttrs(o,
            DTA_TextAttr , (IPTR)&tattr,
            DTA_TextFont , (IPTR)&font,
            DTA_Domain   , (IPTR)&domain,
            DTA_ObjName  , (IPTR)&title,
            TDTA_Buffer  , (IPTR)&buffer,
            TDTA_BufferLen, (IPTR)&bufferlen,
            TDTA_LineList, (IPTR)&linelist,
            TDTA_WordWrap, (IPTR)&wrap,
            TAG_DONE) == 9)
    {
        D(bug("Swrt_DTM_ASYNCLAYOUT: Got all attrs\n"));

        ObtainSemaphore(&(si->si_Lock));

        if (buffer && bufferlen && font && domain)
        {
            InitRastPort(&trp);
            SetFont(&trp, font);

            nomheight = (ULONG)(24 * font->tf_YSize);
            nomwidth  = (ULONG)(80 * font->tf_XSize);

            if (wrap || gpl->gpl_Initial)
            {
                ULONG i;
                
                D(bug("Swrt_DTM_ASYNCLAYOUT: Performing layout\n"));

                while ((line = (struct Line *)RemHead(linelist)))
                    FreePooled(data->Pool, line, sizeof(struct Line));

                for (i = 0, offset = num = 0, anchor = 0;
                     i < bufferlen && bsig == 0 && !abort;
                     i++)
                {
                    newseg = FALSE;

                    if (buffer[i] == '\n' || buffer[i] == '\r' || buffer[i] == '\f')
                    {
                        newseg = linefeed = TRUE;
                        newanchor = i + 1;

                        /* Handle CR+LF */
                        if (buffer[i] == '\r' && i + 1 < bufferlen && buffer[i + 1] == '\n')
                            i++;
                    }
                    else
                    {
                        swidth = TextLength(&trp, &buffer[anchor], num + 1);
                        num++;
                    }

                    if (i == bufferlen - 1)
                        newseg = TRUE;

                    if (newseg && num > 0)
                    {
                        if ((line = AllocPooled(data->Pool, sizeof(struct Line))))
                        {
                            swidth = TextLength(&trp, &buffer[anchor], num);
                            line->ln_Text    = &buffer[anchor];
                            line->ln_TextLen = num;
                            line->ln_XOffset = offset;
                            line->ln_YOffset = yoffset + font->tf_Baseline;
                            line->ln_Width   = swidth;
                            line->ln_Height  = font->tf_YSize;
                            line->ln_Flags   = (linefeed) ? LNF_LF : 0;
                            line->ln_FgPen   = 1;
                            line->ln_BgPen   = 0;
                            line->ln_Style   = FS_NORMAL;
                            line->ln_Data    = NULL;

                            if (line->ln_Width + line->ln_XOffset > max_linelength)
                                max_linelength = line->ln_Width + line->ln_XOffset;

                            AddTail(linelist, (struct Node *)line);

                            if (linefeed)
                            {
                                yoffset += font->tf_YSize;
                                offset = 0;
                                total++;
                            }
                            else
                            {
                                offset += swidth;
                            }
                        }
                        else
                        {
                            abort = TRUE;
                        }

                        newseg = linefeed = FALSE;
                        anchor = newanchor;
                        num = 0;
                        bsig = CheckSignal(SIGBREAKF_CTRL_C);
                    }
                }
            }
            else
            {
                total  = si->si_TotVert;
                max_linelength = si->si_TotHoriz * si->si_HorizUnit;
            }

            si->si_VertUnit  = font->tf_YSize;
            si->si_VisVert   = visible = domain->Height / si->si_VertUnit;
            si->si_TotVert   = total;
            si->si_HorizUnit = hunit = font->tf_XSize;
            si->si_VisHoriz  = domain->Width / hunit;
            si->si_TotHoriz  = max_linelength / hunit;

            if (bsig == 0)
            {
                NotifyAttrChanges(o, gpl->gpl_GInfo, 0,
                    GA_ID          , G(o)->GadgetID,
                    DTA_VisibleVert, visible,
                    DTA_TotalVert  , total,
                    DTA_NominalVert, nomheight,
                    DTA_VertUnit   , font->tf_YSize,
                    DTA_VisibleHoriz, (domain->Width / hunit),
                    DTA_TotalHoriz  , max_linelength / hunit,
                    DTA_NominalHoriz, nomwidth,
                    DTA_HorizUnit   , hunit,
                    DTA_TopHoriz    , si->si_TopHoriz,
                    DTA_Title       , (IPTR)title,
                    DTA_Busy        , FALSE,
                    DTA_Sync        , TRUE,
                    TAG_DONE);
            }
        }

        ReleaseSemaphore(&(si->si_Lock));
    }

    D(bug("Swrt_DTM_ASYNCLAYOUT: Done. Returning %d\n", total));
    return (IPTR)total;
}

/*****************************************************************************/
/*****************************************************************************/
