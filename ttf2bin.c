#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"


// 定义 FontSet 结构体
typedef struct {
    char length;
    char fileFlag;
    char version[4];
    char fontSize;
    char renderMode;
    unsigned char bold : 1;
    unsigned char italic : 1;
    unsigned char scanMode : 1;
    unsigned char indexMethod : 1;
    unsigned char rsvd : 4;
    int indexAreaSize;
    uint8_t fontNameLength;
    short ascent;
    short descent;
    short lineGap;
    char *fontName;
} FontSet;

typedef struct {
    uint16_t unicode;
    uint32_t offset;
} GlyphEntry;

// 定义 FontGlyphData 结构体用于保存字形数据信息
typedef struct {
    short sx0;
    short sy0;
    short sx1;
    short sy1;
    short advance;
    uint8_t winding_count;
    uint8_t *winding_lengths;
    short *windings;
} FontGlyphData;


// 函数声明
char calculateFontSetLength(FontSet *fontSet);
uint16_t* utf8_to_utf16(const char* utf8_str, int* length);
int generateBinFile(const char *ttfPath, const char *binPath, const char *text, FontSet *fontSet);

char calculateFontSetLength(FontSet *fontSet) {
    return 1  // length
         + 1  // fileFlag
         + 4  // version
         + 1  // fontSize
         + 1  // renderMode
         + 1  // flags (bold, italic, scanMode, indexMethod, rsvd)
         + 4  // indexAreaSize
         + 1  // fontNameLength
         + 2  // ascent
         + 2  // descent
         + 2  // lineGap
         + fontSet->fontNameLength; // fontName length
}

