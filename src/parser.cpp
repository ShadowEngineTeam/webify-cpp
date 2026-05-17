#include "parser.h"
#include "utils.h"
#include <stdexcept>
#include <cstring>
#include <iostream>

// Helper functions for byte reading
UShort FontParser::readUShort(const std::vector<Byte>& data, size_t offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("Buffer overflow: readUShort");
    return (data[offset] << 8) | data[offset + 1];
}

Short FontParser::readShort(const std::vector<Byte>& data, size_t offset) {
    return static_cast<Short>(readUShort(data, offset));
}

ULong FontParser::readULong(const std::vector<Byte>& data, size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Buffer overflow: readULong");
    return (data[offset] << 24) | (data[offset + 1] << 16) |
           (data[offset + 2] << 8) | data[offset + 3];
}

Long FontParser::readLong(const std::vector<Byte>& data, size_t offset) {
    return static_cast<Long>(readULong(data, offset));
}

Fixed FontParser::readFixed(const std::vector<Byte>& data, size_t offset) {
    return Fixed(readULong(data, offset));
}

Byte FontParser::readByte(const std::vector<Byte>& data, size_t offset) {
    if (offset >= data.size()) throw std::runtime_error("Buffer overflow: readByte");
    return data[offset];
}

std::vector<Byte> FontParser::readBytes(const std::vector<Byte>& data, size_t offset, size_t length) {
    if (offset + length > data.size()) throw std::runtime_error("Buffer overflow: readBytes");
    return std::vector<Byte>(data.begin() + offset, data.begin() + offset + length);
}

std::string FontParser::readString(const std::vector<Byte>& data, size_t offset, size_t length) {
    if (offset + length > data.size()) throw std::runtime_error("Buffer overflow: readString");
    return std::string(data.begin() + offset, data.begin() + offset + length);
}

// Main parse function
std::unique_ptr<Font> FontParser::parse(const std::string& filename) {
    auto data = Utils::readFileBytes(filename);

    if (data.size() < 12) {
        throw std::runtime_error("File too small to be a valid font");
    }

    std::string ext = Utils::getFileExtension(filename);

    // Try to determine format
    bool isTTF = (ext == "ttf");
    bool isOTF = (ext == "otf");

    if (!isTTF && !isOTF) {
        // Try to detect by file content
        // Check for CFF table presence
        throw std::runtime_error("Unknown font format. Please specify .ttf or .otf");
    }

    if (isTTF) {
        return parseTTF(data);
    } else {
        return parseOTF(data);
    }
}

std::unique_ptr<Font> FontParser::parseTTF(const std::vector<Byte>& data) {
    auto font = std::make_unique<TTFFont>();
    font->rawBytes = data;

    try {
        parseHeader(*font, data);
        parseTableDirectories(*font, data);
        parseCommonTables(*font, data);

        // Parse TTF-specific tables
        try { parseHeadTable(*font, data); } catch (const std::exception& e) { throw std::runtime_error(std::string("head: ") + e.what()); }
        try { parseHheaTable(*font, data); } catch (const std::exception& e) { throw std::runtime_error(std::string("hhea: ") + e.what()); }
        try { parseHmtxTable(*font, data); } catch (const std::exception& e) { throw std::runtime_error(std::string("hmtx: ") + e.what()); }
        try { parseNameTable(*font, data); } catch (const std::exception& e) { throw std::runtime_error(std::string("name: ") + e.what()); }
        try { parseMaxpTable(*font, data); } catch (const std::exception& e) { throw std::runtime_error(std::string("maxp: ") + e.what()); }
        try { parseOS2Table(*font, data); } catch (const std::exception& e) { throw std::runtime_error(std::string("OS/2: ") + e.what()); }
        try { parseLocaTable(*static_cast<TTFFont*>(font.get()), data); } catch (const std::exception& e) { throw std::runtime_error(std::string("loca: ") + e.what()); }
        try { parseGlyfTable(*static_cast<TTFFont*>(font.get()), data); } catch (const std::exception& e) { throw std::runtime_error(std::string("glyf: ") + e.what()); }
        try { parseCmapTable(*static_cast<TTFFont*>(font.get()), data); } catch (const std::exception& e) { throw std::runtime_error(std::string("cmap: ") + e.what()); }
        try { parseKernTable(*static_cast<TTFFont*>(font.get()), data); } catch (const std::exception& e) { throw std::runtime_error(std::string("kern: ") + e.what()); }

        return font;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse TTF: ") + e.what());
    }
}

