#include "generators.h"
#include "utils.h"
#ifdef HAVE_BROTLI
#include <brotli/encode.h>
#endif
#include <cstring>
#include <iostream>
#include <algorithm>

static void writeULongBE(std::vector<Byte>& data, ULong value) {
    data.push_back(static_cast<Byte>((value >> 24) & 0xFF));
    data.push_back(static_cast<Byte>((value >> 16) & 0xFF));
    data.push_back(static_cast<Byte>((value >> 8) & 0xFF));
    data.push_back(static_cast<Byte>(value & 0xFF));
}

static void writeUShortBE(std::vector<Byte>& data, UShort value) {
    data.push_back(static_cast<Byte>((value >> 8) & 0xFF));
    data.push_back(static_cast<Byte>(value & 0xFF));
}

static void writeUIntBase128(std::vector<Byte>& data, uint32_t value) {
    uint8_t buf[5];
    uint8_t* ptr = buf + 5;
    *--ptr = static_cast<uint8_t>(value & 0x7F);
    while ((value >>= 7) > 0) {
        *--ptr = static_cast<uint8_t>((value & 0x7F) | 0x80);
    }
    data.insert(data.end(), ptr, buf + 5);
}

std::vector<Byte> FontGenerator::compressBrotli(const std::vector<Byte>& data) {
    if (data.empty()) return data;

#ifdef HAVE_BROTLI
    size_t maxOut = BrotliEncoderMaxCompressedSize(data.size());
    if (maxOut == 0) return {};

    std::vector<Byte> result(maxOut);
    size_t encodedSize = maxOut;

    int ret = BrotliEncoderCompress(
        11, 22, BROTLI_MODE_FONT,
        data.size(), data.data(),
        &encodedSize, result.data()
    );

    if (ret == BROTLI_TRUE) {
        result.resize(encodedSize);
        if (result.size() >= data.size()) {
            return data;
        }
        return result;
    }
    return data;
#else
    (void)data;
    return data;
#endif
}

bool FontGenerator::generateWOFF2(const Font& font, const std::string& outputPath, bool useBrotli) {
    try {
        std::vector<const TableDirectory*> byTag;
        for (const auto& pair : font.tableDirectories) {
            byTag.push_back(&pair.second);
        }
        std::sort(byTag.begin(), byTag.end(),
                  [](const TableDirectory* a, const TableDirectory* b) {
                      return a->tag < b->tag;
                  });

        std::vector<Byte> tableDirectory;
        std::vector<Byte> tableData;

        for (const auto* dir : byTag) {
            std::vector<Byte> rawData(font.rawBytes.begin() + dir->offset,
                                     font.rawBytes.begin() + dir->offset + dir->length);

            tableDirectory.push_back(static_cast<Byte>(63));
            tableDirectory.push_back(static_cast<Byte>((dir->tag >> 24) & 0xFF));
            tableDirectory.push_back(static_cast<Byte>((dir->tag >> 16) & 0xFF));
            tableDirectory.push_back(static_cast<Byte>((dir->tag >> 8) & 0xFF));
            tableDirectory.push_back(static_cast<Byte>(dir->tag & 0xFF));
            writeUIntBase128(tableDirectory, dir->length);

            tableData.insert(tableData.end(), rawData.begin(), rawData.end());
        }

        std::vector<Byte> compressedData;
        if (useBrotli) {
            compressedData = FontGenerator::compressBrotli(tableData);
        } else {
            compressedData = tableData;
        }

        std::vector<Byte> woff2;

        writeULongBE(woff2, 0x774F4632);
        writeULongBE(woff2, font.version.value);
        writeULongBE(woff2, 0);
        writeUShortBE(woff2, static_cast<UShort>(font.numTables));
        writeUShortBE(woff2, 0);
        writeULongBE(woff2, static_cast<ULong>(font.rawBytes.size()));
        writeULongBE(woff2, static_cast<ULong>(compressedData.size()));
        writeUShortBE(woff2, 0);
        writeUShortBE(woff2, 0);
        writeULongBE(woff2, 0);
        writeULongBE(woff2, 0);
        writeULongBE(woff2, 0);
        writeULongBE(woff2, 0);
        writeULongBE(woff2, 0);

        size_t headerSize = woff2.size();
        woff2.insert(woff2.end(), tableDirectory.begin(), tableDirectory.end());
        size_t dataOffset = woff2.size();
        woff2.insert(woff2.end(), compressedData.begin(), compressedData.end());

        ULong totalLength = static_cast<ULong>(woff2.size());
        woff2[4] = static_cast<Byte>((totalLength >> 24) & 0xFF);
        woff2[5] = static_cast<Byte>((totalLength >> 16) & 0xFF);
        woff2[6] = static_cast<Byte>((totalLength >> 8) & 0xFF);
        woff2[7] = static_cast<Byte>(totalLength & 0xFF);

        return Utils::writeFileBytes(outputPath, woff2);

    } catch (const std::exception& e) {
        std::cerr << "Error generating WOFF2: " << e.what() << "\n";
        return false;
    }
}
