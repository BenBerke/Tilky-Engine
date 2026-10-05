//
// Created by berke on 10/5/2026.
//

#include "Headers/Runtime/Scripting/Lua/LuaScriptCompiler.hpp"

#include "Headers/Objects/Components.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScripting.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <unordered_set>

namespace {
    using LuaScriptCompiler::Diagnostic;
    using LuaScriptCompiler::Result;

    // ------------------------------------------------------------------
    // Tokenizer
    // ------------------------------------------------------------------

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
    // the real parser would reject is still tokenized somehow and the passes
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

    bool IsLuaKeyword(const std::string_view word) {
        static constexpr std::array<std::string_view, 23> kKeywords = {
            "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "global", "goto", "if",
            "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while",
        };
        return std::ranges::find(kKeywords, word) != kKeywords.end();
    }

    // ------------------------------------------------------------------
    // Registries
    // ------------------------------------------------------------------

    // A type a declaration can name, e.g. `number` or `Rigidbody`.
    struct FieldTypeDef {
        std::string_view name;
        ScriptValueType type;
        std::string_view luaType;  // runtime usertype, for editor member completion
        int componentType = -1;    // type == Component only
    };

    // `enum(A, B, C)` and `Key` are handled on top of this table - see ParseType.
    const std::vector<FieldTypeDef>& FieldTypes() {
        static const std::vector<FieldTypeDef> types = {
            {"int",              ScriptValueType::Int,       ""},
            {"integer",          ScriptValueType::Int,       ""},
            {"number",           ScriptValueType::Float,     ""},
            {"float",            ScriptValueType::Float,     ""},
            {"bool",             ScriptValueType::Bool,      ""},
            {"boolean",          ScriptValueType::Bool,      ""},
            {"string",           ScriptValueType::String,    ""},
            {"Vector2",          ScriptValueType::Vector2,   "Vector2"},
            {"Vector3",          ScriptValueType::Vector3,   "Vector3"},
            {"Vector4",          ScriptValueType::Vector4,   "Vector4"},
            {"Entity",           ScriptValueType::Entity,    "Entity"},
            {"Behaviour",        ScriptValueType::Behaviour, "Behaviour"},
            {"Script",           ScriptValueType::Behaviour, "Behaviour"},
            {"Asset",            ScriptValueType::Asset,     ""},
            {"Texture",          ScriptValueType::Asset,     ""},
            {"Wall",             ScriptValueType::Wall,      "Wall"},
            {"Sector",           ScriptValueType::Sector,    "Sector"},
            {"Key",              ScriptValueType::Enum,      ""},
            {"enum",             ScriptValueType::Enum,      ""},
            {"Transform",        ScriptValueType::Component, "Transform",        CMP_TRANSFORM},
            {"Sprite",           ScriptValueType::Component, "Sprite",           CMP_SPRITE},
            {"AudioSource",      ScriptValueType::Component, "AudioSource",      CMP_AUDIO_SOURCE},
            {"PlayerController", ScriptValueType::Component, "PlayerController", CMP_PLAYER_CONTROLLER},
            {"Camera",           ScriptValueType::Component, "Camera",           CMP_CAMERA},
            {"Collider",         ScriptValueType::Component, "Collider",         CMP_COLLIDER},
            {"Rigidbody",        ScriptValueType::Component, "Rigidbody",        CMP_RIGIDBODY},
            {"Model",            ScriptValueType::Component, "Model",            CMP_MODEL},
        };
        return types;
    }

    const FieldTypeDef* FindFieldType(const std::string_view name) {
        for (const FieldTypeDef& type : FieldTypes())
            if (type.name == name) return &type;
        return nullptr;
    }

    // An attribute as written: `range(0, 100)` -> {"range", {"0", "100"}}.
    struct AttributeUse {
        std::string name;
        std::vector<std::string> arguments; // trimmed source text of each argument
        int line = 0;
    };

    // A fully parsed declaration, handed to its keyword's handler.
    struct Declaration {
        std::string keyword;
        std::vector<AttributeUse> attributes;
        const FieldTypeDef* type = nullptr;
        std::string typeName;
        std::vector<ScriptEnumOption> enumOptions;
        std::string name;
        ScriptValue defaultValue;
        std::string luaValue; // what `name` is assigned in the compiled Lua
        int line = 0;
    };