std::unique_ptr<Font> FontParser::parseOTF(const std::vector<Byte>& data) {
    auto font = std::make_unique<OTFFont>();
    font->rawBytes = data;

    try {
        parseHeader(*font, data);
        parseTableDirectories(*font, data);
        parseCommonTables(*font, data);

        // Parse only essential tables for OTF
        parseHeadTable(*font, data);
        parseNameTable(*font, data);
        parseOS2Table(*font, data);
        parseHheaTable(*font, data);
        parseHmtxTable(*font, data);

        return font;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to parse OTF: ") + e.what());
    }
}

void FontParser::parseHeader(Font& font, const std::vector<Byte>& data) {
    font.version = readFixed(data, 0);
    font.numTables = readUShort(data, 4);
    font.searchRange = readUShort(data, 6);
    font.entrySelector = readUShort(data, 8);
    font.rangeShift = readUShort(data, 10);
}

void FontParser::parseTableDirectories(Font& font, const std::vector<Byte>& data) {
    size_t offset = 12;
    for (UShort i = 0; i < font.numTables; ++i) {
        TableDirectory dir;
        dir.tag = readULong(data, offset);
        dir.checksum = readULong(data, offset + 4);
        dir.offset = readULong(data, offset + 8);
        dir.length = readULong(data, offset + 12);

        font.tableDirectories[dir.tag] = dir;
        offset += 16;
    }
}

void FontParser::parseCommonTables(Font& font, const std::vector<Byte>& data) {
    // This is called after header and table directories are parsed
    // Individual table parsers will handle this
}

TableDirectory* FontParser::getTableDirectory(Font& font, ULong tag) {
    auto it = font.tableDirectories.find(tag);
    if (it == font.tableDirectories.end()) {
        return nullptr;
    }
    return &it->second;
}

void FontParser::parseHeadTable(Font& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x68656164); // "head"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    font.head.version = readFixed(data, offset);
    font.head.fontRevision = readFixed(data, offset + 4);
    font.head.checksumAdjustment = readULong(data, offset + 8);
    font.head.magicNumber = readULong(data, offset + 12);
    font.head.flags = readUShort(data, offset + 16);
    font.head.unitsPerEm = readUShort(data, offset + 18);
    font.head.created = readLong(data, offset + 20);
    font.head.modified = readLong(data, offset + 24);
    font.head.xMin = readShort(data, offset + 28);
    font.head.yMin = readShort(data, offset + 30);
    font.head.xMax = readShort(data, offset + 32);
    font.head.yMax = readShort(data, offset + 34);
    font.head.macStyle = readUShort(data, offset + 36);
    font.head.indexToLocFormat = readUShort(data, offset + 50);
}

void FontParser::parseHheaTable(Font& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x68686561); // "hhea"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    font.hhea.version = readFixed(data, offset);
    font.hhea.ascender = readShort(data, offset + 4);
    font.hhea.descender = readShort(data, offset + 6);
    font.hhea.lineGap = readShort(data, offset + 8);
    font.hhea.advanceWidthMax = readUShort(data, offset + 10);
    font.hhea.minLeftSideBearing = readShort(data, offset + 12);
    font.hhea.minRightSideBearing = readShort(data, offset + 14);
    font.hhea.xMaxExtent = readShort(data, offset + 16);
    font.hhea.caretSlopeRise = readShort(data, offset + 18);
    font.hhea.caretSlopeRun = readShort(data, offset + 20);
    font.hhea.metricDataFormat = readShort(data, offset + 28);
    font.hhea.numberOfHMetrics = readUShort(data, offset + 30);
}

