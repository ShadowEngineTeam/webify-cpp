#include "generators.h"
#include "utils.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <map>
#include <vector>
#include <string>
#include <iostream>

#ifdef HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/encoding.h>
#endif

#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#ifdef HAVE_HARFBUZZ
#include <hb.h>
#include <hb-ft.h>
#endif
#endif

// ---------------------------------------------------------------------------
// Coordinate formatting
// ---------------------------------------------------------------------------
static std::string fmtCoord(double x) {
    int floored = static_cast<int>(std::floor(x));
    if (static_cast<double>(floored) == x)
        return std::to_string(floored);
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

// ---------------------------------------------------------------------------
// Valid SVG character codes
// ---------------------------------------------------------------------------
static bool validCharCode(unsigned long cp) {
    return cp == 0x9 || cp == 0xA || cp == 0xD ||
           (cp >= 0x20 && cp <= 0xD7FF) ||
           (cp >= 0xE000 && cp <= 0xFFFD) ||
           (cp >= 0x10000 && cp <= 0x10FFFF);
}

// ---------------------------------------------------------------------------
// Unicode escape for raw string SVG (&#xHH; format)
// ---------------------------------------------------------------------------
static std::string unicodeEscape(unsigned long cp) {
    if (cp < 128) {
        char c = static_cast<char>(cp);
        std::string s;
        s += c;
        return FontGenerator::xmlEscape(s);
    }
    std::ostringstream ss;
    ss << "&#x" << std::hex << cp << ";";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Codepoint to UTF-8 (for libxml2 attribute values)
// ---------------------------------------------------------------------------
static std::string cpToUTF8(unsigned long cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

// ---------------------------------------------------------------------------
// XML escaping
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
                    std::ostringstream ss;
                    ss << "&#x" << std::hex << static_cast<int>(uc) << ";";
                    result += ss.str();
                }
        }
    }
    return result;
}

// ===========================================================================
// FreeType-based SVG path (supports both TTF and OTF/CFF)
// ===========================================================================
#ifdef HAVE_FREETYPE

struct FTContourBuilder {
    std::string path;
    double lastX = 0, lastY = 0;

    static int moveToCb(const FT_Vector* to, void* user) {
        auto* self = static_cast<FTContourBuilder*>(user);
        double x = to->x / 64.0, y = to->y / 64.0;
        self->path += "M" + fmtCoord(x) + " " + fmtCoord(y);
        self->lastX = x; self->lastY = y;
        return 0;
    }

    static int lineToCb(const FT_Vector* to, void* user) {
        auto* self = static_cast<FTContourBuilder*>(user);
        double x = to->x / 64.0, y = to->y / 64.0;
        if (self->lastX == x) {
            std::string ry = fmtCoord(y - self->lastY), ay = fmtCoord(y);
            self->path += (ry.size() < ay.size()) ? ("v" + ry) : ("V" + ay);
        } else if (self->lastY == y) {
            std::string rx = fmtCoord(x - self->lastX), ax = fmtCoord(x);
            self->path += (rx.size() < ax.size()) ? ("h" + rx) : ("H" + ax);
        } else {
            std::string r = fmtCoord(x - self->lastX) + " " + fmtCoord(y - self->lastY);
            std::string a = fmtCoord(x) + " " + fmtCoord(y);
            self->path += (r.size() < a.size()) ? ("l" + r) : ("L " + a);
        }
        self->lastX = x; self->lastY = y;
        return 0;
    }

    static int conicToCb(const FT_Vector* control, const FT_Vector* to, void* user) {
        auto* self = static_cast<FTContourBuilder*>(user);
        double cx = control->x / 64.0, cy = control->y / 64.0;
        double x = to->x / 64.0, y = to->y / 64.0;
        std::string r = fmtCoord(cx - self->lastX) + " " + fmtCoord(cy - self->lastY) + " "
                      + fmtCoord(x - self->lastX) + " " + fmtCoord(y - self->lastY);
        std::string a = fmtCoord(cx) + " " + fmtCoord(cy) + " " + fmtCoord(x) + " " + fmtCoord(y);
        self->path += (r.size() < a.size()) ? ("q" + r) : ("Q " + a);
        self->lastX = x; self->lastY = y;
        return 0;
    }

