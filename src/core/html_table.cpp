#include "html_table.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sheetpad {

namespace {

constexpr int kMaxColSpan = 256;
constexpr int kMaxRowSpan = 10000;

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (Lower(a[i]) != Lower(b[i])) {
            return false;
        }
    }
    return true;
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = 0xFFFD;
    }
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct NamedEntity {
    std::string_view name;
    std::uint32_t codePoint;
};

constexpr NamedEntity kEntities[] = {
    {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},     {"apos", '\''},
    {"nbsp", 0xA0},    {"copy", 0xA9},    {"reg", 0xAE},     {"trade", 0x2122}, {"hellip", 0x2026},
    {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C},
    {"rdquo", 0x201D}, {"euro", 0x20AC},  {"pound", 0xA3},   {"yen", 0xA5},     {"cent", 0xA2},
    {"deg", 0xB0},     {"plusmn", 0xB1},  {"times", 0xD7},   {"divide", 0xF7},  {"laquo", 0xAB},
    {"raquo", 0xBB},   {"middot", 0xB7},  {"bull", 0x2022},  {"sect", 0xA7},    {"para", 0xB6},
};

// Decodes the entity that starts at text[pos] == '&'. Returns the code point and advances `pos` past it, or returns
// 0 and leaves `pos` unchanged when it is not a known entity.
std::uint32_t DecodeEntity(std::string_view text, std::size_t& pos) {
    const std::size_t semicolon = text.find(';', pos);
    if (semicolon == std::string_view::npos || semicolon - pos > 10 || semicolon - pos < 2) {
        return 0;
    }
    const std::string_view body = text.substr(pos + 1, semicolon - pos - 1);
    std::uint32_t value = 0;
    if (body[0] == '#') {
        if (body.size() < 2) {
            return 0;
        }
        const bool hex = body[1] == 'x' || body[1] == 'X';
        const std::string_view digits = body.substr(hex ? 2 : 1);
        if (digits.empty()) {
            return 0;
        }
        for (const char c : digits) {
            std::uint32_t digit;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (hex && Lower(c) >= 'a' && Lower(c) <= 'f') {
                digit = static_cast<std::uint32_t>(Lower(c) - 'a' + 10);
            } else {
                return 0;
            }
            value = value * (hex ? 16 : 10) + digit;
            if (value > 0x10FFFF) {
                return 0;
            }
        }
        if (value == 0) {
            return 0;
        }
    } else {
        for (const NamedEntity& entity : kEntities) {
            if (entity.name == body) {
                value = entity.codePoint;
                break;
            }
        }
        if (value == 0) {
            return 0;
        }
    }
    pos = semicolon + 1;
    return value;
}

struct Tag {
    std::string name;  // lower case, without '/'
    bool closing = false;
    std::string_view attributes;
};

// The value of attribute `name` in a tag's attribute text, or empty.
std::string_view Attribute(std::string_view attributes, std::string_view name) {
    std::size_t i = 0;
    while (i < attributes.size()) {
        while (i < attributes.size() && (IsSpace(attributes[i]) || attributes[i] == '/')) {
            ++i;
        }
        const std::size_t nameStart = i;
        while (i < attributes.size() && !IsSpace(attributes[i]) && attributes[i] != '=' && attributes[i] != '/') {
            ++i;
        }
        const std::string_view key = attributes.substr(nameStart, i - nameStart);
        while (i < attributes.size() && IsSpace(attributes[i])) {
            ++i;
        }
        std::string_view value;
        if (i < attributes.size() && attributes[i] == '=') {
            ++i;
            while (i < attributes.size() && IsSpace(attributes[i])) {
                ++i;
            }
            if (i < attributes.size() && (attributes[i] == '"' || attributes[i] == '\'')) {
                const char quote = attributes[i++];
                const std::size_t start = i;
                while (i < attributes.size() && attributes[i] != quote) {
                    ++i;
                }
                value = attributes.substr(start, i - start);
                if (i < attributes.size()) {
                    ++i;
                }
            } else {
                const std::size_t start = i;
                while (i < attributes.size() && !IsSpace(attributes[i])) {
                    ++i;
                }
                value = attributes.substr(start, i - start);
            }
        }
        if (!key.empty() && IEquals(key, name)) {
            return value;
        }
    }
    return {};
}

