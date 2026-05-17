#include "parser.h"
#include "generators.h"
#include "utils.h"
#include <iostream>
#include <string>
#include <vector>

struct Options {
    bool generateEOT = true;
    bool generateWOFF = true;
    bool generateWOFF2 = true;
    bool generateSVG = true;
    bool useZopfli = false;
    bool useBrotli = true;
    bool svgEnableKerning = false;
    uint16_t svgCmapPlatformID = 0xFFFF;
    uint16_t svgCmapEncodingID = 0xFFFF;
};

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [OPTIONS] font-files...\n"
              << "\nOptions:\n"
              << "  -h,  --help                       Show this help message\n"
              << "  -e,  --no-eot                     Skip EOT generation\n"
              << "  -w,  --no-woff                    Skip WOFF generation\n"
              << "  -w2, --no-woff2                   Skip WOFF2 generation\n"
              << "  -s,  --no-svg                     Skip SVG generation\n"
              << "  -z,  --zopfli                     Use Zopfli compression for WOFF\n"
              << "  -nb,  --no-brotli                 Skip Brotli compression for WOFF2\n"
              << "  --svg-enable-kerning              Enable kerning in SVG output\n"
              << "  --svg-cmap-platform-id <id>       Set SVG cmap platform ID\n"
              << "  --svg-cmap-encoding-id <id>       Set SVG cmap encoding ID\n"
              << "\nExamples:\n"
              << "  " << progName << " myfont.ttf\n"
              << "  " << progName << " --no-svg myfont.ttf\n"
              << "  " << progName << " --zopfli myfont.ttf myfont2.ttf\n";
}

std::string outputPath(const std::string& inputFile, const std::string& extension) {
    size_t pos = inputFile.find_last_of(".");
    if (pos == std::string::npos) {
        return inputFile + extension;
    }
    return inputFile.substr(0, pos) + extension;
}

int main(int argc, char* argv[]) {
    Options opts;
    std::vector<std::string> inputFiles;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-e" || arg == "--no-eot") {
            opts.generateEOT = false;
        } else if (arg == "-w" || arg == "--no-woff") {
            opts.generateWOFF = false;
        } else if (arg == "-w2" || arg == "--no-woff2") {
            opts.generateWOFF2 = false;
        } else if (arg == "-s" || arg == "--no-svg") {
            opts.generateSVG = false;
        } else if (arg == "-z" || arg == "--zopfli") {
            opts.useZopfli = true;
        } else if (arg == "-nb" || arg == "--no-brotli") {
            opts.useBrotli = false;
        }
        } else if (arg == "-k" || arg == "--svg-enable-kerning") {
            opts.svgEnableKerning = true;
        } else if (arg == "--svg-cmap-platform-id") {
            if (i + 1 < argc) {
                opts.svgCmapPlatformID = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg == "--svg-cmap-encoding-id") {
            if (i + 1 < argc) {
                opts.svgCmapEncodingID = static_cast<uint16_t>(std::stoi(argv[++i]));
            }
        } else if (arg[0] != '-') {
            inputFiles.push_back(arg);
        }
    }

    if (inputFiles.empty()) {
        std::cerr << "Error: No input files specified\n";
        printUsage(argv[0]);
        return 1;
    }

    bool anyFailed = false;

    for (const auto& inputFile : inputFiles) {
        try {
            std::cout << "Processing: " << inputFile << "\n";

            // Parse font file
            auto font = FontParser::parse(inputFile);

            if (!font) {
                std::cerr << "Error: Failed to parse font file: " << inputFile << "\n";
                anyFailed = true;
                continue;
            }

            // Generate outputs
            bool success = true;

            if (opts.generateEOT) {
                std::string eotFile = outputPath(inputFile, ".eot");
                std::cout << "  Generating: " << eotFile << "\n";
                if (!FontGenerator::generateEOT(*font, eotFile)) {
                    std::cerr << "  Warning: Failed to generate EOT\n";
                    success = false;
                }
            }

            if (opts.generateWOFF2) {
                std::string woff2File = outputPath(inputFile, ".woff2");
                std::cout << "  Generating: " << woff2File << "\n";
                if (!FontGenerator::generateWOFF2(*font, woff2File, opts.useBrotli)) {
                    std::cerr << "  Warning: Failed to generate WOFF2\n";
                    success = false;
                }
            }

            if (opts.generateWOFF) {
                std::string woffFile = outputPath(inputFile, ".woff");
                std::cout << "  Generating: " << woffFile << "\n";
                if (!FontGenerator::generateWOFF(*font, woffFile, opts.useZopfli)) {
                    std::cerr << "  Warning: Failed to generate WOFF\n";
                    success = false;
                }
            }

            if (opts.generateSVG) {
                std::string svgFile = outputPath(inputFile, ".svg");
                std::cout << "  Generating: " << svgFile << "\n";
#ifdef HAVE_FREETYPE
                if (!FontGenerator::generateSVG(font->rawBytes, svgFile,
                                                opts.svgEnableKerning)) {
                    std::cerr << "  Warning: Failed to generate SVG\n";
                    success = false;
                }
#else
                if (!font->isOTF()) {
                    auto ttfFont = dynamic_cast<TTFFont*>(font.get());
                    if (ttfFont && !FontGenerator::generateSVG(*ttfFont, svgFile,
                                                              opts.svgEnableKerning,
                                                              opts.svgCmapPlatformID,
                                                              opts.svgCmapEncodingID)) {
                        std::cerr << "  Warning: Failed to generate SVG\n";
                        success = false;
                    }
                }
#endif
            }

            if (!success) {
                anyFailed = true;
            }

        } catch (const std::exception& e) {
            std::cerr << "Error processing " << inputFile << ": " << e.what() << "\n";
            anyFailed = true;
        }
    }

    if (anyFailed) {
        std::cerr << "\nSome files failed to process completely.\n";
        return 1;
    }

    std::cout << "\nConversion completed successfully.\n";
    return 0;
}
