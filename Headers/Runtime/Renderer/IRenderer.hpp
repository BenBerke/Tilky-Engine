#ifndef TILKY_ENGINE_IRENDERER_HPP
#define TILKY_ENGINE_IRENDERER_HPP

#include <string>

#include <SDL3/SDL.h>

#include "Headers/Objects/Components.hpp"

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool Initialize(std::string windowName) = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void Update(const bool renderDebug) = 0;
    virtual void EndFrame() = 0;

    virtual void BeginImGuiFrame() const = 0;
    virtual void EndImGuiFrame() const = 0;

    int screenWidth = 1600, screenHeight = 900;
    virtual void OnResize(int width, int height) = 0;

    [[nodiscard]] virtual const char* GetName() const = 0;
    [[nodiscard]] virtual SDL_Window* GetWindow() const = 0;

    virtual int CreateTexture(const std::string& fileName) = 0;

    virtual void RenderTextRaw(const std::string& text, Vector2 position, Vector2 scale, Vector3 color) = 0;

    virtual bool CreateMap() = 0;

    void SetUseEditorCamera(const bool enabled) {
        useEditorCamera = enabled;

        if (useEditorCamera) {
            CreateEditorCamera();
        }
    }

    [[nodiscard]] bool IsUsingEditorCamera() const {
        return useEditorCamera;
    }

    [[nodiscard]] ComponentCamera* GetEditorCamera() {
        CreateEditorCamera();
        return editorCamera;
    }

    [[nodiscard]] ComponentTransform* GetEditorCameraTransform() {
        CreateEditorCamera();
        return editorCameraTransform;
    }

protected:
    bool useEditorCamera = false;

    static constexpr ID EDITOR_CAMERA_ENTITY_ID = 0;

    inline static ComponentCamera* editorCamera = nullptr;
    inline static ComponentTransform* editorCameraTransform = nullptr;

    static void CreateEditorCamera() {
        if (editorCamera != nullptr && editorCameraTransform != nullptr) {
            return;
        }

        DestroyEditorCamera();

        editorCamera = new ComponentCamera();
        editorCameraTransform = new ComponentTransform();

        editorCamera->ownerID = EDITOR_CAMERA_ENTITY_ID;
        editorCameraTransform->ownerID = EDITOR_CAMERA_ENTITY_ID;

        editorCameraTransform->position = {0.0f, 0.0f, 10.0f};

        editorCamera->yaw = 0.0f;
        editorCamera->pitch = 0.0f;

        editorCamera->fov = 90.0f;
        editorCamera->nearPlane = 1.0f;
        editorCamera->farPlane = 10000.0f;

        editorCamera->isActive = true;
    }

    static void DestroyEditorCamera() {
        delete editorCamera;
        editorCamera = nullptr;

        delete editorCameraTransform;
        editorCameraTransform = nullptr;
    }
};

#endif