int SpanOf(std::string_view attributes, std::string_view name, int limit) {
    const std::string_view value = Attribute(attributes, name);
    int parsed = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') {
            break;
        }
        parsed = std::min(parsed * 10 + (c - '0'), limit);
    }
    return std::clamp(parsed, 1, limit);
}

bool IsBlockTag(const std::string& name) {
    return name == "p" || name == "div" || name == "li" || name == "ul" || name == "ol" || name == "h1" ||
           name == "h2" || name == "h3" || name == "h4" || name == "h5" || name == "h6" || name == "pre" ||
           name == "blockquote";
}

bool IsVoidTag(const std::string& name) {
    return name == "br" || name == "img" || name == "hr" || name == "input" || name == "meta" || name == "link" ||
           name == "wbr" || name == "area" || name == "col" || name == "source" || name == "embed";
}

// The value of `display` in an inline style, lower case, or empty.
std::string StyleDisplay(std::string_view attributes) {
    const std::string_view style = Attribute(attributes, "style");
    for (std::size_t at = 0; at + 7 <= style.size(); ++at) {
        if (!IEquals(style.substr(at, 7), "display") || (at > 0 && style[at - 1] != ';' && !IsSpace(style[at - 1]))) {
            continue;
        }
        std::size_t i = at + 7;
        while (i < style.size() && IsSpace(style[i])) {
            ++i;
        }
        if (i >= style.size() || style[i] != ':') {
            continue;
        }
        ++i;
        while (i < style.size() && IsSpace(style[i])) {
            ++i;
        }
        std::string value;
        while (i < style.size() && style[i] != ';' && !IsSpace(style[i])) {
            value += Lower(style[i++]);
        }
        return value;
    }
    return {};
}

bool HasClass(std::string_view attributes, std::string_view wanted) {
    const std::string_view classes = Attribute(attributes, "class");
    std::size_t i = 0;
    while (i < classes.size()) {
        while (i < classes.size() && IsSpace(classes[i])) {
            ++i;
        }
        const std::size_t start = i;
        while (i < classes.size() && !IsSpace(classes[i])) {
            ++i;
        }
        if (i > start && classes.substr(start, i - start) == wanted) {
            return true;
        }
    }
    return false;
}

// Layout rows built from divs and spans: a horizontal flex or grid container, taken from the inline style that
// browsers add to copied content, or from utility classes such as Tailwind's "flex" and "grid".
bool IsRowContainer(std::string_view attributes) {
    const std::string display = StyleDisplay(attributes);
    if (!display.empty()) {
        const bool flexLike = display == "flex" || display == "inline-flex" || display == "grid" ||
                              display == "inline-grid";
        const std::string_view style = Attribute(attributes, "style");
        bool column = false;
        for (std::size_t at = 0; at + 14 <= style.size(); ++at) {
            if (IEquals(style.substr(at, 14), "flex-direction")) {
                column = style.find("column", at) != std::string_view::npos;
                break;
            }
        }
        return flexLike && !column;
    }
    if (HasClass(attributes, "flex-col") || HasClass(attributes, "flex-col-reverse")) {
        return false;
    }
    return HasClass(attributes, "flex") || HasClass(attributes, "inline-flex") || HasClass(attributes, "grid") ||
           HasClass(attributes, "inline-grid");
}

