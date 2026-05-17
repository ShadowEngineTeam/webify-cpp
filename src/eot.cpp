#include "generators.h"
#include "utils.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// Big-endian writers (for WOFF, TTF, etc.)
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

// Little-endian writers (for EOT)
static void writeULongLE(std::vector<Byte>& data, ULong value) {
    data.push_back(static_cast<Byte>(value & 0xFF));
    data.push_back(static_cast<Byte>((value >> 8) & 0xFF));
    data.push_back(static_cast<Byte>((value >> 16) & 0xFF));
    data.push_back(static_cast<Byte>((value >> 24) & 0xFF));
}

static void writeUShortLE(std::vector<Byte>& data, UShort value) {
    data.push_back(static_cast<Byte>(value & 0xFF));
    data.push_back(static_cast<Byte>((value >> 8) & 0xFF));
}

// Find name record matching EOT criteria (same as Haskell `match`)
static const NameRecord* findEOTName(const Font& font, UShort nameID) {
    for (const auto& nr : font.names) {
        if (nr.nameID != nameID) continue;
        if ((nr.platformID == 1 && nr.platformSpecificID == 0 && nr.languageID == 0) ||
            (nr.platformID == 3 && nr.platformSpecificID == 1 && nr.languageID == 0x0409)) {
            return &nr;
        }
    }
    return nullptr;
}

// Write a name string in EOT format (same as Haskell putNameStr)
static void writeEOTNameStr(std::vector<Byte>& data, const Font& font, UShort nameID) {
    auto* nr = findEOTName(font, nameID);
    if (nr && !nr->value.empty()) {
        auto utf16 = FontGenerator::encodeUTF16LE(nr->value);
        writeUShortLE(data, static_cast<UShort>(utf16.size()));
        for (char c : utf16) data.push_back(static_cast<Byte>(c));
        writeUShortLE(data, 0); // null terminator
    } else {
        writeUShortLE(data, 0);
        writeUShortLE(data, 0);
    }
}

std::string FontGenerator::encodeUTF16LE(const std::string& str) {
    std::string result;
    for (unsigned char c : str) {
        result += static_cast<char>(c);
        result += static_cast<char>(0);
    }
    return result;
}

bool FontGenerator::generateEOT(const Font& font, const std::string& outputPath) {
    try {
        std::vector<Byte> payload;

        // Match Haskell `payload` function exactly
        writeULongLE(payload, static_cast<ULong>(font.rawBytes.size())); // font size
        writeULongLE(payload, 0x00020001); // version
        writeULongLE(payload, 0); // flags

        // PANOSE (10 bytes)
        if (font.os2.panose.size() == 10) {
            payload.insert(payload.end(), font.os2.panose.begin(), font.os2.panose.end());
        } else {
            for (int i = 0; i < 10; ++i) payload.push_back(0);
        }

        payload.push_back(0x01); // charset
        payload.push_back((font.os2.fsSelection & 0x01) ? 0x01 : 0); // italic
        writeULongLE(payload, font.os2.usWeightClass); // weight
        writeUShortLE(payload, 0); // embedding permission (Haskell hardcodes 0)
        writeUShortLE(payload, 0x504C); // magic number
        writeULongLE(payload, font.os2.ulUnicodeRange1);
        writeULongLE(payload, font.os2.ulUnicodeRange2);
        writeULongLE(payload, font.os2.ulUnicodeRange3);
        writeULongLE(payload, font.os2.ulUnicodeRange4);
        writeULongLE(payload, font.os2.ulCodePageRange1);
        writeULongLE(payload, font.os2.ulCodePageRange2);
        writeULongLE(payload, font.head.checksumAdjustment);
        // 4 reserved ULongs (16 bytes)
        writeULongLE(payload, 0);
        writeULongLE(payload, 0);
        writeULongLE(payload, 0);
        writeULongLE(payload, 0);
        writeUShortLE(payload, 0); // padding

        writeEOTNameStr(payload, font, 1); // Family
        writeEOTNameStr(payload, font, 2); // Subfamily
        writeEOTNameStr(payload, font, 5); // Version (ID=5)
        writeEOTNameStr(payload, font, 4); // Full name (ID=4)

        writeUShortLE(payload, 0); // RootString

        // Raw font data
        payload.insert(payload.end(), font.rawBytes.begin(), font.rawBytes.end());

        // Combine: total size + payload (same as Haskell `combine`)
        std::vector<Byte> eotData;
        writeULongLE(eotData, static_cast<ULong>(payload.size() + 4));
        eotData.insert(eotData.end(), payload.begin(), payload.end());

        return Utils::writeFileBytes(outputPath, eotData);

    } catch (const std::exception& e) {
        std::cerr << "Error generating EOT: " << e.what() << "\n";
        return false;
    }
}

