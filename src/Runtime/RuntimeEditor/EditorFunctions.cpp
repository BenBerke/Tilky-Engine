#include "Headers/Runtime/RuntimeEditor/EditorFunctions.hpp"

#include <algorithm>
#include <deque>
#include <mutex>
#include <string>

#include "Headers/Runtime/Renderer/IRenderer.hpp"
#include "Headers/Math/Vector/Vector2.hpp"

namespace {
    constexpr int MAX_LINE_COUNT = 10;
    constexpr float DEFAULT_LINE_LIFE_TIME = 8.0f;
    constexpr float FADE_OUT_TIME = 1.0f;

    constexpr float LEFT_PADDING = 8.0f;
    constexpr float TOP_PADDING = 8.0f;
    constexpr float LINE_HEIGHT = 20.0f;

    constexpr Vector2 TEXT_SCALE{0.5f, 0.5f};
    const Vector3 DEFAULT_COLOR{255.0f, 255.0f, 255.0f};

    struct ConsoleLine {
        std::string text;
        Vector3 color = DEFAULT_COLOR;

        float lifeTime = DEFAULT_LINE_LIFE_TIME;
        float timeLeft = DEFAULT_LINE_LIFE_TIME;
    };

    std::deque<ConsoleLine> lines;
    std::mutex linesMutex;

    float Clamp01(const float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    Vector3 ApplyFade(const Vector3 color, const float alpha) {
        return {
            color.x * alpha,
            color.y * alpha,
            color.z * alpha
        };
    }

    void PushLine(const std::string& text, const Vector3 color, const float lifeTime) {
        std::scoped_lock lock(linesMutex);

        ConsoleLine line;
        line.text = text;
        line.color = color;
        line.lifeTime = lifeTime;
        line.timeLeft = lifeTime;

        lines.push_back(line);

        while (static_cast<int>(lines.size()) > MAX_LINE_COUNT) {
            lines.pop_front();
        }
    }
}

namespace EditorFunctions {
    void Print(const std::string& text) {
        PushLine(text, DEFAULT_COLOR, DEFAULT_LINE_LIFE_TIME);
    }

    void Print(const std::string& text, const Vector3 color) {
        PushLine(text, color, DEFAULT_LINE_LIFE_TIME);
    }

    void Print(const std::string& text, const Vector3 color, const float lifeTime) {
        PushLine(text, color, lifeTime);
    }

    void UpdateConsole(const float deltaTime) {
        std::scoped_lock lock(linesMutex);

        for (ConsoleLine& line : lines) {
            line.timeLeft -= deltaTime;
        }

        std::erase_if(lines, [](const ConsoleLine& line) {
            return line.timeLeft <= 0.0f;
        });
    }

    void RenderConsole(IRenderer* renderer) {
        if (renderer == nullptr) {
            return;
        }

        std::scoped_lock lock(linesMutex);

        float y = static_cast<float>(renderer->screenHeight) - TOP_PADDING;

        for (const ConsoleLine& line : lines) {
            float alpha = 1.0f;

            if (line.timeLeft < FADE_OUT_TIME) {
                alpha = Clamp01(line.timeLeft / FADE_OUT_TIME);
            }

            renderer->RenderTextRaw(
                line.text,
                {LEFT_PADDING, y},
                TEXT_SCALE,
                ApplyFade(line.color, alpha)
            );

            y -= LINE_HEIGHT;
        }
    }

    void ClearConsole() {
        std::scoped_lock lock(linesMutex);
        lines.clear();
    }
}