bool IsBlockElement(const std::string& name, std::string_view attributes) {
    const std::string display = StyleDisplay(attributes);
    if (!display.empty()) {
        return display != "inline" && display != "inline-block" && display != "none" && display != "contents";
    }
    return IsBlockTag(name) || HasClass(attributes, "flex") || HasClass(attributes, "grid") ||
           HasClass(attributes, "block");
}

class TableBuilder {
public:
    TableBuilder(Table& out, SpanFill fill) : out_(out), fill_(fill) {}

    void Parse(std::string_view html);

private:
    struct Carry {
        int remaining = 0;
        std::string value;
    };

    void StartRow() {
        FinishRow();
        rowOpen_ = true;
    }

    void StartCell(std::string_view attributes) {
        FinishCell();
        if (!rowOpen_) {
            rowOpen_ = true;
        }
        cellOpen_ = true;
        colSpan_ = SpanOf(attributes, "colspan", kMaxColSpan);
        rowSpan_ = SpanOf(attributes, "rowspan", kMaxRowSpan);
        text_.clear();
    }

    // Fills the columns that earlier rowspans still cover, starting at the end of the current row.
    void FillCarried() {
        while (row_.size() < carry_.size() && carry_[row_.size()].remaining > 0) {
            Carry& carry = carry_[row_.size()];
            row_.push_back(fill_ == SpanFill::Repeat ? carry.value : std::string());
            --carry.remaining;
        }
    }

    void FinishCell() {
        if (!cellOpen_) {
            return;
        }
        cellOpen_ = false;
        while (!text_.empty() && (text_.back() == ' ' || text_.back() == '\n')) {
            text_.pop_back();
        }
        FillCarried();
        const std::size_t start = row_.size();
        for (int i = 0; i < colSpan_; ++i) {
            row_.push_back(i == 0 || fill_ == SpanFill::Repeat ? text_ : std::string());
        }
        if (rowSpan_ > 1) {
            if (carry_.size() < row_.size()) {
                carry_.resize(row_.size());
            }
            for (int i = 0; i < colSpan_; ++i) {
                carry_[start + static_cast<std::size_t>(i)] = {rowSpan_ - 1, text_};
            }
        }
        text_.clear();
    }

    void FinishRow() {
        FinishCell();
        if (!rowOpen_) {
            return;
        }
        rowOpen_ = false;
        FillCarried();
        if (!row_.empty()) {
            out_.push_back(std::move(row_));
        }
        row_.clear();
    }

    // Where text goes: the open cell, or the current line of loose text outside any table.
    std::string* Target() {
        if (cellOpen_) {
            return &text_;
        }
        if (rowIndex_ >= 0) {
            return nullptr;  // whitespace and stray text between the cells of a layout row
        }
        return depth_ == 0 ? &line_ : nullptr;
    }

    // Tags outside real tables: a stack of open elements lets flex/grid containers act as rows.
    void Loose(const Tag& tag) {
        if (IsVoidTag(tag.name)) {
            if (tag.name == "br") {
                if (cellOpen_) {
                    while (!text_.empty() && text_.back() == ' ') {
                        text_.pop_back();
                    }
                    text_ += '\n';
                } else if (rowIndex_ < 0) {
                    FlushLine();
                }
            }
            return;
        }
        if (!tag.closing) {
            const bool rowStart = rowIndex_ < 0 && IsRowContainer(tag.attributes);
            const bool block = rowStart || IsBlockElement(tag.name, tag.attributes);
            const bool rowChild = rowIndex_ >= 0 && static_cast<int>(stack_.size()) == rowIndex_ + 1;
            if (rowStart) {
                FlushLine();
            } else if (block) {
                BlockBreak();
            }
            if (rowChild) {
                StartCell({});
            }
            stack_.push_back({tag.name, block});
            if (rowStart) {
                rowIndex_ = static_cast<int>(stack_.size()) - 1;
                StartRow();
            }
            return;
        }
        std::size_t match = stack_.size();
        while (match > 0 && stack_[match - 1].name != tag.name) {
            --match;
        }
        if (match == 0) {
            return;
        }
        bool block = false;
        while (stack_.size() >= match) {
            const int top = static_cast<int>(stack_.size()) - 1;
            if (top == rowIndex_) {
                FinishRow();
                rowIndex_ = -1;
            } else if (rowIndex_ >= 0 && top == rowIndex_ + 1) {
                FinishCell();
            }
            block = stack_.back().block;
            stack_.pop_back();
        }
        if (block) {
            BlockBreak();
        }
    }