void FontParser::parseHmtxTable(Font& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x686d7478); // "hmtx"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    ULong numMetrics = font.hhea.numberOfHMetrics;

    font.hmtx.clear();
    for (ULong i = 0; i < numMetrics && offset + 4 <= data.size(); ++i) {
        HMetric metric;
        metric.advanceWidth = readUShort(data, offset);
        metric.lsb = readShort(data, offset + 2);
        font.hmtx.push_back(metric);
        offset += 4;
    }
}

static std::string decodeUTF16BE(const std::vector<Byte>& data, size_t pos, UShort length) {
    std::string result;
    for (UShort i = 0; i + 1 < length; i += 2) {
        uint16_t code = (data[pos + i] << 8) | data[pos + i + 1];
        if (code < 0x80) {
            result += static_cast<char>(code);
        } else if (code < 0x800) {
            result += static_cast<char>(0xC0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            result += static_cast<char>(0xE0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    return result;
}

void FontParser::parseNameTable(Font& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x6e616d65); // "name"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    UShort format = readUShort(data, offset);
    UShort count = readUShort(data, offset + 2);
    UShort stringOffset = readUShort(data, offset + 4);

    font.names.clear();
    size_t recordOffset = offset + 6;

    for (UShort i = 0; i < count; ++i) {
        if (recordOffset + 12 > data.size()) break;

        NameRecord record;
        record.platformID = readUShort(data, recordOffset);
        record.platformSpecificID = readUShort(data, recordOffset + 2);
        record.languageID = readUShort(data, recordOffset + 4);
        record.nameID = readUShort(data, recordOffset + 6);
        record.length = readUShort(data, recordOffset + 8);
        record.offset = readUShort(data, recordOffset + 10);

        size_t stringPos = offset + stringOffset + record.offset;
        if (stringPos + record.length <= data.size()) {
            // Decode based on platform
            if (record.platformID == 3 || record.platformID == 0) {
                record.value = decodeUTF16BE(data, stringPos, record.length);
            } else {
                record.value = readString(data, stringPos, record.length);
            }
        }

        font.names.push_back(record);
        recordOffset += 12;
    }
}

void FontParser::parseMaxpTable(Font& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x6d617870); // "maxp"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    font.maxp.version = readFixed(data, offset);
    font.maxp.numGlyphs = readUShort(data, offset + 4);
}

void FontParser::parseOS2Table(Font& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x4f532f32); // "OS/2"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    font.os2.version = readUShort(data, offset);
    font.os2.xAvgCharWidth = readShort(data, offset + 2);
    font.os2.usWeightClass = readUShort(data, offset + 4);
    font.os2.usWidthClass = readUShort(data, offset + 6);
    font.os2.fsType = readUShort(data, offset + 8);

    font.os2.panose.clear();
    for (int i = 0; i < 10 && offset + 32 + i < data.size(); ++i) {
        font.os2.panose.push_back(readByte(data, offset + 32 + i));
    }

    font.os2.ulUnicodeRange1 = readULong(data, offset + 42);
    font.os2.ulUnicodeRange2 = readULong(data, offset + 46);
    font.os2.ulUnicodeRange3 = readULong(data, offset + 50);
    font.os2.ulUnicodeRange4 = readULong(data, offset + 54);

    font.os2.fsSelection = readUShort(data, offset + 62);

    font.os2.ulCodePageRange1 = 0;
    font.os2.ulCodePageRange2 = 0;
    if (offset + 82 + 4 <= data.size()) {
        font.os2.ulCodePageRange1 = readULong(data, offset + 78);
        font.os2.ulCodePageRange2 = readULong(data, offset + 82);
    }
}

void FontParser::parseLocaTable(TTFFont& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x6c6f6361); // "loca"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    font.loca.clear();

    bool isShort = (font.head.indexToLocFormat == 0);
    ULong glyphCount = font.maxp.numGlyphs + 1;

    for (ULong i = 0; i < glyphCount; ++i) {
        Long glyphOffset;
        if (isShort) {
            glyphOffset = readUShort(data, offset) * 2;
            offset += 2;
        } else {
            glyphOffset = readULong(data, offset);
            offset += 4;
        }
        font.loca.push_back(glyphOffset);
    }
}

