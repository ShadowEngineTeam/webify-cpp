#ifndef PARSER_H
#define PARSER_H

#include "font.h"
#include <memory>
#include <string>
#include <vector>

class FontParser {
public:
    // Parse font file and return appropriate Font object
    static std::unique_ptr<Font> parse(const std::string& filename);

private:
    static std::unique_ptr<Font> parseTTF(const std::vector<Byte>& data);
    static std::unique_ptr<Font> parseOTF(const std::vector<Byte>& data);

    // Common parsing utilities
    static void parseHeader(Font& font, const std::vector<Byte>& data);
    static void parseTableDirectories(Font& font, const std::vector<Byte>& data);
    static void parseCommonTables(Font& font, const std::vector<Byte>& data);

    // TTF-specific table parsing
    static void parseHeadTable(Font& font, const std::vector<Byte>& data);
    static void parseHheaTable(Font& font, const std::vector<Byte>& data);
    static void parseHmtxTable(Font& font, const std::vector<Byte>& data);
    static void parseNameTable(Font& font, const std::vector<Byte>& data);
    static void parseMaxpTable(Font& font, const std::vector<Byte>& data);
    static void parseOS2Table(Font& font, const std::vector<Byte>& data);

    // TTF-specific
    static void parseGlyfTable(TTFFont& font, const std::vector<Byte>& data);
    static void parseLocaTable(TTFFont& font, const std::vector<Byte>& data);
    static void parseCmapTable(TTFFont& font, const std::vector<Byte>& data);
    static void parseKernTable(TTFFont& font, const std::vector<Byte>& data);

    // Helper functions
    static UShort readUShort(const std::vector<Byte>& data, size_t offset);
    static Short readShort(const std::vector<Byte>& data, size_t offset);
    static ULong readULong(const std::vector<Byte>& data, size_t offset);
    static Long readLong(const std::vector<Byte>& data, size_t offset);
    static Fixed readFixed(const std::vector<Byte>& data, size_t offset);
    static Byte readByte(const std::vector<Byte>& data, size_t offset);

    static std::vector<Byte> readBytes(const std::vector<Byte>& data, size_t offset, size_t length);
    static std::string readString(const std::vector<Byte>& data, size_t offset, size_t length);

    // Table directory lookup
    static TableDirectory* getTableDirectory(Font& font, ULong tag);
};

#endif // PARSER_H
