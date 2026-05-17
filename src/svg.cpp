#include "generators.h"
#include "utils.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <cassert>

// ---------------------------------------------------------------------------
// Coordinate formatting (matching Haskell formatCoordinate)
// ---------------------------------------------------------------------------
static std::string fmtCoord(double x) {
    int floored = static_cast<int>(std::floor(x));
    if (static_cast<double>(floored) == x) {
        return std::to_string(floored);
    }
    // formatFloat 1: show at most 1 decimal place, no trailing zeros
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << x;
    std::string s = ss.str();
    // Remove trailing zeros after decimal point (but keep at least one digit)
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.size() - 1;
        while (last > dot && s[last] == '0') --last;
        if (last == dot) --last; // keep ".0"
        s = s.substr(0, last + 1);
    }
    return s;
}

// ---------------------------------------------------------------------------
// TrueType contour to SVG path commands (1:1 port of Haskell contourPath)
// ---------------------------------------------------------------------------
// Each point is (x, y, flags) where bit 0 of flags is onCurve
struct Point { double x, y; int flags; };

static std::string contourToSVGPath(const std::vector<Point>& contour) {
    if (contour.empty()) return "";

    // Build a cyclic view: we access points modulo size
    size_t n = contour.size();

    // Start command: M absolute
    std::string result = "M" + fmtCoord(contour[0].x) + " " + fmtCoord(contour[0].y);

    // onCurve check (bit 0 of flags, matching Haskell testBit flag 0)
    auto onCurve = [](const Point& p) { return (p.flags & 0x01) != 0; };

    // midval: a + (b - a) / 2
    auto midval = [](double a, double b) { return a + (b - a) / 2.0; };

    // shortestPath: choose relative (lowercase) vs absolute (uppercase)
    // Each coordinate pair is (relStr, absStr)
    using CoordPair = std::pair<std::string, std::string>;
    auto shortestPath = [](const std::string& cmd, const std::vector<CoordPair>& coords) -> std::string {
        std::string rel = cmd;
        std::string abs = cmd;
        // uppercase command
        std::string absCmd = cmd;
        absCmd[0] = static_cast<char>(std::toupper(absCmd[0]));
        for (size_t i = 0; i < coords.size(); ++i) {
            if (i > 0) {
                rel += " ";
                abs += " ";
            }
            rel += coords[i].first;
            abs += coords[i].second;
        }
        // Also try uppercase with relative coords
        std::string absWithRel = absCmd;
        for (size_t i = 0; i < coords.size(); ++i) {
            if (i > 0) absWithRel += " ";
            absWithRel += coords[i].first;
        }
        // Haskell logic: compare length of relative string vs absolute string
        if (rel.size() < abs.size()) {
            return rel;
        } else {
            return abs;
        }
    };

    // Iterate through contour cyclically up to n steps
    int step = 0;
    double lastx = contour[0].x;
    double lasty = contour[0].y;

    while (step < static_cast<int>(n)) {
        // Current point at index `step`
        size_t i0 = static_cast<size_t>(step) % n;
        size_t i1 = (i0 + 1) % n;
        size_t i2 = (i0 + 2) % n;

        const Point& p0 = contour[i0]; // current
        const Point& p1 = contour[i1]; // next
        const Point& p2 = contour[i2]; // next+1

        bool c0 = onCurve(p0);
        bool c1 = onCurve(p1);
        bool c2 = onCurve(p2);

        // showx, showy, showxy produce (relative, absolute) pairs
        auto showx = [&](double x) -> CoordPair {
            return {fmtCoord(x - lastx), fmtCoord(x)};
        };
        auto showy = [&](double y) -> CoordPair {
            return {fmtCoord(y - lasty), fmtCoord(y)};
        };
        auto showxy = [&](double x, double y) -> CoordPair {
            return {fmtCoord(x - lastx) + " " + fmtCoord(y - lasty),
                    fmtCoord(x) + " " + fmtCoord(y)};
        };

        if (c0 && c1) {
            // True, True, _  : on-curve to on-curve -> line/vertical/horizontal
            if (p0.x == p1.x) {
                result += shortestPath("v", {showy(p1.y)});
            } else if (p0.y == p1.y) {
                result += shortestPath("h", {showx(p1.x)});
            } else {
                result += shortestPath("l", {showxy(p1.x, p1.y)});
            }
            lastx = p1.x;
            lasty = p1.y;
            step += 1;
        } else if (c0 && !c1 && c2) {
            // True, False, True : on-off-on -> quadratic bezier
            result += shortestPath("q", {showxy(p1.x, p1.y), showxy(p2.x, p2.y)});
            lastx = p2.x;
            lasty = p2.y;
            step += 2;
        } else if (c0 && !c1 && !c2) {
            // True, False, False : on-off-off -> quadratic bezier with implicit midpoint
            double mx = midval(p1.x, p2.x);
            double my = midval(p1.y, p2.y);
            result += shortestPath("q", {showxy(p1.x, p1.y), showxy(mx, my)});
            lastx = mx;
            lasty = my;
            step += 2;
        } else if (!c0 && !c1) {
            // False, False, _ : off-off -> smooth quadratic with midpoint of p0 and p1
            double mx = midval(p0.x, p1.x);
            double my = midval(p0.y, p1.y);
            result += shortestPath("t", {showxy(mx, my)});
            lastx = mx;
            lasty = my;
            step += 1;
        } else {
            // False, True, _ : off-on -> smooth quadratic to p1
            result += shortestPath("t", {showxy(p1.x, p1.y)});
            lastx = p1.x;
            lasty = p1.y;
            step += 1;
        }
    }

    result += "Z";
    return result;
}