    // What handlers can do besides returning their Lua: report errors and
    // add to the compile result.
    class CompileContext {
    public:
        explicit CompileContext(Result& result) : result(result) {}

        void Error(const int line, std::string message) const {
            result.errors.push_back({line, std::move(message)});
        }

        Result& result;
    };

    struct FieldAttribute {
        std::string_view name;
        // Applies the attribute to a public field; reports problems through context.
        void (*apply)(const AttributeUse& use, ScriptPublicField& field, CompileContext& context);
    };

    // Attributes a public field can carry (`public <attribute>(...) Type name`).
    // None yet - add an entry here to introduce one.
    const std::vector<FieldAttribute>& FieldAttributes() {
        static const std::vector<FieldAttribute> attributes = {};
        return attributes;
    }

    const FieldAttribute* FindFieldAttribute(const std::string_view name) {
        for (const FieldAttribute& attribute : FieldAttributes())
            if (attribute.name == name) return &attribute;
        return nullptr;
    }

    bool IsReservedFieldName(const std::string& name) {
        static const std::unordered_set<std::string> reserved = {
            "Start", "Update", "FixedUpdate", "OnEnable", "OnDisable", "OnDestroy",
            "OnEntityEnter", "OnEntityExit",
            "OnCollisionEnter", "OnCollision", "OnCollisionExit",
            "OnTriggerEnter", "OnTrigger", "OnTriggerExit", "OnSectorChange",
            "entity", "sector", "Global", "GameTime", "Input", "Game", "Debug",
            LuaScriptCompiler::kVectorRefFunction
        };
        return reserved.contains(name);
    }

    // `public` - an Inspector-editable field. Returns the Lua the declaration compiles to.
    std::string CompilePublic(const Declaration& declaration, CompileContext& context) {
        Result& result = context.result;

        if (IsReservedFieldName(declaration.name)) {
            context.Error(declaration.line, fmt::format("'{}' is a reserved name and can't be a public field", declaration.name));
            return {};
        }

        const bool duplicate = std::ranges::any_of(result.publicFields, [&](const ScriptPublicField& field) {
            return field.name == declaration.name;
        });
        if (duplicate) {
            context.Error(declaration.line, fmt::format("public field '{}' is declared twice", declaration.name));
            return {};
        }

        ScriptPublicField field;
        field.name = declaration.name;
        field.type = declaration.type->type;
        field.componentType = declaration.type->componentType;
        field.enumOptions = declaration.enumOptions;
        field.defaultValue = declaration.defaultValue;
        field.displayName = LuaScriptCompiler::DisplayNameFromIdentifier(declaration.name);

        for (const AttributeUse& use : declaration.attributes)
            FindFieldAttribute(use.name)->apply(use, field, context);

        result.publicFields.push_back(std::move(field));

        return declaration.name + " = " + declaration.luaValue;
    }

    struct DeclarationKeyword {
        std::string_view name;
        // Turns a parsed declaration into compiler output; returns the Lua
        // that replaces it (empty after reporting an error).
        std::string (*compile)(const Declaration& declaration, CompileContext& context);
        bool allowsAttributes;
    };

    // Every declaration keyword. Add an entry here to introduce one.
    const std::vector<DeclarationKeyword>& DeclarationKeywords() {
        static const std::vector<DeclarationKeyword> keywords = {
            {"public", CompilePublic, true},
        };
        return keywords;
    }

    const DeclarationKeyword* FindDeclarationKeyword(const std::string_view name) {
        for (const DeclarationKeyword& keyword : DeclarationKeywords())
            if (keyword.name == name) return &keyword;
        return nullptr;
    }

    const std::vector<ScriptEnumOption>& KeyOptions() {
        static const std::vector<ScriptEnumOption> options = LuaScriptSystem::KeyEnumOptions();
        return options;
    }

