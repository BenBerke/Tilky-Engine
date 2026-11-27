#include "Headers/Editor/ScriptIntellisense.hpp"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using namespace std::string_view_literals;

namespace ScriptIntellisense {
namespace {
    using LuaBindingMetadata::TypeDoc;
    using LuaBindingMetadata::MethodDoc;
    using LuaBindingMetadata::PropertyDoc;

    // ---- Small text helpers ------------------------------------------------

    bool IsNameStart(const char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    bool IsNameChar(const char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    std::string Lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool StartsWithNoCase(std::string_view candidate, std::string_view prefix) {
        if (candidate.size() < prefix.size()) return false;
        for (std::size_t i = 0; i < prefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(candidate[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
                return false;
        }
        return true;
    }

    bool EqualsNoCase(std::string_view a, std::string_view b) {
        return a.size() == b.size() && StartsWithNoCase(a, b);
    }

    // ---- Engine names ------------------------------------------------------

    const std::unordered_set<std::string>& KeywordSet() {
        static const std::unordered_set<std::string> keywords = [] {
            std::unordered_set<std::string> set(LuaBindingMetadata::LuaKeywordNames().begin(), LuaBindingMetadata::LuaKeywordNames().end());
            for (std::string& name : LuaScriptCompiler::DeclarationKeywordNames()) set.insert(std::move(name));
            return set;
        }();
        return keywords;
    }

    // Everything the editor colors as an engine name (the same set the old
    // regex colorizer's KnownIdentifier list held).
    const std::unordered_set<std::string>& EngineNameSet() {
        static const std::unordered_set<std::string> names = [] {
            std::unordered_set<std::string> set;
            for (std::string& type : LuaScriptCompiler::FieldTypeNames()) set.insert(std::move(type));
            for (const TypeDoc& type : LuaBindingMetadata::AllTypes()) set.insert(type.name);
            for (const PropertyDoc& global : LuaBindingMetadata::AllScriptGlobals()) set.insert(global.name);
            for (const MethodDoc& function : LuaBindingMetadata::AllScriptFunctions()) set.insert(function.name);
            for (const auto& callback : LuaBindingMetadata::AllCallbacks()) set.insert(callback.name);
            return set;
        }();
        return names;
    }

    const std::unordered_map<std::string, const TypeDoc*>& TypesByLowerName() {
        static const std::unordered_map<std::string, const TypeDoc*> byName = [] {
            std::unordered_map<std::string, const TypeDoc*> map;
            for (const TypeDoc& type : LuaBindingMetadata::AllTypes()) map.emplace(Lower(type.name), &type);
            return map;
        }();
        return byName;
    }

    const TypeDoc* FindType(std::string_view name) {
        const auto& types = TypesByLowerName();
        const auto it = types.find(Lower(name));
        return it == types.end() ? nullptr : it->second;
    }

    const TypeDoc* BehaviourType() { return FindType("Behaviour"); }

    const LuaBindingMetadata::CallbackDoc* FindCallback(std::string_view name) {
        for (const auto& callback : LuaBindingMetadata::AllCallbacks())
            if (callback.name == name) return &callback;
        return nullptr;
    }

    const MethodDoc* FindScriptFunction(std::string_view name) {
        for (const MethodDoc& function : LuaBindingMetadata::AllScriptFunctions())
            if (function.name == name) return &function;
        return nullptr;
    }

    const PropertyDoc* FindScriptGlobal(std::string_view name) {
        for (const PropertyDoc& global : LuaBindingMetadata::AllScriptGlobals())
            if (global.name == name) return &global;
        return nullptr;
    }

    template<typename T>
    const T* FindByName(const std::vector<T>& list, std::string_view name) {
        for (const T& item : list) if (item.name == name) return &item;
        for (const T& item : list) if (EqualsNoCase(item.name, name)) return &item;
        return nullptr;
    }

    // "Transform?" -> Transform, "Entity[]" -> array of Entity, "A|B" and
    // primitives -> nothing.
    TypeRef TypeFromString(std::string_view type) {
        if (!type.empty() && type.back() == '?') type.remove_suffix(1);

        TypeRef ref;
        if (type.ends_with("[]")) {
            type.remove_suffix(2);
            ref.array = true;
        }

        if (type.find('|') != std::string_view::npos) return {};

        ref.engine = FindType(type);
        return ref.engine != nullptr ? ref : TypeRef{};
    }

    std::string TypeName(const TypeRef& type) {
        std::string name = type.engine != nullptr ? type.engine->name : type.script;
        if (type.array) name += "[]";
        return name;
    }

    // ---- Lexer -------------------------------------------------------------

    // The level of a long bracket "[==[" starting at `at`, or -1.
    int LongBracketLevel(const std::string_view text, const std::size_t at) {
        if (at >= text.size() || text[at] != '[') return -1;
        std::size_t i = at + 1;
        int level = 0;
        while (i < text.size() && text[i] == '=') { ++level; ++i; }
        return i < text.size() && text[i] == '[' ? level : -1;
    }

    // Offset just past the "]==]" closing a long bracket, or npos.
    std::size_t FindLongClose(const std::string_view text, const std::size_t from, const int level) {
        std::string close = "]";
        close.append(static_cast<std::size_t>(level), '=');
        close += ']';
        const std::size_t at = text.find(close, from);
        return at == std::string_view::npos ? at : at + close.size();
    }

    constexpr std::array kMultiCharPunct = {"..."sv, ".."sv, "=="sv, "~="sv, "<="sv, ">="sv, "//"sv, "::"sv, "<<"sv, ">>"sv};

    // ---- Token access ------------------------------------------------------

    struct View {
        const Analysis& a;

        [[nodiscard]] int Count() const { return static_cast<int>(a.code.size()); }
        [[nodiscard]] const Token& Tok(const int i) const { return a.tokens[a.code[i]]; }
        [[nodiscard]] std::string_view Text(const int i) const {
            const Token& t = Tok(i);
            return std::string_view(a.text).substr(t.begin, t.end - t.begin);
        }
        [[nodiscard]] bool Valid(const int i) const { return i >= 0 && i < Count(); }
        [[nodiscard]] bool IsPunct(const int i, std::string_view p) const { return Valid(i) && Tok(i).kind == TokenKind::Punct && Text(i) == p; }
        [[nodiscard]] bool IsKeyword(const int i, std::string_view k) const { return Valid(i) && Tok(i).kind == TokenKind::Keyword && Text(i) == k; }
        [[nodiscard]] bool IsName(const int i) const { return Valid(i) && Tok(i).kind == TokenKind::Name; }

        // The last code token that begins before `offset`, or -1.
        [[nodiscard]] int LastBefore(const int offset) const {
            int lo = 0, hi = Count();
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                if (Tok(mid).begin < offset) lo = mid + 1;
                else hi = mid;
            }
            return lo - 1;
        }

        // A string token's text without its quotes.
        [[nodiscard]] std::string StringContent(const int i) const {
            const std::string_view text = Text(i);
            if (text.empty() || (text.front() != '"' && text.front() != '\'')) return {};
            std::string_view inner = text.substr(1);
            if (!Tok(i).unterminated && !inner.empty()) inner.remove_suffix(1);
            return std::string(inner);
        }

        [[nodiscard]] bool NewlineBetween(const int fromOffset, const int toOffset) const {
            if (toOffset <= fromOffset) return false;
            return std::string_view(a.text).substr(fromOffset, toOffset - fromOffset).find('\n') != std::string_view::npos;
        }

        // Matching ")" for the "(" at i (also skips []/{}), or -1.
        [[nodiscard]] int MatchForward(const int open) const {
            int depth = 0;
            for (int i = open; i < Count(); ++i) {
                if (Tok(i).kind != TokenKind::Punct) continue;
                const std::string_view p = Text(i);
                if (p == "(" || p == "[" || p == "{") ++depth;
                else if (p == ")" || p == "]" || p == "}") {
                    if (--depth == 0) return i;
                }
            }
            return -1;
        }

        [[nodiscard]] int MatchBackward(const int close) const {
            int depth = 0;
            for (int i = close; i >= 0; --i) {
                if (Tok(i).kind != TokenKind::Punct) continue;
                const std::string_view p = Text(i);
                if (p == ")" || p == "]" || p == "}") ++depth;
                else if (p == "(" || p == "[" || p == "{") {
                    if (--depth == 0) return i;
                }
            }
            return -1;
        }
    };

    // ---- Chains ("entity.transform", "target:GetScript("Door")") -----------

    struct Step {
        std::string name;
        char sep = 0; // '.' or ':' before this step, 0 for the first
        bool call = false;
        int firstArg = -1; // code index of the first token inside the call's (), or -1
    };

    // The chain that ends with the code token at `last`, or empty.
    std::vector<Step> ChainEndingAt(const View& v, int i) {
        std::vector<Step> steps;

        while (v.Valid(i)) {
            Step step;

            if (v.IsPunct(i, ")")) {
                const int open = v.MatchBackward(i);
                if (open < 0) return {};
                step.call = true;
                step.firstArg = open + 1 < i ? open + 1 : -1;
                i = open - 1;
            }

            if (!v.IsName(i)) return {};
            step.name = std::string(v.Text(i));
            --i;

            if (v.IsPunct(i, ".") || v.IsPunct(i, ":")) {
                step.sep = v.Text(i).front();
                steps.push_back(std::move(step));
                --i;
                continue;
            }

            steps.push_back(std::move(step));
            break;
        }

        std::ranges::reverse(steps);
        return steps;
    }

    // The chain starting at code token `i`, plus the index just past it.
    std::vector<Step> ChainStartingAt(const View& v, int i, int& outEnd) {
        std::vector<Step> steps;
        char sep = 0;

        while (v.IsName(i)) {
            Step step;
            step.name = std::string(v.Text(i));
            step.sep = sep;
            ++i;

            if (v.IsPunct(i, "(")) {
                const int close = v.MatchForward(i);
                if (close < 0) break;
                step.call = true;
                step.firstArg = i + 1 < close ? i + 1 : -1;
                i = close + 1;
            }

            steps.push_back(std::move(step));

            if ((v.IsPunct(i, ".") || v.IsPunct(i, ":")) && v.IsName(i + 1)) {
                sep = v.Text(i).front();
                ++i;
                continue;
            }
            break;
        }

        outEnd = i;
        return steps;
    }

    const Symbol* FindSymbol(const Analysis& a, std::string_view name, const int offset) {
        const Symbol* best = nullptr;
        for (const Symbol& symbol : a.symbols) {
            if (symbol.name != name || offset < symbol.scopeBegin || offset >= symbol.scopeEnd || symbol.declOffset > offset) continue;
            if (best == nullptr || symbol.declOffset >= best->declOffset) best = &symbol;
        }
        return best;
    }

    // Is this (part of) a script's file name?
    std::string ScriptNameFromArgument(const std::string& argument) {
        return std::filesystem::path(argument).filename().string();
    }

    TypeRef ResolveMember(const View& v, const TypeRef& owner, const Step& step, const Context& context) {
        if (owner.array) return {};

        // GetScript("Door") is Door.lua's Behaviour; AddComponent("Door") adds
        // it, AddComponent(Component.Sprite) adds a Sprite.
        if (step.call && v.Valid(step.firstArg) && (step.name == "GetScript" || step.name == "AddComponent")) {
            if (v.Tok(step.firstArg).kind == TokenKind::String) {
                TypeRef script;
                script.script = ScriptNameFromArgument(v.StringContent(step.firstArg));
                return script.script.empty() ? TypeRef{} : script;
            }
            if (step.name == "AddComponent" && v.Text(step.firstArg) == "Component" && v.IsPunct(step.firstArg + 1, ".") && v.IsName(step.firstArg + 2)) {
                TypeRef component;
                component.engine = FindType(v.Text(step.firstArg + 2));
                return component;
            }
        }

        if (!owner.script.empty()) {
            if (const Analysis* script = context.findScript ? context.findScript(owner.script) : nullptr) {
                for (const Symbol& symbol : script->symbols)
                    if (symbol.kind == SymbolKind::Field && symbol.name == step.name) return TypeFromString(symbol.luaType);
                for (const std::string& function : script->compiled.publicFunctions)
                    if (function == step.name) return {};
            }
            TypeRef behaviour;
            behaviour.engine = BehaviourType();
            return behaviour.engine != nullptr ? ResolveMember(v, behaviour, step, context) : TypeRef{};
        }

        if (owner.engine == nullptr) return {};

        if (const PropertyDoc* property = FindByName(owner.engine->properties, step.name)) return TypeFromString(property->luaType);
        if (const MethodDoc* method = FindByName(owner.engine->methods, step.name)) return TypeFromString(method->returnType);
        return {};
    }

    TypeRef ResolveBase(const Analysis& a, const Step& step, const int offset) {
        if (step.call) {
            if (const Symbol* symbol = FindSymbol(a, step.name, offset); symbol != nullptr) return {};
            if (const MethodDoc* function = FindScriptFunction(step.name)) return TypeFromString(function->returnType);
            return {};
        }

        if (const Symbol* symbol = FindSymbol(a, step.name, offset)) return symbol->type;
        if (const PropertyDoc* global = FindScriptGlobal(step.name)) return TypeFromString(global->luaType);

        TypeRef type;
        type.engine = FindType(step.name);
        return type;
    }

    TypeRef ResolveChain(const View& v, const std::vector<Step>& steps, const int offset, const Context& context) {
        if (steps.empty()) return {};

        TypeRef type = ResolveBase(v.a, steps.front(), offset);
        for (std::size_t i = 1; i < steps.size() && type.Valid(); ++i) type = ResolveMember(v, type, steps[i], context);
        return type;
    }

    // ---- Scope walk --------------------------------------------------------

    struct Frame {
        enum class Kind { Function, Block, If, Loop, Repeat } kind = Kind::Block;
        std::vector<int> symbols;
        bool awaitingDo = false;
        std::vector<Symbol> loopVars; // declared once `do` is reached
    };

    class ScopeWalker {
    public:
        ScopeWalker(Analysis& analysis, const Context& context) : a(analysis), v{analysis}, context(context) {}

        void Run() {
            const int textEnd = static_cast<int>(a.text.size());

            // Public fields and global functions are visible everywhere.
            for (const LuaScriptCompiler::DeclarationInfo& declaration : a.compiled.declarations) {
                Symbol field;
                field.name = declaration.name;
                field.kind = SymbolKind::Field;
                field.scopeEnd = textEnd;
                field.luaType = declaration.luaType.empty() ? declaration.typeName : declaration.luaType;
                field.type = TypeFromString(declaration.luaType);
                a.symbols.push_back(std::move(field));
            }

            frames.push_back({});

            for (int i = 0; i < v.Count();) i = Statement(i);

            while (!frames.empty()) Pop(textEnd);
        }

    private:
        Analysis& a;
        View v;
        const Context& context;
        std::vector<Frame> frames;

        int Declare(Symbol symbol) {
            a.symbols.push_back(std::move(symbol));
            const int index = static_cast<int>(a.symbols.size()) - 1;
            frames.back().symbols.push_back(index);
            return index;
        }

        void EndScopes(Frame& frame, const int offset) {
            for (const int index : frame.symbols) a.symbols[index].scopeEnd = offset;
            frame.symbols.clear();
        }

        void Pop(const int offset) {
            if (frames.empty()) return;
            EndScopes(frames.back(), offset);
            frames.pop_back();
            if (frames.empty() && offset < static_cast<int>(a.text.size())) frames.push_back({}); // a stray `end`
        }

        // Handles one token (or construct starting at it); returns the next index.
        int Statement(const int i) {
            const Token& token = v.Tok(i);
            if (token.kind != TokenKind::Keyword) return i + 1;

            const std::string_view word = v.Text(i);

            if (word == "local") return Local(i);
            if (word == "function") return Function(i, false, -1);
            if (word == "for") return For(i);

            if (word == "while") {
                frames.push_back({Frame::Kind::Loop, {}, true, {}});
                return i + 1;
            }

            if (word == "do") {
                if (frames.back().awaitingDo) {
                    Frame& loop = frames.back();
                    loop.awaitingDo = false;
                    std::vector<Symbol> vars = std::move(loop.loopVars);
                    for (Symbol& var : vars) Declare(std::move(var));
                } else {
                    frames.push_back({Frame::Kind::Block, {}, false, {}});
                }
                return i + 1;
            }

            if (word == "if") {
                frames.push_back({Frame::Kind::If, {}, false, {}});
                return i + 1;
            }

            if ((word == "elseif" || word == "else") && frames.back().kind == Frame::Kind::If) {
                EndScopes(frames.back(), token.begin);
                return i + 1;
            }

            if (word == "repeat") {
                frames.push_back({Frame::Kind::Repeat, {}, false, {}});
                return i + 1;
            }

            if (word == "until" && frames.back().kind == Frame::Kind::Repeat) {
                // `until`'s condition still sees the loop body's locals.
                std::size_t lineEnd = a.text.find('\n', token.end);
                if (lineEnd == std::string::npos) lineEnd = a.text.size();
                Pop(static_cast<int>(lineEnd));
                return i + 1;
            }

            if (word == "end") {
                Pop(token.end);
                return i + 1;
            }

            return i + 1;
        }

        // `local a, b = x, y` / `local function f()`.
        int Local(int i) {
            ++i;
            if (v.IsKeyword(i, "function")) {
                if (!v.IsName(i + 1)) return i + 1;
                Symbol function;
                function.name = std::string(v.Text(i + 1));
                function.kind = SymbolKind::Function;
                function.declOffset = v.Tok(i + 1).begin;
                function.scopeBegin = function.declOffset;
                const int index = Declare(std::move(function));
                return Function(i, true, index);
            }

            std::vector<Symbol> names;
            while (v.IsName(i)) {
                Symbol local;
                local.name = std::string(v.Text(i));
                local.kind = SymbolKind::Local;
                local.declOffset = v.Tok(i).begin;
                local.scopeBegin = local.declOffset;
                names.push_back(std::move(local));
                ++i;

                if (v.IsPunct(i, "<") && v.IsName(i + 1) && v.IsPunct(i + 2, ">")) i += 3; // <const>, <close>
                if (!v.IsPunct(i, ",")) break;
                ++i;
            }

            if (v.IsPunct(i, "=")) {
                int expression = i + 1;
                for (Symbol& name : names) {
                    if (!v.Valid(expression)) break;
                    name.type = ExpressionType(expression);
                    expression = NextExpression(expression);
                    if (expression < 0) break;
                }
            }

            for (Symbol& name : names) Declare(std::move(name));
            return i;
        }

        // The type of the expression starting at `i` if it is a plain chain.
        TypeRef ExpressionType(const int i) {
            int end = i;
            const std::vector<Step> steps = ChainStartingAt(v, i, end);
            if (steps.empty()) return {};

            if (v.Valid(end)) {
                const bool ends =
                    v.IsPunct(end, ",") || v.IsPunct(end, ";") || v.IsPunct(end, ")") ||
                    (v.Tok(end).kind == TokenKind::Keyword && v.Text(end) != "and" && v.Text(end) != "or") ||
                    v.NewlineBetween(v.Tok(end - 1).end, v.Tok(end).begin);
                if (!ends) return {};
            }

            return ResolveChain(v, steps, v.Tok(i).begin, context);
        }

        // The start of the next comma-separated expression after `i`, or -1.
        int NextExpression(int i) {
            int depth = 0;
            for (; v.Valid(i); ++i) {
                const Token& token = v.Tok(i);
                if (token.kind == TokenKind::Keyword && depth == 0) {
                    const std::string_view word = v.Text(i);
                    if (word != "and" && word != "or" && word != "not" && word != "nil" && word != "true" && word != "false" && word != "function")
                        return -1;
                }
                if (token.kind != TokenKind::Punct) continue;

                const std::string_view p = v.Text(i);
                if (p == "(" || p == "[" || p == "{") ++depth;
                else if (p == ")" || p == "]" || p == "}") { if (--depth < 0) return -1; }
                else if (p == "," && depth == 0) return i + 1;
                else if (p == ";" && depth == 0) return -1;
            }
            return -1;
        }

        // `function name.path:method(params)` or `function(params)`. `i` is the
        // `function` keyword. `localIndex` is the symbol of a `local function`.
        int Function(int i, const bool isLocal, const int localIndex) {
            ++i;

            std::string globalName;
            bool method = false;

            if (isLocal) {
                ++i; // the name, already declared
            } else if (v.IsName(i)) {
                globalName = std::string(v.Text(i));
                ++i;
                bool path = false;
                while ((v.IsPunct(i, ".") || v.IsPunct(i, ":")) && v.IsName(i + 1)) {
                    method = v.IsPunct(i, ":");
                    path = true;
                    i += 2;
                }
                if (path) globalName.clear();
            }

            const LuaBindingMetadata::CallbackDoc* callback = globalName.empty() ? nullptr : FindCallback(globalName);

            frames.push_back({Frame::Kind::Function, {}, false, {}});

            std::vector<std::string> paramNames;

            if (v.IsPunct(i, "(")) {
                const int open = i;
                ++i;

                if (method) {
                    Symbol self;
                    self.name = "self";
                    self.kind = SymbolKind::Parameter;
                    self.declOffset = v.Tok(open).begin;
                    self.scopeBegin = self.declOffset;
                    Declare(std::move(self));
                }

                while (v.Valid(i) && !v.IsPunct(i, ")")) {
                    if (v.IsName(i)) {
                        Symbol param;
                        param.name = std::string(v.Text(i));
                        param.kind = SymbolKind::Parameter;
                        param.declOffset = v.Tok(i).begin;
                        param.scopeBegin = v.Tok(open).begin;
                        if (callback != nullptr && paramNames.size() < callback->params.size())
                            param.type = TypeFromString(callback->params[paramNames.size()].luaType);
                        paramNames.push_back(param.name);
                        Declare(std::move(param));
                    } else if (v.IsPunct(i, "...")) {
                        paramNames.emplace_back("...");
                    } else if (!v.IsPunct(i, ",")) {
                        break; // not a parameter list after all
                    }
                    ++i;
                }
                if (v.IsPunct(i, ")")) ++i;
            }

            if (isLocal && localIndex >= 0) a.symbols[localIndex].params = paramNames;

            if (!globalName.empty()) {
                Symbol function;
                function.name = globalName;
                function.kind = SymbolKind::Function;
                function.scopeEnd = static_cast<int>(a.text.size());
                function.params = std::move(paramNames);
                a.symbols.push_back(std::move(function)); // not in any frame: never ends
            }

            return i;
        }

        int For(int i) {
            ++i;
            std::vector<Symbol> vars;
            while (v.IsName(i)) {
                Symbol var;
                var.name = std::string(v.Text(i));
                var.kind = SymbolKind::Local;
                var.declOffset = v.Tok(i).begin;
                var.scopeBegin = var.declOffset;
                vars.push_back(std::move(var));
                ++i;
                if (!v.IsPunct(i, ",")) break;
                ++i;
            }

            // for _, e in ipairs(list) / pairs(list): e has list's element type.
            if (v.IsKeyword(i, "in") && vars.size() >= 2 && v.IsName(i + 1) &&
                (v.Text(i + 1) == "ipairs" || v.Text(i + 1) == "pairs") && v.IsPunct(i + 2, "(")) {
                int end = i + 3;
                const std::vector<Step> steps = ChainStartingAt(v, i + 3, end);
                if (!steps.empty() && v.IsPunct(end, ")")) {
                    TypeRef list = ResolveChain(v, steps, v.Tok(i + 3).begin, context);
                    if (list.array) {
                        list.array = false;
                        vars[1].type = list;
                    }
                }
            }

            frames.push_back({Frame::Kind::Loop, {}, true, std::move(vars)});
            return i;
        }
    };

    // ---- Coloring ----------------------------------------------------------

    void Colorize(Analysis& a) {
        a.colors.assign(a.text.size(), ColorRole::Default);
        const View v{a};
        const auto& keywords = KeywordSet();
        const auto& engineNames = EngineNameSet();

        // FindSymbol, indexed by name: a long script has thousands of names.
        std::unordered_map<std::string_view, std::vector<const Symbol*>> byName;
        for (const Symbol& symbol : a.symbols) byName[symbol.name].push_back(&symbol);
        const auto findSymbol = [&](const std::string& name, const int offset) -> const Symbol* {
            const auto it = byName.find(name);
            if (it == byName.end()) return nullptr;
            const Symbol* best = nullptr;
            for (const Symbol* symbol : it->second) {
                if (offset < symbol->scopeBegin || offset >= symbol->scopeEnd || symbol->declOffset > offset) continue;
                if (best == nullptr || symbol->declOffset >= best->declOffset) best = symbol;
            }
            return best;
        };

        std::vector<char> brackets;

        for (int ti = 0; ti < static_cast<int>(a.tokens.size()); ++ti) {
            const Token& token = a.tokens[ti];
            ColorRole role = ColorRole::Default;

            switch (token.kind) {
                case TokenKind::Comment: role = ColorRole::Comment; break;
                case TokenKind::LongComment: role = ColorRole::MultiLineComment; break;
                case TokenKind::String: role = ColorRole::String; break;
                case TokenKind::Number: role = ColorRole::Number; break;
                case TokenKind::Keyword: role = ColorRole::Keyword; break;
                case TokenKind::Unknown: role = ColorRole::Default; break;
                case TokenKind::Punct: {
                    role = ColorRole::Punctuation;
                    const char c = a.text[token.begin];
                    if (token.end - token.begin == 1) {
                        if (c == '(' || c == '[' || c == '{') brackets.push_back(c);
                        else if ((c == ')' || c == ']' || c == '}') && !brackets.empty()) brackets.pop_back();
                    }
                    break;
                }
                case TokenKind::Name: {
                    const std::string name = a.text.substr(token.begin, token.end - token.begin);
                    role = ColorRole::Identifier;

                    // Find this token among the code tokens to look at its neighbours.
                    const int ci = v.LastBefore(token.begin + 1);
                    const bool member = v.IsPunct(ci - 1, ".") || v.IsPunct(ci - 1, ":");
                    const bool tableKey = !brackets.empty() && brackets.back() == '{' &&
                                          (v.IsPunct(ci - 1, "{") || v.IsPunct(ci - 1, ",") || v.IsPunct(ci - 1, ";")) &&
                                          v.IsPunct(ci + 1, "=");
                    if (member || tableKey) break;

                    const Symbol* symbol = findSymbol(name, token.begin);
                    const bool scoped = symbol != nullptr && symbol->kind != SymbolKind::Field &&
                                        !(symbol->kind == SymbolKind::Function && symbol->scopeBegin == 0 && symbol->declOffset == 0);

                    if (scoped) {
                        role = symbol->kind == SymbolKind::Parameter ? ColorRole::Parameter
                             : symbol->kind == SymbolKind::Function ? ColorRole::Function
                             : ColorRole::Local;
                    } else if (engineNames.contains(name) || keywords.contains(name)) {
                        role = ColorRole::KnownIdentifier;
                    } else if (symbol != nullptr) {
                        role = symbol->kind == SymbolKind::Field ? ColorRole::Field : ColorRole::Function;
                    }
                    break;
                }
            }

            std::fill(a.colors.begin() + token.begin, a.colors.begin() + token.end, role);
        }
    }

    // ---- Completion data ---------------------------------------------------

    std::string MethodSignature(const std::string& owner, const char sep, const MethodDoc& method) {
        std::string text = owner.empty() ? method.name : owner + sep + method.name;
        text += '(';
        for (std::size_t i = 0; i < method.params.size(); ++i) {
            if (i > 0) text += ", ";
            text += method.params[i].name + ": " + method.params[i].luaType;
        }
        text += ')';
        if (!method.returnType.empty()) text += " -> " + method.returnType;
        return text;
    }

    // Every engine name the flat (no `.`/`:`) list offers, with its docs.
    const std::vector<CompletionItem>& EngineItems() {
        static const std::vector<CompletionItem> items = [] {
            std::vector<CompletionItem> list;
            std::unordered_set<std::string> seen;
            const auto add = [&](const std::string& text, const ItemKind kind, std::string detail, std::string doc) {
                if (seen.insert(text).second) list.push_back({text, kind, std::move(detail), std::move(doc)});
            };

            for (const std::string& keyword : LuaBindingMetadata::LuaKeywordNames()) add(keyword, ItemKind::Keyword, "Lua keyword", {});
            for (const std::string& keyword : LuaScriptCompiler::DeclarationKeywordNames()) add(keyword, ItemKind::Keyword, "Script declaration keyword", {});

            for (const auto& callback : LuaBindingMetadata::AllCallbacks())
                add(callback.name, ItemKind::Callback, LuaBindingMetadata::CallbackSignature(callback),
                    LuaBindingMetadata::CallbackOwnerNote(callback.owner) + callback.doc);
            for (const PropertyDoc& global : LuaBindingMetadata::AllScriptGlobals())
                add(global.name, ItemKind::Global, global.name + ": " + global.luaType, global.doc);
            for (const MethodDoc& function : LuaBindingMetadata::AllScriptFunctions())
                add(function.name, ItemKind::EngineFunction, LuaBindingMetadata::FunctionSignature(function), function.doc);

            for (const std::string& type : LuaScriptCompiler::FieldTypeNames()) add(type, ItemKind::Type, "Field type", {});
            for (const std::string& attribute : LuaScriptCompiler::FieldAttributeNames()) add(attribute, ItemKind::Other, "Field attribute", {});

            for (const TypeDoc& type : LuaBindingMetadata::AllTypes()) add(type.name, ItemKind::Type, "type " + type.name, type.doc);

            for (const TypeDoc& type : LuaBindingMetadata::AllTypes()) {
                const char sep = type.isGlobalTable ? '.' : ':';
                for (const PropertyDoc& property : type.properties)
                    add(property.name, ItemKind::Property, type.name + "." + property.name + ": " + property.luaType, property.doc);
                for (const MethodDoc& method : type.methods)
                    add(method.name, ItemKind::Method, MethodSignature(type.name, sep, method), method.doc);
                for (const auto& value : type.enumValues)
                    add(value.name, ItemKind::EnumValue, type.name + "." + value.name + " = " + std::to_string(value.value), value.doc);
            }

            return list;
        }();
        return items;
    }

    CompletionItem SymbolItem(const Symbol& symbol) {
        CompletionItem item;
        item.text = symbol.name;

        switch (symbol.kind) {
            case SymbolKind::Local:
                item.kind = ItemKind::Local;
                item.detail = "local " + symbol.name;
                break;
            case SymbolKind::Parameter:
                item.kind = ItemKind::Parameter;
                item.detail = "parameter " + symbol.name;
                break;
            case SymbolKind::Field:
                item.kind = ItemKind::Field;
                item.detail = "public " + symbol.luaType + " " + symbol.name;
                break;
            case SymbolKind::Function: {
                item.kind = ItemKind::Function;
                item.detail = "function " + symbol.name + "(";
                for (std::size_t i = 0; i < symbol.params.size(); ++i) item.detail += (i > 0 ? ", " : "") + symbol.params[i];
                item.detail += ")";
                break;
            }
        }

        if (symbol.kind != SymbolKind::Field && symbol.kind != SymbolKind::Function && symbol.type.Valid())
            item.detail += ": " + TypeName(symbol.type);
        return item;
    }

    void AddMembers(const TypeRef& type, const char sep, const Context& context, std::vector<CompletionItem>& out) {
        if (type.array) return;

        if (!type.script.empty()) {
            if (const Analysis* script = context.findScript ? context.findScript(type.script) : nullptr) {
                if (sep == '.') {
                    for (const Symbol& symbol : script->symbols)
                        if (symbol.kind == SymbolKind::Field) out.push_back(SymbolItem(symbol));
                } else {
                    for (const std::string& name : script->compiled.publicFunctions) {
                        CompletionItem item;
                        item.text = name;
                        item.kind = ItemKind::Method;
                        item.detail = type.script + ":" + name + "()";
                        for (const Symbol& symbol : script->symbols) {
                            if (symbol.kind != SymbolKind::Function || symbol.name != name) continue;
                            item.detail = type.script + ":" + name + "(";
                            for (std::size_t i = 0; i < symbol.params.size(); ++i) item.detail += (i > 0 ? ", " : "") + symbol.params[i];
                            item.detail += ")";
                            break;
                        }
                        out.push_back(std::move(item));
                    }
                }
            }

            TypeRef behaviour;
            behaviour.engine = BehaviourType();
            if (behaviour.engine != nullptr) AddMembers(behaviour, sep, context, out);
            return;
        }

        const TypeDoc* doc = type.engine;
        if (doc == nullptr) return;

        const bool properties = sep == '.';
        const bool methods = sep == ':' || doc->isGlobalTable;

        if (properties) {
            for (const PropertyDoc& property : doc->properties)
                out.push_back({property.name, ItemKind::Property, doc->name + "." + property.name + ": " + property.luaType, property.doc});
            for (const auto& value : doc->enumValues)
                out.push_back({value.name, ItemKind::EnumValue, doc->name + "." + value.name + " = " + std::to_string(value.value), value.doc});
        }
        if (methods) {
            for (const MethodDoc& method : doc->methods)
                out.push_back({method.name, ItemKind::Method, MethodSignature(doc->name, sep, method), method.doc});
        }
    }

    ItemKind ItemKindOf(const StringKind kind) {
        switch (kind) {
            case StringKind::Tag: return ItemKind::Tag;
            case StringKind::Script: return ItemKind::Script;
            case StringKind::Level: return ItemKind::Level;
            case StringKind::Sound: return ItemKind::Sound;
            default: return ItemKind::Other;
        }
    }

    const char* StringKindLabel(const StringKind kind) {
        switch (kind) {
            case StringKind::Tag: return "Tag";
            case StringKind::Script: return "Script";
            case StringKind::Level: return "Level";
            case StringKind::Sound: return "Sound";
            default: return "";
        }
    }

    // A call around the code token at `inside`: where its "(" is, which
    // argument `inside` is in, and the chain naming the function.
    struct CallSite {
        int open = -1;
        int argument = 0;
        std::vector<Step> callee;
    };

    // Walks back from code token `from` (inclusive) to the "(" of the call it
    // is an argument of. Stops at the start of a statement.
    std::optional<CallSite> EnclosingCall(const View& v, int i) {
        CallSite site;
        int depth = 0;

        for (; i >= 0; --i) {
            const Token& token = v.Tok(i);
            const std::string_view text = v.Text(i);

            if (token.kind == TokenKind::Keyword && depth == 0) {
                if (text == "end") {
                    // Skip a whole `function ... end` (or other block) argument.
                    int blocks = 0;
                    for (; i >= 0; --i) {
                        if (v.Tok(i).kind != TokenKind::Keyword) continue;
                        const std::string_view word = v.Text(i);
                        if (word == "end") ++blocks;
                        else if (word == "function" || word == "do" || word == "if") {
                            if (--blocks == 0) break;
                        }
                    }
                    continue;
                }
                if (text == "function" || text == "local" || text == "then" || text == "do" || text == "else" ||
                    text == "elseif" || text == "return" || text == "repeat" || text == "until" || text == "for" ||
                    text == "while" || text == "if")
                    return std::nullopt;
                continue;
            }

            if (token.kind != TokenKind::Punct) continue;

            if (text == ")" || text == "]" || text == "}") ++depth;
            else if (text == "[" || text == "{") {
                if (depth == 0) site.argument = 0; // the commas so far were inside this table
                else --depth;
            } else if (text == "(") {
                if (depth > 0) { --depth; continue; }
                site.open = i;
                site.callee = ChainEndingAt(v, i - 1);
                if (site.callee.empty() || v.IsKeyword(i - 1, "function") ||
                    (v.IsName(i - 1) && v.IsKeyword(i - 2, "function")))
                    return std::nullopt;
                return site;
            } else if (text == "," && depth == 0) {
                ++site.argument;
            } else if (text == "=" && depth == 0) {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    struct CalleeInfo {
        std::string name;
        std::string ownerName;
        char sep = 0;
        const MethodDoc* method = nullptr;     // an engine method or script function
        std::vector<std::string> scriptParams; // a function a script defines
        bool found = false;
    };

    CalleeInfo ResolveCallee(const View& v, const std::vector<Step>& callee, const int offset, const Context& context) {
        CalleeInfo info;
        if (callee.empty()) return info;
        info.name = callee.back().name;

        if (callee.size() == 1) {
            if (const Symbol* symbol = FindSymbol(v.a, info.name, offset); symbol != nullptr && symbol->kind == SymbolKind::Function) {
                info.scriptParams = symbol->params;
                info.found = true;
                return info;
            }
            if (const MethodDoc* function = FindScriptFunction(info.name)) {
                info.method = function;
                info.found = true;
            }
            return info;
        }

        const std::vector<Step> ownerChain(callee.begin(), callee.end() - 1);
        const TypeRef owner = ResolveChain(v, ownerChain, offset, context);
        info.sep = callee.back().sep;

        if (!owner.script.empty()) {
            info.ownerName = owner.script;
            if (const Analysis* script = context.findScript ? context.findScript(owner.script) : nullptr) {
                for (const Symbol& symbol : script->symbols) {
                    if (symbol.kind == SymbolKind::Function && symbol.name == info.name) {
                        info.scriptParams = symbol.params;
                        info.found = true;
                        return info;
                    }
                }
            }
            if (const TypeDoc* behaviour = BehaviourType()) {
                if (const MethodDoc* method = FindByName(behaviour->methods, info.name)) {
                    info.method = method;
                    info.found = true;
                }
            }
            return info;
        }

        if (owner.engine != nullptr && !owner.array) {
            info.ownerName = owner.engine->name;
            if (const MethodDoc* method = FindByName(owner.engine->methods, info.name)) {
                info.method = method;
                info.found = true;
            }
        }
        return info;
    }

    // The parameter name at `argument` of the called function; when the
    // receiver's type is unknown, of every engine method with that name, if
    // they agree.
    std::string ParamNameAt(const View& v, const CallSite& site, const int offset, const Context& context) {
        const CalleeInfo info = ResolveCallee(v, site.callee, offset, context);
        if (info.method != nullptr)
            return site.argument < static_cast<int>(info.method->params.size()) ? info.method->params[site.argument].name : std::string{};
        if (info.found) return {};

        std::string agreed;
        for (const TypeDoc& type : LuaBindingMetadata::AllTypes()) {
            for (const MethodDoc& method : type.methods) {
                if (method.name != info.name || site.argument >= static_cast<int>(method.params.size())) continue;
                const std::string& name = method.params[site.argument].name;
                if (agreed.empty()) agreed = name;
                else if (agreed != name) return {};
            }
        }
        return agreed;
    }

    Completion StringCompletion(const View& v, const int tokenIndex, const int cursor, const Context& context) {
        Completion completion;
        const Token& token = v.Tok(tokenIndex);
        if (v.a.text[token.begin] != '"' && v.a.text[token.begin] != '\'') return completion;

        if (!(v.IsPunct(tokenIndex - 1, "(") || v.IsPunct(tokenIndex - 1, ","))) return completion;

        const std::optional<CallSite> site = EnclosingCall(v, tokenIndex - 1);
        if (!site) return completion;

        const std::string param = ParamNameAt(v, *site, token.begin, context);
        const StringKind kind = StringKindOfParam(site->callee.back().name, param);
        if (kind == StringKind::None || !context.projectNames) return completion;

        const std::string typed = v.a.text.substr(token.begin + 1, cursor - token.begin - 1);
        completion.replaceBegin = token.begin + 1;
        completion.scoped = true;

        std::vector<std::string> names = context.projectNames(kind);
        std::ranges::sort(names);
        names.erase(std::ranges::unique(names).begin(), names.end());

        for (const std::string& name : names) {
            if (name == typed || !StartsWithNoCase(name, typed)) continue;
            completion.items.push_back({name, ItemKindOf(kind), StringKindLabel(kind), {}});
        }
        return completion;
    }

    // Is code token `i` inside the parameter list of `function name(...)`
    // or `function(...)` (where a new name is being typed)?
    bool InFunctionParameterList(const View& v, int i) {
        while (v.IsName(i) || v.IsPunct(i, ",") || v.IsPunct(i, "...")) --i;
        if (!v.IsPunct(i, "(")) return false;
        --i;

        // function a.b:c(
        while (v.IsName(i)) {
            if (v.IsPunct(i - 1, ".") || v.IsPunct(i - 1, ":")) i -= 2;
            else { --i; break; }
        }
        return v.IsKeyword(i, "function");
    }

    std::string ParamLabel(const std::string& name, const std::string& luaType) {
        return luaType.empty() ? name : name + ": " + luaType;
    }
}

// ============================================================================

std::vector<Token> Tokenize(const std::string_view text) {
    std::vector<Token> tokens;
    const std::size_t n = text.size();
    const auto& keywords = KeywordSet();
    std::size_t i = 0;

    const auto push = [&](const TokenKind kind, const std::size_t begin, const std::size_t end, const bool unterminated = false) {
        tokens.push_back({kind, static_cast<int>(begin), static_cast<int>(end), unterminated});
    };

    while (i < n) {
        const char c = text[i];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
            continue;
        }

        if (c == '-' && i + 1 < n && text[i + 1] == '-') {
            const int level = LongBracketLevel(text, i + 2);
            if (level >= 0) {
                const std::size_t close = FindLongClose(text, i + 2 + static_cast<std::size_t>(level) + 2, level);
                push(TokenKind::LongComment, i, close == std::string_view::npos ? n : close, close == std::string_view::npos);
                i = close == std::string_view::npos ? n : close;
            } else {
                std::size_t end = text.find('\n', i);
                if (end == std::string_view::npos) end = n;
                push(TokenKind::Comment, i, end);
                i = end;
            }
            continue;
        }

        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            bool closed = false;
            while (j < n) {
                if (text[j] == '\\') { j += 2; continue; }
                if (text[j] == c) { ++j; closed = true; break; }
                if (text[j] == '\n') break;
                ++j;
            }
            j = std::min(j, n);
            push(TokenKind::String, i, j, !closed);
            i = j;
            continue;
        }

        if (c == '[') {
            const int level = LongBracketLevel(text, i);
            if (level >= 0) {
                const std::size_t close = FindLongClose(text, i + static_cast<std::size_t>(level) + 2, level);
                push(TokenKind::String, i, close == std::string_view::npos ? n : close, close == std::string_view::npos);
                i = close == std::string_view::npos ? n : close;
                continue;
            }
        }

        if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
            (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)) {
            const bool hex = c == '0' && i + 1 < n && (text[i + 1] == 'x' || text[i + 1] == 'X');
            std::size_t j = hex ? i + 2 : i;
            while (j < n) {
                const char d = text[j];
                const bool exponent = hex ? (d == 'p' || d == 'P') : (d == 'e' || d == 'E');
                if (exponent) {
                    ++j;
                    if (j < n && (text[j] == '+' || text[j] == '-')) ++j;
                    continue;
                }
                if (std::isalnum(static_cast<unsigned char>(d)) != 0 || d == '.' || d == '_') { ++j; continue; }
                break;
            }
            push(TokenKind::Number, i, j);
            i = j;
            continue;
        }

        if (IsNameStart(c)) {
            std::size_t j = i + 1;
            while (j < n && IsNameChar(text[j])) ++j;
            const std::string word(text.substr(i, j - i));
            push(keywords.contains(word) ? TokenKind::Keyword : TokenKind::Name, i, j);
            i = j;
            continue;
        }

        if (static_cast<unsigned char>(c) >= 0x80) {
            std::size_t j = i + 1;
            while (j < n && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80) ++j;
            push(TokenKind::Unknown, i, j);
            i = j;
            continue;
        }

        std::size_t length = 1;
        for (const std::string_view punct : kMultiCharPunct) {
            if (text.substr(i, punct.size()) == punct) {
                length = punct.size();
                break;
            }
        }
        push(TokenKind::Punct, i, i + length);
        i += length;
    }

    return tokens;
}

Analysis Analyze(std::string text, const Context& context) {
    Analysis analysis;
    analysis.text = std::move(text);
    analysis.tokens = Tokenize(analysis.text);

    for (int i = 0; i < static_cast<int>(analysis.tokens.size()); ++i) {
        const TokenKind kind = analysis.tokens[i].kind;
        if (kind != TokenKind::Comment && kind != TokenKind::LongComment) analysis.code.push_back(i);
    }

    analysis.compiled = LuaScriptCompiler::Compile(analysis.text);

    ScopeWalker(analysis, context).Run();
    Colorize(analysis);
    return analysis;
}

Completion Complete(const Analysis& a, const int cursor, const bool manual, const Context& context) {
    Completion none;
    none.replaceBegin = cursor;

    // Inside a comment: nothing.
    for (const Token& token : a.tokens) {
        if (token.begin >= cursor) break;
        if (token.kind == TokenKind::Comment && cursor <= token.end) return none;
        if (token.kind == TokenKind::LongComment && (cursor < token.end || token.unterminated)) return none;
    }

    const View v{a};
    const int last = v.LastBefore(cursor);

    std::string word;
    int wordBegin = cursor;
    int before = last; // the code token before the word

    if (v.Valid(last)) {
        const Token& token = v.Tok(last);
        const bool touching = cursor <= token.end;

        if (token.kind == TokenKind::String && (cursor < token.end || token.unterminated)) return StringCompletion(v, last, cursor, context);
        if (touching && token.kind == TokenKind::Number) return none;
        if (touching && (token.kind == TokenKind::Name || token.kind == TokenKind::Keyword)) {
            word = a.text.substr(token.begin, cursor - token.begin);
            wordBegin = token.begin;
            before = last - 1;
        }
    }

    Completion completion;
    completion.replaceBegin = wordBegin;

    // Member access.
    if (v.IsPunct(before, ".") || v.IsPunct(before, ":")) {
        const char sep = v.Text(before).front();
        const std::vector<Step> chain = ChainEndingAt(v, before - 1);
        if (chain.empty()) return none;

        const TypeRef type = ResolveChain(v, chain, v.Tok(before).begin, context);
        if (!type.Valid()) return none;

        std::vector<CompletionItem> members;
        AddMembers(type, sep, context, members);
        std::ranges::stable_sort(members, {}, [](const CompletionItem& item) { return Lower(item.text); });

        std::unordered_set<std::string> seen;
        for (CompletionItem& item : members) {
            if (!StartsWithNoCase(item.text, word) || !seen.insert(item.text).second) continue;
            completion.items.push_back(std::move(item));
        }
        completion.scoped = true;
        return completion;
    }

    // Naming something new: a local, or a function's parameters.
    if (v.IsKeyword(before, "local") || InFunctionParameterList(v, before)) return none;

    if (!manual && word.size() < 2) return none;

    std::unordered_set<std::string> seen;

    // The script's own names first.
    std::vector<CompletionItem> own;
    for (const Symbol& symbol : a.symbols) {
        if (cursor < symbol.scopeBegin || cursor > symbol.scopeEnd || symbol.declOffset > cursor) continue;
        if (symbol.declOffset == wordBegin) continue; // the word being typed itself
        if (symbol.name == word || !StartsWithNoCase(symbol.name, word)) continue;
        if (!seen.insert(symbol.name).second) continue;
        own.push_back(SymbolItem(symbol));
    }
    std::ranges::stable_sort(own, {}, [](const CompletionItem& item) { return Lower(item.text); });

    std::vector<CompletionItem> engine;
    for (const CompletionItem& item : EngineItems()) {
        if (item.text.size() <= word.size() || !StartsWithNoCase(item.text, word) || seen.contains(item.text)) continue;
        engine.push_back(item);
    }
    std::ranges::stable_sort(engine, {}, [](const CompletionItem& item) { return Lower(item.text); });

    completion.items = std::move(own);
    completion.items.insert(completion.items.end(), std::make_move_iterator(engine.begin()), std::make_move_iterator(engine.end()));
    return completion;
}

std::optional<SignatureHelp> GetSignatureHelp(const Analysis& a, const int cursor, const Context& context) {
    // Not inside a comment (inside a string argument is fine).
    for (const Token& token : a.tokens) {
        if (token.begin >= cursor) break;
        if (token.kind == TokenKind::Comment && cursor <= token.end) return std::nullopt;
        if (token.kind == TokenKind::LongComment && (cursor < token.end || token.unterminated)) return std::nullopt;
    }

    const View v{a};
    const std::optional<CallSite> site = EnclosingCall(v, v.LastBefore(cursor));
    if (!site) return std::nullopt;

    const CalleeInfo info = ResolveCallee(v, site->callee, v.Tok(site->open).begin, context);
    if (!info.found) return std::nullopt;

    SignatureHelp help;
    help.activeParam = site->argument;

    help.label = info.ownerName.empty() ? info.name : info.ownerName + std::string(1, info.sep == 0 ? '.' : info.sep) + info.name;
    help.label += '(';

    const auto addParam = [&](const std::string& text) {
        if (!help.paramRanges.empty()) help.label += ", ";
        const int begin = static_cast<int>(help.label.size());
        help.label += text;
        help.paramRanges.emplace_back(begin, static_cast<int>(help.label.size()));
    };

    if (info.method != nullptr) {
        for (const auto& param : info.method->params) addParam(ParamLabel(param.name, param.luaType));
        help.label += ')';
        if (!info.method->returnType.empty()) help.label += " -> " + info.method->returnType;
        help.doc = info.method->doc;
    } else {
        for (const std::string& param : info.scriptParams) addParam(param);
        help.label += ')';
    }

    // A trailing "..." parameter takes every extra argument.
    if (!help.paramRanges.empty() && help.activeParam >= static_cast<int>(help.paramRanges.size())) {
        const auto& [begin, end] = help.paramRanges.back();
        if (help.label.compare(begin, 3, "...") == 0) help.activeParam = static_cast<int>(help.paramRanges.size()) - 1;
    }

    return help;
}

std::vector<SyntaxError> CheckSyntax(const Analysis& analysis) {
    std::vector<SyntaxError> errors;

    if (!analysis.compiled.Succeeded()) {
        for (const LuaScriptCompiler::Diagnostic& diagnostic : analysis.compiled.errors)
            errors.push_back({std::max(1, diagnostic.line), diagnostic.message});
        return errors;
    }

    static lua_State* state = luaL_newstate(); // parse-only: never runs anything
    if (state == nullptr) return errors;

    const std::string& source = analysis.compiled.luaSource;
    if (luaL_loadbufferx(state, source.data(), source.size(), "=script", "t") == LUA_OK) {
        lua_pop(state, 1);
        return errors;
    }

    std::string message = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "syntax error";
    lua_pop(state, 1);

    // "script:12: 'end' expected near <eof>"
    int line = 1;
    if (message.starts_with("script:")) {
        std::size_t i = 7;
        int parsed = 0;
        while (i < message.size() && std::isdigit(static_cast<unsigned char>(message[i])) != 0) parsed = parsed * 10 + (message[i++] - '0');
        if (parsed > 0) line = parsed;
        if (i < message.size() && message[i] == ':') ++i;
        while (i < message.size() && message[i] == ' ') ++i;
        message.erase(0, i);
    }

    errors.push_back({line, std::move(message)});
    return errors;
}

StringKind StringKindOfParam(const std::string_view method, const std::string_view param) {
    // Add a row to hand another string parameter a list of project names.
    struct Rule {
        std::string_view method; // empty: any function
        std::string_view param;
        bool suffix;             // `param` only has to end the name ("noStainTag")
        StringKind kind;
    };
    static constexpr Rule kRules[] = {
        {"GetScript", "name", false, StringKind::Script},
        {"HasScriptNamed", "name", false, StringKind::Script},
        {"AddComponent", "component", false, StringKind::Script},
        {"", "tag", false, StringKind::Tag},
        {"", "Tag", true, StringKind::Tag},
        {"", "sound", false, StringKind::Sound},
        {"", "levelName", false, StringKind::Level},
    };

    if (param.empty()) return StringKind::None;

    for (const Rule& rule : kRules) {
        if (!rule.method.empty() && rule.method != method) continue;
        if (rule.suffix ? param.ends_with(rule.param) : param == rule.param) return rule.kind;
    }
    return StringKind::None;
}
}