// ---------------------------------------------------------------------------
// Extract glyph point contours from a glyph (matching Haskell glyphPoints)
// ---------------------------------------------------------------------------
static void collectGlyphContours(const TTFFont& font, UShort glyphID,
                                  double scaleX, double scaleY,
                                  double scale01, double scale10,
                                  short offsetX, short offsetY,
                                  std::vector<std::vector<Point>>& contours)
{
    if (glyphID >= font.glyphs.size()) return;
    const Glyph& glyph = font.glyphs[glyphID];

    if (glyph.numberOfContours == 0) return;

    auto transform = [&](double x, double y, int flags) -> Point {
        double tx = x * scaleX + y * scale10 + static_cast<double>(offsetX);
        double ty = y * scaleY + x * scale01 + static_cast<double>(offsetY);
        return {tx, ty, flags};
    };

    if (!glyph.isComposite) {
        // Simple glyph: split points by endPtsOfContours (matching Haskell:
        //   endPts = map fromIntegral $ sEndPtsOfCountours glyph
        //   pts = zipWith3 (\x y f -> (fromIntegral x, fromIntegral y, fromIntegral f)) ...
        //   splitPts (ac, offset', pts') x = (take (x - offset') pts' : ac, x, drop (x - offset') pts')
        //   (bpts, _, _) = foldl' splitPts ([], -1, pts) endPts
        //   reverse bpts
        const auto& sg = glyph.simpleGlyph;
        if (sg.xCoordinates.empty() || sg.endPtsOfContours.empty()) return;

        // Build point list
        std::vector<Point> pts;
        for (size_t k = 0; k < sg.xCoordinates.size(); ++k) {
            pts.push_back(transform(
                static_cast<double>(sg.xCoordinates[k]),
                static_cast<double>(sg.yCoordinates[k]),
                (k < sg.flags.size()) ? static_cast<int>(sg.flags[k]) : 0));
        }

        // Split by endPtsOfContours (Haskell foldl' splitPts ([], -1, pts) endPts)
        int offset = -1;
        size_t ptsOffset = 0;
        for (UShort endPt : sg.endPtsOfContours) {
            if (endPt >= pts.size()) break;
            size_t count = static_cast<size_t>(endPt) - ptsOffset + 1;
            std::vector<Point> contour(pts.begin() + static_cast<long long>(ptsOffset),
                                       pts.begin() + static_cast<long long>(ptsOffset) + static_cast<long long>(count));
            if (!contour.empty()) {
                contours.push_back(std::move(contour));
            }
            ptsOffset = static_cast<size_t>(endPt) + 1;
            offset = static_cast<int>(endPt);
        }
    } else {
        // Composite glyph: recurse into each component (matching Haskell)
        //   cglyhPoints cg = map (map (transform cg)) (glyphPoints (glyf $ cGlyphIndex cg) ttf)
        //   transform cg (x, y, flag) = (x * cXScale cg + y * cScale10 cg + fromIntegral (cXoffset cg),
        //                                  y * cYScale cg + x * cScale01 cg + fromIntegral (cYoffset cg), flag)
        for (const auto& comp : glyph.compositeGlyph.components) {
            double sx = comp.xScale;
            double sy = comp.yScale;
            double s01 = comp.scale01;
            double s10 = comp.scale10;
            short ox = comp.xOffset;
            short oy = comp.yOffset;
            // Compose transformations
            double nsx = scaleX * sx;
            double nsy = scaleY * sy;
            double ns01 = scaleX * s01;
            double ns10 = scaleY * s10;
            short nox = static_cast<short>(offsetX + static_cast<short>(ox * scaleX));
            short noy = static_cast<short>(offsetY + static_cast<short>(oy * scaleY));
            collectGlyphContours(font, comp.glyphIndex, nsx, nsy, ns01, ns10, nox, noy, contours);
        }
    }
}

