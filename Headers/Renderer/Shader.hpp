//
// Created by berke on 4/12/2026.
//

#ifndef WOLFY_ENGINE_SHADER_H
#define WOLFY_ENGINE_SHADER_H

#include <glad/glad.h>

#include <string>
#include <fstream>
#include <sstream>
#include <SDL3/SDL_log.h>

class Shader {
    public:
    unsigned int ID{};
    Shader(const char *vertexShaderPath, const char *fragmentShaderPath) {
        std::string vertexShaderSourceCode;
        std::string fragmentShaderSourceCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;

        vShaderFile.exceptions(std::ifstream::badbit | std::ifstream::failbit);
        fShaderFile.exceptions(std::ifstream::badbit | std::ifstream::failbit);
        try {
            vShaderFile.open(vertexShaderPath);
            fShaderFile.open(fragmentShaderPath);
            std::stringstream vShaderStream, fShaderStream;

            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            vShaderFile.close();
            fShaderFile.close();

            vertexShaderSourceCode = vShaderStream.str();
            fragmentShaderSourceCode = fShaderStream.str();
        }
        catch (std::ifstream::failure& e) {
            SDL_Log("FILE ERROR: %s", e.what());
            return;
        }
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

#endif //WOLFY_ENGINE_SHADER_H