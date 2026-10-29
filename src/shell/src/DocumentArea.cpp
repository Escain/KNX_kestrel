#include "kestrel/shell/DocumentArea.hpp"

#include "kestrel/shell/QmlHost.hpp"
#include "kestrel/viewmodels/DeviceEditorVM.hpp"
#include "kestrel/viewmodels/GroupMonitorVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/WelcomeVM.hpp"
#include "kestrel/viewmodels/Workspace.hpp"

#include <QPointer>
#include <QTabBar>
#include <QTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace kestrel::shell
{

DocumentArea::DocumentArea(QQmlEngine* engine, viewmodels::Workspace* workspace, QWidget* parent)
	: QTabWidget(parent), m_engine(engine), m_workspace(workspace)
{
	setObjectName(u"documentArea"_s);
	setDocumentMode(true);
	setTabsClosable(true);
	setMovable(true);
	connect(this, &QTabWidget::tabCloseRequested, this, &DocumentArea::closeTabAt);
	connect(workspace->session(), &viewmodels::ProjectSession::projectReplaced, this,
		[this]
		{
			// Device editors belong to the old project; the group monitor and the welcome tab stay open.
			QStringList stale;
			for (const Document& document : m_documents)
			{
				if (document.id != kGroupMonitorId && document.id != kWelcomeId)
					stale.append(document.id);
			}
			for (const QString& id : stale)
				closeDocument(id);
			if (m_documents.empty())
				showWelcome();
		});
	showWelcome();
}

DocumentArea::~DocumentArea()
{
	closeAll();
}

QString DocumentArea::deviceDocumentId(model::Id<model::Device> device)
{
	return u"doc.device."_s + model::EntityRef(device).toString();
}

std::vector<DocumentArea::Document>::iterator DocumentArea::findDocument(const QString& id)
{
	return std::find_if(m_documents.begin(), m_documents.end(), [&](const Document& d) { return d.id == id; });
}

QmlHost* DocumentArea::host(const QString& id) const
{
	const auto it = std::find_if(m_documents.begin(), m_documents.end(), [&](const Document& d) { return d.id == id; });
	return it == m_documents.end() ? nullptr : it->host;
}

QStringList DocumentArea::documentIds() const
{
	QStringList ids;
	for (int i = 0; i < count(); ++i)
	{
		ids.append(widget(i)->objectName());
	}
	return ids;
}

void DocumentArea::addDocument(Document document, const QString& title)
{
	document.host->setObjectName(document.id);
	const int index = addTab(document.host, title);
	m_documents.push_back(std::move(document));
	setCurrentIndex(index);
}

void DocumentArea::showWelcome()
{
	if (const auto it = findDocument(kWelcomeId); it != m_documents.end())
	{
		setCurrentWidget(it->host);
		return;
	}
	auto* welcome = m_workspace->welcome();
	auto* host = new QmlHost(m_engine, u"Kestrel.Views"_s, u"Welcome"_s,
		{{u"vm"_s, QVariant::fromValue<QObject*>(welcome)}}, this);
	addDocument({kWelcomeId, host, nullptr}, tr("Welcome"));
}

void DocumentArea::openDevice(model::Id<model::Device> device)
{
	const QString id = deviceDocumentId(device);
	if (const auto it = findDocument(id); it != m_documents.end())
	{
		setCurrentWidget(it->host);
		return;
	}
	std::unique_ptr<viewmodels::DeviceEditorVM> vm = m_workspace->createDeviceEditor(device);
	if (!vm->exists())
	{
		return;
	}
	auto* host = new QmlHost(m_engine, u"Kestrel.Views"_s, u"DeviceEditor"_s,
		{{u"vm"_s, QVariant::fromValue<QObject*>(vm.get())}}, this);
	QPointer<QmlHost> guard(host);
	connect(vm.get(), &viewmodels::DeviceEditorVM::deviceChanged, this,
		[this, guard, editor = vm.get()]
		{
			if (guard)
				setTabText(indexOf(guard), editor->title());
		});
	// Deferred: the signal comes from inside a ChangeSet delivery, and the editor must not die in it.
	connect(vm.get(), &viewmodels::DeviceEditorVM::deviceRemoved, this,
		[this, id] { QTimer::singleShot(0, this, [this, id] { closeDocument(id); }); });
	const QString title = vm->title();
	addDocument({id, host, std::move(vm)}, title);
	closeDocument(kWelcomeId);
}

void DocumentArea::openGroupMonitor()
{
	if (const auto it = findDocument(kGroupMonitorId); it != m_documents.end())
	{
		setCurrentWidget(it->host);
		return;
	}
	std::unique_ptr<viewmodels::GroupMonitorVM> vm = m_workspace->createGroupMonitor();
	auto* host = new QmlHost(m_engine, u"Kestrel.Views"_s, u"GroupMonitor"_s,
		{{u"vm"_s, QVariant::fromValue<QObject*>(vm.get())}}, this);
	addDocument({kGroupMonitorId, host, std::move(vm)}, tr("Group Monitor"));
	closeDocument(kWelcomeId);
}

void DocumentArea::closeTabAt(int index)
{
	if (QWidget* page = widget(index))
	{
		closeDocument(page->objectName());
	}
}

void DocumentArea::closeDocument(const QString& id)
{
	const auto it = findDocument(id);
	if (it == m_documents.end())
	{
		return;
	}
	Document document = std::move(*it);
	m_documents.erase(it);
	removeTab(indexOf(document.host));
	delete document.host;       // host first …
	document.viewModel.reset(); // … then its view model
	if (m_documents.empty() && id != kWelcomeId)
	{
		showWelcome();
	}
}

void DocumentArea::closeAll()
{
	while (!m_documents.empty())
	{
		Document document = std::move(m_documents.back());
		m_documents.pop_back();
		removeTab(indexOf(document.host));
		delete document.host;
		document.viewModel.reset();
	}
}

} // namespace kestrel::shell
