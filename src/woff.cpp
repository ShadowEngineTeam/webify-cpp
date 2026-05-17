#include "generators.h"
#include "utils.h"
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif
#ifdef HAVE_ZOPFLI
#include "zopfli.h"
#endif
#include <cstring>
#include <iostream>

std::vector<Byte> FontGenerator::compressTableZlib(const std::vector<Byte>& data) {
    if (data.empty()) return data;

#ifdef HAVE_ZLIB
    std::vector<Byte> compressed;
    uLongf compressedSize = compressBound(data.size());
    compressed.resize(static_cast<size_t>(compressedSize));

    int ret = compress2(compressed.data(), &compressedSize,
                        data.data(), static_cast<uLong>(data.size()), 6);
    if (ret != Z_OK) {
        return data;
    }

    compressed.resize(static_cast<size_t>(compressedSize));
    if (compressed.size() >= data.size()) {
        return data;
    }
    return compressed;
#else
    (void)data;
    return data;
#endif
}

std::vector<Byte> FontGenerator::compressTableZopfli(const std::vector<Byte>& data) {
    if (data.empty()) return data;

#ifdef HAVE_ZOPFLI
    ZopfliOptions options;
    ZopfliInitOptions(&options);
    options.numiterations = 15;

    unsigned char* out = nullptr;
    size_t outsize = 0;
    ZopfliCompress(&options, ZOPFLI_FORMAT_ZLIB,
                   data.data(), data.size(),
                   &out, &outsize);

    std::vector<Byte> result;
    if (out && outsize < data.size()) {
        result.assign(out, out + outsize);
    } else {
        result = data;
    }
    free(out);
    return result;
#else
    (void)data;
    return data;
#endif
}

std::vector<Byte> FontGenerator::compressTable(const std::vector<Byte>& data) {
    return compressTableZlib(data);
}
