// The OpenGL renderer's GLSL shaders, made into Vulkan SPIR-V when loaded,
// and the GL-like state the drawing code sets (see Vulkan.hpp).
//
// Each stage is run through glslang's preprocessor (so PASS_* blocks are
// resolved and comments gone), then rewritten:
//
//   - `#version` becomes 460 with the extensions below and VULKAN defined,
//   - loose uniforms move into one std430 block in a buffer the push
//     constant points at (both stages see the same block, with explicit
//     offsets the C++ side computed), and every use of one reads it there,
//   - an SSBO becomes a buffer reference type; its members are read
//     through the address the block holds for its binding,
//   - a sampler uniform becomes a texture and a sampler index into the
//     bindless set,
//   - varyings get locations by name, so the stages match,
//   - gl_VertexID/gl_InstanceID/gl_FragCoord read as GL's do,
//   - gl_ClipDistance only clips while GL_CLIP_DISTANCE0 would be on.

#include "Headers/Runtime/Renderer/Vulkan/Vulkan.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <type_traits>

#include <glslang/build_info.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <spdlog/spdlog.h>

#include "Headers/Engine/BuildVariant.hpp"
#include "Headers/Project/ProjectManager.hpp"
#include "Headers/Runtime/Renderer/EmbeddedShaders.hpp"

using namespace VulkanInternal;

namespace {
    // The block's fixed start: an address per SSBO binding, then the
    // render target's height (for gl_FragCoord) and whether clipping is on.
    constexpr uint32_t BLOCK_SSBO_OFFSET = 0;
    constexpr uint32_t BLOCK_TARGET_HEIGHT_OFFSET = STORAGE_BINDINGS * 8;
    constexpr uint32_t BLOCK_CLIP_ENABLED_OFFSET = BLOCK_TARGET_HEIGHT_OFFSET + 4;
    constexpr uint32_t BLOCK_HEADER_SIZE = BLOCK_CLIP_ENABLED_OFFSET + 4;

