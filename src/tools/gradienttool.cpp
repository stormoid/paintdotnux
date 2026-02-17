#include "tools/gradienttool.h"
#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <cmath>
#include <cstring>

namespace paintnux {

/// Alpha-blend src over dst (SourceOver).
static inline ColorBgra blendOver(ColorBgra dst, ColorBgra src) {
    uint8_t sa = src.a;
    if (sa == 255) return src;
    if (sa == 0) return dst;
    uint8_t ia = 255 - sa;
    return ColorBgra::fromBgra(
        static_cast<uint8_t>((src.b * sa + dst.b * ia) / 255),
        static_cast<uint8_t>((src.g * sa + dst.g * ia) / 255),
        static_cast<uint8_t>((src.r * sa + dst.r * ia) / 255),
        static_cast<uint8_t>(sa + (dst.a * ia) / 255));
}

// 256-entry color LUT: index i = lerp(colorA, colorB, i/255.0)
static void buildColorLut(ColorBgra lut[256], ColorBgra a, ColorBgra b) {
    // Use fixed-point: channel = a_ch * (255 - i) + b_ch * i, divided by 255
    for (int i = 0; i < 256; ++i) {
        int inv = 255 - i;
        lut[i] = ColorBgra::fromBgra(
            static_cast<uint8_t>((a.b * inv + b.b * i + 127) / 255),
            static_cast<uint8_t>((a.g * inv + b.g * i + 127) / 255),
            static_cast<uint8_t>((a.r * inv + b.r * i + 127) / 255),
            static_cast<uint8_t>((a.a * inv + b.a * i + 127) / 255));
    }
}

QPointF GradientTool::constrainShift(QPointF start, QPointF end) {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal angle = std::atan2(dy, dx);
    qreal snapped = std::round(angle / (M_PI / 4.0)) * (M_PI / 4.0);
    qreal dist = std::sqrt(dx * dx + dy * dy);
    return QPointF(start.x() + dist * std::cos(snapped),
                   start.y() + dist * std::sin(snapped));
}

void GradientTool::commitToHistory() {
    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        QRegion(activeLayer()->surface().bounds()));
    history()->pushNewMemento(std::move(memento));
}

void GradientTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    auto* layer = activeLayer();
    if (!layer) return;

    m_drawing = true;
    m_button = button;
    m_startPos = docPos;

    Surface& surf = layer->surface();
    const int w = surf.width();
    const int h = surf.height();

    // Save surface for preview restoration
    m_savedSurface = Surface(w, h);
    for (int y = 0; y < h; ++y) {
        std::memcpy(m_savedSurface.rowPtr(y), surf.rowPtr(y), w * sizeof(ColorBgra));
    }
    m_hasSaved = true;

    // Cache selection mask once (avoid rebuilding every mouse move)
    auto* sel = workspace()->selection();
    m_hasSel = sel && !sel->isEmpty();
    if (m_hasSel) {
        m_selMask.assign(static_cast<size_t>(w) * h, 0);
        const QRegion& region = sel->region();
        for (const QRect& r : region) {
            QRect clipped = r.intersected(surf.bounds());
            for (int ry = clipped.top(); ry <= clipped.bottom(); ++ry) {
                std::memset(m_selMask.data() + ry * w + clipped.left(), 1,
                            clipped.width());
            }
        }
    } else {
        m_selMask.clear();
    }

    // Save undo snapshot before any modifications
    commitToHistory();
}

void GradientTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers mods) {
    if (!m_drawing) return;
    renderGradient(docPos, mods);
    invalidateCanvas();
}

void GradientTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!m_drawing || button != m_button) return;
    renderGradient(docPos, mods);
    invalidateCanvas();
    m_drawing = false;
    m_hasSaved = false;
    m_selMask.clear();
}