uint16_t* utf8_to_utf16(const char* utf8_str, int* length) {
    int utf16_len = 0;
    const char *ptr = utf8_str;
    uint16_t *utf16_str;

    while (*ptr) {
        unsigned char c = (unsigned char)*ptr;
        if (c < 0x80) {
            utf16_len++;
            ptr++;
        } else if (c < 0xE0) {
            utf16_len++;
            ptr += 2;
        } else if (c < 0xF0) {
            utf16_len++;
            ptr += 3;
        } else {
            utf16_len++;
            ptr += 4;
        }
    }

    utf16_str = (uint16_t*)malloc((utf16_len + 1) * sizeof(uint16_t));
    if (!utf16_str) {
        *length = 0;
        return NULL;
    }

    ptr = utf8_str;
    uint16_t *out = utf16_str;
    while (*ptr) {
        unsigned char c = (unsigned char)*ptr;
        if (c < 0x80) {
            *out++ = c;
            ptr++;
        } else if (c < 0xE0) {
            *out++ = ((c & 0x1F) << 6) | (ptr[1] & 0x3F);
            ptr += 2;
        } else if (c < 0xF0) {
            *out++ = ((c & 0x0F) << 12) | ((ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F);
            ptr += 3;
        } else {
            *out++ = ((c & 0x07) << 18) | ((ptr[1] & 0x3F) << 12) | ((ptr[2] & 0x3F) << 6) | (ptr[3] & 0x3F);
            ptr += 4;
        }
    }
    *out = 0;

    *length = utf16_len;
    return utf16_str;
}

int compare_u16(const void *a, const void *b) {
    return *(uint16_t *)a - *(uint16_t *)b;
}

int unique_u16(uint16_t *arr, int length) {
    if (length == 0) return 0;

    qsort(arr, length, sizeof(uint16_t), compare_u16);

    int new_length = 1;
    for (int i = 1; i < length; ++i) {
        if (arr[i] != arr[i - 1]) {
            arr[new_length++] = arr[i];
        }
    }
    return new_length;
}

typedef struct {
    uint16_t unicode;
    uint32_t offset;
    uint32_t length; // 增加一个长度字段以存储绘制字形的信息长度
    void *glyphData; // 直接保存对应的字形信息（包括轮廓和线段信息）
} GlyphData;

int generateBinFile(const char *ttfPath, const char *binPath, const char *text, FontSet *fontSet) {
    FILE *ttfFile = fopen(ttfPath, "rb");
    if (!ttfFile) {
        fprintf(stderr, "Error opening TTF file!\n");
        return -1;
    }

    fseek(ttfFile, 0, SEEK_END);
    long size = ftell(ttfFile);
    fseek(ttfFile, 0, SEEK_SET);
    unsigned char *ttfBuffer = (unsigned char *)malloc(size);
    if (!ttfBuffer) {
        fprintf(stderr, "Memory allocation error!\n");
        fclose(ttfFile);
        return -1;
    }

    fread(ttfBuffer, 1, size, ttfFile);
    fclose(ttfFile);

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, ttfBuffer, stbtt_GetFontOffsetForIndex(ttfBuffer, 0))) {
        fprintf(stderr, "Failed to initialize font!\n");
        free(ttfBuffer);
        return -1;
    }

    FILE *binFile = fopen(binPath, "wb");
    if (!binFile) {
        fprintf(stderr, "Error creating BIN file!\n");
        free(ttfBuffer);
        return -1;
    }

    int utf16_len;
    uint16_t* utf16_text = utf8_to_utf16(text, &utf16_len);
    if (!utf16_text) {
        fprintf(stderr, "UTF-16 conversion error!\n");
        free(ttfBuffer);
        fclose(binFile);
        return -1;
    }

    int unique_len = unique_u16(utf16_text, utf16_len);
    fontSet->indexAreaSize = unique_len * (2 + 4);

    int nameStringLength;
    const char* nameString = stbtt_GetFontNameString(&font, &nameStringLength, 1, 0, 0, 1);

    // 检查 nameString 及 nameStringLength。如果无效，则使用默认字体名 "font"
    if (nameString == NULL || nameStringLength <= 0) {
        nameString = "font";
        nameStringLength = strlen(nameString);
    }

    fontSet->fontName = (char *)malloc(nameStringLength + 1);
    if (!fontSet->fontName) {
        fprintf(stderr, "Memory allocation error for font name!\n");
        free(ttfBuffer);
        free(utf16_text);
        fclose(binFile);
        return -1;
    }

    strncpy(fontSet->fontName, nameString, nameStringLength);
    fontSet->fontName[nameStringLength] = '\0';
    fontSet->fontNameLength = (uint8_t)nameStringLength;

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    fontSet->ascent = (short)ascent;
    fontSet->descent = (short)descent;
    fontSet->lineGap = (short)lineGap;

    fontSet->length = calculateFontSetLength(fontSet);

    fwrite(&fontSet->length, sizeof(char), 1, binFile);
    fwrite(&fontSet->fileFlag, sizeof(char), 1, binFile);
    fwrite(fontSet->version, sizeof(char), 4, binFile);
    fwrite(&fontSet->fontSize, sizeof(char), 1, binFile);
    fwrite(&fontSet->renderMode, sizeof(char), 1, binFile);

    unsigned char flagByte = (fontSet->bold << 7) |
                             (fontSet->italic << 6) |
                             (fontSet->scanMode << 5) |
                             (fontSet->indexMethod << 4);
    fwrite(&flagByte, sizeof(char), 1, binFile);

    fwrite(&fontSet->indexAreaSize, sizeof(int), 1, binFile);
    fwrite(&fontSet->fontNameLength, sizeof(uint8_t), 1, binFile);
    fwrite(&fontSet->ascent, sizeof(short), 1, binFile);
    fwrite(&fontSet->descent, sizeof(short), 1, binFile);
    fwrite(&fontSet->lineGap, sizeof(short), 1, binFile);
    fwrite(fontSet->fontName, sizeof(char), fontSet->fontNameLength, binFile);

    int *glyphOffsets = (int *)malloc(unique_len * sizeof(int));
    memset(glyphOffsets, 0, unique_len * sizeof(int));

    int indexStartOffset = ftell(binFile);

    for (int i = 0; i < unique_len; ++i) {
        uint16_t unicode = utf16_text[i];
        fwrite(&unicode, sizeof(uint16_t), 1, binFile);
        glyphOffsets[i] = ftell(binFile);
        int placeholder = 0;
        fwrite(&placeholder, sizeof(int), 1, binFile); // 地址占位符
    }

    int glyphDataOffset = ftell(binFile);
    float scale = stbtt_ScaleForPixelHeight(&font, fontSet->fontSize);

    // 存储每个字形数据实际偏移地址的数组
    int *glyphDataStartOffsets = (int *)malloc(unique_len * sizeof(int));

    for (int i = 0; i < unique_len; ++i) {
        // 记录当前字形数据的偏移地址
        glyphDataStartOffsets[i] = ftell(binFile);

        int glyphIndex = stbtt_FindGlyphIndex(&font, utf16_text[i]);
        if (glyphIndex == 0) {
            continue;
        }

        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&font, glyphIndex, 1.0, 1.0, &x0, &y0, &x1, &y1);

        short sx0 = (short)x0;
        short sy0 = (short)y0;
        short sx1 = (short)x1;
        short sy1 = (short)y1;

        fwrite(&sx0, sizeof(short), 1, binFile);
        fwrite(&sy0, sizeof(short), 1, binFile);
        fwrite(&sx1, sizeof(short), 1, binFile);
        fwrite(&sy1, sizeof(short), 1, binFile);

        int advance, lsb;
        stbtt_GetGlyphHMetrics(&font, glyphIndex, &advance, &lsb);

        short sadvancd = (short)advance;
        fwrite(&sadvancd, sizeof(short), 1, binFile);

        stbtt_vertex *stbVertex = NULL;
        int verCount = stbtt_GetGlyphShape(&font, glyphIndex, &stbVertex);
        int winding_count = 0;
        int *winding_lengths = NULL;
        stbtt__point *windings = stbtt_FlattenCurves(stbVertex, verCount, 1.0f / scale / fontSet->renderMode, &winding_lengths, &winding_count, NULL);

        uint8_t winding_count_u8 = (uint8_t)winding_count;
        fwrite(&winding_count_u8, sizeof(uint8_t), 1, binFile);

        for (int j = 0; j < winding_count; ++j) {
            uint8_t winding_length_u8 = (uint8_t)winding_lengths[j];
            fwrite(&winding_length_u8, sizeof(uint8_t), 1, binFile);
        }

        int index = 0;
        for (int j = 0; j < winding_count; ++j) {
            for (int k = 0; k < winding_lengths[j]; ++k) {
                short wx = (short)windings[index].x;
                short wy = (short)windings[index].y;
                fwrite(&wx, sizeof(short), 1, binFile);
                fwrite(&wy, sizeof(short), 1, binFile);
                index++;
            }
        }

        free(windings);
        free(winding_lengths);
        stbtt_FreeShape(&font, stbVertex);
    }

    // 计算实际偏移地址并回写到索引区域
    for (int i = 0; i < unique_len; ++i) {
        fseek(binFile, glyphOffsets[i], SEEK_SET);
        int glyphDataStartOffset = glyphDataStartOffsets[i];
        fwrite(&glyphDataStartOffset, sizeof(int), 1, binFile);
    }

    free(glyphOffsets);
    free(glyphDataStartOffsets);
    free(utf16_text);
    free(ttfBuffer);
    free(fontSet->fontName);
    fclose(binFile);

    return 0;
}