    bool ReadFile(const std::filesystem::path& path, std::string& out) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        std::stringstream stream;
        stream << file.rdbuf();
        out = stream.str();
        return true;
    }

    struct GlslangProcess {
        GlslangProcess() { glslang_initialize_process(); }
        ~GlslangProcess() { glslang_finalize_process(); }
    };

    void EnsureGlslang() {
        static GlslangProcess process;
    }

    // The source with `#define <define>` after its #version line.
    std::string WithDefine(const std::string& source, const char* define) {
        if (define == nullptr) return source;
        const size_t versionEnd = source.find('\n');
        if (versionEnd == std::string::npos) return source;
        return source.substr(0, versionEnd + 1) + "#define " + define + "\n" + source.substr(versionEnd + 1);
    }

    bool Preprocess(const std::string& source, const glslang_stage_t stage, const std::string& label, std::string& out) {
        glslang_input_t input{};
        input.language = GLSLANG_SOURCE_GLSL;
        input.stage = stage;
        input.client = GLSLANG_CLIENT_OPENGL;
        input.client_version = GLSLANG_TARGET_OPENGL_450;
        input.target_language = GLSLANG_TARGET_NONE;
        input.code = source.c_str();
        input.default_version = 430;
        input.default_profile = GLSLANG_CORE_PROFILE;
        input.messages = GLSLANG_MSG_DEFAULT_BIT;
        input.resource = glslang_default_resource();

        glslang_shader_t* shader = glslang_shader_create(&input);
        const bool ok = glslang_shader_preprocess(shader, &input) != 0;

        if (!ok) spdlog::error("Vulkan: preprocessing {} failed: {}", label, glslang_shader_get_info_log(shader));
        else out = glslang_shader_get_preprocessed_code(shader);

        glslang_shader_delete(shader);
        return ok;
    }

    bool CompileSpirv(const std::string& source, const glslang_stage_t stage, const std::string& label, std::vector<uint32_t>& out) {
        glslang_input_t input{};
        input.language = GLSLANG_SOURCE_GLSL;
        input.stage = stage;
        input.client = GLSLANG_CLIENT_VULKAN;
        input.client_version = GLSLANG_TARGET_VULKAN_1_3;
        input.target_language = GLSLANG_TARGET_SPV;
        input.target_language_version = GLSLANG_TARGET_SPV_1_6;
        input.code = source.c_str();
        input.default_version = 460;
        input.default_profile = GLSLANG_NO_PROFILE;
        input.messages = static_cast<glslang_messages_t>(GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
        input.resource = glslang_default_resource();

        glslang_shader_t* shader = glslang_shader_create(&input);

        const auto fail = [&](const char* what, const char* log) {
            spdlog::error("Vulkan: {} {} failed: {}", what, label, log != nullptr ? log : "");

            // The line numbers in the log are of this text.
            std::istringstream lines(source);
            std::string line;
            int number = 1;
            std::string listing;
            while (std::getline(lines, line)) listing += std::to_string(number++) + ": " + line + "\n";
            spdlog::debug("Vulkan: translated {}:\n{}", label, listing);
        };

        if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input)) {
            fail("compiling", glslang_shader_get_info_log(shader));
            glslang_shader_delete(shader);
            return false;
        }

        glslang_program_t* program = glslang_program_create();
        glslang_program_add_shader(program, shader);

        if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
            fail("linking", glslang_program_get_info_log(program));
            glslang_program_delete(program);
            glslang_shader_delete(shader);
            return false;
        }

        glslang_program_SPIRV_generate(program, stage);
        const size_t words = glslang_program_SPIRV_get_size(program);
        out.assign(glslang_program_SPIRV_get_ptr(program), glslang_program_SPIRV_get_ptr(program) + words);

        if (const char* messages = glslang_program_SPIRV_get_messages(program); messages != nullptr && messages[0] != '\0')
            spdlog::debug("Vulkan: {}: {}", label, messages);

        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return true;
    }

    // std430 size and alignment of a GLSL type.
    bool TypeLayout(const std::string& type, uint32_t& size, uint32_t& alignment) {
        static const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> layouts = {
            {"float", {4, 4}}, {"int", {4, 4}}, {"uint", {4, 4}}, {"bool", {4, 4}},
            {"vec2", {8, 8}}, {"ivec2", {8, 8}}, {"uvec2", {8, 8}}, {"bvec2", {8, 8}},
            {"vec3", {12, 16}}, {"ivec3", {12, 16}}, {"uvec3", {12, 16}}, {"bvec3", {12, 16}},
            {"vec4", {16, 16}}, {"ivec4", {16, 16}}, {"uvec4", {16, 16}}, {"bvec4", {16, 16}},
            {"mat2", {32, 16}}, {"mat3", {48, 16}}, {"mat4", {64, 16}},
        };

        const auto found = layouts.find(type);
        if (found == layouts.end()) return false;
        size = found->second.first;
        alignment = found->second.second;
        return true;
    }

    bool IsSamplerType(const std::string& type) {
        return type == "sampler2D" || type == "sampler2DArray";
    }

    // One top-level statement of a preprocessed shader.
    struct Statement {
        std::string text;
    };

    // Splits at the top level: a ';' outside braces and parentheses, or the
    // '}' closing a function body.
    std::vector<Statement> SplitTopLevel(const std::string& code) {
        std::vector<Statement> statements;
        std::string current;
        int braces = 0;
        int parens = 0;

        for (size_t i = 0; i < code.size(); ++i) {
            const char c = code[i];

            // Directives are statements of their own.
            if (c == '#' && braces == 0 && parens == 0 && current.find_first_not_of(" \t\r\n") == std::string::npos) {
                const size_t end = code.find('\n', i);
                statements.push_back({code.substr(i, end == std::string::npos ? std::string::npos : end - i)});
                current.clear();
                if (end == std::string::npos) break;
                i = end;
                continue;
            }

            current += c;

            if (c == '(') ++parens;
            else if (c == ')') --parens;
            else if (c == '{') ++braces;
            else if (c == '}') {
                --braces;
                if (braces == 0 && parens == 0) {
                    // A struct or block goes on to its ';'; a function (its
                    // header ends with ')') ends here.
                    const std::string header = current.substr(0, current.find('{'));
                    const size_t last = header.find_last_not_of(" \t\r\n");
                    const bool function = last != std::string::npos && header[last] == ')';
                    if (function) {
                        statements.push_back({current});
                        current.clear();
                    }
                }
            }
            else if (c == ';' && braces == 0 && parens == 0) {
                statements.push_back({current});
                current.clear();
            }
        }

        if (current.find_first_not_of(" \t\r\n") != std::string::npos) statements.push_back({current});
        return statements;
    }

    std::string Trim(const std::string& text) {
        const size_t start = text.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return {};
        const size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(start, end - start + 1);
    }

    std::string ReplaceIdentifier(const std::string& text, const std::string& name, const std::string& with) {
        // Not a member access (".name"), not part of a longer identifier.
        const std::regex pattern("(^|[^\\w.])" + name + "(?!\\w)");
        return std::regex_replace(text, pattern, "$1" + with);
    }

    struct UniformDecl {
        std::string type;
        std::string name;
        uint32_t count = 1;
        bool array = false;
    };

    struct StorageDecl {
        int binding = 0;
        std::string typeName;
        std::string body;
        std::vector<std::string> members;
    };

    struct ParsedStage {
        std::vector<std::string> extensions;
        std::vector<std::string> statements; // kept, rewritten later
        std::vector<int> kinds;              // per statement: 0 code, 1 struct, 2 storage (index in storage), 3 varying
        std::vector<UniformDecl> uniforms;
        std::vector<StorageDecl> storage;
        struct Varying {
            std::string qualifiers; // "flat " ...
            std::string direction;  // in / out
            std::string type;
            std::string name;
        };
        std::vector<Varying> varyings; // by statement (kind 3), in order
        std::vector<int> varyingStatement;
    };

    bool ParseStage(const std::string& preprocessed, ParsedStage& out, const std::string& label) {
        static const std::regex uniformPattern(R"(^uniform\s+(\w+)\s+(\w+)\s*(\[\s*(\d+)\s*\])?\s*(=[^;]*)?;$)");
        static const std::regex storagePattern(R"(^layout\s*\(([^)]*)\)\s*(readonly\s+|writeonly\s+|restrict\s+|coherent\s+)*buffer\s+(\w+)\s*\{([\s\S]*)\}\s*(\w*)\s*;$)");
        static const std::regex bindingPattern(R"(binding\s*=\s*(\d+))");
        static const std::regex memberPattern(R"((\w+)\s+(\w+)\s*(\[[^\]]*\])?\s*;)");
        static const std::regex varyingPattern(R"(^((?:(?:flat|smooth|noperspective|centroid)\s+)*)(in|out)\s+(\w+)\s+(\w+)\s*;$)");

        for (const Statement& statement : SplitTopLevel(preprocessed)) {
            std::string text = Trim(statement.text);
            if (text.empty()) continue;

            if (text[0] == '#') {
                if (text.rfind("#extension", 0) == 0) out.extensions.push_back(text);
                // #version, #line, #pragma: not carried over.
                continue;
            }

            // One line, so the patterns see the whole declaration.
            std::string flat = std::regex_replace(text, std::regex(R"(\s+)"), " ");
            std::smatch match;

            if (std::regex_match(flat, match, uniformPattern)) {
                UniformDecl uniform;
                uniform.type = match[1];
                uniform.name = match[2];
                if (match[4].matched) {
                    uniform.array = true;
                    uniform.count = static_cast<uint32_t>(std::stoul(match[4]));
                }
                if (match[5].matched)
                    spdlog::debug("Vulkan: {}: the initializer of uniform '{}' is ignored (it starts at 0)", label, uniform.name);
                out.uniforms.push_back(uniform);
                continue;
            }

            if (flat.rfind("layout", 0) == 0 && flat.find(" buffer ") != std::string::npos && std::regex_match(flat, match, storagePattern)) {
                StorageDecl storage;
                const std::string layout = match[1];
                std::smatch bindingMatch;
                if (!std::regex_search(layout, bindingMatch, bindingPattern)) {
                    spdlog::error("Vulkan: {}: buffer '{}' has no binding", label, std::string(match[3]));
                    return false;
                }
                storage.binding = std::stoi(bindingMatch[1]);
                storage.typeName = match[3];
                storage.body = match[4];
                if (std::string(match[5]).size() > 0) {
                    spdlog::error("Vulkan: {}: named buffer blocks are not supported ({})", label, storage.typeName);
                    return false;
                }

                for (std::sregex_iterator it(storage.body.begin(), storage.body.end(), memberPattern), end; it != end; ++it)
                    storage.members.push_back((*it)[2]);

                out.kinds.push_back(2);
                out.statements.push_back(std::to_string(out.storage.size()));
                out.storage.push_back(storage);
                continue;
            }

            if (std::regex_match(flat, match, varyingPattern)) {
                ParsedStage::Varying varying{match[1], match[2], match[3], match[4]};
                out.kinds.push_back(3);
                out.statements.push_back(std::to_string(out.varyings.size()));
                out.varyings.push_back(varying);
                continue;
            }

            out.kinds.push_back(flat.rfind("struct", 0) == 0 ? 1 : 0);
            out.statements.push_back(text);
        }

        return true;
    }

    struct Translation {
        std::unordered_map<std::string, int> varyingLocations; // shared by both stages
        std::vector<UniformDecl> uniforms;                     // the union, in block order
        std::unordered_map<std::string, uint32_t> offsets;
        uint32_t blockSize = BLOCK_HEADER_SIZE;
    };

    void LayOutUniforms(Translation& translation, const std::vector<UniformDecl>& uniforms, std::unordered_map<std::string, UniformField>& fields) {
        for (const UniformDecl& uniform : uniforms) {
            if (fields.contains(uniform.name)) continue;

            UniformField field;
            field.type = uniform.type;
            field.count = uniform.count;
            field.sampler = IsSamplerType(uniform.type);

            uint32_t size = 0;
            uint32_t alignment = 0;
            if (field.sampler) {
                size = 8;
                alignment = 8; // two uints: texture slot, sampler slot
            }
            else if (!TypeLayout(uniform.type, size, alignment)) {
                spdlog::error("Vulkan: uniform '{}' has a type the translator doesn't know ({})", uniform.name, uniform.type);
                continue;
            }

            // std430: an array's stride is its element's size rounded to its alignment.
            const uint32_t stride = (size + alignment - 1) / alignment * alignment;
            uint32_t offset = translation.blockSize;
            offset = (offset + alignment - 1) / alignment * alignment;

            field.offset = offset;
            field.size = size;
            field.stride = stride;

            translation.blockSize = offset + (uniform.array ? stride * uniform.count : size);
            translation.offsets[uniform.name] = offset;
            translation.uniforms.push_back(uniform);
            fields.emplace(uniform.name, field);
        }

        translation.blockSize = (translation.blockSize + 15) / 16 * 16;
    }

    std::string BlockDeclaration(const Translation& translation) {
        std::string block =
            "layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer TilkyUniforms {\n"
            "    layout(offset = 0) uvec2 _ssbo[" + std::to_string(STORAGE_BINDINGS) + "];\n"
            "    layout(offset = " + std::to_string(BLOCK_TARGET_HEIGHT_OFFSET) + ") float _targetHeight;\n"
            "    layout(offset = " + std::to_string(BLOCK_CLIP_ENABLED_OFFSET) + ") int _clipEnabled;\n";

        for (const UniformDecl& uniform : translation.uniforms) {
            const uint32_t offset = translation.offsets.at(uniform.name);
            if (IsSamplerType(uniform.type)) {
                block += "    layout(offset = " + std::to_string(offset) + ") uint " + uniform.name + "_t;\n";
                block += "    layout(offset = " + std::to_string(offset + 4) + ") uint " + uniform.name + "_s;\n";
                continue;
            }

            block += "    layout(offset = " + std::to_string(offset) + ") " + uniform.type + " " + uniform.name;
            if (uniform.array) block += "[" + std::to_string(uniform.count) + "]";
            block += ";\n";
        }

        block += "};\nlayout(push_constant) uniform TilkyPush { TilkyUniforms u; } pc;\n";
        return block;
    }

    std::string Rewrite(const ParsedStage& stage, const Translation& translation, const bool fragment) {
        std::string out =
            "#version 460\n"
            "#extension GL_EXT_buffer_reference : require\n"
            "#extension GL_EXT_buffer_reference2 : require\n"
            "#extension GL_EXT_buffer_reference_uvec2 : require\n"
            "#extension GL_EXT_nonuniform_qualifier : require\n";

        for (const std::string& extension : stage.extensions) out += extension + "\n";

        out += "#define TILKY_VULKAN 1\n"
               "layout(set = 0, binding = 0) uniform texture2D tilkyTextures2D[];\n"
               "layout(set = 0, binding = 1) uniform texture2DArray tilkyTextureArrays[];\n"
               "layout(set = 0, binding = 2) uniform sampler tilkySamplers[];\n";

        // Storage blocks are referenced by type, so the uniform block (which
        // only holds their addresses) can come first.
        out += BlockDeclaration(translation);

        // Identifiers the code reads through the block or a buffer.
        std::vector<std::pair<std::string, std::string>> replacements;
        for (const UniformDecl& uniform : translation.uniforms) {
            if (uniform.type == "sampler2D")
                replacements.emplace_back(uniform.name, "sampler2D(tilkyTextures2D[nonuniformEXT(pc.u." + uniform.name +
                                                        "_t)], tilkySamplers[nonuniformEXT(pc.u." + uniform.name + "_s)])");
            else if (uniform.type == "sampler2DArray")
                replacements.emplace_back(uniform.name, "sampler2DArray(tilkyTextureArrays[nonuniformEXT(pc.u." + uniform.name +
                                                        "_t)], tilkySamplers[nonuniformEXT(pc.u." + uniform.name + "_s)])");
            else replacements.emplace_back(uniform.name, "pc.u." + uniform.name);
        }

        for (const StorageDecl& storage : stage.storage)
            for (const std::string& member : storage.members)
                replacements.emplace_back(member, storage.typeName + "(pc.u._ssbo[" + std::to_string(storage.binding) + "])." + member);

        static const std::regex clipPattern(R"(gl_ClipDistance\s*\[\s*0\s*\]\s*=\s*([^;]+);)");

        for (size_t i = 0; i < stage.statements.size(); ++i) {
            const int kind = stage.kinds[i];

            if (kind == 2) {
                const StorageDecl& storage = stage.storage[std::stoul(stage.statements[i])];
                out += "layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer " + storage.typeName + " {" +
                       storage.body + "};\n";
                continue;
            }

            if (kind == 3) {
                const ParsedStage::Varying& varying = stage.varyings[std::stoul(stage.statements[i])];
                int location = 0;
                if (!(fragment && varying.direction == "out")) location = translation.varyingLocations.at(varying.name);
                out += "layout(location = " + std::to_string(location) + ") " + varying.qualifiers + varying.direction + " " +
                       varying.type + " " + varying.name + ";\n";
                continue;
            }

            std::string text = stage.statements[i];

            if (kind == 0) {
                for (const auto& [name, with] : replacements)
                    if (text.find(name) != std::string::npos) text = ReplaceIdentifier(text, name, with);

                text = std::regex_replace(text, std::regex(R"(\bgl_VertexID\b)"), "gl_VertexIndex");
                text = std::regex_replace(text, std::regex(R"(\bgl_InstanceID\b)"), "gl_InstanceIndex");
                // GL's gl_FragCoord counts rows from the bottom.
                if (fragment)
                    text = std::regex_replace(text, std::regex(R"(\bgl_FragCoord\b)"),
                                              "vec4(gl_FragCoord.x, pc.u._targetHeight - gl_FragCoord.y, gl_FragCoord.zw)");
                text = std::regex_replace(text, clipPattern, "gl_ClipDistance[0] = pc.u._clipEnabled != 0 ? ($1) : 1.0;");
            }

            out += text + "\n";
        }

        return out;
    }

    // ---- Shader cache ----
    //
    // Translating and compiling every program took ~2 s of each engine
    // start. The engine keeps what it made under the user's Tilky Engine
    // folder and reuses it while the input is the same: the GLSL of both
    // stages, the define, glslang's version and this file's build stamp (the
    // translator above lives here, so rebuilding it starts the cache over).
    // The exported game compiles as before: it doesn't write to the player's
    // Documents folder.
    //
    // A file holds a fingerprint of that input, not the input itself, so no
    // shader source is written to disk (format 1 held the source; the
    // clean-up below removes those files).

    constexpr uint32_t CACHE_FORMAT = 2;
    constexpr char CACHE_MAGIC[4] = {'T', 'S', 'P', 'V'};
    constexpr const char* CACHE_EXTENSION = ".tspv";
    // Files no engine has used for this long are removed when a new one is written.
    constexpr auto CACHE_UNUSED_LIFETIME = std::chrono::hours(24 * 30);

    struct CachedProgram {
        std::unordered_map<std::string, UniformField> uniforms;
        uint32_t blockSize = 0;
        bool writesClipDistance = false;
        std::vector<uint32_t> vertexSpirv;
        std::vector<uint32_t> fragmentSpirv;
    };

    std::filesystem::path ShaderCacheFolder() {
        return ProjectManager::GetDefaultProjectsFolder().parent_path() / "Cache" / "VulkanShaders";
    }

    std::string ShaderCacheInput(const std::string& name, const char* define, const std::string& vertexSource,
                                 const std::string& fragmentSource) {
        std::string input = "build " __DATE__ " " __TIME__ "\n";
        input += "glslang " + std::to_string(GLSLANG_VERSION_MAJOR) + "." + std::to_string(GLSLANG_VERSION_MINOR) + "." +
                 std::to_string(GLSLANG_VERSION_PATCH) + GLSLANG_VERSION_FLAVOR + "\n";
        input += name;
        input += '\0';
        input += define != nullptr ? define : "";
        input += '\0';
        input += vertexSource;
        input += '\0';
        input += fragmentSource;

        // Two different 64-bit hashes and the length: a clash would need all
        // three to match.
        uint64_t fnv = 14695981039346656037ull;
        for (const char c : input) {
            fnv ^= static_cast<unsigned char>(c);
            fnv *= 1099511628211ull;
        }
        const uint64_t hash = std::hash<std::string>{}(input);
        const uint64_t size = input.size();

        std::string fingerprint;
        fingerprint.append(reinterpret_cast<const char*>(&fnv), sizeof(fnv));
        fingerprint.append(reinterpret_cast<const char*>(&hash), sizeof(hash));
        fingerprint.append(reinterpret_cast<const char*>(&size), sizeof(size));
        return fingerprint;
    }

    std::filesystem::path ShaderCachePath(const std::string& name, const std::string& input) {
        // The name keeps the folder readable; the hash tells inputs apart (the
        // file holds the whole fingerprint too, so a clash just misses).
        std::string fileName;
        for (const char c : name) fileName += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';

        char hash[17];
        std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(std::hash<std::string>{}(input)));
        return ShaderCacheFolder() / (fileName + "-" + hash + CACHE_EXTENSION);
    }

    template <typename T>
    void WriteValue(std::string& out, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        out.append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void WriteString(std::string& out, const std::string& text) {
        WriteValue(out, static_cast<uint64_t>(text.size()));
        out += text;
    }

    void WriteWords(std::string& out, const std::vector<uint32_t>& words) {
        WriteValue(out, static_cast<uint64_t>(words.size()));
        out.append(reinterpret_cast<const char*>(words.data()), words.size() * sizeof(uint32_t));
    }

    struct CacheReader {
        const std::string& data;
        size_t position = 0;

        template <typename T>
        bool Read(T& value) {
            static_assert(std::is_trivially_copyable_v<T>);
            if (data.size() - position < sizeof(T)) return false;
            std::memcpy(&value, data.data() + position, sizeof(T));
            position += sizeof(T);
            return true;
        }

        bool ReadString(std::string& text) {
            uint64_t size = 0;
            if (!Read(size) || data.size() - position < size) return false;
            text.assign(data, position, size);
            position += size;
            return true;
        }

        bool ReadWords(std::vector<uint32_t>& words) {
            uint64_t count = 0;
            if (!Read(count) || count > (data.size() - position) / sizeof(uint32_t)) return false;
            words.resize(count);
            std::memcpy(words.data(), data.data() + position, count * sizeof(uint32_t));
            position += count * sizeof(uint32_t);
            return true;
        }
    };

    bool LoadCachedProgram(const std::filesystem::path& path, const std::string& input, CachedProgram& out) {
        std::string data;
        if (!ReadFile(path, data)) return false;

        CacheReader reader{data};

        char magic[4];
        uint32_t format = 0;
        std::string storedInput;
        if (!reader.Read(magic) || std::memcmp(magic, CACHE_MAGIC, 4) != 0 || !reader.Read(format) || format != CACHE_FORMAT ||
            !reader.ReadString(storedInput) || storedInput != input)
            return false;

        uint64_t uniformCount = 0;
        if (!reader.Read(out.blockSize) || !reader.Read(out.writesClipDistance) || !reader.Read(uniformCount)) return false;

        for (uint64_t i = 0; i < uniformCount; ++i) {
            std::string uniformName;
            UniformField field;
            if (!reader.ReadString(uniformName) || !reader.ReadString(field.type) || !reader.Read(field.offset) ||
                !reader.Read(field.size) || !reader.Read(field.stride) || !reader.Read(field.count) || !reader.Read(field.sampler))
                return false;
            out.uniforms.emplace(std::move(uniformName), std::move(field));
        }

        if (!reader.ReadWords(out.vertexSpirv) || !reader.ReadWords(out.fragmentSpirv)) return false;
        if (out.vertexSpirv.empty() || out.fragmentSpirv.empty() || reader.position != data.size()) return false;

        // Used: it stays past the clean-up below.
        std::error_code ec;
        std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), ec);
        return true;
    }

    void RemoveUnusedCacheFiles() {
        std::error_code ec;
        const auto now = std::filesystem::file_time_type::clock::now();

        for (std::filesystem::directory_iterator it(ShaderCacheFolder(), ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension() != CACHE_EXTENSION) continue;

            // Another format (format 1 held shader source) goes at once.
            bool otherFormat = false;
            {
                std::ifstream file(it->path(), std::ios::binary);
                char header[8] = {};
                uint32_t format = 0;
                if (file.read(header, sizeof(header))) {
                    std::memcpy(&format, header + 4, sizeof(format));
                    otherFormat = std::memcmp(header, CACHE_MAGIC, 4) != 0 || format != CACHE_FORMAT;
                }
            }

            std::error_code timeError;
            const auto writeTime = it->last_write_time(timeError);
            if (otherFormat || (!timeError && now - writeTime > CACHE_UNUSED_LIFETIME)) {
                std::error_code removeError;
                std::filesystem::remove(it->path(), removeError);
            }
        }
    }

    void SaveCachedProgram(const std::filesystem::path& path, const std::string& input, const Program& program,
                           const std::vector<uint32_t>& vertexSpirv, const std::vector<uint32_t>& fragmentSpirv) {
        std::string data;
        data.append(CACHE_MAGIC, 4);
        WriteValue(data, CACHE_FORMAT);
        WriteString(data, input);
        WriteValue(data, program.blockSize);
        WriteValue(data, program.writesClipDistance);
        WriteValue(data, static_cast<uint64_t>(program.uniforms.size()));

        for (const auto& [uniformName, field] : program.uniforms) {
            WriteString(data, uniformName);
            WriteString(data, field.type);
            WriteValue(data, field.offset);
            WriteValue(data, field.size);
            WriteValue(data, field.stride);
            WriteValue(data, field.count);
            WriteValue(data, field.sampler);
        }

        WriteWords(data, vertexSpirv);
        WriteWords(data, fragmentSpirv);

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            spdlog::warn("Vulkan: can't create the shader cache folder {}: {}", path.parent_path().string(), ec.message());
            return;
        }

        RemoveUnusedCacheFiles();

        // Written aside and moved in, so another engine reading it never
        // sees half a file.
        std::filesystem::path temporary = path;
        temporary += ".tmp" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file || !file.write(data.data(), static_cast<std::streamsize>(data.size()))) {
                spdlog::warn("Vulkan: can't write the shader cache file {}", temporary.string());
                file.close();
                std::filesystem::remove(temporary, ec);
                return;
            }
        }

        std::filesystem::rename(temporary, path, ec);
        if (ec) std::filesystem::remove(temporary, ec);
    }

    VkShaderModule MakeModule(const VkDevice device, const std::vector<uint32_t>& spirv) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = spirv.size() * sizeof(uint32_t);
        info.pCode = spirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
        return module;
    }
}