    void BlockBreak() {
        if (cellOpen_) {
            AppendNewline();
        } else if (rowIndex_ < 0) {
            FlushLine();
        }
    }

    void AppendText(char c) {
        std::string* target = Target();
        if (!target) {
            return;
        }
        if (IsSpace(c)) {
            if (!target->empty() && target->back() != ' ' && target->back() != '\n') {
                *target += ' ';
            }
        } else {
            *target += c;
        }
    }

    void AppendCodePoint(std::uint32_t cp) {
        std::string* target = Target();
        if (!target) {
            return;
        }
        if (cp == 0xA0) {
            *target += ' ';
        } else {
            AppendUtf8(*target, cp);
        }
    }

    void AppendNewline() {
        if (cellOpen_ && !text_.empty() && text_.back() != '\n') {
            while (!text_.empty() && text_.back() == ' ') {
                text_.pop_back();
            }
            text_ += '\n';
        }
    }

    // Loose text outside tables becomes one single-cell row per line.
    void FlushLine() {
        while (!line_.empty() && line_.back() == ' ') {
            line_.pop_back();
        }
        if (!line_.empty()) {
            out_.push_back(Row{std::move(line_)});
        }
        line_.clear();
    }

    Table& out_;
    SpanFill fill_;
    std::vector<Carry> carry_;
    Row row_;
    std::string text_;
    std::string line_;
    struct Open {
        std::string name;
        bool block;
    };
    std::vector<Open> stack_;
    int rowIndex_ = -1;  // index in stack_ of the open layout row, or -1
    int depth_ = 0;
    bool rowOpen_ = false;
    bool cellOpen_ = false;
    int colSpan_ = 1;
    int rowSpan_ = 1;
};

