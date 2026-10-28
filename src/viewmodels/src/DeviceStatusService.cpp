#include "kestrel/viewmodels/DeviceStatusService.hpp"

#include "kestrel/appconfig/Evaluator.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QPointer>

#include <algorithm>
#include <memory>
#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

DeviceStatusService::DeviceStatusService(ProjectSession* session, CatalogService* catalog, QObject* parent)
	: QObject(parent), m_session(session), m_catalog(catalog)
{
	m_pool.setMaxThreadCount(1);
	m_timer.setSingleShot(true);
	connect(&m_timer, &QTimer::timeout, this, &DeviceStatusService::check);
	connect(session, &ProjectSession::changed, this, &DeviceStatusService::onChanged);
	connect(session, &ProjectSession::projectReplaced, this, &DeviceStatusService::reset);
	// A catalog that changed (imported or re-indexed files) may change every result.
	connect(catalog, &CatalogService::entriesChanged, this, &DeviceStatusService::reset);
	connect(catalog, &CatalogService::busyChanged, this,
		[this]
		{
			if (m_active && !m_catalog->isBusy())
				schedule(0);
		});
}

DeviceStatusService::~DeviceStatusService()
{
	m_pool.waitForDone();
}

void DeviceStatusService::setPlanner(DownloadPlanner planner)
{
	m_planner = std::move(planner);
	reset();
}

void DeviceStatusService::activate()
{
	if (m_active)
		return;
	m_active = true;
	schedule(0);
}

bool DeviceStatusService::isBusy() const
{
	return m_timer.isActive() || m_running || !m_queue.empty();
}

bool DeviceStatusService::waitForIdle(int timeoutMs)
{
	const QDeadlineTimer deadline(timeoutMs);
	while (isBusy() || m_catalog->isBusy())
	{
		if (deadline.hasExpired())
			return false;
		m_pool.waitForDone(5);
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	}
	return true;
}

void DeviceStatusService::reset()
{
	++m_generation;
	m_entries.clear();
	m_queue.clear();
	if (!m_active)
		return;
	schedule(0);
	for (const model::Device& device : m_session->project().devices())
		Q_EMIT deviceStatusChanged(device.id);
	Q_EMIT diagnosticsChanged();
}

void DeviceStatusService::onChanged(const model::ChangeSet& changeSet)
{
	if (!m_active)
		return;
	if (changeSet.stateOnly)
	{
		// A download (or another commissioning fact): the comparison changes, the inputs do not.
		for (const model::Change& change : changeSet.changes)
		{
			if (change.ref.is<model::Device>())
				Q_EMIT deviceStatusChanged(change.ref.as<model::Device>());
		}
	}
	schedule();
}

void DeviceStatusService::schedule(int delayMs)
{
	if (!m_active)
		return;
	m_timer.start(delayMs);
}

QByteArray DeviceStatusService::fingerprint(const model::Device& device) const
{
	// Everything a plan or an evaluation depends on, cheaply: application, parameters, com objects and links.
	const model::Project& project = m_session->project();
	QCryptographicHash hash(QCryptographicHash::Sha1);
	auto add = [&hash](const QString& text)
	{
		hash.addData(text.toUtf8());
		hash.addData(QByteArrayView("\0", 1));
	};
	add(device.appProgramRef);
	for (const auto& [ref, value] : device.params)
	{
		add(ref);
		add(value);
	}
	std::vector<std::tuple<QString, unsigned, bool, std::uint16_t>> links;
	for (const auto objectId : project.comObjectsOf(device.id))
	{
		const model::ComObject& object = project.comObjects().at(objectId);
		links.emplace_back(object.catalogRef, object.number, false, std::uint16_t{0});
		for (const auto linkId : project.linksOfComObject(objectId))
		{
			const model::Link& link = project.links().at(linkId);
			links.emplace_back(object.catalogRef, object.number, link.sending, project.groupAddresses().at(link.groupAddress).raw);
		}
	}
	std::sort(links.begin(), links.end());
	for (const auto& [ref, number, sending, raw] : links)
		add(u"%1/%2/%3/%4"_s.arg(ref).arg(number).arg(sending ? 1 : 0).arg(raw));
	return hash.result();
}