    static int cubicToCb(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
        auto* self = static_cast<FTContourBuilder*>(user);
        double c1x = c1->x / 64.0, c1y = c1->y / 64.0;
        double c2x = c2->x / 64.0, c2y = c2->y / 64.0;
        double x = to->x / 64.0, y = to->y / 64.0;
        std::string r = fmtCoord(c1x - self->lastX) + " " + fmtCoord(c1y - self->lastY) + " "
                      + fmtCoord(c2x - self->lastX) + " " + fmtCoord(c2y - self->lastY) + " "
                      + fmtCoord(x - self->lastX) + " " + fmtCoord(y - self->lastY);
        std::string a = fmtCoord(c1x) + " " + fmtCoord(c1y) + " " + fmtCoord(c2x) + " " + fmtCoord(c2y) + " "
                      + fmtCoord(x) + " " + fmtCoord(y);
        self->path += (r.size() < a.size()) ? ("c" + r) : ("C " + a);
        self->lastX = x; self->lastY = y;
        return 0;
    }
};

static std::string ftOutlineToSVG(FT_Outline& outline) {
    if (outline.n_points == 0 || outline.n_contours == 0) return {};
    FTContourBuilder builder;
    FT_Outline_Funcs funcs = {};
    funcs.move_to = FTContourBuilder::moveToCb;
    funcs.line_to = FTContourBuilder::lineToCb;
    funcs.conic_to = FTContourBuilder::conicToCb;
    funcs.cubic_to = FTContourBuilder::cubicToCb;
    funcs.shift = 0;
    funcs.delta = 0;
    FT_Outline_Decompose(&outline, &funcs, &builder);
    return builder.path;
}

