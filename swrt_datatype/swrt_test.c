/*
 * Standalone test of the SWRT IFF parser logic.
 * Compile on Linux: gcc -o swrt_test swrt_test.c
 * Run: ./swrt_test /tmp/smacros/SuperMacrosDOCS.FinalWriter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_FORM 0x464F524DUL
#define ID_SWRT 0x53575254UL
#define ID_CHRS 0x43485253UL
#define ID_ATTR 0x41545452UL
#define ID_ILBM 0x494C424DUL
#define ID_GINF 0x47494E46UL

static unsigned long read32(unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8)  | (unsigned long)p[3];
}

int main(int argc, char *argv[])
{
    FILE *fp;
    unsigned long filesize;
    unsigned char *buf;
    unsigned long pos;
    int chrs_count = 0, chrs_bytes = 0;
    FILE *text_out;

    int prev_was_empty = 0;
    int last_was_nl = 1;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.FinalWriter>\n", argv[0]);
        return 1;
    }

    fp = fopen(argv[1], "rb");
    if (!fp) { perror("fopen"); return 1; }

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    buf = malloc(filesize);
    if (!buf) { perror("malloc"); fclose(fp); return 1; }

    if (fread(buf, 1, filesize, fp) != filesize) {
        perror("fread"); free(buf); fclose(fp); return 1;
    }
    fclose(fp);

    if (read32(buf) != ID_FORM) {
        fprintf(stderr, "Not an IFF file\n"); free(buf); return 1;
    }
    if (read32(buf + 8) != ID_SWRT) {
        fprintf(stderr, "Not FORM/SWRT\n"); free(buf); return 1;
    }

    printf("FORM/SWRT document: %lu bytes\n", filesize);

    text_out = fopen("swrt_extracted.txt", "wb");
    if (!text_out) { perror("fopen output"); free(buf); return 1; }

    pos = 12;
    while (pos + 8 <= filesize) {
        unsigned long chunk_id = read32(buf + pos);
        unsigned long chunk_size = read32(buf + pos + 4);
        unsigned long padded_size;

        if (chunk_size > filesize - pos - 8)
            break;

        if (chunk_id == ID_ATTR) {
            prev_was_empty = 0;
        }
        else if (chunk_id == ID_CHRS) {
            if (chunk_size > 0) {
                unsigned char *data = buf + pos + 8;
                unsigned long i;

                if (prev_was_empty && !last_was_nl)
                {
                    fputc('\n', text_out);
                    last_was_nl = 1;
                }

                for (i = 0; i < chunk_size; i++) {
                    unsigned char c = data[i];
                    if (c == '\r') {
                        fputc('\n', text_out);
                        last_was_nl = 1;
                        if (i + 1 < chunk_size && data[i + 1] == '\n')
                            i++;
                    } else if (c == '\n') {
                        fputc('\n', text_out);
                        last_was_nl = 1;
                    } else {
                        if (c == '\t')
                            fputc('\t', text_out);
                        else if (c >= 32 && c < 127)
                            fputc(c, text_out);
                        else if (c == 0x0C)
                            fputc('\f', text_out);
                        else if (c == ' ')
                            fputc(' ', text_out);
                        last_was_nl = 0;
                    }
                }
                chrs_bytes += chunk_size;
                prev_was_empty = 0;
            } else {
                prev_was_empty = 1;
            }
            chrs_count++;
        }
        else if (chunk_id == ID_ILBM) {
            fputs("\n[IMAGE]\n", text_out);
            prev_was_empty = 0; last_was_nl = 1;
        }
        else if (chunk_id == ID_GINF) {
            if (!last_was_nl) {
                fputc('\n', text_out);
                last_was_nl = 1;
            }
            prev_was_empty = 0;
        }
        else if (chunk_id == ID_FORM) {
            unsigned long nested_size = read32(buf + pos + 4);
            padded_size = nested_size + 8;
            if (padded_size & 1) padded_size++;
            pos += padded_size;
            continue;
        }

        padded_size = 8 + chunk_size;
        if (padded_size & 1) padded_size++;
        pos += padded_size;
    }

    fclose(text_out);
    free(buf);

    printf("Parsed %d CHRS chunks (%d raw bytes)\n", chrs_count, chrs_bytes);
    {
        FILE *check = fopen("swrt_extracted.txt", "rb");
        if (check) {
            fseek(check, 0, SEEK_END);
            printf("Output: %ld bytes\n", ftell(check));
            fclose(check);
        }
    }

    return 0;
}