void TableBuilder::Parse(std::string_view html) {
    int& depth = depth_;
    std::size_t i = 0;
    while (i < html.size()) {
        const char c = html[i];
        if (c == '<') {
            if (html.compare(i, 4, "<!--") == 0) {
                const std::size_t end = html.find("-->", i + 4);
                i = end == std::string_view::npos ? html.size() : end + 3;
                continue;
            }
            std::size_t j = i + 1;
            Tag tag;
            if (j < html.size() && html[j] == '/') {
                tag.closing = true;
                ++j;
            }
            const std::size_t nameStart = j;
            while (j < html.size() && !IsSpace(html[j]) && html[j] != '>' && html[j] != '/') {
                ++j;
            }
            if (j == nameStart || !((html[nameStart] >= 'A' && html[nameStart] <= 'Z') ||
                                    (html[nameStart] >= 'a' && html[nameStart] <= 'z'))) {
                // "<!DOCTYPE", "<?xml" or a stray '<': skip to the closing '>' or treat '<' as text.
                if (nameStart < html.size() && (html[nameStart] == '!' || html[nameStart] == '?')) {
                    const std::size_t end = html.find('>', i);
                    i = end == std::string_view::npos ? html.size() : end + 1;
                } else {
                    AppendText('<');
                    ++i;
                }
                continue;
            }
            for (std::size_t k = nameStart; k < j; ++k) {
                tag.name += Lower(html[k]);
            }
            // Find the end of the tag, honoring quoted attribute values that may contain '>'.
            const std::size_t attrStart = j;
            char quote = 0;
            while (j < html.size() && (quote != 0 || html[j] != '>')) {
                if (quote != 0) {
                    if (html[j] == quote) {
                        quote = 0;
                    }
                } else if (html[j] == '"' || html[j] == '\'') {
                    quote = html[j];
                }
                ++j;
            }
            tag.attributes = html.substr(attrStart, j - attrStart);
            i = j < html.size() ? j + 1 : html.size();

            if ((tag.name == "script" || tag.name == "style" || tag.name == "head" || tag.name == "title") &&
                !tag.closing) {
                std::size_t k = i;
                while (k < html.size()) {
                    const std::size_t open = html.find("</", k);
                    if (open == std::string_view::npos) {
                        k = html.size();
                        break;
                    }
                    if (html.size() - open >= 2 + tag.name.size() &&
                        IEquals(html.substr(open + 2, tag.name.size()), tag.name)) {
                        k = open;
                        break;
                    }
                    k = open + 2;
                }
                const std::size_t end = html.find('>', k);
                i = end == std::string_view::npos ? html.size() : end + 1;
                continue;
            }

            if (tag.name == "table") {
                if (!tag.closing) {
                    if (depth == 0) {
                        FinishRow();
                        FlushLine();
                        carry_.clear();
                    }
                    ++depth;
                } else if (depth > 0) {
                    --depth;
                    if (depth == 0) {
                        FinishRow();
                    }
                }
            } else if (depth == 0) {
                Loose(tag);
            } else if (depth == 1 && tag.name == "tr") {
                if (tag.closing) {
                    FinishRow();
                } else {
                    StartRow();
                }
            } else if (depth == 1 && (tag.name == "td" || tag.name == "th")) {
                if (tag.closing) {
                    FinishCell();
                } else {
                    StartCell(tag.attributes);
                }
            } else if (depth > 1 && (tag.name == "td" || tag.name == "th") && tag.closing) {
                AppendText(' ');
            } else if (depth > 1 && tag.name == "tr" && tag.closing) {
                AppendNewline();
            } else if (depth >= 1) {
                if (tag.name == "br") {
                    if (cellOpen_) {
                        while (!text_.empty() && text_.back() == ' ') {
                            text_.pop_back();
                        }
                        text_ += '\n';
                    }
                } else if (IsBlockTag(tag.name)) {
                    AppendNewline();
                }
            }
            continue;
        }

        if (c == '&') {
            std::size_t pos = i;
            if (const std::uint32_t cp = DecodeEntity(html, pos)) {
                AppendCodePoint(cp);
                i = pos;
                continue;
            }
        }
        AppendText(c);
        ++i;
    }
    FinishRow();
    FlushLine();
}

bool ParseNumber(std::string_view text, std::size_t& value) {
    if (text.empty()) {
        return false;
    }
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<std::size_t>(c - '0');
        if (value > (std::size_t{1} << 40)) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string_view ExtractClipboardHtmlFragment(std::string_view clipboard) {
    while (!clipboard.empty() && clipboard.back() == '\0') {
        clipboard.remove_suffix(1);
    }
    const auto field = [&](std::string_view key, std::size_t& value) {
        const std::size_t at = clipboard.find(key);
        if (at == std::string_view::npos) {
            return false;
        }
        const std::size_t start = at + key.size();
        std::size_t end = start;
        while (end < clipboard.size() && clipboard[end] >= '0' && clipboard[end] <= '9') {
            ++end;
        }
        return ParseNumber(clipboard.substr(start, end - start), value);
    };
    std::size_t begin = 0;
    std::size_t end = 0;
    if (field("StartFragment:", begin) && field("EndFragment:", end) && begin < end && end <= clipboard.size()) {
        return clipboard.substr(begin, end - begin);
    }
    return clipboard;
}

bool ParseHtml(std::string_view html, Table& out, SpanFill fill) {
    out.clear();
    TableBuilder builder(out, fill);
    builder.Parse(html);
    return !out.empty();
}

}  // namespace sheetpad