    ScriptValue ZeroValue(const ScriptValueType type, const std::vector<ScriptEnumOption>& enumOptions) {
        switch (type) {
            case ScriptValueType::Int:       return ScriptValue{0};
            case ScriptValueType::Float:     return ScriptValue{0.0f};
            case ScriptValueType::Bool:      return ScriptValue{false};
            case ScriptValueType::String:    return ScriptValue{std::string{}};
            case ScriptValueType::Vector2:   return ScriptValue{Vector2{0.0f, 0.0f}};
            case ScriptValueType::Vector3:   return ScriptValue{Vector3{0.0f, 0.0f, 0.0f}};
            case ScriptValueType::Vector4:   return ScriptValue{Vector4{0.0f, 0.0f, 0.0f, 0.0f}};
            case ScriptValueType::Enum:      return ScriptValue{enumOptions.empty() ? 0 : enumOptions.front().value};
            case ScriptValueType::Entity:    return ScriptValue{EntityRefValue{}};
            case ScriptValueType::Component: return ScriptValue{ComponentRefValue{}};
            case ScriptValueType::Behaviour: return ScriptValue{BehaviourRefValue{}};
            case ScriptValueType::Asset:     return ScriptValue{AssetRefValue{}};
            case ScriptValueType::Wall:      return ScriptValue{WallRefValue{}};
            case ScriptValueType::Sector:    return ScriptValue{SectorRefValue{}};
        }
        return ScriptValue{0};
    }