size_t VulkanInternal::PipelineKeyHash::operator()(const PipelineKey& key) const noexcept {
    size_t hash = std::hash<const void*>{}(key.program);
    const auto mix = [&hash](const size_t value) { hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2); };
    mix(static_cast<size_t>(key.topology));
    mix(key.blend ? 1 : 0);
    mix(static_cast<size_t>(key.blendFunc));
    mix(key.colorMask);
    mix(key.wireframe ? 1 : 0);
    mix(static_cast<size_t>(key.colorFormat));
    mix(static_cast<size_t>(key.depthFormat));
    return hash;
}

std::unique_ptr<Program> Vulkan::LoadProgram(const std::string& name, const std::string_view vertexShader,
                                             const std::string_view fragmentShader, const char* define,
                                             std::vector<VertexAttribute> attributes) {
    const std::optional<std::string_view> vertexEmbedded = EmbeddedShaders::Find(vertexShader);
    const std::optional<std::string_view> fragmentEmbedded = EmbeddedShaders::Find(fragmentShader);
    if (!vertexEmbedded || !fragmentEmbedded) {
        spdlog::error("Vulkan: shaders for {} aren't embedded ({}, {})", name, vertexShader, fragmentShader);
        return nullptr;
    }
    const std::string vertexSource(*vertexEmbedded);
    const std::string fragmentSource(*fragmentEmbedded);

    const bool useCache = !BuildVariant::IsStandalone();
    const std::string cacheInput = useCache ? ShaderCacheInput(name, define, vertexSource, fragmentSource) : std::string();
    const std::filesystem::path cachePath = useCache ? ShaderCachePath(name, cacheInput) : std::filesystem::path();

    if (CachedProgram cached; useCache && LoadCachedProgram(cachePath, cacheInput, cached)) {
        auto program = std::make_unique<Program>();
        program->name = name;
        program->attributes = attributes;
        program->uniforms = std::move(cached.uniforms);
        program->blockSize = cached.blockSize;
        program->values.assign(program->blockSize, 0);
        program->writesClipDistance = cached.writesClipDistance;
        program->vertex = MakeModule(device, cached.vertexSpirv);
        program->fragment = MakeModule(device, cached.fragmentSpirv);

        if (program->vertex != VK_NULL_HANDLE && program->fragment != VK_NULL_HANDLE) {
            spdlog::info("Vulkan: loaded {} from the shader cache ({} uniform bytes)", name, program->blockSize);
            return program;
        }

        // A file that doesn't make modules is compiled again below, and replaced.
        spdlog::warn("Vulkan: the shader cache for {} didn't load; compiling it", name);
        if (program->vertex != VK_NULL_HANDLE) vkDestroyShaderModule(device, program->vertex, nullptr);
        if (program->fragment != VK_NULL_HANDLE) vkDestroyShaderModule(device, program->fragment, nullptr);
    }

    EnsureGlslang();

    std::string vertexCode;
    std::string fragmentCode;
    if (!Preprocess(WithDefine(vertexSource, define), GLSLANG_STAGE_VERTEX, name + " (vertex)", vertexCode) ||
        !Preprocess(WithDefine(fragmentSource, define), GLSLANG_STAGE_FRAGMENT, name + " (fragment)", fragmentCode))
        return nullptr;

    ParsedStage vertex;
    ParsedStage fragment;
    if (!ParseStage(vertexCode, vertex, name + " (vertex)") || !ParseStage(fragmentCode, fragment, name + " (fragment)")) return nullptr;

    auto program = std::make_unique<Program>();
    program->name = name;
    program->attributes = std::move(attributes);

    Translation translation;
    LayOutUniforms(translation, vertex.uniforms, program->uniforms);
    LayOutUniforms(translation, fragment.uniforms, program->uniforms);
    program->blockSize = translation.blockSize;
    program->values.assign(program->blockSize, 0);

    int nextLocation = 0;
    for (const ParsedStage::Varying& varying : vertex.varyings)
        if (varying.direction == "out" && !translation.varyingLocations.contains(varying.name))
            translation.varyingLocations[varying.name] = nextLocation++;
    for (const ParsedStage::Varying& varying : fragment.varyings)
        if (varying.direction == "in" && !translation.varyingLocations.contains(varying.name))
            translation.varyingLocations[varying.name] = nextLocation++;
    for (const ParsedStage::Varying& varying : vertex.varyings)
        if (varying.direction == "in" && !translation.varyingLocations.contains(varying.name))
            translation.varyingLocations[varying.name] = 0; // a vertex input without a layout: location 0

    program->writesClipDistance = vertexCode.find("gl_ClipDistance") != std::string::npos;

    const std::string vertexVulkan = Rewrite(vertex, translation, false);
    const std::string fragmentVulkan = Rewrite(fragment, translation, true);

    std::vector<uint32_t> vertexSpirv;
    std::vector<uint32_t> fragmentSpirv;
    if (!CompileSpirv(vertexVulkan, GLSLANG_STAGE_VERTEX, name + " (vertex)", vertexSpirv) ||
        !CompileSpirv(fragmentVulkan, GLSLANG_STAGE_FRAGMENT, name + " (fragment)", fragmentSpirv))
        return nullptr;

    program->vertex = MakeModule(device, vertexSpirv);
    program->fragment = MakeModule(device, fragmentSpirv);

    if (program->vertex == VK_NULL_HANDLE || program->fragment == VK_NULL_HANDLE) {
        spdlog::error("Vulkan: vkCreateShaderModule failed for {}", name);
        if (program->vertex != VK_NULL_HANDLE) vkDestroyShaderModule(device, program->vertex, nullptr);
        if (program->fragment != VK_NULL_HANDLE) vkDestroyShaderModule(device, program->fragment, nullptr);
        return nullptr;
    }

    if (useCache) SaveCachedProgram(cachePath, cacheInput, *program, vertexSpirv, fragmentSpirv);

    spdlog::info("Vulkan: compiled {} ({} uniform bytes)", name, program->blockSize);
    return program;
}