void printFontSet(const FontSet *fontSet) {
    printf("FontSet Information:\n");
    printf("Length: %d\n", fontSet->length);
    printf("FileFlag: %d\n", fontSet->fileFlag);
    printf("Version: %.4s\n", fontSet->version);
    printf("FontSize: %d\n", fontSet->fontSize);
    printf("RenderMode: %d\n", fontSet->renderMode);
    printf("Bold: %d\n", fontSet->bold);
    printf("Italic: %d\n", fontSet->italic);
    printf("ScanMode: %d\n", fontSet->scanMode);
    printf("IndexMethod: %d\n", fontSet->indexMethod);
    printf("IndexAreaSize: %d\n", fontSet->indexAreaSize);
    printf("FontNameLength: %d\n", fontSet->fontNameLength);
    printf("Ascent: %d\n", fontSet->ascent);
    printf("Descent: %d\n", fontSet->descent);
    printf("LineGap: %d\n", fontSet->lineGap);
    printf("FontName: %s\n", fontSet->fontName);
}


void readBinFile(const char *binPath) {
    FILE *binFile = fopen(binPath, "rb");
    if (!binFile) {
        fprintf(stderr, "Error opening BIN file!\n");
        return;
    }

    fseek(binFile, 0, SEEK_END);
    long size = ftell(binFile);
    fseek(binFile, 0, SEEK_SET);

    if (size < sizeof(char)) {
        fprintf(stderr, "BIN file is too small!\n");
        fclose(binFile);
        return;
    }

    FontSet fontSet;
    fread(&fontSet.length, sizeof(char), 1, binFile);
    fread(&fontSet.fileFlag, sizeof(char), 1, binFile);
    fread(fontSet.version, sizeof(char), 4, binFile);
    fread(&fontSet.fontSize, sizeof(char), 1, binFile);
    fread(&fontSet.renderMode, sizeof(char), 1, binFile);

    unsigned char flagByte;
    fread(&flagByte, sizeof(char), 1, binFile);
    fontSet.bold = (flagByte >> 7) & 1;
    fontSet.italic = (flagByte >> 6) & 1;
    fontSet.scanMode = (flagByte >> 5) & 1;
    fontSet.indexMethod = (flagByte >> 4) & 1;

    fread(&fontSet.indexAreaSize, sizeof(int), 1, binFile);
    fread(&fontSet.fontNameLength, sizeof(uint8_t), 1, binFile);
    fread(&fontSet.ascent, sizeof(short), 1, binFile);
    fread(&fontSet.descent, sizeof(short), 1, binFile);
    fread(&fontSet.lineGap, sizeof(short), 1, binFile);

    fontSet.fontName = (char *)malloc(fontSet.fontNameLength + 1);
    if (!fontSet.fontName) {
        fprintf(stderr, "Memory allocation error for font name!\n");
        fclose(binFile);
        return;
    }
    fread(fontSet.fontName, sizeof(char), fontSet.fontNameLength, binFile);
    fontSet.fontName[fontSet.fontNameLength] = '\0';

    // 打印 FontSet 结构体内容
    printFontSet(&fontSet);

    if (fontSet.indexAreaSize <= 0) {
        fprintf(stderr, "Index area size is invalid!\n");
        free(fontSet.fontName);
        fclose(binFile);
        return;
    }

    GlyphEntry *glyphEntries = (GlyphEntry *)malloc(fontSet.indexAreaSize);
    if (!glyphEntries) {
        fprintf(stderr, "Memory allocation error for glyph entries!\n");
        free(fontSet.fontName);
        fclose(binFile);
        return;
    }

    int entryCount = fontSet.indexAreaSize / sizeof(GlyphEntry);
    for (int i = 0; i < entryCount; ++i) {
        if (fread(&glyphEntries[i].unicode, sizeof(uint16_t), 1, binFile) != 1 ||
            fread(&glyphEntries[i].offset, sizeof(uint32_t), 1, binFile) != 1) {
            fprintf(stderr, "Error reading glyph entry!\n");
            free(glyphEntries);
            free(fontSet.fontName);
            fclose(binFile);
            return;
        }
    }

    // 打印前三个字的 Unicode 编码及其在文件中的地址
    printf("Index\tUnicode\tAddress\n");
    for (int i = 0; i < entryCount && i < 3; ++i) {
        printf("%d\t0x%04X\t0x%08X\n", i + 1, glyphEntries[i].unicode, glyphEntries[i].offset);
    }

    // 打印第一个字的全部信息
    if (entryCount > 0) {
        fseek(binFile, glyphEntries[0].offset, SEEK_SET);

        short x0, y0, x1, y1;
        if (fread(&x0, sizeof(short), 1, binFile) != 1 ||
            fread(&y0, sizeof(short), 1, binFile) != 1 ||
            fread(&x1, sizeof(short), 1, binFile) != 1 ||
            fread(&y1, sizeof(short), 1, binFile) != 1) {
            fprintf(stderr, "Error reading glyph bounding box!\n");
            free(glyphEntries);
            free(fontSet.fontName);
            fclose(binFile);
            return;
        }

        short advance;
        if (fread(&advance, sizeof(short), 1, binFile) != 1) {
            fprintf(stderr, "Error reading glyph advance!\n");
            free(glyphEntries);
            free(fontSet.fontName);
            fclose(binFile);
            return;
        }

    printf("Character: 0x%04X, x0: %d, y0: %d, x1: %d, y1: %d, advance: %d\n",
           glyphEntries[0].unicode, x0, y0, x1, y1, advance);

        uint8_t winding_count;
        if (fread(&winding_count, sizeof(uint8_t), 1, binFile) != 1) {
            fprintf(stderr, "Error reading winding count!\n");
            free(glyphEntries);
            free(fontSet.fontName);
            fclose(binFile);
            return;
        }

        printf("Number of winding paths: %d\n", winding_count);

        uint8_t *winding_lengths = (uint8_t *)malloc(winding_count * sizeof(uint8_t));
        if (!winding_lengths) {
            fprintf(stderr, "Memory allocation error for winding lengths!\n");
            free(glyphEntries);
            free(fontSet.fontName);
            fclose(binFile);
            return;
        }

        if (fread(winding_lengths, sizeof(uint8_t), winding_count, binFile) != winding_count) {
            fprintf(stderr, "Error reading winding lengths!\n");
            free(winding_lengths);
            free(glyphEntries);
            free(fontSet.fontName);
            fclose(binFile);
            return;
        }

        for (int j = 0; j < winding_count; ++j) {
            printf("Winding %d has %d segments\n", j, winding_lengths[j]);
            for (int k = 0; k < winding_lengths[j]; ++k) {
                short wx, wy;
                if (fread(&wx, sizeof(short), 1, binFile) != 1 ||
                    fread(&wy, sizeof(short), 1, binFile) != 1) {
                    fprintf(stderr, "Error reading windings data!\n");
                    free(winding_lengths);
                    free(glyphEntries);
                    free(fontSet.fontName);
                    fclose(binFile);
                    return;
                }
                // printf("Character: 0x%04X, Winding Point %d-%d: x = %d, y = %d\n", glyphEntries[0].unicode, j, k, wx, wy);
            }
        }

        free(winding_lengths);
    }

    free(glyphEntries);
    free(fontSet.fontName);
    fclose(binFile);
}

