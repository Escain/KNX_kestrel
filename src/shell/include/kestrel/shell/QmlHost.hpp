#pragma once

// One QML scene in a widget (ADR-006): a QQuickWidget on the shared engine. The content (a type of a QML
// module, e.g. Kestrel.Views/TopologyPanel) is created lazily on first show with
// QQmlComponent::loadFromModule + createWithInitialProperties (`required property` injection), then handed
// to QQuickWidget::setContent. The view models passed as initial properties must outlive the host.

#include <QQuickWidget>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QQmlComponent;
class QQmlEngine;

namespace kestrel::shell
{

class QmlHost : public QQuickWidget
{
	Q_OBJECT

public:
	QmlHost(QQmlEngine* engine, const QString& module, const QString& type, const QVariantMap& initialProperties,
		QWidget* parent = nullptr);
	~QmlHost() override;

	/// Creates the content if not done yet. Returns true if the root item exists.
	bool load();
	bool isLoaded() const { return rootObject() != nullptr; }
	QString typeName() const { return m_type; }
	/// Errors of a failed load.
	QStringList loadErrors() const { return m_errors; }

Q_SIGNALS:
	void loaded();

protected:
	bool event(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	QString m_module;
	QString m_type;
	QVariantMap m_initialProperties;
	QQmlComponent* m_component = nullptr;
	QStringList m_errors;
	bool m_attempted = false;
};

} // namespace kestrel::shell