// Helper: parse glyph flags with repetition
static std::vector<Byte> parseGlyphFlags(const std::vector<Byte>& data, size_t& pos, int count) {
    std::vector<Byte> flags;
    while (count > 0) {
        Byte flag = data[pos++];
        flags.push_back(flag);
        --count;
        if (flag & 0x08) { // repeat flag
            Byte repeats = data[pos++];
            for (int r = 0; r < repeats; ++r) {
                flags.push_back(flag);
                --count;
            }
        }
    }
    return flags;
}

// Helper: parse glyph coordinates
static std::vector<Short> parseCoordinates(const std::vector<Byte>& data, size_t& pos,
                                            const std::vector<Byte>& flags,
                                            int shortBit, int sameBit) {
    std::vector<Short> coords;
    Short current = 0;
    for (Byte flag : flags) {
        if (flag & (1 << shortBit)) {
            Byte delta = data[pos++];
            if (flag & (1 << sameBit))
                current += delta;
            else
                current -= delta;
        } else {
            if (flag & (1 << sameBit)) {
                // same as current
            } else {
                Short delta = static_cast<Short>((data[pos] << 8) | data[pos + 1]);
                pos += 2;
                current += delta;
            }
        }
        coords.push_back(current);
    }
    return coords;
}

void FontParser::parseGlyfTable(TTFFont& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x676c7966); // "glyf"
    if (!tableDir) return;

    size_t tableOffset = tableDir->offset;
    font.glyphs.clear();

    for (size_t i = 0; i < font.loca.size() - 1; ++i) {
        Glyph glyph;
        Long startOffset = font.loca[i];
        Long endOffset = font.loca[i + 1];

        if (startOffset == endOffset) {
            glyph.numberOfContours = 0;
            font.glyphs.push_back(glyph);
            continue;
        }

        size_t glyphPos = tableOffset + startOffset;
        glyph.numberOfContours = readShort(data, glyphPos);
        glyph.xMin = readShort(data, glyphPos + 2);
        glyph.yMin = readShort(data, glyphPos + 4);
        glyph.xMax = readShort(data, glyphPos + 6);
        glyph.yMax = readShort(data, glyphPos + 8);

        size_t pos = glyphPos + 10;
        glyph.isComposite = (glyph.numberOfContours < 0);

        if (glyph.numberOfContours > 0) {
            // Simple glyph
            UShort numContours = static_cast<UShort>(glyph.numberOfContours);
            for (UShort j = 0; j < numContours; ++j) {
                glyph.simpleGlyph.endPtsOfContours.push_back(readUShort(data, pos));
                pos += 2;
            }
            UShort instrLength = readUShort(data, pos);
            pos += 2;
            for (int j = 0; j < instrLength; ++j) {
                glyph.simpleGlyph.instructions.push_back(data[pos++]);
            }
            int pointCount = glyph.simpleGlyph.endPtsOfContours.empty()
                ? 0 : glyph.simpleGlyph.endPtsOfContours.back() + 1;
            if (pointCount > 0) {
                glyph.simpleGlyph.flags = parseGlyphFlags(data, pos, pointCount);
                glyph.simpleGlyph.xCoordinates = parseCoordinates(data, pos, glyph.simpleGlyph.flags, 1, 4);
                glyph.simpleGlyph.yCoordinates = parseCoordinates(data, pos, glyph.simpleGlyph.flags, 2, 5);
            }
        } else if (glyph.numberOfContours < 0) {
            // Composite glyph
            bool more = true;
            while (more) {
                CompositeGlyph::Component comp;
                comp.flags = readUShort(data, pos);
                pos += 2;
                comp.glyphIndex = readUShort(data, pos);
                pos += 2;

                if (comp.flags & 0x0001) { // args are words
                    comp.argument1 = readShort(data, pos);
                    comp.argument2 = readShort(data, pos + 2);
                    pos += 4;
                } else {
                    comp.argument1 = static_cast<Short>(data[pos++]);
                    comp.argument2 = static_cast<Short>(data[pos++]);
                }

                if (comp.flags & 0x0002) { // args are xy values
                    comp.xOffset = comp.argument1;
                    comp.yOffset = comp.argument2;
                } else {
                    comp.xOffset = 0;
                    comp.yOffset = 0;
                }

                comp.xScale = 1.0;
                comp.yScale = 1.0;
                comp.scale01 = 0.0;
                comp.scale10 = 0.0;

                if (comp.flags & 0x0008) { // we have a scale
                    comp.xScale = static_cast<double>(readUShort(data, pos)) / 0x4000;
                    comp.yScale = comp.xScale;
                    pos += 2;
                } else if (comp.flags & 0x0040) { // we have x/y scale
                    comp.xScale = static_cast<double>(readUShort(data, pos)) / 0x4000;
                    comp.yScale = static_cast<double>(readUShort(data, pos + 2)) / 0x4000;
                    pos += 4;
                } else if (comp.flags & 0x0080) { // we have 2x2
                    comp.xScale = static_cast<double>(readUShort(data, pos)) / 0x4000;
                    comp.scale01 = static_cast<double>(readUShort(data, pos + 2)) / 0x4000;
                    comp.scale10 = static_cast<double>(readUShort(data, pos + 4)) / 0x4000;
                    comp.yScale = static_cast<double>(readUShort(data, pos + 6)) / 0x4000;
                    pos += 8;
                }

                glyph.compositeGlyph.components.push_back(comp);
                more = (comp.flags & 0x0020) != 0; // more components
            }

            if (!glyph.compositeGlyph.components.empty()) {
                UShort lastFlags = glyph.compositeGlyph.components.back().flags;
                if (lastFlags & 0x0100) { // we have instructions
                    glyph.compositeGlyph.numInstructions = readUShort(data, pos);
                    pos += 2;
                    for (int j = 0; j < glyph.compositeGlyph.numInstructions; ++j) {
                        glyph.compositeGlyph.instructions.push_back(data[pos++]);
                    }
                }
            }
        }

        font.glyphs.push_back(glyph);
    }
}

