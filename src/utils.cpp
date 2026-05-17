#include "utils.h"
#include <fstream>
#include <algorithm>
#include <sstream>
#include <cctype>

std::vector<uint8_t> Utils::readFileBytes(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read file: " + filename);
    }

    return buffer;
}

bool Utils::writeFileBytes(const std::string& filename, const std::vector<uint8_t>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return file.good();
}

std::string Utils::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

std::string Utils::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool Utils::endsWith(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) return false;
    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

std::string Utils::getFileExtension(const std::string& filename) {
    size_t pos = filename.find_last_of(".");
    if (pos == std::string::npos) return "";
    return toLower(filename.substr(pos + 1));
}

std::vector<uint8_t> Utils::concatenate(const std::vector<uint8_t>& a,
                                        const std::vector<uint8_t>& b) {
    std::vector<uint8_t> result = a;
    result.insert(result.end(), b.begin(), b.end());
    return result;
}

uint32_t Utils::calculateChecksum(const std::vector<uint8_t>& data) {
    uint32_t sum = 0;
    size_t numLongs = (data.size() + 3) / 4;

    for (size_t i = 0; i < numLongs; ++i) {
        size_t offset = i * 4;
        uint32_t val = 0;

        for (int j = 0; j < 4 && offset + j < data.size(); ++j) {
            val = (val << 8) | data[offset + j];
        }

        sum += val;
    }

    return sum;
}

std::string Utils::bytesToHex(const std::vector<uint8_t>& data, size_t maxLength) {
    std::stringstream ss;
    size_t len = std::min(data.size(), maxLength);

    for (size_t i = 0; i < len; ++i) {
        ss << std::hex << (int)data[i] << " ";
    }

    if (data.size() > maxLength) {
        ss << "...";
    }

    return ss.str();
}

size_t Utils::padToMultiple(size_t value, size_t alignment) {
    if (value % alignment == 0) return value;
    return value + (alignment - (value % alignment));
}

std::vector<uint8_t> Utils::padBytes(const std::vector<uint8_t>& data, size_t alignment) {
    size_t padded = padToMultiple(data.size(), alignment);
    std::vector<uint8_t> result = data;
    result.resize(padded, 0);
    return result;
}

std::vector<int> Utils::diff(const std::vector<int>& values) {
    if (values.size() < 2) return {};
    std::vector<int> result;
    for (size_t i = 0; i < values.size() - 1; ++i) {
        result.push_back(values[i + 1] - values[i]);
    }
    return result;
}

int Utils::maxDuplicate(const std::vector<int>& values) {
    if (values.empty()) return 0;
    if (values.size() == 1) return values[0];
    auto sorted = values;
    std::sort(sorted.begin(), sorted.end());
    int bestVal = sorted[0];
    int bestCount = 1;
    int curCount = 1;
    for (size_t i = 1; i < sorted.size(); ++i) {
        if (sorted[i] == sorted[i - 1]) {
            ++curCount;
        } else {
            if (curCount > bestCount) {
                bestCount = curCount;
                bestVal = sorted[i - 1];
            }
            curCount = 1;
        }
    }
    if (curCount > bestCount) {
        bestVal = sorted.back();
    }
    return bestVal;
}

std::string Utils::formatTable(const std::vector<std::vector<std::string>>& table) {
    if (table.empty()) return "";
    size_t cols = table[0].size();
    std::vector<size_t> maxSize(cols, 0);
    for (const auto& row : table) {
        for (size_t i = 0; i < row.size() && i < cols; ++i) {
            maxSize[i] = std::max(maxSize[i], row[i].size());
        }
    }
    std::string line;
    size_t lineLen = 0;
    for (size_t i = 0; i < cols; ++i) {
        lineLen += maxSize[i];
        if (i > 0) lineLen += 3;
    }
    line = std::string(lineLen, '-') + "\n";
    std::string result = line;
    for (const auto& row : table) {
        for (size_t i = 0; i < row.size() && i < cols; ++i) {
            if (i > 0) result += " | ";
            auto field = row[i];
            result += field;
            if (field.size() < maxSize[i])
                result += std::string(maxSize[i] - field.size(), ' ');
        }
        result += "\n";
    }
    result += line;
    return result;
}
