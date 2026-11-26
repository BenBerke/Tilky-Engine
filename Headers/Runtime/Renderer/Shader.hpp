//
// Created by berke on 4/12/2026.
//

#ifndef TILKY_ENGINE_SHADER_H
#define TILKY_ENGINE_SHADER_H

#include <glad/glad.h>

#include <string>
#include <fstream> // kept: files including OpenGL.hpp may rely on these
#include <sstream>
#include <SDL3/SDL_log.h>

#include "Headers/Runtime/Renderer/EmbeddedShaders.hpp"


// This was taken from learnopengl.com. The sources come from the shaders
// compiled into the executable (EmbeddedShaders), named relative to Shaders/.
class Shader {
    public:
    unsigned int ID{};
    Shader(const char *vertexShaderName, const char *fragmentShaderName) {
        const std::optional<std::string_view> vertexSource = EmbeddedShaders::Find(vertexShaderName);
        const std::optional<std::string_view> fragmentSource = EmbeddedShaders::Find(fragmentShaderName);
        if (!vertexSource || !fragmentSource) {
            SDL_Log("SHADER ERROR: %s or %s isn't embedded", vertexShaderName, fragmentShaderName);
            return;
        }
        const std::string vertexShaderSourceCode(*vertexSource);
        const std::string fragmentShaderSourceCode(*fragmentSource);
        const char* vShaderCode = vertexShaderSourceCode.c_str();
        const char* fShaderCode = fragmentShaderSourceCode.c_str();

        unsigned int vertex, fragment;
        int success;
        char infoLog[512];

        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, nullptr);
        glCompileShader(vertex);
        glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertex, 512, nullptr, infoLog);
            SDL_Log("VERTEX ERROR: %s", infoLog);
        }

        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, nullptr);
        glCompileShader(fragment);
        glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragment, 512, nullptr, infoLog);
            SDL_Log("FRAGMENT ERROR: %s", infoLog);
        }

        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        glGetProgramiv(ID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(ID, 512, nullptr, infoLog);
            SDL_Log("PROGRAM ERROR: %s", infoLog);
        }

        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }
    void use() const {
        glUseProgram(ID);
    }

    void setBool(const std::string &name, const bool value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), static_cast<int>(value));
    }
    void setInt(const std::string &name, const int value) const {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }
    void setFloat(const std::string &name, const float value) const {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }

};

#endif //TILKY_ENGINE_SHADER_H