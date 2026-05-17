#ifndef GENERATORS_H
#define GENERATORS_H

#include "font.h"
#include <string>
#include <vector>
#include <memory>

class FontGenerator {
public:
    // Generate EOT file
    static bool generateEOT(const Font& font, const std::string& outputPath);

    // Generate WOFF file
    static bool generateWOFF(const Font& font, const std::string& outputPath, bool useZopfli = false);

    // Generate WOFF2 file
    static bool generateWOFF2(const Font& font, const std::string& outputPath);

    // Generate SVG file (TTF only)
    static bool generateSVG(const TTFFont& font, const std::string& outputPath,
                          bool enableKerning = false, UShort cmapPlatformID = 0xFFFF,
                          UShort cmapEncodingID = 0xFFFF);

private:
    // WOFF helpers
    static std::vector<Byte> compressTable(const std::vector<Byte>& data);
    static std::vector<Byte> compressTableZlib(const std::vector<Byte>& data);
    static std::vector<Byte> compressTableZopfli(const std::vector<Byte>& data);
    static std::vector<Byte> compressBrotli(const std::vector<Byte>& data);

    static std::vector<Byte> buildSVGDocument(const TTFFont& font, bool enableKerning,
                                              UShort cmapPlatformID, UShort cmapEncodingID);

public:
    // SVG helpers (public so internal helpers can use them)
    static std::string xmlEscape(const std::string& str);
    // UTF-16LE encoding helper (used by static functions in eot.cpp)
    static std::string encodeUTF16LE(const std::string& str);
};

#endif // GENERATORS_H