void FontParser::parseCmapTable(TTFFont& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x636d6170); // "cmap"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    UShort version = readUShort(data, offset);
    UShort numTables = readUShort(data, offset + 2);

    for (UShort i = 0; i < numTables; ++i) {
        size_t recordOffset = offset + 4 + (i * 8);
        UShort platformID = readUShort(data, recordOffset);
        UShort platformSpecificID = readUShort(data, recordOffset + 2);
        ULong subtableOffset = readULong(data, recordOffset + 4);

        CmapSubtable subtable;
        subtable.platformID = platformID;
        subtable.platformSpecificID = platformSpecificID;

        size_t pos = offset + subtableOffset;
        subtable.format = readUShort(data, pos);

        if (subtable.format == 0) {
            subtable.glyphIds0.resize(256);
            for (int j = 0; j < 256; ++j) {
                subtable.glyphIds0[j] = readByte(data, pos + 6 + j);
            }
        } else if (subtable.format == 4) {
            UShort length = readUShort(data, pos + 2);
            UShort language = readUShort(data, pos + 4);
            UShort segCountX2 = readUShort(data, pos + 6);
            UShort segCount = segCountX2 / 2;
            /* UShort searchRange = */ readUShort(data, pos + 8);
            /* UShort entrySelector = */ readUShort(data, pos + 10);
            /* UShort rangeShift = */ readUShort(data, pos + 12);

            size_t endCodesPos = pos + 14;
            size_t startCodesPos = endCodesPos + segCountX2 + 2; // +2 for reservedPad
            size_t idDeltasPos = startCodesPos + segCountX2;
            size_t idRangeOffsetsPos = idDeltasPos + segCountX2;
            size_t glyphIdArrayPos = idRangeOffsetsPos + segCountX2;

            for (UShort s = 0; s < segCount; ++s) {
                CmapSegment seg;
                seg.endCode = readUShort(data, endCodesPos + s * 2);
                seg.startCode = readUShort(data, startCodesPos + s * 2);
                seg.idDelta = readUShort(data, idDeltasPos + s * 2);
                seg.idRangeOffset = readUShort(data, idRangeOffsetsPos + s * 2);
                subtable.segments.push_back(seg);
            }

            // Read glyphIdArray
            size_t arrayEnd = pos + length;
            for (size_t gpos = glyphIdArrayPos; gpos + 1 < arrayEnd; gpos += 2) {
                subtable.glyphIdArray.push_back(readUShort(data, gpos));
            }
        } else if (subtable.format == 6) {
            /* UShort length = */ readUShort(data, pos + 2);
            /* UShort language = */ readUShort(data, pos + 4);
            subtable.firstCode = readUShort(data, pos + 6);
            subtable.entryCount = readUShort(data, pos + 8);
            subtable.glyphIds6.resize(subtable.entryCount);
            for (UShort j = 0; j < subtable.entryCount; ++j) {
                subtable.glyphIds6[j] = readUShort(data, pos + 10 + j * 2);
            }
        } else if (subtable.format == 12) {
            /* UShort reserved = */ readUShort(data, pos + 2);
            ULong length = readULong(data, pos + 4);
            /* ULong language = */ readULong(data, pos + 8);
            ULong nGroups = readULong(data, pos + 12);
            for (ULong g = 0; g < nGroups; ++g) {
                size_t gp = pos + 16 + g * 12;
                CmapFormat12Group group;
                group.startCharCode = readULong(data, gp);
                group.endCharCode = readULong(data, gp + 4);
                group.startGlyphID = readULong(data, gp + 8);
                subtable.groups12.push_back(group);
            }
        }

        subtable.populateCharToGlyph();
        font.cmaps.push_back(subtable);
    }
}