void GradientTool::renderGradient(QPointF endPos, Qt::KeyboardModifiers mods) {
    auto* layer = activeLayer();
    if (!layer) return;

    QPointF constrainedEnd = endPos;
    if (mods & Qt::ShiftModifier)
        constrainedEnd = constrainShift(m_startPos, endPos);

    // Determine colors based on mouse button
    ColorBgra colorA, colorB;
    if (m_button == Qt::LeftButton) {
        colorA = settings().primaryColor;
        colorB = settings().secondaryColor;
    } else {
        colorA = settings().secondaryColor;
        colorB = settings().primaryColor;
    }

    // Build 256-entry color LUT — eliminates per-pixel lerp
    ColorBgra lut[256];
    buildColorLut(lut, colorA, colorB);

    Surface& surf = layer->surface();
    const int w = surf.width();
    const int h = surf.height();
    const bool overwrite = settings().blendMode == ToolBlendMode::Overwrite;

    // Write helper: overwrite assigns directly, normal blends over saved pixel
    auto writePixel = [overwrite](ColorBgra* dst, ColorBgra src, ColorBgra orig) {
        *dst = overwrite ? src : blendOver(orig, src);
    };

    // Gradient direction vector (use float for speed)
    const float sx = static_cast<float>(m_startPos.x());
    const float sy = static_cast<float>(m_startPos.y());
    const float fdx = static_cast<float>(constrainedEnd.x()) - sx;
    const float fdy = static_cast<float>(constrainedEnd.y()) - sy;
    const float lenSq = fdx * fdx + fdy * fdy;

    // If start == end, fill with colorA
    if (lenSq < 0.001f) {
        for (int y = 0; y < h; ++y) {
            ColorBgra* row = surf.rowPtr(y);
            const ColorBgra* saved = m_savedSurface.rowPtr(y);
            if (m_hasSel) {
                const uint8_t* mask = m_selMask.data() + y * w;
                for (int x = 0; x < w; ++x)
                    row[x] = mask[x] ? (overwrite ? colorA : blendOver(saved[x], colorA)) : saved[x];
            } else {
                for (int x = 0; x < w; ++x)
                    writePixel(&row[x], colorA, saved[x]);
            }
        }
        return;
    }

    const float invLenSq = 1.0f / lenSq;
    const float len = std::sqrt(lenSq);
    const float invLen = 1.0f / len;
    const GradientType gradType = settings().gradientType;

    // Precompute dtdx/dtdy for linear variants (matches Paint.NET LinearBase)
    const float dtdx = fdx * invLenSq;
    const float dtdy = fdy * invLenSq;

    // Precompute conical offset: angle from start to end, normalized
    float conicalOffset = 0.0f;
    if (gradType == GradientType::Conical) {
        float ex = constrainedEnd.x() - sx;
        float ey = constrainedEnd.y() - sy;
        conicalOffset = -std::atan2(ey, ex) * static_cast<float>(1.0 / M_PI);
    }

    // Helper lambda: compute t for a pixel, dispatch by gradient type
    // Inlined per-type loops below for performance.

    switch (gradType) {
    case GradientType::LinearClamped: {
        const float tStepX = dtdx;
        for (int y = 0; y < h; ++y) {
            ColorBgra* row = surf.rowPtr(y);
            const ColorBgra* saved = m_savedSurface.rowPtr(y);
            float py = static_cast<float>(y) - sy;
            float tBase = (-sx * dtdx + py * dtdy);

            if (m_hasSel) {
                const uint8_t* mask = m_selMask.data() + y * w;
                float t = tBase;
                for (int x = 0; x < w; ++x, t += tStepX) {
                    if (!mask[x]) { row[x] = saved[x]; continue; }
                    int idx = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            } else {
                float t = tBase;
                for (int x = 0; x < w; ++x, t += tStepX) {
                    int idx = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            }
        }
        break;
    }

    case GradientType::LinearReflected: {
        const float tStepX = dtdx;
        for (int y = 0; y < h; ++y) {
            ColorBgra* row = surf.rowPtr(y);
            const ColorBgra* saved = m_savedSurface.rowPtr(y);
            float py = static_cast<float>(y) - sy;
            float tBase = (-sx * dtdx + py * dtdy);

            if (m_hasSel) {
                const uint8_t* mask = m_selMask.data() + y * w;
                float t = tBase;
                for (int x = 0; x < w; ++x, t += tStepX) {
                    if (!mask[x]) { row[x] = saved[x]; continue; }
                    int idx = static_cast<int>(std::clamp(std::abs(t), 0.0f, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            } else {
                float t = tBase;
                for (int x = 0; x < w; ++x, t += tStepX) {
                    int idx = static_cast<int>(std::clamp(std::abs(t), 0.0f, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            }
        }
        break;
    }

    case GradientType::LinearDiamond: {
        for (int y = 0; y < h; ++y) {
            ColorBgra* row = surf.rowPtr(y);
            const ColorBgra* saved = m_savedSurface.rowPtr(y);
            float dy = static_cast<float>(y) - sy;
            float lerp1_ypart = dy * dtdy;
            float lerp2_ypart = -dy * dtdx;

            if (m_hasSel) {
                const uint8_t* mask = m_selMask.data() + y * w;
                for (int x = 0; x < w; ++x) {
                    if (!mask[x]) { row[x] = saved[x]; continue; }
                    float dx = static_cast<float>(x) - sx;
                    float lerp1 = dx * dtdx + lerp1_ypart;
                    float lerp2 = dx * dtdy + lerp2_ypart;
                    float t = std::abs(lerp1) + std::abs(lerp2);
                    int idx = static_cast<int>(std::min(t, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            } else {
                for (int x = 0; x < w; ++x) {
                    float dx = static_cast<float>(x) - sx;
                    float lerp1 = dx * dtdx + lerp1_ypart;
                    float lerp2 = dx * dtdy + lerp2_ypart;
                    float t = std::abs(lerp1) + std::abs(lerp2);
                    int idx = static_cast<int>(std::min(t, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            }
        }
        break;
    }

    case GradientType::Radial: {
        for (int y = 0; y < h; ++y) {
            ColorBgra* row = surf.rowPtr(y);
            const ColorBgra* saved = m_savedSurface.rowPtr(y);
            float py = static_cast<float>(y) - sy;
            float pySq = py * py;

            if (m_hasSel) {
                const uint8_t* mask = m_selMask.data() + y * w;
                for (int x = 0; x < w; ++x) {
                    if (!mask[x]) { row[x] = saved[x]; continue; }
                    float px = static_cast<float>(x) - sx;
                    float dist = std::sqrt(px * px + pySq);
                    float t = dist * invLen;
                    int idx = static_cast<int>(std::min(t, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            } else {
                for (int x = 0; x < w; ++x) {
                    float px = static_cast<float>(x) - sx;
                    float dist = std::sqrt(px * px + pySq);
                    float t = dist * invLen;
                    int idx = static_cast<int>(std::min(t, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            }
        }
        break;
    }

    case GradientType::Conical: {
        constexpr float invPi = static_cast<float>(1.0 / M_PI);
        for (int y = 0; y < h; ++y) {
            ColorBgra* row = surf.rowPtr(y);
            const ColorBgra* saved = m_savedSurface.rowPtr(y);
            float py = static_cast<float>(y) - sy;

            if (m_hasSel) {
                const uint8_t* mask = m_selMask.data() + y * w;
                for (int x = 0; x < w; ++x) {
                    if (!mask[x]) { row[x] = saved[x]; continue; }
                    float px = static_cast<float>(x) - sx;
                    float theta = std::atan2(py, px);
                    float t = theta * invPi + conicalOffset;
                    if (t > 1.0f) t -= 2.0f;
                    else if (t < -1.0f) t += 2.0f;
                    int idx = static_cast<int>(std::clamp(std::abs(t), 0.0f, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            } else {
                for (int x = 0; x < w; ++x) {
                    float px = static_cast<float>(x) - sx;
                    float theta = std::atan2(py, px);
                    float t = theta * invPi + conicalOffset;
                    if (t > 1.0f) t -= 2.0f;
                    else if (t < -1.0f) t += 2.0f;
                    int idx = static_cast<int>(std::clamp(std::abs(t), 0.0f, 1.0f) * 255.0f + 0.5f);
                    writePixel(&row[x], lut[idx], saved[x]);
                }
            }
        }
        break;
    }
    } // switch
}

} // namespace paintnux
