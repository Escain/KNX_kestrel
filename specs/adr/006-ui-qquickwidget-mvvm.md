# ADR-006 — QMainWindow + QQuickWidget per dock, MVVM

Status: Accepted

**Decision.**
- The shell is widgets: `QMainWindow`, docks, menus, `QAction`s, native `QMenu` context menus.
- Every dock and editor tab body is a `QmlHost : QQuickWidget` on one shared `QQmlEngine`, created lazily.
- QML is presentation only. View models (`viewmodels`, no Quick/Widgets) expose `QAbstractItemModel`s and `Q_INVOKABLE` intents.
  They are registered with `qt_add_qml_module`/`QML_ELEMENT` and passed as `required property`. No context properties.
- Fusion style for both widgets and Quick Controls.
- Models apply ChangeSets incrementally and never reset on an edit.

**Trade-offs accepted.**
- QQuickWidget renders on the GUI thread, so heavy views such as the group monitor must batch.
- QML popups are clipped to the widget.
- `createWindowContainer` was rejected because floating and tabbed docks break focus, stacking and shortcuts.

## Spike results (2026-10-28, Qt 6.8.2, GCC 14)
- Two `QQuickWidget(sharedEngine, parent)` in docks work. Loading via `QQmlComponent::loadFromModule(uri, type)` +
  `createWithInitialProperties({{"vm", …}})` + `QQuickWidget::setContent(component.url(), &component, item)` gives
  `required property` injection, and the widget takes ownership of the item.
- `QT_QPA_PLATFORM=offscreen` renders correctly with both `QT_QUICK_BACKEND=software` and the default RHI backend
  (`grabFramebuffer()` returns the expected pixels), so GUI tests run headless.
- Teardown order matters: view models must outlive their `QmlHost`s, or bindings re-evaluate on a null `vm` during destruction.
  `MainWindow` deletes its hosts before the view models and the engine.
- AUTOMOC only detects `Q_OBJECT` at the start of a line.
- Debian's Qt prints "qml plugin … will not be linked" warnings for executables that are QML modules. Keep QML modules
  in separate libraries linked into the app.