void DeviceStatusService::check()
{
	if (!m_active)
		return;
	const model::Project& project = m_session->project();
	QHash<quint64, Entry> kept;
	bool anyCatalogDevice = false;
	std::vector<Job> jobs;
	for (const model::Device& device : project.devices())
	{
		if (device.appProgramRef.isEmpty())
			continue;
		anyCatalogDevice = true;
		Entry entry = m_entries.value(device.id.value());
		entry.fingerprint = fingerprint(device);
		const bool needPlan = m_planner && !project.deviceState(device.id).downloadedHash.isEmpty();
		if (!entry.computed || entry.computedFrom != entry.fingerprint || (needPlan && !entry.planComputed))
			jobs.push_back({device.id, entry.fingerprint, needPlan});
		kept.insert(device.id.value(), std::move(entry));
	}
	const bool removed = kept.size() != m_entries.size();
	m_entries = std::move(kept);
	if (removed)
		Q_EMIT diagnosticsChanged();
	if (anyCatalogDevice)
	{
		m_catalog->ensureScanned();
		if (m_catalog->isBusy())
			return; // busyChanged schedules the next check
	}
	m_queue = std::move(jobs);
	if (!m_running)
		startBatch();
	if (!isBusy())
		Q_EMIT idle();
}

void DeviceStatusService::startBatch()
{
	if (m_queue.empty())
		return;
	m_running = true;
	std::vector<Job> jobs = std::move(m_queue);
	m_queue.clear();
	// One snapshot for the batch; the worker is its only user (lazy indexes are not thread-safe).
	auto snapshot = std::make_shared<const model::Project>(m_session->project());
	CatalogService* catalog = m_catalog;
	const DownloadPlanner planner = m_planner;
	const int generation = m_generation;
	m_pool.start(
		[this, jobs = std::move(jobs), snapshot, catalog, planner, generation]
		{
			for (const Job& job : jobs)
			{
				Outcome outcome = compute(*snapshot, job, catalog, planner);
				QMetaObject::invokeMethod(
					this,
					[this, job, outcome = std::move(outcome), generation]() mutable
					{
						if (generation == m_generation)
							onResult(job, std::move(outcome));
					},
					Qt::QueuedConnection);
			}
			QMetaObject::invokeMethod(
				this,
				[this]
				{
					m_running = false;
					if (!m_queue.empty())
						startBatch();
					else if (!isBusy())
						Q_EMIT idle();
				},
				Qt::QueuedConnection);
		});
}

DeviceStatusService::Outcome DeviceStatusService::compute(
	const model::Project& project, const Job& job, CatalogService* catalog, const DownloadPlanner& planner)
{
	Outcome outcome;
	const model::Device* device = project.devices().find(job.device);
	if (!device)
		return outcome;
	if (job.needPlan && planner)
	{
		outcome.planComputed = true;
		const auto plan = planner(project, job.device);
		if (plan)
			outcome.planHash = planHash(*plan);
		else
			outcome.planError = errorText(plan.error());
	}
	const auto database = catalog->databaseForProgram(device->appProgramRef);
	const catalog::ApplicationProgram* program =
		database ? database->database.findApplicationProgram(device->appProgramRef) : nullptr;
	if (!program)
		return outcome;
	const auto evaluation = appconfig::evaluate(*program, parameterValues(*program, *device));
	if (!evaluation)
		return outcome;
	for (const auto objectId : project.comObjectsOf(job.device))
	{
		const model::ComObject& object = project.comObjects().at(objectId);
		if (!object.catalogRef.isEmpty() && !project.linksOfComObject(objectId).empty()
			&& !evaluation->isComObjectRefActive(object.catalogRef))
		{
			outcome.inactive.push_back(objectId);
		}
	}
	return outcome;
}