// CmapSubtable::populateCharToGlyph implementation
void CmapSubtable::populateCharToGlyph() {
    charToGlyph.clear();
    if (format == 0) {
        for (int i = 0; i < 256; ++i) {
            if (glyphIds0[i] > 0) charToGlyph[i] = glyphIds0[i];
        }
    } else if (format == 4) {
        for (size_t si = 0; si < segments.size(); ++si) {
            const auto& seg = segments[si];
            if (seg.startCode == 0xFFFF && seg.endCode == 0xFFFF) continue;
            if (seg.idRangeOffset == 0) {
                for (ULong cp = seg.startCode; cp <= seg.endCode; ++cp) {
                    auto gid = static_cast<UShort>((seg.idDelta + static_cast<UShort>(cp)) & 0xFFFF);
                    if (gid > 0) charToGlyph[cp] = gid;
                }
            } else {
                for (ULong cp = seg.startCode; cp <= seg.endCode; ++cp) {
                    size_t offset = (seg.idRangeOffset / 2) + (cp - seg.startCode) - (segments.size() - 1 - si);
                    if (offset < glyphIdArray.size()) {
                        UShort gid = glyphIdArray[offset];
                        if (gid != 0) {
                            gid = static_cast<UShort>((gid + seg.idDelta) & 0xFFFF);
                            if (gid > 0) charToGlyph[cp] = gid;
                        }
                    }
                }
            }
        }
    } else if (format == 6) {
        for (UShort j = 0; j < entryCount; ++j) {
            if (glyphIds6[j] > 0) charToGlyph[firstCode + j] = glyphIds6[j];
        }
    } else if (format == 12) {
        for (const auto& g : groups12) {
            for (ULong cp = g.startCharCode; cp <= g.endCharCode && cp <= 0x10FFFF; ++cp) {
                ULong gid = g.startGlyphID + (cp - g.startCharCode);
                if (gid > 0) charToGlyph[cp] = static_cast<UShort>(gid);
            }
        }
    }
}

