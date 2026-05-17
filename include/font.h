#ifndef FONT_H
#define FONT_H

#include <cstdint>
#include <map>
#include <vector>
#include <string>
#include <memory>

// Primitive types for font parsing
using Byte = uint8_t;
using UShort = uint16_t;
using Short = int16_t;
using ULong = uint32_t;
using Long = int32_t;
using FWord = int16_t;
using UFWord = uint16_t;

// Fixed-point number (16.16 format)
struct Fixed {
    uint32_t value;
    Fixed() : value(0) {}
    explicit Fixed(uint32_t v) : value(v) {}
    double toDouble() const {
        return (value >> 16) + ((value & 0xFFFF) / 65536.0);
    }
};

// Table directory entry
struct TableDirectory {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
};

// Head table
struct HeadTable {
    Fixed version;
    Fixed fontRevision;
    ULong checksumAdjustment;
    ULong magicNumber;
    UShort flags;
    UShort unitsPerEm;
    Long created;
    Long modified;
    Short xMin;
    Short yMin;
    Short xMax;
    Short yMax;
    UShort macStyle;
    UShort indexToLocFormat;
};

// Hhea table
struct HheaTable {
    Fixed version;
    FWord ascender;
    FWord descender;
    FWord lineGap;
    UFWord advanceWidthMax;
    FWord minLeftSideBearing;
    FWord minRightSideBearing;
    FWord xMaxExtent;
    Short caretSlopeRise;
    Short caretSlopeRun;
    Short reserved1;
    Short reserved2;
    Short reserved3;
    Short reserved4;
    Short reserved5;
    Short metricDataFormat;
    UShort numberOfHMetrics;
};

// Hmtx table entry
struct HMetric {
    UShort advanceWidth;
    Short lsb;
};

// Name table record
struct NameRecord {
    UShort platformID;
    UShort platformSpecificID;
    UShort languageID;
    UShort nameID;
    UShort length;
    UShort offset;
    std::string value;
};

// Maxp table
struct MaxpTable {
    Fixed version;
    UShort numGlyphs;
    UShort maxPoints;
    UShort maxContours;
    UShort maxCompositePoints;
    UShort maxCompositeContours;
    UShort maxZones;
    UShort maxTwilightPoints;
    UShort maxStorage;
    UShort maxFunctionDefs;
    UShort maxInstructionDefs;
    UShort maxStackElements;
    UShort maxSizeOfInstructions;
    UShort maxComponentElements;
    UShort maxComponentDepth;
};

// OS/2 table (simplified)
struct OS2Table {
    UShort version;
    Short xAvgCharWidth;
    UShort usWeightClass;
    UShort usWidthClass;
    UShort fsType;
    Short ySubscriptXSize;
    Short ySubscriptYSize;
    Short ySubscriptXOffset;
    Short ySubscriptYOffset;
    Short ySuperscriptXSize;
    Short ySuperscriptYSize;
    Short ySuperscriptXOffset;
    Short ySuperscriptYOffset;
    Short yStrikeoutSize;
    Short yStrikeoutPosition;
    Short sFamilyClass;
    std::vector<Byte> panose;
    ULong ulUnicodeRange1;
    ULong ulUnicodeRange2;
    ULong ulUnicodeRange3;
    ULong ulUnicodeRange4;
    std::string achVendID;
    UShort fsSelection;
    UShort usFirstCharIndex;
    UShort usLastCharIndex;
    Short sTypoAscender;
    Short sTypoDescender;
    Short sTypoLineGap;
    UShort usWinAscent;
    UShort usWinDescent;
    ULong ulCodePageRange1;
    ULong ulCodePageRange2;
};

// Glyph data structures
struct SimpleGlyph {
    std::vector<UShort> endPtsOfContours;
    std::vector<Byte> flags;
    std::vector<Short> xCoordinates;
    std::vector<Short> yCoordinates;
    std::vector<Byte> instructions;
};

struct CompositeGlyph {
    struct Component {
        UShort flags;
        UShort glyphIndex;
        Short xOffset;
        Short yOffset;
        Short argument1;
        Short argument2;
        double xScale;
        double yScale;
        double scale01;
        double scale10;
    };
    std::vector<Component> components;
    UShort numInstructions;
    std::vector<Byte> instructions;
};

struct Glyph {
    Short numberOfContours;
    Short xMin;
    Short yMin;
    Short xMax;
    Short yMax;
    bool isComposite;
    SimpleGlyph simpleGlyph;
    CompositeGlyph compositeGlyph;
};

// Cmap format 4 segment
struct CmapSegment {
    UShort endCode;
    UShort startCode;
    UShort idDelta;
    UShort idRangeOffset;
};

// Cmap format 12 sequential map group
struct CmapFormat12Group {
    ULong startCharCode;
    ULong endCharCode;
    ULong startGlyphID;
};

// Cmap subtable
struct CmapSubtable {
    UShort platformID;
    UShort platformSpecificID;
    UShort format;
    std::map<ULong, UShort> charToGlyph; // populated after lookup

    // Format 4 data
    std::vector<CmapSegment> segments;
    std::vector<UShort> glyphIdArray;

    // Format 0 data
    std::vector<Byte> glyphIds0;

    // Format 6 data
    UShort firstCode;
    UShort entryCount;
    std::vector<UShort> glyphIds6;

    // Format 12 data
    std::vector<CmapFormat12Group> groups12;

    void populateCharToGlyph();
    ULong getGlyphID(ULong codePoint) const;
};

// Kern subtable
struct KernPair {
    UShort left;
    UShort right;
    Short value;
};

// Base Font class (mirrors Haskell Font typeclass)
class Font {
public:
    virtual ~Font() = default;

    Fixed version;
    UShort numTables;
    UShort searchRange;
    UShort entrySelector;
    UShort rangeShift;
    std::map<ULong, TableDirectory> tableDirectories;
    std::vector<Byte> rawBytes;

    // Common tables
    HeadTable head;
    HheaTable hhea;
    std::vector<HMetric> hmtx;
    MaxpTable maxp;
    OS2Table os2;
    std::vector<NameRecord> names;

    virtual bool isOTF() const = 0;

    // Font typeclass methods (matching Haskell)
    virtual std::vector<Byte> getOS2Panose() const;
    virtual UShort getOS2FsSelection() const;
    virtual UShort getOS2UsWeightClass() const;
    virtual ULong getOS2UlUnicodeRange1() const;
    virtual ULong getOS2UlUnicodeRange2() const;
    virtual ULong getOS2UlUnicodeRange3() const;
    virtual ULong getOS2UlUnicodeRange4() const;
    virtual ULong getOS2UlCodePageRange1() const;
    virtual ULong getOS2UlCodePageRange2() const;
    virtual ULong getHeadCheckSumAdjustment() const;

    UShort getOS2Weight() const { return os2.usWeightClass; }
    std::string getFamilyName() const;
    std::string getSubfamilyName() const;
    std::string getFullName() const;
    std::string getVersionName() const;

    // Name lookup by ID
    const NameRecord* findName(UShort nameID) const;
};

// TTF Font
class TTFFont : public Font {
public:
    std::vector<Glyph> glyphs;
    std::vector<Long> loca;
    std::vector<CmapSubtable> cmaps;
    std::vector<KernPair> kernPairs;

    bool isOTF() const override { return false; }

    UShort getGlyphID(ULong codePoint) const;
    const Glyph* getGlyph(UShort glyphID) const;
    const CmapSubtable* findCmap(UShort platformID, UShort encodingID) const;
};

// OTF Font
class OTFFont : public Font {
public:
    bool isOTF() const override { return true; }
};

#endif // FONT_H
