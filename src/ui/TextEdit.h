#pragma once
#include <algorithm>
#include <string>

namespace genesis::ui {
// Single-line UTF-8 editing state. Byte offsets always stay on codepoint
// boundaries. Platform clipboard/IME integration is deliberately outside it.
class TextEdit {
public:
    std::string value;
    size_t cursor = 0, anchor = 0;
    void set(std::string text) { value = std::move(text); cursor = anchor = value.size(); }
    size_t begin() const { return std::min(cursor,anchor); }
    size_t end() const { return std::max(cursor,anchor); }
    std::string selection() const { return value.substr(begin(),end()-begin()); }
    static size_t previous(const std::string& text, size_t at) {
        if (!at) return 0;
        --at;
        while (at && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) --at;
        return at;
    }
    static size_t next(const std::string& text, size_t at) {
        if (at >= text.size()) return text.size();
        ++at;
        while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) ++at;
        return at;
    }
    void move(size_t at, bool select) { cursor = std::min(at,value.size()); if (!select) anchor = cursor; }
    void all() { anchor = 0; cursor = value.size(); }
    bool replace(std::string text) {
        text.erase(std::remove_if(text.begin(),text.end(),[](char c) { return c == '\n' || c == '\r' || c == '\t'; }),text.end());
        if (value.size()-(end()-begin())+text.size() > 16384) return false;
        const auto start = begin();
        value.replace(start,end()-start,text);
        cursor = anchor = start+text.size();
        return true;
    }
    void backspace() { if (begin() == end()) anchor = previous(value,cursor); replace(""); }
    void erase() { if (begin() == end()) anchor = next(value,cursor); replace(""); }
};
}
