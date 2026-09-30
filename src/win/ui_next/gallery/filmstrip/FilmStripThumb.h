#pragma once
// FilmStripThumb.h — thumbnail types for the FEATURE_D2D_GALLERY filmstrip.
// Adapted from apps/win32-mini/filmstrip/: WIC factory is passed in at
// construction; no extern g_wic dependency.

#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <string>
#include <memory>

namespace filmstrip {

// ── Options (mirrored from FilmStrip.h — kept here so Thumb can see them) ──

namespace options::thumbOptions {
    enum class ThumbFill { Contain, Cover };
    constexpr ThumbFill fill             = ThumbFill::Cover;
    constexpr float dropShadowSize       = 3.f;
    constexpr float dropShadowHardness   = 0.15f;
} // namespace options::thumbOptions

// ── Thumb base ───────────────────────────────────────────────────────────────

class Thumb {
public:
    virtual ~Thumb() = default;

    virtual const std::wstring& GetPath()  const = 0;

    virtual void Render(ID2D1RenderTarget* rt,
                        const D2D1_ROUNDED_RECT& rect,
                        float opacity) const = 0;
    virtual void RenderPlaceholder(ID2D1RenderTarget* rt,
                                   const D2D1_ROUNDED_RECT& rect,
                                   bool dark = true) const;

    virtual bool IsLoaded() const = 0;
    virtual void Preload(ID2D1RenderTarget* rt) = 0;
    virtual void Unload() = 0;

protected:
    Thumb() = default;
    static D2D1_ROUNDED_RECT MakeRoundedRect(float cx, float cy,
                                              float size, float radius);
};

// ── ImageThumb — WIC + D2D rounded thumbnail ─────────────────────────────────

class ImageThumb final : public Thumb {
public:
    ImageThumb(std::wstring path, IWICImagingFactory* wic);
    ~ImageThumb() override;

    const std::wstring& GetPath() const override { return path_; }

    void Render(ID2D1RenderTarget* rt,
                const D2D1_ROUNDED_RECT& rect,
                float opacity) const override;

    bool IsLoaded() const override { return bitmap_ != nullptr; }
    void Preload(ID2D1RenderTarget* rt) override;
    void Unload() override;

private:
    std::wstring        path_;
    IWICImagingFactory* wic_;             // non-owning — lifetime = Impl
    mutable ID2D1Bitmap* bitmap_       = nullptr;
    mutable bool         attempted_    = false;

    void EnsureLoaded(ID2D1RenderTarget* rt) const;
};

// ── ThumbFactory ─────────────────────────────────────────────────────────────

class ThumbFactory {
public:
    // Creates an ImageThumb for any non-empty path; caller filters by extension.
    static std::unique_ptr<Thumb> Create(const std::wstring& path,
                                         IWICImagingFactory* wic);
};

// ── DrawChrome helper ─────────────────────────────────────────────────────────

namespace thumb {
    constexpr float kDefaultThumbSize   = 100.f;
    constexpr float kDefaultCornerRadius = 9.f;

    void DrawChrome(ID2D1RenderTarget* rt,
                    const D2D1_ROUNDED_RECT& rect,
                    bool selected);
} // namespace thumb

} // namespace filmstrip