void Vulkan::DestroyPrograms() {
    for (const auto& pipeline : pipelines) vkDestroyPipeline(device, pipeline.second, nullptr);
    pipelines.clear();

    const auto destroy = [this](std::unique_ptr<Program>& program) {
        if (program == nullptr) return;
        vkDestroyShaderModule(device, program->vertex, nullptr);
        vkDestroyShaderModule(device, program->fragment, nullptr);
        program.reset();
    };

    destroy(projectionProgram);
    destroy(backgroundProgram);
    destroy(uiProgram);
    destroy(textProgram);
    for (std::unique_ptr<Program>& program : editor2DPrograms) destroy(program);

    editor2DReady = false;
    editor2DInitTried = false;
    program = nullptr;
}

VkPipeline Vulkan::GetPipeline(const PipelineKey& key) {
    if (const auto found = pipelines.find(key); found != pipelines.end()) return found->second;

    const Program& source = *key.program;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = source.vertex;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = source.fragment;
    stages[1].pName = "main";

    // One binding; its stride is dynamic (0 for the zero buffer).
    VkVertexInputBindingDescription binding{0, 0, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const VertexAttribute& attribute : source.attributes) attributes.push_back({attribute.location, 0, attribute.format, attribute.offset});

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (!attributes.empty()) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = key.topology;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    const bool lines = key.topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST || key.topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    raster.polygonMode = key.wireframe && !lines ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = key.colorMask;
    attachment.blendEnable = key.blend ? VK_TRUE : VK_FALSE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = key.blendFunc == Blend::AlphaSeparate ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
        VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE, VK_DYNAMIC_STATE_DEPTH_BIAS,
    };
    if (!attributes.empty()) dynamicStates.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);

    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &key.colorFormat;
    rendering.depthAttachmentFormat = key.depthFormat;
    rendering.stencilAttachmentFormat = key.depthFormat;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
        spdlog::error("Vulkan: creating a pipeline for {} failed", source.name);
        pipeline = VK_NULL_HANDLE;
    }

    pipelines.emplace(key, pipeline);
    return pipeline;
}

