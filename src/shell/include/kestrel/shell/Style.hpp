#pragma once

namespace kestrel::shell
{

/// Fusion for widgets and Qt Quick Controls, so both draw from the same QPalette (ADR-006).
/// Call after constructing QApplication and before creating any QML engine.
void initializeStyle();

} // namespace kestrel::shell