// 返回指定 Unicode 字符在二进制文件中的字形数据偏移地址，不存在则返回 0
int getGlyphOffsetFromBinFile(uint16_t unicode, const char *binFilePath) {
    FILE *binFile = fopen(binFilePath, "rb");
    if (!binFile) {
        fprintf(stderr, "Error opening BIN file %s!\n", binFilePath);
        return 0;
    }

    FontSet fontSetHeader;

    // 读取并跳过第一个头部数据块
    fread(&fontSetHeader.length, sizeof(char), 1, binFile);
    fseek(binFile, 1 + 4 + 1 + 1, SEEK_CUR); // 跳过其余部分:fileFlag（1字节）, version（4字节）， fontSize（1字节）， renderMode（1字节）

    // 读取和解析标志字节
    unsigned char flags;
    fread(&flags, sizeof(unsigned char), 1, binFile);
    fontSetHeader.bold = (flags >> 7) & 0x01;
    fontSetHeader.italic = (flags >> 6) & 0x01;
    fontSetHeader.scanMode = (flags >> 5) & 0x01;
    fontSetHeader.indexMethod = (flags >> 4) & 0x01;
    fontSetHeader.rsvd = flags & 0x0F;

    // 读取 indexAreaSize
    fread(&fontSetHeader.indexAreaSize, sizeof(int), 1, binFile);

    // 跳过其余头部数据
    fread(&fontSetHeader.fontNameLength, sizeof(uint8_t), 1, binFile);
    fseek(binFile, sizeof(short) * 3 + fontSetHeader.fontNameLength, SEEK_CUR);  // 跳过 ascent, descent, lineGap 和 fontName

    // 读取并解析索引区
    int indexEntries = fontSetHeader.indexAreaSize / (sizeof(uint16_t) + sizeof(int));
    for (int i = 0; i < indexEntries; ++i) {
        uint16_t currentUnicode;
        fread(&currentUnicode, sizeof(uint16_t), 1, binFile);

        int glyphOffset;
        fread(&glyphOffset, sizeof(int), 1, binFile);

        if (currentUnicode == unicode) {
            fclose(binFile);
            return glyphOffset;
        }
    }

    fclose(binFile);
    return 0;
}