// ============================================================================
// Uniforms
// ============================================================================

namespace {
    UniformField* FindField(Program* program, const char* name) {
        if (program == nullptr) return nullptr;
        const auto found = program->uniforms.find(name);
        return found == program->uniforms.end() ? nullptr : &found->second;
    }
}

void Vulkan::SetInt(const char* name, const int value) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->sampler) return;

    if (field->type == "float") {
        const float asFloat = static_cast<float>(value);
        std::memcpy(program->values.data() + field->offset, &asFloat, 4);
        return;
    }
    std::memcpy(program->values.data() + field->offset, &value, 4);
}

void Vulkan::SetFloat(const char* name, const float value) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->sampler) return;
    std::memcpy(program->values.data() + field->offset, &value, 4);
}

void Vulkan::SetVec2(const char* name, const float x, const float y) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->size < 8) return;
    const float values[2] = {x, y};
    std::memcpy(program->values.data() + field->offset, values, 8);
}

void Vulkan::SetVec3(const char* name, const float x, const float y, const float z) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->size < 12) return;
    const float values[3] = {x, y, z};
    std::memcpy(program->values.data() + field->offset, values, 12);
}

void Vulkan::SetVec4(const char* name, const float x, const float y, const float z, const float w) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->size < 16) return;
    const float values[4] = {x, y, z, w};
    std::memcpy(program->values.data() + field->offset, values, 16);
}