std::vector<Byte> FontGenerator::buildSVGDocument(const std::vector<Byte>& fontData,
                                                  bool enableKerning) {
    FT_Library library;
    if (FT_Init_FreeType(&library)) {
        std::cerr << "Error initializing FreeType\n";
        return {};
    }
    FT_Face face;
    if (FT_New_Memory_Face(library, fontData.data(), static_cast<FT_Long>(fontData.size()),
                           0, &face)) {
        std::cerr << "Error loading font with FreeType\n";
        FT_Done_FreeType(library);
        return {};
    }

#ifdef HAVE_HARFBUZZ
    hb_font_t* hb_ft_font = hb_ft_font_create_referenced(face);
#endif

    std::vector<std::pair<unsigned long, unsigned int>> codepoints;
    FT_ULong charcode;
    FT_UInt gindex;
    charcode = FT_Get_First_Char(face, &gindex);
    while (gindex) {
        if (validCharCode(charcode))
            codepoints.push_back({charcode, gindex});
        charcode = FT_Get_Next_Char(face, charcode, &gindex);
    }

    struct GlyphInfo { int advance; std::string path; };
    GlyphInfo missing{};
    missing.advance = face->units_per_EM;
    if (FT_Load_Glyph(face, 0, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) == 0) {
        missing.advance = face->glyph->metrics.horiAdvance;
        if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
            missing.path = ftOutlineToSVG(face->glyph->outline);
    }

    std::map<unsigned int, GlyphInfo> glyphMap;
    glyphMap[0] = missing;
    std::vector<int> advances = {missing.advance};

    for (auto& cp : codepoints) {
        unsigned int gid = cp.second;
        if (glyphMap.find(gid) != glyphMap.end()) continue;
        GlyphInfo gi{};
        if (FT_Load_Glyph(face, gid, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) == 0) {
            gi.advance = face->glyph->metrics.horiAdvance;
            if (face->glyph->format == FT_GLYPH_FORMAT_OUTLINE)
                gi.path = ftOutlineToSVG(face->glyph->outline);
        } else {
            gi.advance = face->units_per_EM;
        }
        glyphMap[gid] = gi;
        advances.push_back(gi.advance);
    }

    int upem = face->units_per_EM;
    int ascender = face->ascender;
    int descender = face->descender;
    std::string familyName = face->family_name ? face->family_name : "Unknown";

    int avgAdvanceX = upem;
    if (!advances.empty())
        avgAdvanceX = Utils::maxDuplicate(advances);

    struct KernInfo { unsigned long leftCP, rightCP; int value; };
    std::vector<KernInfo> validKerns;

    if (enableKerning && !codepoints.empty()) {
        std::map<unsigned int, std::vector<unsigned long>> gidToCP;
        for (auto& cp : codepoints)
            gidToCP[cp.second].push_back(cp.first);

        for (auto& left : codepoints) {
            for (auto& right : codepoints) {
                int kern = 0;
#ifdef HAVE_HARFBUZZ
                kern = hb_font_get_glyph_h_kerning(hb_ft_font, left.second, right.second);
#endif
                if (kern == 0) {
                    FT_Vector ftkern;
                    if (FT_Get_Kerning(face, left.second, right.second,
                                       FT_KERNING_UNFITTED, &ftkern) == 0)
                        kern = static_cast<int>(ftkern.x);
                }
                if (kern != 0) {
                    auto itL = gidToCP.find(left.second);
                    auto itR = gidToCP.find(right.second);
                    if (itL != gidToCP.end() && itR != gidToCP.end()) {
                        for (auto lcp : itL->second)
                            for (auto rcp : itR->second)
                                validKerns.push_back({lcp, rcp, kern});
                    }
                }
            }
        }
    }

    std::vector<Byte> result;
    int viewW = upem;
    int viewH = ascender - descender;

#ifdef HAVE_LIBXML2
    {
        xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
        doc->encoding = xmlCharStrdup("UTF-8");
        doc->standalone = 0;
        xmlNodePtr svgNode = xmlNewNode(nullptr, BAD_CAST "svg");
        xmlNewProp(svgNode, BAD_CAST "xmlns", BAD_CAST "http://www.w3.org/2000/svg");
        std::string viewBox = "0 0 " + std::to_string(viewW) + " " + std::to_string(viewH);
        xmlNewProp(svgNode, BAD_CAST "viewBox", BAD_CAST viewBox.c_str());
        xmlDocSetRootElement(doc, svgNode);

        xmlNodePtr defs = xmlNewChild(svgNode, nullptr, BAD_CAST "defs", nullptr);
        xmlNodePtr fontNode = xmlNewChild(defs, nullptr, BAD_CAST "font", nullptr);
        xmlNewProp(fontNode, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(avgAdvanceX).c_str());

        xmlNodePtr fontFace = xmlNewChild(fontNode, nullptr, BAD_CAST "font-face", nullptr);
        xmlNewProp(fontFace, BAD_CAST "font-family", BAD_CAST familyName.c_str());
        xmlNewProp(fontFace, BAD_CAST "units-per-em", BAD_CAST std::to_string(upem).c_str());
        xmlNewProp(fontFace, BAD_CAST "ascent", BAD_CAST std::to_string(ascender).c_str());
        xmlNewProp(fontFace, BAD_CAST "descent", BAD_CAST std::to_string(descender).c_str());

        {
            xmlNodePtr mg = xmlNewChild(fontNode, nullptr, BAD_CAST "missing-glyph", nullptr);
            xmlNewProp(mg, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(missing.advance).c_str());
            if (!missing.path.empty())
                xmlNewProp(mg, BAD_CAST "d", BAD_CAST missing.path.c_str());
        }

        for (auto& cp : codepoints) {
            auto it = glyphMap.find(cp.second);
            if (it == glyphMap.end()) continue;
            xmlNodePtr glyphNode = xmlNewChild(fontNode, nullptr, BAD_CAST "glyph", nullptr);
            xmlNewProp(glyphNode, BAD_CAST "unicode", BAD_CAST cpToUTF8(cp.first).c_str());
            if (it->second.advance != avgAdvanceX)
                xmlNewProp(glyphNode, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(it->second.advance).c_str());
            if (!it->second.path.empty())
                xmlNewProp(glyphNode, BAD_CAST "d", BAD_CAST it->second.path.c_str());
        }

        if (!validKerns.empty()) {
            std::map<std::pair<unsigned long, unsigned long>, int> kernDedup;
            for (auto& k : validKerns) kernDedup[std::make_pair(k.leftCP, k.rightCP)] = k.value;
            for (auto& kd : kernDedup) {
                xmlNodePtr kernNode = xmlNewChild(fontNode, nullptr, BAD_CAST "hkern", nullptr);
                xmlNewProp(kernNode, BAD_CAST "u1", BAD_CAST cpToUTF8(kd.first.first).c_str());
                xmlNewProp(kernNode, BAD_CAST "u2", BAD_CAST cpToUTF8(kd.first.second).c_str());
                xmlNewProp(kernNode, BAD_CAST "k", BAD_CAST std::to_string(-kd.second).c_str());
            }
        }

        xmlNodePtr g = xmlNewChild(svgNode, nullptr, BAD_CAST "g", nullptr);
        std::string style = "font-family: " + familyName + "; font-size:50;fill:black";
        xmlNewProp(g, BAD_CAST "style", BAD_CAST style.c_str());
        std::vector<std::string> testStrings = {
            "!\"#$%&'()*+,-./0123456789:;\u00E5<>?",
            "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
            "` abcdefghijklmnopqrstuvwxyz|{}~"
        };
        for (size_t i = 0; i < testStrings.size(); ++i) {
            xmlNodePtr textNode = xmlNewChild(g, nullptr, BAD_CAST "text", nullptr);
            xmlNewProp(textNode, BAD_CAST "x", BAD_CAST "20");
            xmlNewProp(textNode, BAD_CAST "y", BAD_CAST std::to_string((i + 1) * 50).c_str());
            xmlAddChild(textNode, xmlNewText(BAD_CAST testStrings[i].c_str()));
        }

        xmlChar* xmlBuffer = nullptr;
        int docSize = 0;
        xmlDocDumpMemory(doc, &xmlBuffer, &docSize);
        if (xmlBuffer && docSize > 0)
            result.assign(xmlBuffer, xmlBuffer + docSize);
        xmlFree(xmlBuffer);
        xmlFreeDoc(doc);
    }
#else
    {
        std::ostringstream svg;
        svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        svg << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
            << "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" >\n";
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
            << viewW << " " << viewH << "\">\n";
        svg << "  <defs>\n";
        svg << "    <font horiz-adv-x=\"" << avgAdvanceX << "\">\n";
        svg << "      <font-face font-family=\"" << FontGenerator::xmlEscape(familyName) << "\" "
            << "units-per-em=\"" << upem << "\" "
            << "ascent=\"" << ascender << "\" "
            << "descent=\"" << descender << "\" />\n";
        {
            svg << "      <missing-glyph horiz-adv-x=\"" << missing.advance << "\"";
            if (!missing.path.empty()) svg << " d=\"" << missing.path << "\"";
            svg << " />\n";
        }
        for (auto& cp : codepoints) {
            auto it = glyphMap.find(cp.second);
            if (it == glyphMap.end()) continue;
            svg << "      <glyph unicode=\"" << unicodeEscape(cp.first) << "\"";
            if (it->second.advance != avgAdvanceX)
                svg << " horiz-adv-x=\"" << it->second.advance << "\"";
            if (!it->second.path.empty())
                svg << " d=\"" << it->second.path << "\"";
            svg << " />\n";
        }
        if (!validKerns.empty()) {
            std::map<std::pair<unsigned long, unsigned long>, int> kernDedup;
            for (auto& k : validKerns) kernDedup[std::make_pair(k.leftCP, k.rightCP)] = k.value;
            for (auto& kd : kernDedup) {
                svg << "      <hkern u1=\"" << cpToUTF8(kd.first.first) << "\" "
                    << "u2=\"" << cpToUTF8(kd.first.second) << "\" "
                    << "k=\"" << (-kd.second) << "\" />\n";
            }
        }
        svg << "    </font>\n";
        svg << "  </defs>\n";
        svg << "  <g style=\"font-family: " << familyName << "; font-size:50;fill:black\">\n";
        std::vector<std::string> testStrings = {
            "!\"#$%&'()*+,-./0123456789:;\u00E5<>?",
            "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
            "` abcdefghijklmnopqrstuvwxyz|{}~"
        };
        for (size_t i = 0; i < testStrings.size(); ++i) {
            svg << "    <text x=\"20\" y=\"" << ((i + 1) * 50) << "\">"
                << FontGenerator::xmlEscape(testStrings[i]) << "</text>\n";
        }
        svg << "  </g>\n";
        svg << "</svg>\n";
        std::string svgStr = svg.str();
        result.assign(svgStr.begin(), svgStr.end());
    }
#endif

#ifdef HAVE_HARFBUZZ
    hb_font_destroy(hb_ft_font);
#endif
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return result;
}

