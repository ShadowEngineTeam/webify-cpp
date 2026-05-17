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

#ifdef HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

// ---------------------------------------------------------------------------
// Coordinate formatting (same as svg.cpp - inline to avoid dependency)
// ---------------------------------------------------------------------------
static std::string fmtCoordSVG(double x) {
    int floored = static_cast<int>(std::floor(x));
    if (static_cast<double>(floored) == x) {
        return std::to_string(floored);
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << x;
    std::string s = ss.str();
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.size() - 1;
        while (last > dot && s[last] == '0') --last;
        if (last == dot) --last;
        s = s.substr(0, last + 1);
    }
    return s;
}

struct PtSVG { double x, y; int flags; };

static std::string contourToSVGPathLibXML2(const std::vector<PtSVG>& contour) {
    if (contour.empty()) return "";
    size_t n = contour.size();
    std::string result = "M" + fmtCoordSVG(contour[0].x) + " " + fmtCoordSVG(contour[0].y);
    auto onCurve = [](const PtSVG& p) { return (p.flags & 0x01) != 0; };
    auto midval = [](double a, double b) { return a + (b - a) / 2.0; };
    using CoordPair = std::pair<std::string, std::string>;
    auto shortestPath = [](const std::string& cmd, const std::vector<CoordPair>& coords) -> std::string {
        std::string rel = cmd;
        std::string abs = cmd;
        std::string absCmd = cmd;
        absCmd[0] = static_cast<char>(std::toupper(absCmd[0]));
        for (size_t i = 0; i < coords.size(); ++i) {
            if (i > 0) { rel += " "; abs += " "; }
            rel += coords[i].first;
            abs += coords[i].second;
        }
        if (rel.size() < abs.size()) return rel;
        return abs;
    };
    int step = 0;
    double lastx = contour[0].x;
    double lasty = contour[0].y;
    while (step < static_cast<int>(n)) {
        size_t i0 = static_cast<size_t>(step) % n;
        size_t i1 = (i0 + 1) % n;
        size_t i2 = (i0 + 2) % n;
        const PtSVG& p0 = contour[i0];
        const PtSVG& p1 = contour[i1];
        const PtSVG& p2 = contour[i2];
        bool c0 = onCurve(p0), c1 = onCurve(p1), c2 = onCurve(p2);
        auto showx = [&](double x) -> CoordPair {
            return {fmtCoordSVG(x - lastx), fmtCoordSVG(x)};
        };
        auto showy = [&](double y) -> CoordPair {
            return {fmtCoordSVG(y - lasty), fmtCoordSVG(y)};
        };
        auto showxy = [&](double x, double y) -> CoordPair {
            return {fmtCoordSVG(x - lastx) + " " + fmtCoordSVG(y - lasty),
                    fmtCoordSVG(x) + " " + fmtCoordSVG(y)};
        };
        if (c0 && c1) {
            if (p0.x == p1.x) result += shortestPath("v", {showy(p1.y)});
            else if (p0.y == p1.y) result += shortestPath("h", {showx(p1.x)});
            else result += shortestPath("l", {showxy(p1.x, p1.y)});
            lastx = p1.x; lasty = p1.y; step += 1;
        } else if (c0 && !c1 && c2) {
            result += shortestPath("q", {showxy(p1.x, p1.y), showxy(p2.x, p2.y)});
            lastx = p2.x; lasty = p2.y; step += 2;
        } else if (c0 && !c1 && !c2) {
            double mx = midval(p1.x, p2.x), my = midval(p1.y, p2.y);
            result += shortestPath("q", {showxy(p1.x, p1.y), showxy(mx, my)});
            lastx = mx; lasty = my; step += 2;
        } else if (!c0 && !c1) {
            double mx = midval(p0.x, p1.x), my = midval(p0.y, p1.y);
            result += shortestPath("t", {showxy(mx, my)});
            lastx = mx; lasty = my; step += 1;
        } else {
            result += shortestPath("t", {showxy(p1.x, p1.y)});
            lastx = p1.x; lasty = p1.y; step += 1;
        }
    }
    result += "Z";
    return result;
}

