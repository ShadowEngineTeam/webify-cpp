#include "font.h"

// Font typeclass method implementations (mirrors Haskell Font typeclass)

std::vector<Byte> Font::getOS2Panose() const {
    return os2.panose;
}

UShort Font::getOS2FsSelection() const {
    return os2.fsSelection;
}

UShort Font::getOS2UsWeightClass() const {
    return os2.usWeightClass;
}

ULong Font::getOS2UlUnicodeRange1() const {
    return os2.ulUnicodeRange1;
}

ULong Font::getOS2UlUnicodeRange2() const {
    return os2.ulUnicodeRange2;
}

ULong Font::getOS2UlUnicodeRange3() const {
    return os2.ulUnicodeRange3;
}

ULong Font::getOS2UlUnicodeRange4() const {
    return os2.ulUnicodeRange4;
}

ULong Font::getOS2UlCodePageRange1() const {
    return os2.ulCodePageRange1;
}

ULong Font::getOS2UlCodePageRange2() const {
    return os2.ulCodePageRange2;
}

ULong Font::getHeadCheckSumAdjustment() const {
    return head.checksumAdjustment;
}

const NameRecord* Font::findName(UShort nameID) const {
    for (const auto& nr : names) {
        if (nr.nameID == nameID) return &nr;
    }
    return nullptr;
}

std::string Font::getFullName() const {
    for (const auto& record : names) {
        if (record.nameID == 4) return record.value;
    }
    return "";
}

std::string Font::getVersionName() const {
    for (const auto& record : names) {
        if (record.nameID == 5) return record.value;
    }
    return "";
}
