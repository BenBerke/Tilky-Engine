//
// Created by berke on 10/5/2026.
//

#include "Headers/Runtime/Scripting/Lua/LuaSourceRewrite.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <vector>

namespace {
    enum class TokenKind { Name, String, Number, Symbol };

    struct Token {
        TokenKind kind;
        std::size_t begin;
        std::size_t end;
    };

    bool IsNameStart(const char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
    bool IsNameChar(const char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
    bool IsDigit(const char c)     { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

    // `[[`, `[=[`, `[==[` ... -> the level (number of '='), or nullopt.
    std::optional<std::size_t> LongBracketLevel(const std::string_view source, const std::size_t i) {
        if (i >= source.size() || source[i] != '[') return std::nullopt;
        std::size_t j = i + 1;
        while (j < source.size() && source[j] == '=') ++j;
        if (j < source.size() && source[j] == '[') return j - i - 1;
        return std::nullopt;
    }

    // `i` is at the opening `[`. Returns the index just past the closing bracket.
    std::size_t SkipLongBracket(const std::string_view source, const std::size_t i, const std::size_t level) {
        std::string closing = "]";
        closing.append(level, '=');
        closing += ']';
        const std::size_t close = source.find(closing, i + level + 2);
        return close == std::string_view::npos ? source.size() : close + closing.size();
    }

    // Lexes just enough Lua to find names, strings and punctuation; anything
    // the real parser would reject is still tokenized somehow and the rewrite
    // simply won't match, leaving the error to Lua.
    std::vector<Token> Tokenize(const std::string_view source) {
        std::vector<Token> tokens;
        std::size_t i = 0;
        const std::size_t n = source.size();

        while (i < n) {
            const char c = source[i];

            if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

            if (c == '-' && i + 1 < n && source[i + 1] == '-') {
                if (const auto level = LongBracketLevel(source, i + 2)) {
                    i = SkipLongBracket(source, i + 2, *level);
                } else {
                    const std::size_t newline = source.find('\n', i);
                    i = newline == std::string_view::npos ? n : newline;
                }
                continue;
            }

            const std::size_t begin = i;

            if (IsNameStart(c)) {
                while (i < n && IsNameChar(source[i])) ++i;
                tokens.push_back({TokenKind::Name, begin, i});
                continue;
            }

            if (IsDigit(c) || (c == '.' && i + 1 < n && IsDigit(source[i + 1]))) {
                const bool hex = c == '0' && i + 1 < n && (source[i + 1] == 'x' || source[i + 1] == 'X');
                if (hex) i += 2;
                while (i < n) {
                    const char d = source[i];
                    const bool exponent = hex ? (d == 'p' || d == 'P') : (d == 'e' || d == 'E');
                    if (exponent && i + 1 < n && (source[i + 1] == '+' || source[i + 1] == '-')) { i += 2; continue; }
                    if (IsNameChar(d) || d == '.') { ++i; continue; }
                    break;
                }
                tokens.push_back({TokenKind::Number, begin, i});
                continue;
            }

            if (c == '"' || c == '\'') {
                ++i;
                while (i < n && source[i] != c && source[i] != '\n') {
                    if (source[i] == '\\' && i + 1 < n) ++i;
                    ++i;
                }
                if (i < n && source[i] == c) ++i;
                tokens.push_back({TokenKind::String, begin, i});
                continue;
            }

            if (const auto level = LongBracketLevel(source, i)) {
                i = SkipLongBracket(source, i, *level);
                tokens.push_back({TokenKind::String, begin, i});
                continue;
            }

            if (source.substr(i, 3) == "...") {
                i += 3;
            } else {
                static constexpr std::array<std::string_view, 9> kTwoChar = {"==", "~=", "<=", ">=", "//", "..", "::", "<<", ">>"};
                const std::string_view two = source.substr(i, 2);
                i += std::ranges::find(kTwoChar, two) != kTwoChar.end() ? 2 : 1;
            }
            tokens.push_back({TokenKind::Symbol, begin, i});
        }

        return tokens;
    }

    class Rewriter {
    public:
        explicit Rewriter(const std::string_view source) : source(source), tokens(Tokenize(source)) {
            MatchBrackets();
        }

        std::string Run() {
            struct Edit {
                std::size_t begin;
                std::size_t end; // == begin for a pure insertion
                std::string text;
            };
            std::vector<Edit> edits;

            // Pattern: <prefix> . <key> . <x|y|z|w>   followed by `=` or by
            // `, <more targets> =`. `dot` is the index of the `.` before key.
            for (std::size_t dot = 1; dot + 3 < tokens.size(); ++dot) {
                if (!IsSymbol(dot, ".") || !IsPlainName(dot + 1) || !IsSymbol(dot + 2, ".")) continue;
                if (!IsComponentName(dot + 3)) continue;
                if (!IsAssignmentTarget(dot + 4)) continue;

                const std::optional<std::size_t> prefixStart = FindPrefixStart(dot - 1);
                if (!prefixStart) continue;

                const Token& key = tokens[dot + 1];
                edits.push_back({tokens[*prefixStart].begin, tokens[*prefixStart].begin,
                                 std::string(LuaSourceRewrite::kVectorRefFunction) + "("});
                // Replace `.` and the key separately so whatever sits between
                // them (whitespace, newlines, comments) survives and line
                // numbers stay put.
                edits.push_back({tokens[dot].begin, tokens[dot].end, ","});
                edits.push_back({key.begin, key.end, "\"" + std::string(Text(dot + 1)) + "\")"});
            }

            if (edits.empty()) return std::string(source);

            std::ranges::stable_sort(edits, {}, &Edit::begin);

            std::string result;
            result.reserve(source.size() + edits.size() * 8);
            std::size_t cursor = 0;
            for (const Edit& edit : edits) {
                result.append(source.substr(cursor, edit.begin - cursor));
                result += edit.text;
                cursor = edit.end;
            }
            result.append(source.substr(cursor));
            return result;
        }

    private:
        std::string_view source;
        std::vector<Token> tokens;
        std::vector<std::size_t> match; // matching bracket index, or npos

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        void MatchBrackets() {
            match.assign(tokens.size(), npos);
            std::vector<std::size_t> stack;
            for (std::size_t i = 0; i < tokens.size(); ++i) {
                if (tokens[i].kind != TokenKind::Symbol) continue;
                const std::string_view t = Text(i);
                if (t == "(" || t == "[" || t == "{") {
                    stack.push_back(i);
                } else if (t == ")" || t == "]" || t == "}") {
                    if (stack.empty()) continue;
                    const std::size_t open = stack.back();
                    stack.pop_back();
                    match[open] = i;
                    match[i] = open;
                }
            }
        }

        [[nodiscard]] std::string_view Text(const std::size_t i) const {
            return source.substr(tokens[i].begin, tokens[i].end - tokens[i].begin);
        }

        [[nodiscard]] bool IsSymbol(const std::size_t i, const std::string_view symbol) const {
            return i < tokens.size() && tokens[i].kind == TokenKind::Symbol && Text(i) == symbol;
        }

        [[nodiscard]] bool IsOpenBracket(const std::size_t i) const {
            return IsSymbol(i, "(") || IsSymbol(i, "[") || IsSymbol(i, "{");
        }

        [[nodiscard]] bool IsPlainName(const std::size_t i) const {
            static constexpr std::array<std::string_view, 23> kKeywords = {
                "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "global", "goto", "if",
                "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
            };
            return i < tokens.size() && tokens[i].kind == TokenKind::Name
                && std::ranges::find(kKeywords, Text(i)) == kKeywords.end();
        }

        [[nodiscard]] bool IsComponentName(const std::size_t i) const {
            if (!IsPlainName(i)) return false;
            const std::string_view t = Text(i);
            return t == "x" || t == "y" || t == "z" || t == "w";
        }

        // `i` is the token right after the matched `.x`. True for `=`, and for
        // `, a.b, c[1] =` (the rest of a multiple-assignment target list).
        [[nodiscard]] bool IsAssignmentTarget(std::size_t i) const {
            while (true) {
                if (IsSymbol(i, "=")) return true;
                if (!IsSymbol(i, ",")) return false;
                ++i;

                // One more target: Name or (expr), then any suffixes.
                if (IsPlainName(i)) ++i;
                else if (IsSymbol(i, "(") && match[i] != npos) i = match[i] + 1;
                else return false;

                while (i < tokens.size()) {
                    if ((IsSymbol(i, ".") || IsSymbol(i, ":")) && IsPlainName(i + 1)) i += 2;
                    else if (IsOpenBracket(i) && match[i] != npos) i = match[i] + 1;
                    else if (tokens[i].kind == TokenKind::String) ++i;
                    else break;
                }
            }
        }

        // Walks back from `last` (the final token of the prefix expression)
        // to its first token. nullopt if it isn't a prefix expression.
        [[nodiscard]] std::optional<std::size_t> FindPrefixStart(std::size_t last) const {
            while (true) {
                const Token& token = tokens[last];

                if (IsPlainName(last)) {
                    if (last >= 2 && (IsSymbol(last - 1, ".") || IsSymbol(last - 1, ":"))) {
                        last -= 2;
                        continue;
                    }
                    return last;
                }

                std::size_t open = last;
                if (token.kind == TokenKind::String) {
                    // f"str" call argument; must follow a callable.
                } else if ((IsSymbol(last, ")") || IsSymbol(last, "]") || IsSymbol(last, "}")) && match[last] != npos) {
                    open = match[last];
                } else {
                    return std::nullopt;
                }

                const bool isParen = IsSymbol(open, "(");
                const bool followsExpression = open > 0 && (IsPlainName(open - 1) || IsSymbol(open - 1, ")")
                    || IsSymbol(open - 1, "]") || IsSymbol(open - 1, "}") || tokens[open - 1].kind == TokenKind::String);

                if (followsExpression) {
                    last = open - 1;
                    continue;
                }
                // `(expr).position.y = ...` - a parenthesized expression starts the prefix.
                if (isParen) return open;
                return std::nullopt;
            }
        }
    };
}

namespace LuaSourceRewrite {
    std::string RewriteVectorComponentAssignments(const std::string_view source) {
        return Rewriter(source).Run();
    }
}