// ---------------------------------------------------------------------------
// Convert a glyph to SVG path string
// ---------------------------------------------------------------------------
static std::string glyphToSVGPath(const TTFFont& font, UShort glyphID) {
    std::vector<std::vector<Point>> contours;
    collectGlyphContours(font, glyphID, 1.0, 1.0, 0.0, 0.0, 0, 0, contours);
    std::string result;
    for (const auto& c : contours)
        result += contourToSVGPath(c);
    return result;
}

// ---------------------------------------------------------------------------
// XML escaping (matching Haskell escapeXMLChar)
// ---------------------------------------------------------------------------
std::string FontGenerator::xmlEscape(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default:
                unsigned char uc = static_cast<unsigned char>(c);
                if (uc <= 0x7f && std::isprint(uc)) {
                    result += c;
                } else {
                    // Hex escape: &#xXX;
                    std::ostringstream ss;
                    ss << "&#x" << std::hex << static_cast<int>(uc) << ";";
                    result += ss.str();
                }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Valid SVG character codes (matching Haskell validCharCode)
// ---------------------------------------------------------------------------
static bool validCharCode(ULong cp) {
    return cp == 0x9 || cp == 0xA || cp == 0xD ||
           (cp >= 0x20 && cp <= 0xD7FF) ||
           (cp >= 0xE000 && cp <= 0xFFFD) ||
           (cp >= 0x10000 && cp <= 0x10FFFF);
}

// ---------------------------------------------------------------------------
// Unicode escape for SVG glyph unicode attribute
// ---------------------------------------------------------------------------
static std::string unicodeEscape(ULong codePoint) {
    // Match Haskell: escapeXMLChar $ chr code
    if (codePoint < 128) {
        char c = static_cast<char>(codePoint);
        std::string s;
        s += c;
        return FontGenerator::xmlEscape(s);
    }
    std::ostringstream ss;
    ss << "&#x" << std::hex << codePoint << ";";
    return ss.str();
}

// ---------------------------------------------------------------------------
// GlyphId to Code mapping (matching Haskell glyphIdToCode)
// ---------------------------------------------------------------------------
static std::map<int, std::vector<int>> buildGlyphIdToCode(const CmapSubtable& cmap) {
    std::map<int, std::vector<int>> m;
    for (const auto& pair : cmap.charToGlyph) {
        int code = static_cast<int>(pair.first);
        int gid = static_cast<int>(pair.second);
        if (gid > 0) {
            m[gid].push_back(code);
        }
    }
    return m;
}

// ---------------------------------------------------------------------------
// SVG document builder (matching Haskell svgbody, svgGlyphs, fontFace, etc.)
// Raw string version used when libxml2 is not available
// ---------------------------------------------------------------------------
#ifndef HAVE_LIBXML2
std::vector<Byte> FontGenerator::buildSVGDocument(const TTFFont& font, bool enableKerning,
                                                  UShort cmapPlatformID, UShort cmapEncodingID)
{
    std::ostringstream svg;

    // Find the requested cmap subtable
    const CmapSubtable* cmap = font.findCmap(cmapPlatformID, cmapEncodingID);
    if (!cmap && !font.cmaps.empty()) {
        cmap = &font.cmaps[0];
    }

    // SVG document prologue (matching Haskell: svgDocType, svgDocInfo)
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
        << "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" >\n";

    // Compute viewBox for the SVG
    int viewW = font.head.unitsPerEm;
    int viewH = static_cast<int>(font.hhea.ascender) - static_cast<int>(font.hhea.descender);

    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
        << viewW << " " << viewH << "\">\n";

    // Gather valid glyph codes and compute averageAdvanceX
    std::vector<int> validCodes;
    std::vector<int> advances;
    if (cmap) {
        for (const auto& pair : cmap->charToGlyph) {
            ULong cp = pair.first;
            UShort gid = pair.second;
            if (gid > 0 && validCharCode(cp)) {
                validCodes.push_back(static_cast<int>(cp));
                if (gid < font.hmtx.size())
                    advances.push_back(font.hmtx[gid].advanceWidth);
            }
        }
    }

    // averageAdvanceX = maxDuplicate (matching Haskell maxDuplicate)
    int avgAdvanceX = font.head.unitsPerEm;
    if (!advances.empty()) {
        avgAdvanceX = Utils::maxDuplicate(advances);
    }

    // Kerning pairs from all kern tables (matching Haskell svgKerns)
    struct KernInfo {
        UShort left, right;
        Short value;
        UShort coverage;
    };
    std::vector<KernInfo> validKerns;

    std::map<int, std::vector<int>> gidToCode;
    if (cmap && enableKerning) {
        gidToCode = buildGlyphIdToCode(*cmap);
    }

    if (enableKerning && !font.kernPairs.empty()) {
        for (const auto& kp : font.kernPairs) {
            // validKernPair: glyphId cmapTable left > 0 && glyphId cmapTable right > 0
            if (cmap && cmap->getGlyphID(kp.left) > 0 && cmap->getGlyphID(kp.right) > 0) {
                validKerns.push_back({kp.left, kp.right, kp.value, 0});
            }
        }
    }

    // Build SVG font
    svg << "  <defs>\n";
    svg << "    <font horiz-adv-x=\"" << avgAdvanceX << "\">\n";

    // font-face element (matching Haskell fontFace)
    svg << "      <font-face font-family=\"" << xmlEscape(font.getFamilyName()) << "\" "
        << "units-per-em=\"" << font.head.unitsPerEm << "\" "
        << "ascent=\"" << font.hhea.ascender << "\" "
        << "descent=\"" << font.hhea.descender << "\" />\n";

    // missing-glyph (glyph 0)
    {
        std::string path = glyphToSVGPath(font, 0);
        int advance0 = (!font.hmtx.empty()) ? font.hmtx[0].advanceWidth : avgAdvanceX;
        svg << "      <missing-glyph horiz-adv-x=\"" << advance0 << "\"";
        if (!path.empty()) svg << " d=\"" << path << "\"";
        svg << " />\n";
    }

    // Glyph elements (matching Haskell svgGlyphs)
    for (int code : validCodes) {
        if (!cmap) break;
        UShort gid = static_cast<UShort>(cmap->getGlyphID(static_cast<ULong>(code)));
        if (gid == 0 || gid >= font.glyphs.size()) continue;

        std::string path = glyphToSVGPath(font, gid);
        int advanceX = (gid < font.hmtx.size()) ? font.hmtx[gid].advanceWidth : avgAdvanceX;

        svg << "      <glyph unicode=\"" << unicodeEscape(static_cast<ULong>(code)) << "\"";
        if (advanceX != avgAdvanceX)
            svg << " horiz-adv-x=\"" << advanceX << "\"";
        if (!path.empty())
            svg << " d=\"" << path << "\"";
        svg << " />\n";
    }

    // Kerning elements (matching Haskell svgKerns)
    if (enableKerning && !validKerns.empty()) {
        for (const auto& kern : validKerns) {
            // Unicode strings for left/right glyph IDs
            std::string u1, u2;
            auto it1 = gidToCode.find(static_cast<int>(kern.left));
            auto it2 = gidToCode.find(static_cast<int>(kern.right));
            if (it1 != gidToCode.end() && it2 != gidToCode.end()) {
                for (size_t i = 0; i < it1->second.size(); ++i) {
                    if (i > 0) u1 += ",";
                    u1 += unicodeEscape(static_cast<ULong>(it1->second[i]));
                }
                for (size_t i = 0; i < it2->second.size(); ++i) {
                    if (i > 0) u2 += ",";
                    u2 += unicodeEscape(static_cast<ULong>(it2->second[i]));
                }
                std::string ktype = (kern.coverage & 0x01) ? "vkern" : "hkern";
                svg << "      <" << ktype << " u1=\"" << u1 << "\" "
                    << "u2=\"" << u2 << "\" "
                    << "k=\"" << (-kern.value) << "\" />\n";
            }
        }
    }

    svg << "    </font>\n";
    svg << "  </defs>\n";

    // Test text (matching Haskell testText)
    std::string familyName = font.getFamilyName();
    svg << "  <g style=\"font-family: " << familyName << "; font-size:50;fill:black\">\n";
    std::vector<std::string> testStrings = {
        "!\"#$%&'()*+,-./0123456789:;å<>?",
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
        "` abcdefghijklmnopqrstuvwxyz|{}~"
    };
    for (size_t i = 0; i < testStrings.size(); ++i) {
        svg << "    <text x=\"20\" y=\"" << ((i + 1) * 50) << "\">"
            << xmlEscape(testStrings[i]) << "</text>\n";
    }
    svg << "  </g>\n";

    svg << "</svg>\n";

    std::string svgStr = svg.str();
    return std::vector<Byte>(svgStr.begin(), svgStr.end());
}
#endif // !HAVE_LIBXML2