void Vulkan::SetVec4Array(const char* name, const float* values, const int count) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->size < 16) return;
    const int n = std::min(count, static_cast<int>(field->count));
    for (int i = 0; i < n; ++i) std::memcpy(program->values.data() + field->offset + i * field->stride, values + i * 4, 16);
}

void Vulkan::SetMat4RowMajor(const char* name, const float* rowMajor) {
    float columnMajor[16];
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) columnMajor[column * 4 + row] = rowMajor[row * 4 + column];
    SetMat4ColumnMajor(name, columnMajor);
}

void Vulkan::SetMat4ColumnMajor(const char* name, const float* columnMajor) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->type != "mat4") return;
    std::memcpy(program->values.data() + field->offset, columnMajor, 64);
}

void Vulkan::SetMat3ColumnMajor(const char* name, const float* columnMajor) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || field->type != "mat3") return;
    // std430 pads each column to a vec4.
    for (int column = 0; column < 3; ++column)
        std::memcpy(program->values.data() + field->offset + column * 16, columnMajor + column * 3, 12);
}

void Vulkan::SetTexture(const char* name, const Image* image, const uint32_t sampler) {
    UniformField* field = FindField(program, name);
    if (field == nullptr || !field->sampler) return;

    // Slot 0 of either kind is a black placeholder, as GL's texture 0 reads.
    const uint32_t values[2] = {image != nullptr && image->slot != UINT32_MAX ? image->slot : 0u, sampler};
    std::memcpy(program->values.data() + field->offset, values, 8);
}

