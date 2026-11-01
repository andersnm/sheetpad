#include "text_encoding.h"

#include <windows.h>

#include <cstring>

namespace sheetpad {

namespace {

std::string AnsiToUtf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return WideToUtf8(wide);
}

}  // namespace

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.data(), length, nullptr, nullptr);
    return utf8;
}

std::string DecodeFileBytes(std::string_view bytes, Encoding& encoding) {
    const auto at = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };

    if (bytes.size() >= 2 && ((at(0) == 0xFF && at(1) == 0xFE) || (at(0) == 0xFE && at(1) == 0xFF))) {
        const bool bigEndian = at(0) == 0xFE;
        std::wstring wide((bytes.size() - 2) / 2, L'\0');
        for (std::size_t i = 0; i < wide.size(); ++i) {
            const unsigned char lo = at(2 + i * 2 + (bigEndian ? 1 : 0));
            const unsigned char hi = at(2 + i * 2 + (bigEndian ? 0 : 1));
            wide[i] = static_cast<wchar_t>(lo | (hi << 8));
        }
        encoding = bigEndian ? Encoding::Utf16Be : Encoding::Utf16Le;
        return WideToUtf8(wide);
    }
    if (bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF) {
        encoding = Encoding::Utf8Bom;
        return std::string(bytes.substr(3));
    }

    if (IsValidUtf8(bytes)) {
        encoding = Encoding::Utf8;
        return std::string(bytes);
    }
    encoding = Encoding::Ansi;
    return AnsiToUtf8(bytes);
}

bool IsAnsiRepresentable(std::string_view utf8) {
    const std::wstring wide = Utf8ToWide(utf8);
    if (wide.empty()) {
        return true;
    }
    BOOL usedDefault = FALSE;
    WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, &usedDefault);
    return !usedDefault;
}

std::string EncodeFileText(std::string_view utf8, Encoding encoding) {
    switch (encoding) {
    case Encoding::Utf8Bom:
        return std::string("\xEF\xBB\xBF") + std::string(utf8);
    case Encoding::Utf16Le:
    case Encoding::Utf16Be: {
        const bool bigEndian = encoding == Encoding::Utf16Be;
        const std::wstring wide = Utf8ToWide(utf8);
        std::string out(bigEndian ? "\xFE\xFF" : "\xFF\xFE", 2);
        out.reserve(2 + wide.size() * 2);
        for (const wchar_t unit : wide) {
            const char lo = static_cast<char>(unit & 0xFF);
            const char hi = static_cast<char>((unit >> 8) & 0xFF);
            out.push_back(bigEndian ? hi : lo);
            out.push_back(bigEndian ? lo : hi);
        }
        return out;
    }
    case Encoding::Ansi: {
        const std::wstring wide = Utf8ToWide(utf8);
        if (wide.empty()) {
            return {};
        }
        const int length = WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                               nullptr, nullptr);
        std::string out(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_ACP, 0, wide.data(), static_cast<int>(wide.size()), out.data(), length, nullptr,
                            nullptr);
        return out;
    }
    case Encoding::Utf8:
        break;
    }
    return std::string(utf8);
}

}  // namespace sheetpad
