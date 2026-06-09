#ifndef TILKY_ENGINE_IRENDERER_HPP
#define TILKY_ENGINE_IRENDERER_HPP

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool Initialize(std::string windowName) = 0;
    virtual void Shutdown() = 0;

    virtual void BeginFrame() = 0;
    virtual void Update() = 0;
    virtual void EndFrame() = 0;

    virtual void BeginImGuiFrame() const = 0;
    virtual void EndImGuiFrame() const = 0;

    int screenWidth = 1600, screenHeight = 900;
    virtual void OnResize(int width, int height) = 0;

    [[nodiscard]] virtual const char* GetName() const = 0;
    [[nodiscard]] virtual SDL_Window* GetWindow() const = 0;

    virtual int CreateTexture(const std::string& fileName) = 0;
};

#endif