// ============================================================================
// Drawing
// ============================================================================

namespace {
    VkRect2D ToVulkanRect(const int rect[4], const int targetWidth, const int targetHeight) {
        // GL counts y from the bottom.
        int x = rect[0];
        int y = targetHeight - (rect[1] + rect[3]);
        int width = rect[2];
        int height = rect[3];

        if (x < 0) {
            width += x;
            x = 0;
        }
        if (y < 0) {
            height += y;
            y = 0;
        }
        width = std::clamp(width, 0, std::max(targetWidth - x, 0));
        height = std::clamp(height, 0, std::max(targetHeight - y, 0));

        return {{x, y}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    }
}

bool Vulkan::PrepareDraw(const VkPrimitiveTopology topology) {
    if (program == nullptr) return false;

    VkCommandBuffer commands = Commands();
    if (!rendering) BeginRendering(false, {}, false);
    if (!rendering) return false;

    const Target target = CurrentTarget();

    PipelineKey key;
    key.program = program;
    key.topology = topology;
    key.blend = state.blend;
    key.blendFunc = state.blendFunc;
    key.colorMask = state.colorMask;
    key.wireframe = wireframe;
    key.colorFormat = target.color != nullptr ? target.color->format : swapchainFormat;
    key.depthFormat = depthFormat;

    const VkPipeline pipeline = GetPipeline(key);
    if (pipeline == VK_NULL_HANDLE) return false;

    if (pipeline != boundPipeline) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundPipeline = pipeline;
    }

