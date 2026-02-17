#pragma once

#include "tools/tool.h"
#include "tools/selecttools.h"

namespace paintnux {

/// Magic Wand tool -- flood fill to create a selection based on color similarity.
class MagicWandTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Magic Wand"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    /// Check if two colors are within tolerance.
    [[nodiscard]] static bool colorMatch(ColorBgra a, ColorBgra b, int tolerance);

    SelectionResizeHelper m_resizeHelper;
};

} // namespace paintnux
