#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <cstdint>

class Utils {
public:
    // File I/O
    static std::vector<uint8_t> readFileBytes(const std::string& filename);
    static bool writeFileBytes(const std::string& filename, const std::vector<uint8_t>& data);

    // String utilities
    static std::string trim(const std::string& str);
    static std::string toLower(const std::string& str);
    static bool endsWith(const std::string& str, const std::string& suffix);
    static std::string getFileExtension(const std::string& filename);

    // Vector utilities
    static std::vector<uint8_t> concatenate(const std::vector<uint8_t>& a,
                                           const std::vector<uint8_t>& b);

    // Byte utilities
    static uint32_t calculateChecksum(const std::vector<uint8_t>& data);
    static std::string bytesToHex(const std::vector<uint8_t>& data, size_t maxLength = 100);

    // Alignment
    static size_t padToMultiple(size_t value, size_t alignment);
    static std::vector<uint8_t> padBytes(const std::vector<uint8_t>& data, size_t alignment);

    // Math/sequence utilities (matching Haskell)
    static std::vector<int> diff(const std::vector<int>& values);
    static int maxDuplicate(const std::vector<int>& values);

    // Formatting
    static std::string formatTable(const std::vector<std::vector<std::string>>& table);
};

#endif // UTILS_H