bool FontGenerator::generateSVG(const std::vector<Byte>& fontData,
                                const std::string& outputPath,
                                bool enableKerning) {
    try {
        auto svgData = buildSVGDocument(fontData, enableKerning);
        if (svgData.empty()) {
            std::cerr << "Failed to build SVG document\n";
            return false;
        }
        return Utils::writeFileBytes(outputPath, svgData);
    } catch (const std::exception& e) {
        std::cerr << "Error generating SVG: " << e.what() << "\n";
        return false;
    }
}

#endif // HAVE_FREETYPE

// ===========================================================================
// TTF-only SVG path (fallback when FreeType is not available)
// ===========================================================================
#ifndef HAVE_FREETYPE

struct Point { double x, y; int flags; };

static std::string contourToSVGPath(const std::vector<Point>& contour) {
    if (contour.empty()) return "";
    size_t n = contour.size();
    std::string result = "M" + fmtCoord(contour[0].x) + " " + fmtCoord(contour[0].y);
    auto onCurve = [](const Point& p) { return (p.flags & 0x01) != 0; };
    auto midval = [](double a, double b) { return a + (b - a) / 2.0; };
    using CoordPair = std::pair<std::string, std::string>;
    auto shortestPath = [](const std::string& cmd, const std::vector<CoordPair>& coords) -> std::string {
        std::string rel = cmd, abs = cmd;
        std::string absCmd = cmd;
        absCmd[0] = static_cast<char>(std::toupper(absCmd[0]));
        for (size_t i = 0; i < coords.size(); ++i) {
            if (i > 0) { rel += " "; abs += " "; }
            rel += coords[i].first;
            abs += coords[i].second;
        }
        return (rel.size() < abs.size()) ? rel : abs;
    };
    int step = 0;
    double lastx = contour[0].x, lasty = contour[0].y;
    while (step < static_cast<int>(n)) {
        size_t i0 = static_cast<size_t>(step) % n;
        size_t i1 = (i0 + 1) % n;
        size_t i2 = (i0 + 2) % n;
        const Point& p0 = contour[i0], p1 = contour[i1], p2 = contour[i2];
        bool c0 = onCurve(p0), c1 = onCurve(p1), c2 = onCurve(p2);
        auto showx = [&](double x) -> CoordPair { return {fmtCoord(x - lastx), fmtCoord(x)}; };
        auto showy = [&](double y) -> CoordPair { return {fmtCoord(y - lasty), fmtCoord(y)}; };
        auto showxy = [&](double x, double y) -> CoordPair {
            return {fmtCoord(x - lastx) + " " + fmtCoord(y - lasty), fmtCoord(x) + " " + fmtCoord(y)};
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

static void collectGlyphContours(const TTFFont& font, UShort glyphID,
                                 double scaleX, double scaleY,
                                 double scale01, double scale10,
                                 short offsetX, short offsetY,
                                 std::vector<std::vector<Point>>& contours) {
    if (glyphID >= font.glyphs.size()) return;
    const Glyph& glyph = font.glyphs[glyphID];
    if (glyph.numberOfContours == 0) return;
    auto transform = [&](double x, double y, int flags) -> Point {
        return {x * scaleX + y * scale10 + offsetX, y * scaleY + x * scale01 + offsetY, flags};
    };
    if (!glyph.isComposite) {
        const auto& sg = glyph.simpleGlyph;
        if (sg.xCoordinates.empty() || sg.endPtsOfContours.empty()) return;
        std::vector<Point> pts;
        for (size_t k = 0; k < sg.xCoordinates.size(); ++k)
            pts.push_back(transform(sg.xCoordinates[k], sg.yCoordinates[k],
                          (k < sg.flags.size()) ? static_cast<int>(sg.flags[k]) : 0));
        size_t ptsOffset = 0;
        for (UShort endPt : sg.endPtsOfContours) {
            if (endPt >= pts.size()) break;
            size_t count = static_cast<size_t>(endPt) - ptsOffset + 1;
            std::vector<Point> contour(pts.begin() + ptsOffset, pts.begin() + ptsOffset + count);
            if (!contour.empty()) contours.push_back(std::move(contour));
            ptsOffset = static_cast<size_t>(endPt) + 1;
        }
    } else {
        for (const auto& comp : glyph.compositeGlyph.components) {
            collectGlyphContours(font, comp.glyphIndex,
                scaleX * comp.xScale, scaleY * comp.yScale,
                scaleX * comp.scale01, scaleY * comp.scale10,
                static_cast<short>(offsetX + comp.xOffset * scaleX),
                static_cast<short>(offsetY + comp.yOffset * scaleY),
                contours);
        }
    }
}

static std::string glyphToSVGPath(const TTFFont& font, UShort glyphID) {
    std::vector<std::vector<Point>> contours;
    collectGlyphContours(font, glyphID, 1.0, 1.0, 0.0, 0.0, 0, 0, contours);
    std::string result;
    for (const auto& c : contours) result += contourToSVGPath(c);
    return result;
}

static std::map<int, std::vector<int>> buildGlyphIdToCode(const CmapSubtable& cmap) {
    std::map<int, std::vector<int>> m;
    for (const auto& pair : cmap.charToGlyph) {
        int gid = static_cast<int>(pair.second);
        if (gid > 0) m[gid].push_back(static_cast<int>(pair.first));
    }
    return m;
}

// Raw string version (no libxml2)
#ifndef HAVE_LIBXML2
std::vector<Byte> FontGenerator::buildSVGDocument(const TTFFont& font, bool enableKerning,
                                                  UShort cmapPlatformID, UShort cmapEncodingID) {
    std::ostringstream svg;
    const CmapSubtable* cmap = font.findCmap(cmapPlatformID, cmapEncodingID);
    if (!cmap && !font.cmaps.empty()) cmap = &font.cmaps[0];

    std::vector<int> validCodes, advances;
    if (cmap) {
        for (const auto& pair : cmap->charToGlyph) {
            ULong cp = pair.first;
            UShort gid = pair.second;
            if (gid > 0 && validCharCode(cp)) {
                validCodes.push_back(static_cast<int>(cp));
                if (gid < font.hmtx.size()) advances.push_back(font.hmtx[gid].advanceWidth);
            }
        }
    }
    int avgAdvanceX = font.head.unitsPerEm;
    if (!advances.empty()) avgAdvanceX = Utils::maxDuplicate(advances);

    struct KernInfo { UShort left, right; Short value; UShort coverage; };
    std::vector<KernInfo> validKerns;
    std::map<int, std::vector<int>> gidToCode;
    if (cmap && enableKerning) gidToCode = buildGlyphIdToCode(*cmap);
    if (enableKerning && !font.kernPairs.empty()) {
        for (const auto& kp : font.kernPairs) {
            if (cmap && cmap->getGlyphID(kp.left) > 0 && cmap->getGlyphID(kp.right) > 0)
                validKerns.push_back({kp.left, kp.right, kp.value, 0});
        }
    }

    int viewW = font.head.unitsPerEm;
    int viewH = static_cast<int>(font.hhea.ascender) - static_cast<int>(font.hhea.descender);
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
        << "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" >\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "
        << viewW << " " << viewH << "\">\n";
    svg << "  <defs>\n";
    svg << "    <font horiz-adv-x=\"" << avgAdvanceX << "\">\n";
    svg << "      <font-face font-family=\"" << FontGenerator::xmlEscape(font.getFamilyName()) << "\" "
        << "units-per-em=\"" << font.head.unitsPerEm << "\" "
        << "ascent=\"" << font.hhea.ascender << "\" "
        << "descent=\"" << font.hhea.descender << "\" />\n";
    {
        std::string path = glyphToSVGPath(font, 0);
        int advance0 = (!font.hmtx.empty()) ? font.hmtx[0].advanceWidth : avgAdvanceX;
        svg << "      <missing-glyph horiz-adv-x=\"" << advance0 << "\"";
        if (!path.empty()) svg << " d=\"" << path << "\"";
        svg << " />\n";
    }
    for (int code : validCodes) {
        if (!cmap) break;
        UShort gid = static_cast<UShort>(cmap->getGlyphID(static_cast<ULong>(code)));
        if (gid == 0 || gid >= font.glyphs.size()) continue;
        std::string path = glyphToSVGPath(font, gid);
        int advanceX = (gid < font.hmtx.size()) ? font.hmtx[gid].advanceWidth : avgAdvanceX;
        svg << "      <glyph unicode=\"" << unicodeEscape(static_cast<ULong>(code)) << "\"";
        if (advanceX != avgAdvanceX) svg << " horiz-adv-x=\"" << advanceX << "\"";
        if (!path.empty()) svg << " d=\"" << path << "\"";
        svg << " />\n";
    }
    if (!validKerns.empty()) {
        for (const auto& kern : validKerns) {
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
    std::string familyName = font.getFamilyName();
    svg << "  <g style=\"font-family: " << familyName << "; font-size:50;fill:black\">\n";
    std::vector<std::string> testStrings = {
        "!\"#$%&'()*+,-./0123456789:;\u00E5<>?",
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
        "` abcdefghijklmnopqrstuvwxyz|{}~"
    };
    for (size_t i = 0; i < testStrings.size(); ++i)
        svg << "    <text x=\"20\" y=\"" << ((i + 1) * 50) << "\">"
            << FontGenerator::xmlEscape(testStrings[i]) << "</text>\n";
    svg << "  </g>\n";
    svg << "</svg>\n";
    std::string svgStr = svg.str();
    return std::vector<Byte>(svgStr.begin(), svgStr.end());
}
#endif // !HAVE_LIBXML2

// libxml2 version
#ifdef HAVE_LIBXML2
std::vector<Byte> FontGenerator::buildSVGDocument(const TTFFont& font, bool enableKerning,
                                                  UShort cmapPlatformID, UShort cmapEncodingID) {
    const CmapSubtable* cmap = font.findCmap(cmapPlatformID, cmapEncodingID);
    if (!cmap && !font.cmaps.empty()) cmap = &font.cmaps[0];

    std::vector<int> validCodes, advances;
    if (cmap) {
        for (const auto& pair : cmap->charToGlyph) {
            ULong cp = pair.first;
            UShort gid = pair.second;
            if (gid > 0 && validCharCode(cp)) {
                validCodes.push_back(static_cast<int>(cp));
                if (gid < font.hmtx.size()) advances.push_back(font.hmtx[gid].advanceWidth);
            }
        }
    }
    int avgAdvanceX = font.head.unitsPerEm;
    if (!advances.empty()) avgAdvanceX = Utils::maxDuplicate(advances);

    std::map<int, std::vector<int>> gidToCode;
    if (cmap && enableKerning) gidToCode = buildGlyphIdToCode(*cmap);

    struct KernInfo { UShort left, right; Short value; };
    std::vector<KernInfo> validKerns;
    if (enableKerning && !font.kernPairs.empty()) {
        for (const auto& kp : font.kernPairs) {
            if (cmap && cmap->getGlyphID(kp.left) > 0 && cmap->getGlyphID(kp.right) > 0)
                validKerns.push_back({kp.left, kp.right, kp.value});
        }
    }

    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    doc->encoding = xmlCharStrdup("UTF-8");
    doc->standalone = 0;

    int viewW = font.head.unitsPerEm;
    int viewH = static_cast<int>(font.hhea.ascender) - static_cast<int>(font.hhea.descender);
    xmlNodePtr svgNode = xmlNewNode(nullptr, BAD_CAST "svg");
    xmlNewProp(svgNode, BAD_CAST "xmlns", BAD_CAST "http://www.w3.org/2000/svg");
    std::string viewBox = "0 0 " + std::to_string(viewW) + " " + std::to_string(viewH);
    xmlNewProp(svgNode, BAD_CAST "viewBox", BAD_CAST viewBox.c_str());
    xmlDocSetRootElement(doc, svgNode);

    xmlNodePtr defs = xmlNewChild(svgNode, nullptr, BAD_CAST "defs", nullptr);
    xmlNodePtr fontNode = xmlNewChild(defs, nullptr, BAD_CAST "font", nullptr);
    xmlNewProp(fontNode, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(avgAdvanceX).c_str());

    xmlNodePtr fontFace = xmlNewChild(fontNode, nullptr, BAD_CAST "font-face", nullptr);
    xmlNewProp(fontFace, BAD_CAST "font-family", BAD_CAST font.getFamilyName().c_str());
    xmlNewProp(fontFace, BAD_CAST "units-per-em", BAD_CAST std::to_string(font.head.unitsPerEm).c_str());
    xmlNewProp(fontFace, BAD_CAST "ascent", BAD_CAST std::to_string(font.hhea.ascender).c_str());
    xmlNewProp(fontFace, BAD_CAST "descent", BAD_CAST std::to_string(font.hhea.descender).c_str());

    {
        std::string path = glyphToSVGPath(font, 0);
        int advance0 = (!font.hmtx.empty()) ? font.hmtx[0].advanceWidth : avgAdvanceX;
        xmlNodePtr mg = xmlNewChild(fontNode, nullptr, BAD_CAST "missing-glyph", nullptr);
        xmlNewProp(mg, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(advance0).c_str());
        if (!path.empty()) xmlNewProp(mg, BAD_CAST "d", BAD_CAST path.c_str());
    }

    for (int code : validCodes) {
        if (!cmap) break;
        UShort gid = static_cast<UShort>(cmap->getGlyphID(static_cast<ULong>(code)));
        if (gid == 0 || gid >= font.glyphs.size()) continue;
        std::string path = glyphToSVGPath(font, gid);
        int advanceX = (gid < font.hmtx.size()) ? font.hmtx[gid].advanceWidth : avgAdvanceX;
        xmlNodePtr glyphNode = xmlNewChild(fontNode, nullptr, BAD_CAST "glyph", nullptr);
        xmlNewProp(glyphNode, BAD_CAST "unicode", BAD_CAST cpToUTF8(static_cast<unsigned long>(code)).c_str());
        if (advanceX != avgAdvanceX)
            xmlNewProp(glyphNode, BAD_CAST "horiz-adv-x", BAD_CAST std::to_string(advanceX).c_str());
        if (!path.empty())
            xmlNewProp(glyphNode, BAD_CAST "d", BAD_CAST path.c_str());
    }

    if (!validKerns.empty()) {
        auto toUTF8 = [](int cp) -> std::string {
            if (cp < 0x80) return std::string(1, static_cast<char>(cp));
            std::string s;
            if (cp < 0x800) {
                s += static_cast<char>(0xC0 | (cp >> 6));
                s += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                s += static_cast<char>(0xE0 | (cp >> 12));
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                s += static_cast<char>(0xF0 | (cp >> 18));
                s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (cp & 0x3F));
            }
            return s;
        };
        for (const auto& kern : validKerns) {
            auto it1 = gidToCode.find(static_cast<int>(kern.left));
            auto it2 = gidToCode.find(static_cast<int>(kern.right));
            if (it1 == gidToCode.end() || it2 == gidToCode.end()) continue;
            std::string u1, u2;
            for (size_t i = 0; i < it1->second.size(); ++i) {
                if (i > 0) u1 += ",";
                u1 += toUTF8(it1->second[i]);
            }
            for (size_t i = 0; i < it2->second.size(); ++i) {
                if (i > 0) u2 += ",";
                u2 += toUTF8(it2->second[i]);
            }
            xmlNodePtr kernNode = xmlNewChild(fontNode, nullptr, BAD_CAST "hkern", nullptr);
            xmlNewProp(kernNode, BAD_CAST "u1", BAD_CAST u1.c_str());
            xmlNewProp(kernNode, BAD_CAST "u2", BAD_CAST u2.c_str());
            xmlNewProp(kernNode, BAD_CAST "k", BAD_CAST std::to_string(-kern.value).c_str());
        }
    }

    xmlNodePtr g = xmlNewChild(svgNode, nullptr, BAD_CAST "g", nullptr);
    std::string familyName = font.getFamilyName();
    std::string style = "font-family: " + familyName + "; font-size:50;fill:black";
    xmlNewProp(g, BAD_CAST "style", BAD_CAST style.c_str());
    std::vector<std::string> testStrings = {
        "!\"#$%&'()*+,-./0123456789:;\u00E5<>?",
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
        "` abcdefghijklmnopqrstuvwxyz|{}~"
    };
    for (size_t i = 0; i < testStrings.size(); ++i) {
        xmlNodePtr textNode = xmlNewChild(g, nullptr, BAD_CAST "text", nullptr);
        xmlNewProp(textNode, BAD_CAST "x", BAD_CAST "20");
        xmlNewProp(textNode, BAD_CAST "y", BAD_CAST std::to_string((i + 1) * 50).c_str());
        xmlAddChild(textNode, xmlNewText(BAD_CAST testStrings[i].c_str()));
    }

    xmlChar* xmlBuffer = nullptr;
    int docSize = 0;
    xmlDocDumpMemory(doc, &xmlBuffer, &docSize);
    std::vector<Byte> result;
    if (xmlBuffer && docSize > 0) result.assign(xmlBuffer, xmlBuffer + docSize);
    xmlFree(xmlBuffer);
    xmlFreeDoc(doc);
    return result;
}
#endif // HAVE_LIBXML2

bool FontGenerator::generateSVG(const TTFFont& font, const std::string& outputPath,
                               bool enableKerning, UShort cmapPlatformID,
                               UShort cmapEncodingID) {
    try {
        auto svgData = buildSVGDocument(font, enableKerning, cmapPlatformID, cmapEncodingID);
        if (svgData.empty()) {
            std::cerr << "Failed to build SVG document\n";
            return false;
        }
        return Utils::writeFileBytes(outputPath, svgData);
    } catch (const std::exception& e) {
        std::cerr << "Error generating SVG: " << e.what() << "\n";
        return false;
    }
}

#endif // !HAVE_FREETYPE
