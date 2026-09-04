//
// Created by fang on 2022/6/5.
//

#pragma once

#include <borealis.hpp>
#include <lunasvg.h>

class SVGImage : public brls::Image {
public:
    SVGImage();

    ~SVGImage() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;

    // A View's width/height only reflect actual (Yoga-computed) layout
    // dimensions, which don't exist yet the first time an SVGImage is
    // populated (via the "svg" XML attribute, applied while the view is
    // still being built, well before its first layout pass) -- rasterizing
    // at that point asks lunasvg for a 0x0 bitmap. lunasvg falls back to
    // the SVG's own intrinsic size when both are 0, which saves most icons,
    // but one with no width/height/usable viewBox of its own still yields
    // an empty bitmap ("Cannot set texture: 0"). Retrying here, once real
    // dimensions exist, is what actually fixes those.
    void onLayout() override;

    void setImageFromSVGRes(const std::string& value);

    void setImageFromSVGFile(const std::string& value);

    void setImageFromSVGString(const std::string& value);

    void rotate(float value);

    void updateBitmap();

    static View* create();

private:
    std::unique_ptr<lunasvg::Document> document = nullptr;
    brls::VoidEvent::Subscription subscription;
    std::string filePath;
    float angle = 0;
};