// 模拟从内存中获取指定 Unicode 字符的字形数据偏移地址
int getGlyphOffsetFromMemory(uint16_t unicode, const uint8_t *mem) {
    const uint8_t *ptr = mem;

    // 跳过无用头部信息直到 indexAreaSize (13 字节: 1+1+4+1+1+1)
    ptr += 9;

    // 读取 indexAreaSize
    int indexAreaSize = *(int*)ptr;
    ptr += sizeof(int);

    // 跳过其余无用头部信息 (即: fontNameLength, ascent, descent, lineGap, 和 fontName)
    uint8_t fontNameLength = *ptr;
    ptr += 1 + sizeof(short) * 3 + fontNameLength;

    // 读取并解析索引区
    int indexEntries = indexAreaSize / (sizeof(uint16_t) + sizeof(int));
    for (int i = 0; i < indexEntries; ++i) {
        uint16_t currentUnicode = *(uint16_t*)ptr;
        ptr += sizeof(uint16_t);

        int glyphOffset = *(int*)ptr;
        ptr += sizeof(int);

        if (currentUnicode == unicode) {
            return glyphOffset;
        }
    }

    return 0;
}

int readFontGlyphData(const uint8_t *mem, int offset, FontGlyphData *glyphData) {
    const uint8_t *ptr = mem + offset;

    // 读取基本的坐标和计数
    size_t winding_byte_offset = offsetof(FontGlyphData, winding_lengths);
    memcpy(&glyphData->sx0, ptr, winding_byte_offset);
    ptr += winding_byte_offset;

    // 分配并读取 winding_lengths
    glyphData->winding_lengths = (uint8_t *)malloc(glyphData->winding_count * sizeof(uint8_t));
    if (!glyphData->winding_lengths) {
        fprintf(stderr, "Memory allocation failed for winding_lengths!\n");
        return -1;
    }
    memcpy(glyphData->winding_lengths, ptr, glyphData->winding_count);
    ptr += glyphData->winding_count;

    // 计算线条数量
    int line_count = 0;
    for (int i = 0; i < glyphData->winding_count; ++i) {
        line_count += glyphData->winding_lengths[i];
    }

    // 分配并读取 windings
    glyphData->windings = (short *)malloc(line_count * 2 * sizeof(short));
    if (!glyphData->windings) {
        fprintf(stderr, "Memory allocation failed for windings!\n");
        free(glyphData->winding_lengths);
        return -1;
    }
    memcpy(glyphData->windings, ptr, line_count * 2 * sizeof(short));

    return 0; // 成功
}