static void collectGlyphContoursLibXML2(const TTFFont& font, UShort glyphID,
                                         double scaleX, double scaleY,
                                         double scale01, double scale10,
                                         short offsetX, short offsetY,
                                         std::vector<std::vector<PtSVG>>& contours)
{
    if (glyphID >= font.glyphs.size()) return;
    const Glyph& glyph = font.glyphs[glyphID];
    if (glyph.numberOfContours == 0) return;
    auto transform = [&](double x, double y, int flags) -> PtSVG {
        double tx = x * scaleX + y * scale10 + static_cast<double>(offsetX);
        double ty = y * scaleY + x * scale01 + static_cast<double>(offsetY);
        return {tx, ty, flags};
    };
    if (!glyph.isComposite) {
        const auto& sg = glyph.simpleGlyph;
        if (sg.xCoordinates.empty() || sg.endPtsOfContours.empty()) return;
        std::vector<PtSVG> pts;
        for (size_t k = 0; k < sg.xCoordinates.size(); ++k) {
            pts.push_back(transform(
                static_cast<double>(sg.xCoordinates[k]),
                static_cast<double>(sg.yCoordinates[k]),
                (k < sg.flags.size()) ? static_cast<int>(sg.flags[k]) : 0));
        }
        size_t ptsOffset = 0;
        for (UShort endPt : sg.endPtsOfContours) {
            if (endPt >= pts.size()) break;
            size_t count = static_cast<size_t>(endPt) - ptsOffset + 1;
            std::vector<PtSVG> contour(pts.begin() + static_cast<long long>(ptsOffset),
                                       pts.begin() + static_cast<long long>(ptsOffset) + static_cast<long long>(count));
            if (!contour.empty()) contours.push_back(std::move(contour));
            ptsOffset = static_cast<size_t>(endPt) + 1;
        }
    } else {
        for (const auto& comp : glyph.compositeGlyph.components) {
            double nsx = scaleX * comp.xScale;
            double nsy = scaleY * comp.yScale;
            double ns01 = scaleX * comp.scale01;
            double ns10 = scaleY * comp.scale10;
            short nox = static_cast<short>(offsetX + static_cast<short>(comp.xOffset * scaleX));
            short noy = static_cast<short>(offsetY + static_cast<short>(comp.yOffset * scaleY));
            collectGlyphContoursLibXML2(font, comp.glyphIndex, nsx, nsy, ns01, ns10, nox, noy, contours);
        }
    }
}

static std::string glyphToSVGPathLibXML2(const TTFFont& font, UShort glyphID) {
    std::vector<std::vector<PtSVG>> contours;
    collectGlyphContoursLibXML2(font, glyphID, 1.0, 1.0, 0.0, 0.0, 0, 0, contours);
    std::string result;
    for (const auto& c : contours)
        result += contourToSVGPathLibXML2(c);
    return result;
}

static bool validCharCodeSVG(ULong cp) {
    return cp == 0x9 || cp == 0xA || cp == 0xD ||
           (cp >= 0x20 && cp <= 0xD7FF) ||
           (cp >= 0xE000 && cp <= 0xFFFD) ||
           (cp >= 0x10000 && cp <= 0x10FFFF);
}

static std::map<int, std::vector<int>> buildGidToCodeLibXML2(const CmapSubtable& cmap) {
    std::map<int, std::vector<int>> m;
    for (const auto& pair : cmap.charToGlyph) {
        int gid = static_cast<int>(pair.second);
        if (gid > 0) m[gid].push_back(static_cast<int>(pair.first));
    }
    return m;
}

// Helper: create a text node with escaped content
static xmlNodePtr xmlNewTextChildEscaped(xmlNodePtr parent, const char* content) {
    return xmlNewTextChild(parent, nullptr, BAD_CAST content, nullptr);
}