    if (!descriptorsBound) {
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &bindlessSet, 0, nullptr);
        descriptorsBound = true;
    }

    // GL's viewport, y up: a negative height keeps NDC +y at the top.
    const int* view = state.viewport;
    const float viewportTop = static_cast<float>(target.height - (view[1] + view[3]));
    const VkViewport viewport{
        static_cast<float>(view[0]), viewportTop + static_cast<float>(view[3]),
        static_cast<float>(view[2]), -static_cast<float>(view[3]), 0.0f, 1.0f
    };
    vkCmdSetViewport(commands, 0, 1, &viewport);

    const int full[4] = {0, 0, target.width, target.height};
    const VkRect2D scissor = ToVulkanRect(state.scissorTest ? state.scissor : full, target.width, target.height);
    vkCmdSetScissor(commands, 0, 1, &scissor);

    vkCmdSetCullMode(commands, state.cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(commands, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(commands, state.depthTest ? VK_TRUE : VK_FALSE);
    // GL writes no depth while its test is off.
    vkCmdSetDepthWriteEnable(commands, state.depthTest && state.depthWrite ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(commands, state.depthCompare);
    vkCmdSetStencilTestEnable(commands, state.stencilTest ? VK_TRUE : VK_FALSE);
    vkCmdSetStencilOp(commands, VK_STENCIL_FACE_FRONT_AND_BACK, state.stencilFail, state.stencilPass, state.stencilDepthFail,
                      state.stencilCompare);
    vkCmdSetStencilCompareMask(commands, VK_STENCIL_FACE_FRONT_AND_BACK, state.stencilCompareMask);
    vkCmdSetStencilWriteMask(commands, VK_STENCIL_FACE_FRONT_AND_BACK, state.stencilWriteMask);
    vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, state.stencilReference);
    vkCmdSetDepthBiasEnable(commands, state.polygonOffset ? VK_TRUE : VK_FALSE);
    // glPolygonOffset(1, 4): factor 1, units 4.
    vkCmdSetDepthBias(commands, 4.0f, 0.0f, 1.0f);

    // The uniform block, with this draw's buffer addresses and target height.
    const ArenaAllocation block = ArenaAllocate(program->blockSize, 16);
    std::memcpy(block.pointer, program->values.data(), program->blockSize);

    auto* bytes = static_cast<uint8_t*>(block.pointer);
    for (int binding = 0; binding < STORAGE_BINDINGS; ++binding) {
        const VkDeviceAddress address = storage[binding] != 0 ? storage[binding] : zeroBuffer.address;
        std::memcpy(bytes + BLOCK_SSBO_OFFSET + binding * 8, &address, 8);
    }
    const float targetHeight = static_cast<float>(target.height);
    std::memcpy(bytes + BLOCK_TARGET_HEIGHT_OFFSET, &targetHeight, 4);
    const int clipEnabled = state.clipDistance ? 1 : 0;
    std::memcpy(bytes + BLOCK_CLIP_ENABLED_OFFSET, &clipEnabled, 4);

    vkCmdPushConstants(commands, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkDeviceAddress),
                       &block.address);

    return true;
}

void Vulkan::Draw(const VkPrimitiveTopology topology, const uint32_t vertexCount, const uint32_t instanceCount, const uint32_t firstVertex) {
    if (vertexCount == 0 || instanceCount == 0) return;
    if (!PrepareDraw(topology)) return;

    VkCommandBuffer commands = Commands();

    if (!program->attributes.empty()) {
        // Every vertex reads the zero buffer's first bytes.
        const VkDeviceSize offset = 0;
        const VkDeviceSize stride = 0;
        vkCmdBindVertexBuffers2(commands, 0, 1, &zeroBuffer.buffer, &offset, nullptr, &stride);
    }

    vkCmdDraw(commands, vertexCount, instanceCount, firstVertex, 0);
}

void Vulkan::DrawIndexed(const Buffer& vertices, const uint32_t vertexStride, const Buffer& indices, const uint32_t indexCount,
                         const uint32_t instanceCount) {
    if (indexCount == 0 || instanceCount == 0) return;
    if (!PrepareDraw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)) return;

    VkCommandBuffer commands = Commands();
    const VkDeviceSize offset = 0;
    const VkDeviceSize stride = vertexStride;
    vkCmdBindVertexBuffers2(commands, 0, 1, &vertices.buffer, &offset, nullptr, &stride);
    vkCmdBindIndexBuffer(commands, indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commands, indexCount, instanceCount, 0, 0, 0);
}

void Vulkan::DrawVertices(const void* vertices, const size_t bytes, const uint32_t stride, const uint32_t vertexCount) {
    if (vertexCount == 0 || bytes == 0) return;
    if (!PrepareDraw(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)) return;

    const ArenaAllocation allocation = ArenaAllocate(bytes, 16);
    std::memcpy(allocation.pointer, vertices, bytes);

    VkCommandBuffer commands = Commands();
    const VkDeviceSize offset = allocation.offset;
    const VkDeviceSize strideBytes = stride;
    vkCmdBindVertexBuffers2(commands, 0, 1, &allocation.buffer, &offset, nullptr, &strideBytes);
    vkCmdDraw(commands, vertexCount, 1, 0, 0);
}

void Vulkan::ClearDepth() {
    VkCommandBuffer commands = Commands();
    if (!rendering) BeginRendering(false, {}, false);
    if (!rendering) return;

    const Target target = CurrentTarget();

    VkClearAttachment clear{};
    clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clear.clearValue.depthStencil = {0.0f, 0};

    VkClearRect rect{};
    rect.rect = {{0, 0}, {static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)}};
    rect.layerCount = 1;

    vkCmdClearAttachments(commands, 1, &clear, 1, &rect);
}
