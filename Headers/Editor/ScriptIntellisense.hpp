#ifndef TILKY_ENGINE_SCRIPTINTELLISENSE_HPP
#define TILKY_ENGINE_SCRIPTINTELLISENSE_HPP

#include "Headers/Runtime/Scripting/Lua/LuaBindingMetadata.hpp"
#include "Headers/Runtime/Scripting/Lua/LuaScriptCompiler.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// The script editor's language smarts: one hand-written Lua tokenizer plus a
// light scope pass over a whole script. Its result drives the editor's
// coloring, autocomplete, parameter hints and syntax-error markers (see
// AssetBrowser::DrawTextEditorWindow). No ImGui in here, so it can be tested
// headlessly.
//
// All positions are byte offsets into the analysed text (as returned by
// TextEditor::GetText(): lines joined with '\n').
//
// Needs scripting initialized (LevelSystem::EnsureScriptingInitialized)
// before the first call: the engine's names come from LuaBindingMetadata.
namespace ScriptIntellisense {
    enum class TokenKind : uint8_t { Name, Keyword, Number, String, Comment, LongComment, Punct, Unknown };

    struct Token {
        TokenKind kind = TokenKind::Unknown;
        int begin = 0;
        int end = 0;
        // A string or long comment/string with no closing quote/bracket.
        bool unterminated = false;
    };

    // What one byte of the text is drawn as.
    enum class ColorRole : uint8_t {
        Default, Keyword, Number, String, Punctuation, Identifier, KnownIdentifier,
        Comment, MultiLineComment, Local, Parameter, Function, Field
    };

    // A value's type as far as the editor can tell: an engine type, or a
    // script (what GetScript("Door") returns - Door.lua's public members).
    struct TypeRef {
        const LuaBindingMetadata::TypeDoc* engine = nullptr;
        std::string script;
        // `Entity[]`: the type of what ipairs()/pairs() gives back.
        bool array = false;

        [[nodiscard]] bool Valid() const { return engine != nullptr || !script.empty(); }
    };

    enum class SymbolKind : uint8_t { Local, Parameter, Function, Field };

    // A name the script itself defines, visible in [scopeBegin, scopeEnd)
    // from declOffset on.
    struct Symbol {
        std::string name;
        SymbolKind kind = SymbolKind::Local;
        int declOffset = 0;
        int scopeBegin = 0;
        int scopeEnd = 0;
        TypeRef type;
        std::vector<std::string> params; // functions only
        std::string luaType;             // fields only, as declared
    };

    struct Analysis {
        std::string text;
        std::vector<Token> tokens;
        // Indices into `tokens` of everything but comments.
        std::vector<int> code;
        std::vector<Symbol> symbols;
        LuaScriptCompiler::Result compiled;
        std::vector<ColorRole> colors; // one per byte of `text`
    };

    // Where a string argument names something in the project.
    enum class StringKind : uint8_t { None, Tag, Script, Level, Sound };

    // Host-side data the analysis can't get by itself.
    struct Context {
        // The analysis of the script GetScript(name) would return (matched
        // by file name without extension), or nullptr.
        std::function<const Analysis*(const std::string& scriptName)> findScript;
        // Every project name of that kind (tags, script names, ...).
        std::function<std::vector<std::string>(StringKind)> projectNames;
    };

    [[nodiscard]] std::vector<Token> Tokenize(std::string_view text);

    // Tokenizes, compiles (for the Tilky declarations), walks scopes and
    // colors the whole script.
    [[nodiscard]] Analysis Analyze(std::string text, const Context& context = {});

    enum class ItemKind : uint8_t {
        Keyword, Local, Parameter, Function, Field, Type, Global, EngineFunction, Callback,
        Property, Method, EnumValue, Tag, Script, Level, Sound, Other
    };

    struct CompletionItem {
        std::string text;
        ItemKind kind = ItemKind::Other;
        std::string detail; // signature or type, shown in the docs panel
        std::string doc;
    };

    struct Completion {
        std::vector<CompletionItem> items;
        // The text accepting an item replaces: [replaceBegin, cursor).
        int replaceBegin = 0;
        // True when the list is about a `.`/`:` member or a string argument,
        // so it shows with nothing typed yet.
        bool scoped = false;
    };

    // Suggestions at `cursor`. `manual` (Ctrl+Space) shows the list even
    // with fewer than two characters typed.
    [[nodiscard]] Completion Complete(const Analysis& analysis, int cursor, bool manual, const Context& context = {});

    struct SignatureHelp {
        std::string label; // "HasTag(tag: string) -> boolean"
        std::vector<std::pair<int, int>> paramRanges; // [begin, end) in label
        int activeParam = 0;
        std::string doc;
    };

    // The call whose parentheses `cursor` sits in, or nullopt.
    [[nodiscard]] std::optional<SignatureHelp> GetSignatureHelp(const Analysis& analysis, int cursor, const Context& context = {});

    struct SyntaxError {
        int line = 0; // 1-based
        std::string message;
    };

    // Compiles the script the way the engine does (Tilky declarations, then
    // Lua's own parser) without running it. Empty when it compiles.
    [[nodiscard]] std::vector<SyntaxError> CheckSyntax(const Analysis& analysis);

    // What a project-name string argument of `method`'s `param` holds.
    [[nodiscard]] StringKind StringKindOfParam(std::string_view method, std::string_view param);
}

#endif //TILKY_ENGINE_SCRIPTINTELLISENSE_HPP