void DeviceStatusService::onResult(const Job& job, Outcome outcome)
{
	if (!m_entries.contains(job.device.value()))
		return;
	Entry& entry = m_entries[job.device.value()];
	const bool diagnosticsDiffer = entry.inactive != outcome.inactive;
	entry.computed = true;
	entry.computedFrom = job.fingerprint;
	entry.planComputed = outcome.planComputed;
	entry.planHash = outcome.planHash;
	entry.planError = outcome.planError;
	entry.inactive = std::move(outcome.inactive);
	Q_EMIT deviceStatusChanged(job.device);
	if (diagnosticsDiffer)
		Q_EMIT diagnosticsChanged();
}

// --- Queries -----------------------------------------------------------------------------------------------

DeviceStatusService::DownloadState DeviceStatusService::downloadState(model::Id<model::Device> device)
{
	activate();
	const model::Project& project = m_session->project();
	const model::Device* entity = project.devices().find(device);
	if (!entity || entity->appProgramRef.isEmpty())
		return DownloadState::None;
	const QString& downloaded = project.deviceState(device).downloadedHash;
	if (downloaded.isEmpty())
		return DownloadState::NotDownloaded;
	if (!m_planner)
		return DownloadState::Unknown;
	const auto it = m_entries.constFind(device.value());
	if (it == m_entries.cend() || !it->planComputed)
		return DownloadState::Pending;
	if (!it->planError.isEmpty())
		return DownloadState::Unknown;
	return it->planHash == downloaded ? DownloadState::UpToDate : DownloadState::NeedsDownload;
}

QString DeviceStatusService::shortText(model::Id<model::Device> device)
{
	switch (downloadState(device))
	{
		case DownloadState::None: return {};
		case DownloadState::NotDownloaded: return tr("Not loaded");
		case DownloadState::Pending: return tr("…");
		case DownloadState::UpToDate: return tr("Loaded");
		case DownloadState::NeedsDownload: return tr("Modified");
		case DownloadState::Unknown: return tr("?");
	}
	return {};
}

QString DeviceStatusService::describe(model::Id<model::Device> device)
{
	switch (downloadState(device))
	{
		case DownloadState::None: return {};
		case DownloadState::NotDownloaded: return tr("Application not downloaded yet");
		case DownloadState::Pending: return tr("Checking the configuration…");
		case DownloadState::UpToDate: return tr("Application downloaded, up to date");
		case DownloadState::NeedsDownload: return tr("Configuration changed since the last download: download needed");
		case DownloadState::Unknown:
		{
			const QString why = detail(device);
			return why.isEmpty() ? tr("Application downloaded (cannot check for changes)")
								 : tr("Application downloaded (cannot check for changes: %1)").arg(why);
		}
	}
	return {};
}

QString DeviceStatusService::detail(model::Id<model::Device> device) const
{
	const auto it = m_entries.constFind(device.value());
	return it == m_entries.cend() ? QString() : it->planError;
}

std::vector<model::Id<model::ComObject>> DeviceStatusService::inactiveLinked(model::Id<model::Device> device)
{
	activate();
	std::vector<model::Id<model::ComObject>> result;
	const auto it = m_entries.constFind(device.value());
	if (it == m_entries.cend())
		return result;
	const model::Project& project = m_session->project();
	for (const auto object : it->inactive)
	{
		// Unlinked or removed since the last result: no longer a problem.
		if (project.comObjects().contains(object) && project.comObjects().at(object).device == device
			&& !project.linksOfComObject(object).empty())
		{
			result.push_back(object);
		}
	}
	return result;
}

std::vector<validate::Diagnostic> DeviceStatusService::diagnostics()
{
	activate();
	std::vector<validate::Diagnostic> result;
	for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
	{
		for (const auto object : inactiveLinked(model::Id<model::Device>(it.key())))
		{
			result.push_back({kInactiveLinkedCode, validate::Severity::Warning,
				QStringLiteral("Linked com object is not active with the device's parameters: its links are not downloaded"),
				{model::EntityRef(object)}});
		}
	}
	return result;
}

} // namespace kestrel::viewmodels