bool FontGenerator::generateWOFF(const Font& font, const std::string& outputPath, bool useZopfli) {
    (void)useZopfli;
    try {
        // Collect table directories and sort by original offset (for data layout)
        std::vector<const TableDirectory*> byOffset;
        for (const auto& pair : font.tableDirectories) {
            byOffset.push_back(&pair.second);
        }
        std::sort(byOffset.begin(), byOffset.end(),
                  [](const TableDirectory* a, const TableDirectory* b) {
                      return a->offset < b->offset;
                  });

        // Collect sorted by tag for directory entries
        std::vector<const TableDirectory*> byTag;
        for (const auto& pair : font.tableDirectories) {
            byTag.push_back(&pair.second);
        }
        std::sort(byTag.begin(), byTag.end(),
                  [](const TableDirectory* a, const TableDirectory* b) {
                      return a->tag < b->tag;
                  });

        // Calculate offsets with compression (matching Haskell calculateOffset)
        struct DataBlock {
            std::vector<Byte> compressedData;
            size_t size;
            size_t padding;
        };
        std::vector<DataBlock> blocks;

        // Initial offset: header(44) + tableDirectory(numTables * 20)
        const ULong headerSize = 44;
        const ULong dirSize = 20 * font.numTables;
        size_t currentOffset = headerSize + dirSize;

        for (const auto* dir : byOffset) {
            std::vector<Byte> rawData(font.rawBytes.begin() + dir->offset,
                                     font.rawBytes.begin() + dir->offset + dir->length);
            std::vector<Byte> compressed;
            if (useZopfli) {
                compressed = FontGenerator::compressTableZopfli(rawData);
            } else {
                compressed = FontGenerator::compressTableZlib(rawData);
            }
            // Keep original if compression doesn't help (matching Haskell)
            size_t compSize = compressed.size();
            size_t origSize = rawData.size();
            size_t size = (origSize <= compSize) ? origSize : compSize;
            auto& data = (origSize <= compSize) ? rawData : compressed;

            size_t padding = (size % 4 == 0) ? 0 : 4 - (size % 4);

            blocks.push_back({data, size, padding});

            currentOffset += size + padding;
        }

        // Build WOFF payload (matching Haskell payload function)
        std::vector<Byte> payload;

        // Payload header fields (after 12-byte outer wrapper)
        writeUShortBE(payload, font.numTables);
        writeUShortBE(payload, 0); // reserved
        writeULongBE(payload, static_cast<ULong>(font.rawBytes.size())); // totalSfntSize
        writeUShortBE(payload, 1); // woff version major (matching Haskell)
        writeUShortBE(payload, 0); // woff version minor
        writeULongBE(payload, 0); // meta offset
        writeULongBE(payload, 0); // meta length
        writeULongBE(payload, 0); // meta length uncompressed
        writeULongBE(payload, 0); // private block offset
        writeULongBE(payload, 0); // private block length

        // Table directory entries sorted by tag (matching Haskell sortedByTag)
        // Each entry pairs with its block data by matching the directory pointer
        for (const auto* dir : byTag) {
            // Find the block index for this directory (based on offset order)
            size_t blockIdx = 0;
            for (size_t i = 0; i < byOffset.size(); ++i) {
                if (byOffset[i]->tag == dir->tag) {
                    blockIdx = i;
                    break;
                }
            }
            const auto& block = blocks[blockIdx];

            // Calculate offset: base + sum of sizes+paddings of blocks before this one
            size_t blockOff = headerSize + dirSize;
            for (size_t j = 0; j < blockIdx; ++j) {
                blockOff += blocks[j].size + blocks[j].padding;
            }

            writeULongBE(payload, dir->tag);
            writeULongBE(payload, static_cast<ULong>(blockOff));
            writeULongBE(payload, static_cast<ULong>(block.size));
            writeULongBE(payload, dir->length);
            writeULongBE(payload, dir->checksum);
        }

        // Compressed table data (in original offset order, matching Haskell)
        for (size_t i = 0; i < blocks.size(); ++i) {
            payload.insert(payload.end(), blocks[i].compressedData.begin(),
                          blocks[i].compressedData.begin() + static_cast<long long>(blocks[i].size));
            for (size_t j = 0; j < blocks[i].padding; ++j) {
                payload.push_back(0x0);
            }
        }

        // Build final WOFF: outer combine wrapper (matching Haskell combine)
        std::vector<Byte> woff;
        writeULongBE(woff, 0x774F4646); // signature
        writeULongBE(woff, font.version.value); // sfVersion
        writeULongBE(woff, static_cast<ULong>(payload.size() + 12)); // total size
        woff.insert(woff.end(), payload.begin(), payload.end());

        return Utils::writeFileBytes(outputPath, woff);

    } catch (const std::exception& e) {
        std::cerr << "Error generating WOFF: " << e.what() << "\n";
        return false;
    }
}
