#pragma once

// Builds Editor2D::Frame overlay triangles the way SDL_Renderer drew them:
// it keeps SDL's draw state (colour, blend mode), because some colours only
// look the way they do because of it - an alpha drawn while blending is off
// comes out opaque. Used by MapEditorFrame.cpp and UIEditorFrame.cpp.

#include "Headers/Runtime/Renderer/Editor2DTypes.hpp"
#include "Headers/Math/Vector/Vector2.hpp"
#include "Headers/Math/Vector/Vector4.hpp"

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>

#include <cmath>
#include <string>

namespace Editor2DCanvas {
    // Draw state, as SDL_Renderer keeps it.
    struct Canvas {
        Editor2D::Frame* frame = nullptr;
        Editor2D::Layer layer = Editor2D::Layer::Background;
        bool blend = false;
        bool clip = false; // SDL_SetRenderClipRect to Frame::clipRect
        Vector4 color{0.0f, 0.0f, 0.0f, 255.0f}; // 0..255

        void SetColor(const float r, const float g, const float b, const float a) {
            // SDL_SetRenderDrawColor takes Uint8s.
            color = {static_cast<float>(static_cast<Uint8>(r)), static_cast<float>(static_cast<Uint8>(g)),
                     static_cast<float>(static_cast<Uint8>(b)), static_cast<float>(static_cast<Uint8>(a))};
        }

        // SDL_SetRenderDrawColorFloat: 0..1, not rounded to bytes.
        void SetColorFloat(const float r, const float g, const float b, const float a) {
            color = {r * 255.0f, g * 255.0f, b * 255.0f, a * 255.0f};
        }

        void SetBlend(const bool enabled) { blend = enabled; }

        // An alpha drawn with blending off replaces the pixel: opaque.
        [[nodiscard]] Vector4 Effective(const Vector4& rgba01) const {
            return {rgba01.x, rgba01.y, rgba01.z, blend ? rgba01.w : 1.0f};
        }

        [[nodiscard]] Vector4 DrawColor() const {
            return Effective({color.x / 255.0f, color.y / 255.0f, color.z / 255.0f, color.w / 255.0f});
        }

        void Triangle(const Vector2 a, const Vector2 b, const Vector2 c, const Vector4& rgba) const {
            frame->AddOverlayTriangle(layer, {{a.x, a.y, 0, 0}, rgba}, {{b.x, b.y, 0, 0}, rgba}, {{c.x, c.y, 0, 0}, rgba}, {}, false, clip);
        }

        // SDL_RenderGeometry with a texture: corners TL, TR, BR, BL with uv
        // 0..1, tinted. Textures keep their own (blending) mode.
        void TexturedQuad(const Vector2 (&corners)[4], const std::string& texture, const Vector4& tint) const {
            const Editor2D::GpuOverlayVertex v0{{corners[0].x, corners[0].y, 0.0f, 0.0f}, tint};
            const Editor2D::GpuOverlayVertex v1{{corners[1].x, corners[1].y, 1.0f, 0.0f}, tint};
            const Editor2D::GpuOverlayVertex v2{{corners[2].x, corners[2].y, 1.0f, 1.0f}, tint};
            const Editor2D::GpuOverlayVertex v3{{corners[3].x, corners[3].y, 0.0f, 1.0f}, tint};
            frame->AddOverlayTriangle(layer, v0, v1, v2, texture, true, clip);
            frame->AddOverlayTriangle(layer, v0, v2, v3, texture, true, clip);
        }

        void Quad(const Vector2 a, const Vector2 b, const Vector2 c, const Vector2 d, const Vector4& rgba) const {
            Triangle(a, b, c, rgba);
            Triangle(a, c, d, rgba);
        }

        // A band `width` pixels wide along a -> b, lengthened by `extend` at both ends.
        void Band(const Vector2 a, const Vector2 b, const float width, const float extend, const Vector4& rgba) const {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float length = std::sqrt(dx * dx + dy * dy);

            if (length <= 0.0001f) {
                // SDL still plots the single point.
                if (extend > 0.0f) FillRectColor(a.x - width * 0.5f, a.y - width * 0.5f, width, width, rgba);
                return;
            }

            const float ux = dx / length;
            const float uy = dy / length;
            const float nx = -uy * width * 0.5f;
            const float ny = ux * width * 0.5f;
            const Vector2 s = {a.x - ux * extend, a.y - uy * extend};
            const Vector2 e = {b.x + ux * extend, b.y + uy * extend};

            Quad({s.x + nx, s.y + ny}, {e.x + nx, e.y + ny}, {e.x - nx, e.y - ny}, {s.x - nx, s.y - ny}, rgba);
        }

        // SDL_RenderLine: one pixel, both ends included.
        // SDL lights the pixel a point falls in, so a line runs through
        // pixel centres: half a pixel right and down of its points.
        void Line(const Vector2 a, const Vector2 b) const {
            Band({a.x + 0.5f, a.y + 0.5f}, {b.x + 0.5f, b.y + 0.5f}, 1.0f, 0.5f, DrawColor());
        }

        // MapEditorInternal::DrawThickLine: whole pixel lines either side.
        void ThickLine(const Vector2 a, const Vector2 b, const float thickness) const {
            const int half = static_cast<int>(thickness * 0.5f);
            Band({a.x + 0.5f, a.y + 0.5f}, {b.x + 0.5f, b.y + 0.5f}, static_cast<float>(half * 2 + 1), 0.5f, DrawColor());
        }

        void FillRectColor(const float x, const float y, const float w, const float h, const Vector4& rgba) const {
            if (w <= 0.0f || h <= 0.0f) return;
            Quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, rgba);
        }

        void FillRect(const SDL_FRect& rect) const {
            FillRectColor(rect.x, rect.y, rect.w, rect.h, DrawColor());
        }

        // SDL_RenderRect: a one pixel frame just inside the rectangle.
        void Rect(const SDL_FRect& rect) const {
            const Vector4 rgba = DrawColor();
            if (rect.w <= 0.0f || rect.h <= 0.0f) return;
            if (rect.w <= 2.0f || rect.h <= 2.0f) {
                FillRectColor(rect.x, rect.y, rect.w, rect.h, rgba);
                return;
            }
            FillRectColor(rect.x, rect.y, rect.w, 1.0f, rgba);
            FillRectColor(rect.x, rect.y + rect.h - 1.0f, rect.w, 1.0f, rgba);
            FillRectColor(rect.x, rect.y + 1.0f, 1.0f, rect.h - 2.0f, rgba);
            FillRectColor(rect.x + rect.w - 1.0f, rect.y + 1.0f, 1.0f, rect.h - 2.0f, rgba);
        }

        // SDL_RenderGeometry without a texture: its vertex colours, under
        // the draw blend mode.
        void GeometryTriangle(const Vector2 a, const Vector2 b, const Vector2 c, const Vector4& rgba01) const {
            Triangle(a, b, c, Effective(rgba01));
        }
    };

}