    // Body of a short string literal with the common escapes resolved.
    std::string UnquoteString(const std::string_view literal) {
        if (literal.starts_with('[')) {
            const std::size_t open = literal.find('[', 1) + 1;
            const std::size_t level = open - 2;
            std::string_view body = literal.substr(open, literal.size() - open - level - 2);
            if (body.starts_with('\n')) body.remove_prefix(1); // Lua drops a first newline
            return std::string(body);
        }

        std::string text;
        for (std::size_t i = 1; i + 1 < literal.size(); ++i) {
            if (literal[i] != '\\' || i + 2 >= literal.size()) { text += literal[i]; continue; }
            switch (const char escaped = literal[++i]) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                default:  text += escaped; break; // \\ \" \'
            }
        }
        return text;
    }

    // ------------------------------------------------------------------
    // Compiler
    // ------------------------------------------------------------------

    class Compiler {
    public:
        explicit Compiler(const std::string_view source) : source(source), tokens(Tokenize(source)) {
            MatchBrackets();
            ComputeLineStarts();
            ComputeTopLevel();
        }

        Result Run() {
            Result result;
            CompileContext context(result);

            for (std::size_t i = 0; i < tokens.size(); ++i) {
                if (tokens[i].kind != TokenKind::Name) continue;
                const DeclarationKeyword* keyword = FindDeclarationKeyword(Text(i));
                if (keyword == nullptr) continue;
                if (i > 0 && (IsSymbol(i - 1, ".") || IsSymbol(i - 1, ":"))) continue; // `obj.public` is a field access

                CompileDeclaration(i, *keyword, context);
            }

            CompileVectorAssignments();

            result.luaSource = ApplyEdits();
            std::ranges::stable_sort(result.errors, {}, &Diagnostic::line);
            return result;
        }

    private:
        struct Edit {
            std::size_t begin;
            std::size_t end; // == begin for a pure insertion
            std::string text;
        };

        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        std::string_view source;
        std::vector<Token> tokens;
        std::vector<std::size_t> match;      // matching bracket index, or npos
        std::vector<std::size_t> lineStarts; // source offset of each line
        std::vector<bool> topLevel;          // token sits outside every block and bracket
        std::vector<Edit> edits;

        // -- token helpers -------------------------------------------------

        [[nodiscard]] std::string_view Text(const std::size_t i) const {
            return source.substr(tokens[i].begin, tokens[i].end - tokens[i].begin);
        }

        [[nodiscard]] std::string_view Span(const std::size_t first, const std::size_t last) const {
            return source.substr(tokens[first].begin, tokens[last].end - tokens[first].begin);
        }

        [[nodiscard]] int LineOf(const std::size_t i) const {
            const std::size_t offset = i < tokens.size() ? tokens[i].begin : source.size();
            return static_cast<int>(std::ranges::upper_bound(lineStarts, offset) - lineStarts.begin());
        }

        [[nodiscard]] bool IsSymbol(const std::size_t i, const std::string_view symbol) const {
            return i < tokens.size() && tokens[i].kind == TokenKind::Symbol && Text(i) == symbol;
        }

        [[nodiscard]] bool IsWord(const std::size_t i, const std::string_view word) const {
            return i < tokens.size() && tokens[i].kind == TokenKind::Name && Text(i) == word;
        }

        [[nodiscard]] bool IsOpenBracket(const std::size_t i) const {
            return IsSymbol(i, "(") || IsSymbol(i, "[") || IsSymbol(i, "{");
        }

        [[nodiscard]] bool IsPlainName(const std::size_t i) const {
            return i < tokens.size() && tokens[i].kind == TokenKind::Name && !IsLuaKeyword(Text(i));
        }

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

        void ComputeLineStarts() {
            lineStarts.push_back(0);
            for (std::size_t i = 0; i < source.size(); ++i)
                if (source[i] == '\n') lineStarts.push_back(i + 1);
        }

        // Block openers are `function`, `do` (also ending `while`/`for`
        // headers), `if` and `repeat`; `end` and `until` close them.
        void ComputeTopLevel() {
            topLevel.assign(tokens.size(), false);
            int blocks = 0;
            int brackets = 0;

            for (std::size_t i = 0; i < tokens.size(); ++i) {
                const std::string_view t = Text(i);

                if (tokens[i].kind == TokenKind::Symbol) {
                    if (t == "(" || t == "[" || t == "{") ++brackets;
                    else if (t == ")" || t == "]" || t == "}") brackets = std::max(0, brackets - 1);
                } else if (tokens[i].kind == TokenKind::Name) {
                    if (t == "end" || t == "until") blocks = std::max(0, blocks - 1);
                }

                topLevel[i] = blocks == 0 && brackets == 0;

                if (tokens[i].kind == TokenKind::Name && (t == "function" || t == "do" || t == "if" || t == "repeat"))
                    ++blocks;
            }
        }

        // Replaces source[begin, end) with `text`, keeping the replaced
        // range's newlines so every later line keeps its number.
        void Replace(const std::size_t begin, const std::size_t end, std::string text) {
            text.append(static_cast<std::size_t>(std::ranges::count(source.substr(begin, end - begin), '\n')), '\n');
            edits.push_back({begin, end, std::move(text)});
        }

        std::string ApplyEdits() {
            std::ranges::stable_sort(edits, {}, &Edit::begin);

            std::string output;
            output.reserve(source.size() + edits.size() * 8);
            std::size_t cursor = 0;
            for (const Edit& edit : edits) {
                output.append(source.substr(cursor, edit.begin - cursor));
                output += edit.text;
                cursor = edit.end;
            }
            output.append(source.substr(cursor));
            return output;
        }

        // `( a, b, c )` at `open` -> {"a", "b", "c"} (each argument's trimmed text).
        [[nodiscard]] std::vector<std::string> Arguments(const std::size_t open) const {
            std::vector<std::string> arguments;
            const std::size_t close = match[open];
            std::size_t first = open + 1;

            for (std::size_t i = open + 1; i <= close; ++i) {
                if (IsOpenBracket(i) && match[i] != npos) { i = match[i]; continue; }
                if (i != close && !IsSymbol(i, ",")) continue;
                if (i > first) arguments.emplace_back(Span(first, i - 1));
                else if (i != close || !arguments.empty()) arguments.emplace_back();
                first = i + 1;
            }
            return arguments;
        }

        // -- declarations ------------------------------------------------------

        void CompileDeclaration(const std::size_t keywordIndex, const DeclarationKeyword& keyword, CompileContext& context) {
            const int line = LineOf(keywordIndex);
            const std::string keywordName(keyword.name);

            if (!topLevel[keywordIndex]) {
                context.Error(line, fmt::format("'{}' is only allowed at the top level of a script, outside functions and blocks", keywordName));
                return;
            }
            if (keywordIndex > 0 && (IsWord(keywordIndex - 1, "local") || IsWord(keywordIndex - 1, "global"))) {
                context.Error(line, fmt::format("'{} {}' is not allowed - '{}' variables can't be local", Text(keywordIndex - 1), keywordName, keywordName));
                return;
            }

            Declaration declaration;
            declaration.keyword = keywordName;
            declaration.line = line;

            std::size_t i = keywordIndex + 1;

            // Attributes, up to the first type name.
            while (true) {
                if (!IsPlainName(i)) {
                    context.Error(line, fmt::format("expected a type after '{}'", keywordName));
                    return;
                }
                if (FindFieldType(Text(i)) != nullptr) break;

                AttributeUse use{std::string(Text(i)), {}, LineOf(i)};
                const bool hasArguments = IsSymbol(i + 1, "(") && match[i + 1] != npos;

                if (FindFieldAttribute(use.name) == nullptr) {
                    context.Error(line, hasArguments
                        ? fmt::format("unknown attribute '{}'", use.name)
                        : fmt::format("unknown type '{}'", use.name));
                    return;
                }
                if (!keyword.allowsAttributes) {
                    context.Error(line, fmt::format("'{}' declarations can't have attributes", keywordName));
                    return;
                }

                if (hasArguments) {
                    use.arguments = Arguments(i + 1);
                    i = match[i + 1] + 1;
                } else {
                    ++i;
                }
                declaration.attributes.push_back(std::move(use));
            }

            if (!ParseType(i, declaration, context)) return;

            if (!IsPlainName(i)) {
                context.Error(line, fmt::format("expected a variable name after '{}'", declaration.typeName));
                return;
            }
            declaration.name = std::string(Text(i));
            std::size_t last = i;
            ++i;

            if (IsSymbol(i, "=")) {
                const std::optional<std::size_t> valueLast = ParseDefault(i + 1, declaration, context);
                if (!valueLast) return;
                last = *valueLast;
            } else {
                declaration.defaultValue = ZeroValue(declaration.type->type, declaration.enumOptions);
                declaration.luaValue = "nil";
            }

            std::string lua = keyword.compile(declaration, context);
            if (lua.empty()) return;

            context.result.declarations.push_back({
                keywordName, declaration.typeName, std::string(declaration.type->luaType), declaration.name, line
            });
            Replace(tokens[keywordIndex].begin, tokens[last].end, std::move(lua));
        }

        // Reads the type at `i` (and `enum(...)`'s options); leaves `i` after it.
        bool ParseType(std::size_t& i, Declaration& declaration, CompileContext& context) {
            declaration.type = FindFieldType(Text(i));
            declaration.typeName = std::string(Text(i));
            const int line = declaration.line;
            ++i;

            if (declaration.typeName == "Key") {
                declaration.enumOptions = KeyOptions();
            } else if (declaration.typeName == "enum") {
                if (!IsSymbol(i, "(") || match[i] == npos) {
                    context.Error(line, "enum needs its options: enum(OptionA, OptionB, ...)");
                    return false;
                }

                for (const std::string& option : Arguments(i)) {
                    const bool validName = !option.empty() && IsNameStart(option.front())
                        && std::ranges::all_of(option, IsNameChar) && !IsLuaKeyword(option);
                    if (!validName) {
                        context.Error(line, fmt::format("enum option '{}' is not a valid name", option));
                        return false;
                    }
                    if (std::ranges::any_of(declaration.enumOptions, [&](const ScriptEnumOption& o) { return o.name == option; })) {
                        context.Error(line, fmt::format("enum option '{}' is listed twice", option));
                        return false;
                    }
                    declaration.enumOptions.push_back({option, static_cast<int>(declaration.enumOptions.size())});
                }

                if (declaration.enumOptions.empty()) {
                    context.Error(line, "enum needs at least one option");
                    return false;
                }
                i = match[i] + 1;
            }

            if (IsSymbol(i, "[") && IsSymbol(i + 1, "]")) {
                context.Error(line, "list/array fields are not supported yet");
                return false;
            }
            return true;
        }

        // Parses the literal after `=` at `first` into the declaration's
        // default. Returns the literal's last token, or nullopt after an error.
        std::optional<std::size_t> ParseDefault(const std::size_t first, Declaration& declaration, CompileContext& context) {
            const int line = declaration.line;
            const ScriptValueType type = declaration.type->type;
            const std::string& typeName = declaration.typeName;

            const auto fail = [&](const std::string& expected) -> std::optional<std::size_t> {
                context.Error(line, fmt::format("the default value of '{}' must be {}", declaration.name, expected));
                return std::nullopt;
            };

            // Extent: [-] Number | String | Name{.Name}[(args)]
            std::size_t last = first;
            const bool negative = IsSymbol(first, "-");
            const std::size_t valueStart = negative ? first + 1 : first;
            if (valueStart >= tokens.size()) return fail("a value");

            if (tokens[valueStart].kind == TokenKind::Name) {
                last = valueStart;
                while (IsSymbol(last + 1, ".") && IsPlainName(last + 2)) last += 2;
                if (IsSymbol(last + 1, "(") && match[last + 1] != npos) last = match[last + 1];
            } else {
                last = valueStart;
            }

            // Only a plain value: `= 15 * 2` would run fine but store 15 as the default.
            if ((tokens.size() > last + 1 && tokens[last + 1].kind == TokenKind::Symbol && !IsSymbol(last + 1, ";") && !IsSymbol(last + 1, "::"))
                || IsWord(last + 1, "and") || IsWord(last + 1, "or"))
                return fail("a plain value, not an expression");

            const TokenKind kind = tokens[valueStart].kind;
            const std::string_view value = Text(valueStart);
            declaration.luaValue = std::string(Span(first, last));

            const auto numberAt = [&](const std::string& text, float& out) {
                char* end = nullptr;
                out = std::strtof(text.c_str(), &end);
                return !text.empty() && end == text.c_str() + text.size();
            };

            switch (type) {
                case ScriptValueType::Int: {
                    if (kind != TokenKind::Number || last != valueStart) return fail("a whole number");
                    const std::string text(value);
                    char* end = nullptr;
                    const bool hex = text.starts_with("0x") || text.starts_with("0X");
                    const long parsed = std::strtol(text.c_str(), &end, hex ? 16 : 10);
                    if (end != text.c_str() + text.size()) return fail("a whole number");
                    declaration.defaultValue = ScriptValue{static_cast<int>(negative ? -parsed : parsed)};
                    break;
                }

                case ScriptValueType::Float: {
                    float parsed = 0.0f;
                    if (kind != TokenKind::Number || last != valueStart || !numberAt(std::string(value), parsed)) return fail("a number");
                    declaration.defaultValue = ScriptValue{negative ? -parsed : parsed};
                    break;
                }

                case ScriptValueType::Bool:
                    if (negative || (value != "true" && value != "false") || last != valueStart) return fail("true or false");
                    declaration.defaultValue = ScriptValue{value == "true"};
                    break;

                case ScriptValueType::String:
                    if (negative || kind != TokenKind::String) return fail("a string in quotes");
                    declaration.defaultValue = ScriptValue{UnquoteString(value)};
                    break;

                case ScriptValueType::Vector2:
                case ScriptValueType::Vector3:
                case ScriptValueType::Vector4: {
                    const std::size_t size = type == ScriptValueType::Vector2 ? 2 : type == ScriptValueType::Vector3 ? 3 : 4;
                    const std::string expected = size == 2 ? "Vector2(x, y)" : size == 3 ? "Vector3(x, y, z)" : "Vector4(x, y, z, w)";
                    if (negative || value != typeName || !IsSymbol(valueStart + 1, "(")) return fail(expected);

                    const std::vector<std::string> arguments = Arguments(valueStart + 1);
                    if (!arguments.empty() && arguments.size() != size) return fail(expected);

                    std::array<float, 4> components{};
                    for (std::size_t c = 0; c < arguments.size(); ++c) {
                        std::string text = arguments[c];
                        text.erase(std::remove_if(text.begin(), text.end(), [](const unsigned char ch) { return std::isspace(ch); }), text.end());
                        if (!numberAt(text, components[c])) return fail(expected + " with plain numbers");
                    }

                    if (size == 2) declaration.defaultValue = ScriptValue{Vector2{components[0], components[1]}};
                    else if (size == 3) declaration.defaultValue = ScriptValue{Vector3{components[0], components[1], components[2]}};
                    else declaration.defaultValue = ScriptValue{Vector4{components[0], components[1], components[2], components[3]}};
                    break;
                }

                case ScriptValueType::Enum: {
                    if (typeName == "Key") {
                        // Stays `Key.E` in the Lua; the Key table holds the number.
                        const bool shape = !negative && value == "Key" && last == valueStart + 2 && IsSymbol(valueStart + 1, ".");
                        const auto option = shape
                            ? std::ranges::find(declaration.enumOptions, Text(last), &ScriptEnumOption::name)
                            : declaration.enumOptions.end();
                        if (option == declaration.enumOptions.end()) return fail("a key such as Key.E");
                        declaration.defaultValue = ScriptValue{option->value};
                        break;
                    }

                    std::vector<std::string> names;
                    for (const ScriptEnumOption& option : declaration.enumOptions) names.push_back(option.name);
                    const auto option = std::ranges::find(declaration.enumOptions, value, &ScriptEnumOption::name);
                    if (negative || last != valueStart || option == declaration.enumOptions.end())
                        return fail(fmt::format("one of its options ({})", fmt::join(names, ", ")));

                    // Scripts see enum fields as numbers: `mode = Patrol` compiles to `mode = 1`.
                    declaration.defaultValue = ScriptValue{option->value};
                    declaration.luaValue = std::to_string(option->value);
                    break;
                }

                case ScriptValueType::Entity:
                case ScriptValueType::Component:
                case ScriptValueType::Behaviour:
                case ScriptValueType::Asset:
                case ScriptValueType::Wall:
                case ScriptValueType::Sector:
                    if (negative || value != "nil" || last != valueStart)
                        return fail("nil (pick the " + typeName + " in the Inspector)");
                    declaration.defaultValue = ZeroValue(type, declaration.enumOptions);
                    break;
            }

            return last;
        }

        // -- vector write-through ----------------------------------------------

        // <prefix> . <key> . <x|y|z|w>  followed by `=` or `, <more targets> =`
        // becomes  __vref(<prefix>, "key").x
        void CompileVectorAssignments() {
            for (std::size_t dot = 1; dot + 3 < tokens.size(); ++dot) {
                if (!IsSymbol(dot, ".") || !IsPlainName(dot + 1) || !IsSymbol(dot + 2, ".")) continue;
                if (!IsComponentName(dot + 3)) continue;
                if (!IsAssignmentTarget(dot + 4)) continue;

                const std::optional<std::size_t> prefixStart = FindPrefixStart(dot - 1);
                if (!prefixStart) continue;

                const std::size_t at = tokens[*prefixStart].begin;
                edits.push_back({at, at, std::string(LuaScriptCompiler::kVectorRefFunction) + "("});
                // Replace `.` and the key separately so whatever sits between
                // them (whitespace, newlines, comments) survives.
                edits.push_back({tokens[dot].begin, tokens[dot].end, ","});
                edits.push_back({tokens[dot + 1].begin, tokens[dot + 1].end, "\"" + std::string(Text(dot + 1)) + "\")"});
            }
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
                if (IsPlainName(last)) {
                    if (last >= 2 && (IsSymbol(last - 1, ".") || IsSymbol(last - 1, ":"))) {
                        last -= 2;
                        continue;
                    }
                    return last;
                }

                std::size_t open = last;
                if (tokens[last].kind == TokenKind::String) {
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

namespace LuaScriptCompiler {
    Result Compile(const std::string_view source) {
        // luaL_loadfile skips a UTF-8 BOM and a leading `#` line, but scripts
        // are loaded from memory. The `#` line is emptied, not removed, so
        // line numbers stay put.
        std::string text(source);
        if (text.starts_with("\xEF\xBB\xBF")) text.erase(0, 3);
        if (text.starts_with('#')) text.erase(0, std::min(text.find('\n'), text.size()));

        return Compiler(text).Run();
    }

    std::vector<std::string> DeclarationKeywordNames() {
        std::vector<std::string> names;
        for (const DeclarationKeyword& keyword : DeclarationKeywords()) names.emplace_back(keyword.name);
        return names;
    }

    std::vector<std::string> FieldTypeNames() {
        std::vector<std::string> names;
        for (const FieldTypeDef& type : FieldTypes()) names.emplace_back(type.name);
        return names;
    }

    std::vector<std::string> FieldAttributeNames() {
        std::vector<std::string> names;
        for (const FieldAttribute& attribute : FieldAttributes()) names.emplace_back(attribute.name);
        return names;
    }

    std::string DisplayNameFromIdentifier(const std::string_view identifier) {
        const auto upper = [](const char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; };
        const auto lower = [](const char c) { return std::islower(static_cast<unsigned char>(c)) != 0; };
        const auto alpha = [](const char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };

        std::string name;
        for (std::size_t i = 0; i < identifier.size(); ++i) {
            const char c = identifier[i];
            if (c == '_') {
                if (!name.empty() && name.back() != ' ') name += ' ';
                continue;
            }

            if (i > 0 && !name.empty() && name.back() != ' ') {
                const char previous = identifier[i - 1];
                const bool nextIsLower = i + 1 < identifier.size() && lower(identifier[i + 1]);
                const bool boundary =
                    (upper(c) && (lower(previous) || IsDigit(previous)))   // moveSpeed, ammo2Max
                    || (upper(c) && upper(previous) && nextIsLower)        // UIScale -> UI Scale
                    || (IsDigit(c) && alpha(previous));                    // health2 -> Health 2
                if (boundary) name += ' ';
            }

            // Every word starts with a capital, including after `_`.
            const bool wordStart = name.empty() || name.back() == ' ';
            name += wordStart ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        }

        while (!name.empty() && name.back() == ' ') name.pop_back();
        return name;
    }
}