// 模拟从文件读取整个文件到内存中
uint8_t* loadFileToMemory(const char *filePath, int *length) {
    FILE *file = fopen(filePath, "rb");
    if (!file) {
        fprintf(stderr, "Error opening file %s!\n", filePath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *length = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t *buffer = (uint8_t*)malloc(*length);
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed!\n");
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, *length, file);
    fclose(file);

    return buffer;
}

int main() {
    FontSet fontSet = {
        .fileFlag = 2,
        .version = { '1', '0', '0', '4' },
        .fontSize = 32,
        .renderMode = 2,
        .bold = 0,
        .italic = 0,
        .scanMode = 0,
        .indexMethod = 1,
        .indexAreaSize = 0,
        .fontNameLength = 0,
        .ascent = 0,
        .descent = 0,
        .lineGap = 0,
        .fontName = NULL
    };

    // const char *text = "滕王高阁临江渚";

    const char *text = "滕王高阁临江渚，佩玉鸣鸾罢歌舞。画栋朝飞南浦云，珠帘暮卷西山雨。闲云潭影日悠悠，物换星移几度秋。阁中帝子今何在？槛外长江空自流。0123456789#=+-ABCDEFG";

    int result = generateBinFile("STXihei.ttf", "outputxh32_2.bin", text, &fontSet);
    // STXihei.ttf
    // STXINGKA.TTF
    if (result == 0) {
        readBinFile("outputxh32_2.bin");
    }

    const char *binFilePath = "outputxh32_2.bin";
    // uint16_t unicode = 0x738B;  // 例如：Unicode字符"王"

    // int offset = getGlyphOffsetFromBinFile(unicode, binFilePath);
    // if (offset) {
    //     printf("Glyph offset for Unicode 0x%X is %d\n", unicode, offset);
    // } else {
    //     printf("Unicode 0x%X not found in the file.\n", unicode);
    // }

    int fileLength = 0;    // 将文件加载到内存中
    uint8_t *memoryBuffer = loadFileToMemory(binFilePath, &fileLength);
    if (!memoryBuffer) {
        return -1;
    }

    uint16_t unicode = 0x738B;  // 例如：Unicode字符"王"
    int offset = getGlyphOffsetFromMemory(unicode, memoryBuffer);
    if (offset) {
        printf("Glyph offset for Unicode 0x%X is %d\n", unicode, offset);

        FontGlyphData glyphData;
        if (readFontGlyphData(memoryBuffer, offset, &glyphData) == 0) {
            // 打印基本数据以验证
            printf("sx0: %d, sy0: %d, sx1: %d, sy1: %d\n",
                   glyphData.sx0, glyphData.sy0, glyphData.sx1, glyphData.sy1);
            printf("winding_count: %d\n", glyphData.winding_count);

            // 打印 winding_lengths
            printf("winding_lengths:");
            for (int i = 0; i < glyphData.winding_count; ++i) {
                printf(" %d", glyphData.winding_lengths[i]);
            }
            printf("\n");

            // 打印 windings (x, y)
            printf("windings:");
            int line_count = 0;
            for (int i = 0; i < glyphData.winding_count; ++i) {
                line_count += glyphData.winding_lengths[i];
            }
            for (int i = 0; i < line_count; ++i) {
                printf(" (%d, %d)", glyphData.windings[2 * i], glyphData.windings[2 * i + 1]);
            }
            printf("\n");

            // 释放分配的内存
            free(glyphData.winding_lengths);
            free(glyphData.windings);
        }
    } else {
        printf("Unicode 0x%X not found in memory.\n", unicode);
    }


    free(memoryBuffer);
    return 0;
}

