#pragma once

// Central area: a QTabWidget of editors (ADR-006). Each tab body is a QmlHost; device editors own their
// DeviceEditorVM. The Welcome tab is shown whenever no editor is open.
// Tab objectNames: "doc.welcome", "doc.device.<dev-ref>", "doc.groupMonitor".

#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"

#include <QTabWidget>

#include <memory>
#include <vector>

class QQmlEngine;

namespace kestrel::viewmodels
{
class Workspace;
}

namespace kestrel::shell
{

class QmlHost;

class DocumentArea : public QTabWidget
{
	Q_OBJECT

public:
	static inline const QString kWelcomeId = QStringLiteral("doc.welcome");
	static inline const QString kGroupMonitorId = QStringLiteral("doc.groupMonitor");

	DocumentArea(QQmlEngine* engine, viewmodels::Workspace* workspace, QWidget* parent = nullptr);
	~DocumentArea() override;

	/// Opens (or focuses) the editor of a device.
	void openDevice(model::Id<model::Device> device);
	void showWelcome();
	/// Opens (or focuses) the group monitor tab.
	void openGroupMonitor();
	/// Closes every tab, deleting hosts before their view models.
	void closeAll();
	void closeDocument(const QString& id);
	/// The host of a document id, nullptr if not open.
	QmlHost* host(const QString& id) const;
	QStringList documentIds() const;
	static QString deviceDocumentId(model::Id<model::Device> device);

private:
	struct Document
	{
		QString id;
		QmlHost* host = nullptr;
		std::unique_ptr<QObject> viewModel; ///< null for the Welcome tab (shared view model)
	};

	void addDocument(Document document, const QString& title);
	void closeTabAt(int index);
	std::vector<Document>::iterator findDocument(const QString& id);

	QQmlEngine* m_engine;
	viewmodels::Workspace* m_workspace;
	std::vector<Document> m_documents;
};

} // namespace kestrel::shell