// ---------------------------------------------------------------------------
// libxml2-based SVG document builder
// Uses libxml2 tree API to build proper XML (matching Haskell Text.XML.Generator)
// ---------------------------------------------------------------------------
std::vector<Byte> FontGenerator::buildSVGDocument(const TTFFont& font, bool enableKerning,
                                                  UShort cmapPlatformID, UShort cmapEncodingID)
{
    // Find cmap
    const CmapSubtable* cmap = font.findCmap(cmapPlatformID, cmapEncodingID);
    if (!cmap && !font.cmaps.empty()) cmap = &font.cmaps[0];

    // Gather valid codes and advances
    std::vector<int> validCodes;
    std::vector<int> advances;
    if (cmap) {
        for (const auto& pair : cmap->charToGlyph) {
            ULong cp = pair.first;
            UShort gid = pair.second;
            if (gid > 0 && validCharCodeSVG(cp)) {
                validCodes.push_back(static_cast<int>(cp));
                if (gid < font.hmtx.size())
                    advances.push_back(font.hmtx[gid].advanceWidth);
            }
        }
    }
    int avgAdvanceX = font.head.unitsPerEm;
    if (!advances.empty()) avgAdvanceX = Utils::maxDuplicate(advances);

    // Kerning setup
    std::map<int, std::vector<int>> gidToCode;
    if (cmap && enableKerning) gidToCode = buildGidToCodeLibXML2(*cmap);

    struct KernInfo { UShort left, right; Short value; };
    std::vector<KernInfo> validKerns;
    if (enableKerning && !font.kernPairs.empty()) {
        for (const auto& kp : font.kernPairs) {
            if (cmap && cmap->getGlyphID(kp.left) > 0 && cmap->getGlyphID(kp.right) > 0)
                validKerns.push_back({kp.left, kp.right, kp.value});
        }
    }

    // Create XML document
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    doc->encoding = xmlCharStrdup("UTF-8");
    doc->standalone = 0;

    // Create SVG element
    xmlNodePtr svgNode = xmlNewNode(nullptr, BAD_CAST "svg");
    xmlNewProp(svgNode, BAD_CAST "xmlns", BAD_CAST "http://www.w3.org/2000/svg");
    int viewW = font.head.unitsPerEm;
    int viewH = static_cast<int>(font.hhea.ascender) - static_cast<int>(font.hhea.descender);
    std::string viewBox = "0 0 " + std::to_string(viewW) + " " + std::to_string(viewH);
    xmlNewProp(svgNode, BAD_CAST "viewBox", BAD_CAST viewBox.c_str());
    xmlDocSetRootElement(doc, svgNode);

    // defs > font
    xmlNodePtr defs = xmlNewChild(svgNode, nullptr, BAD_CAST "defs", nullptr);
    xmlNodePtr fontNode = xmlNewChild(defs, nullptr, BAD_CAST "font", nullptr);
    std::string horizAdvX = std::to_string(avgAdvanceX);
    xmlNewProp(fontNode, BAD_CAST "horiz-adv-x", BAD_CAST horizAdvX.c_str());

    // font-face
    xmlNodePtr fontFace = xmlNewChild(fontNode, nullptr, BAD_CAST "font-face", nullptr);
    xmlNewProp(fontFace, BAD_CAST "font-family", BAD_CAST font.getFamilyName().c_str());
    xmlNewProp(fontFace, BAD_CAST "units-per-em", BAD_CAST std::to_string(font.head.unitsPerEm).c_str());
    xmlNewProp(fontFace, BAD_CAST "ascent", BAD_CAST std::to_string(font.hhea.ascender).c_str());
    xmlNewProp(fontFace, BAD_CAST "descent", BAD_CAST std::to_string(font.hhea.descender).c_str());

    // missing-glyph (glyph 0)
    {
        std::string path = glyphToSVGPathLibXML2(font, 0);
        int advance0 = (!font.hmtx.empty()) ? font.hmtx[0].advanceWidth : avgAdvanceX;
        xmlNodePtr mg = xmlNewChild(fontNode, nullptr, BAD_CAST "missing-glyph", nullptr);
        xmlNewProp(mg, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(advance0).c_str());
        if (!path.empty())
            xmlNewProp(mg, BAD_CAST "d", BAD_CAST path.c_str());
    }

    // Glyph elements
    for (int code : validCodes) {
        if (!cmap) break;
        UShort gid = static_cast<UShort>(cmap->getGlyphID(static_cast<ULong>(code)));
        if (gid == 0 || gid >= font.glyphs.size()) continue;
        std::string path = glyphToSVGPathLibXML2(font, gid);
        int advanceX = (gid < font.hmtx.size()) ? font.hmtx[gid].advanceWidth : avgAdvanceX;

        xmlNodePtr glyphNode = xmlNewChild(fontNode, nullptr, BAD_CAST "glyph", nullptr);
        // Unicode attribute
        std::string unicodeAttr;
        if (code < 128) {
            char c = static_cast<char>(code);
            switch (c) {
                case '<': unicodeAttr = "&lt;"; break;
                case '>': unicodeAttr = "&gt;"; break;
                case '&': unicodeAttr = "&amp;"; break;
                case '"': unicodeAttr = "&quot;"; break;
                default: unicodeAttr = std::string(1, c);
            }
        } else {
            std::ostringstream ss;
            ss << "&#x" << std::hex << code << ";";
            unicodeAttr = ss.str();
        }
        xmlNewProp(glyphNode, BAD_CAST "unicode", BAD_CAST unicodeAttr.c_str());
        if (advanceX != avgAdvanceX)
            xmlNewProp(glyphNode, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(advanceX).c_str());
        if (!path.empty())
            xmlNewProp(glyphNode, BAD_CAST "d", BAD_CAST path.c_str());
    }

    // Kerning elements
    if (!validKerns.empty()) {
        for (const auto& kern : validKerns) {
            auto it1 = gidToCode.find(static_cast<int>(kern.left));
            auto it2 = gidToCode.find(static_cast<int>(kern.right));
            if (it1 == gidToCode.end() || it2 == gidToCode.end()) continue;
            std::string u1, u2;
            for (size_t i = 0; i < it1->second.size(); ++i) {
                if (i > 0) u1 += ",";
                std::ostringstream ss;
                ss << "&#x" << std::hex << it1->second[i] << ";";
                u1 += ss.str();
            }
            for (size_t i = 0; i < it2->second.size(); ++i) {
                if (i > 0) u2 += ",";
                std::ostringstream ss;
                ss << "&#x" << std::hex << it2->second[i] << ";";
                u2 += ss.str();
            }
            xmlNodePtr kernNode = xmlNewChild(fontNode, nullptr, BAD_CAST "hkern", nullptr);
            xmlNewProp(kernNode, BAD_CAST "u1", BAD_CAST u1.c_str());
            xmlNewProp(kernNode, BAD_CAST "u2", BAD_CAST u2.c_str());
            xmlNewProp(kernNode, BAD_CAST "k", BAD_CAST std::to_string(-kern.value).c_str());
        }
    }

    // Test text (matching Haskell testText)
    xmlNodePtr g = xmlNewChild(svgNode, nullptr, BAD_CAST "g", nullptr);
    std::string familyName = font.getFamilyName();
    std::string style = "font-family: " + familyName + "; font-size:50;fill:black";
    xmlNewProp(g, BAD_CAST "style", BAD_CAST style.c_str());
    std::vector<std::string> testStrings = {
        "!\"#$%&'()*+,-./0123456789:;å<>?",
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
        "` abcdefghijklmnopqrstuvwxyz|{}~"
    };
    for (size_t i = 0; i < testStrings.size(); ++i) {
        xmlNodePtr textNode = xmlNewChild(g, nullptr, BAD_CAST "text", nullptr);
        xmlNewProp(textNode, BAD_CAST "x", BAD_CAST "20");
        xmlNewProp(textNode, BAD_CAST "y", BAD_CAST std::to_string((i + 1) * 50).c_str());
        xmlNodePtr content = xmlNewText(BAD_CAST testStrings[i].c_str());
        xmlAddChild(textNode, content);
    }

    // Serialize to string
    xmlChar* xmlBuffer = nullptr;
    int docSize = 0;
    xmlDocDumpMemory(doc, &xmlBuffer, &docSize);

    std::vector<Byte> result;
    if (xmlBuffer && docSize > 0) {
        result.assign(xmlBuffer, xmlBuffer + docSize);
    }
    xmlFree(xmlBuffer);
    xmlFreeDoc(doc);

    return result;
}

#endif // HAVE_LIBXML2