// CmapSubtable::getGlyphID implementation
ULong CmapSubtable::getGlyphID(ULong codePoint) const {
    if (format == 0) {
        if (codePoint < 256) {
            return glyphIds0[codePoint];
        }
        return 0;
    }

    if (format == 4) {
        auto n = static_cast<UShort>(codePoint & 0xFFFF);
        if (codePoint > 0xFFFF) return 0;

        // Find segment where endCode >= n
        for (size_t i = 0; i < segments.size(); ++i) {
            if (segments[i].endCode >= n) {
                if (segments[i].startCode > n) return 0;

                if (segments[i].idRangeOffset == 0) {
                    return (segments[i].idDelta + n) & 0xFFFF;
                }

                size_t offset = (segments[i].idRangeOffset / 2) +
                              (n - segments[i].startCode) -
                              (segments.size() - 1 - i);
                if (offset < glyphIdArray.size()) {
                    UShort gid = glyphIdArray[offset];
                    if (gid != 0) {
                        return (gid + segments[i].idDelta) & 0xFFFF;
                    }
                    return 0;
                }
                return 0;
            }
        }
        return 0;
    }

    if (format == 6) {
        auto n = static_cast<UShort>(codePoint & 0xFFFF);
        auto start = static_cast<ULong>(firstCode);
        auto end = start + static_cast<ULong>(entryCount);
        if (codePoint >= start && codePoint < end) {
            return glyphIds6[n - firstCode];
        }
        return 0;
    }

    if (format == 12) {
        for (const auto& g : groups12) {
            if (codePoint >= g.startCharCode && codePoint <= g.endCharCode) {
                return g.startGlyphID + (codePoint - g.startCharCode);
            }
        }
        return 0;
    }

    return 0;
}

void FontParser::parseKernTable(TTFFont& font, const std::vector<Byte>& data) {
    auto* tableDir = getTableDirectory(font, 0x6b65726e); // "kern"
    if (!tableDir) return;

    size_t offset = tableDir->offset;
    UShort version = readUShort(data, offset);
    UShort nTables = readUShort(data, offset + 2);

    for (UShort t = 0; t < nTables; ++t) {
        size_t subtableOffset = offset + 4;
        if (t > 0) {
            // Need to calculate offset based on previous table length
            // Skip - this is an estimate, real impl would track lengths
            break;
        }
        UShort subtableVersion = readUShort(data, subtableOffset);
        UShort subtableLength = readUShort(data, subtableOffset + 2);
        UShort format = readUShort(data, subtableOffset + 4);

        if (format == 0) {
            UShort nPairs = readUShort(data, subtableOffset + 6);
            // Skip searchRange, entrySelector, rangeShift

            size_t pairOffset = subtableOffset + 14;
            for (UShort i = 0; i < nPairs && pairOffset + 6 <= data.size(); ++i) {
                KernPair pair;
                pair.left = readUShort(data, pairOffset);
                pair.right = readUShort(data, pairOffset + 2);
                pair.value = readShort(data, pairOffset + 4);

                font.kernPairs.push_back(pair);
                pairOffset += 6;
            }
        }
        offset = subtableOffset + subtableLength;
    }
}

// Font class implementations
std::string Font::getFamilyName() const {
    for (const auto& record : names) {
        if (record.nameID == 1) { // Family name
            return record.value;
        }
    }
    return "";
}

std::string Font::getSubfamilyName() const {
    for (const auto& record : names) {
        if (record.nameID == 2) { // Subfamily name
            return record.value;
        }
    }
    return "";
}

// TTFFont implementations
UShort TTFFont::getGlyphID(ULong codePoint) const {
    if (cmaps.empty()) return 0;

    for (const auto& cmap : cmaps) {
        auto gid = cmap.getGlyphID(codePoint);
        if (gid > 0) {
            return static_cast<UShort>(gid);
        }
    }

    return 0;
}

const Glyph* TTFFont::getGlyph(UShort glyphID) const {
    if (glyphID < glyphs.size()) {
        return &glyphs[glyphID];
    }
    return nullptr;
}

const CmapSubtable* TTFFont::findCmap(UShort platformID, UShort encodingID) const {
    for (const auto& cmap : cmaps) {
        if (cmap.platformID == platformID && cmap.platformSpecificID == encodingID) {
            return &cmap;
        }
    }
    // Fallback: first cmap
    if (!cmaps.empty()) return &cmaps[0];
    return nullptr;
}
