#pragma once

// Per-device configuration status of catalog devices (FR-DEV-3; specs/08-gui.md §5), computed lazily and off the
// GUI thread:
// - "needs download": planHash() of the plan for the current configuration (the DownloadPlanner hook) compared
//   with DeviceState::downloadedHash;
// - linked com objects that are not active with the device's parameters (appconfig::evaluate): their links stay
//   in the project but are not downloaded. Reported as Problems (CO-101).
//
// Nothing runs until the first query (topology column, editor header, Problems). Then each change of the project
// restarts a short debounce; after it, a cheap fingerprint of every catalog device (application, parameters,
// com objects, links) selects the devices whose inputs changed, and one worker thread evaluates them on a snapshot
// copy of the project. Typing in the parameter editor therefore never waits for a plan. Until a new result
// arrives, the previous one is shown.

#include "kestrel/model/ChangeSet.hpp"
#include "kestrel/model/Entities.hpp"
#include "kestrel/model/Id.hpp"
#include "kestrel/validate/Validate.hpp"
#include "kestrel/viewmodels/ConfigurationHooks.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <QTimer>

#include <optional>
#include <vector>

namespace kestrel::viewmodels
{

class CatalogService;
class ProjectSession;

class DeviceStatusService : public QObject
{
	Q_OBJECT

public:
	enum class DownloadState
	{
		None,          ///< not a catalog device (or unknown device)
		NotDownloaded, ///< never downloaded by Kestrel (no downloadedHash)
		Pending,       ///< being computed, no earlier result
		UpToDate,      ///< the device has the current configuration
		NeedsDownload, ///< the configuration changed since the last download
		Unknown,       ///< no planner, or the plan cannot be built (see detail())
	};
	Q_ENUM(DownloadState)

	static constexpr int kDebounceMs = 250;
	/// Code of the "linked but inactive com object" diagnostic.
	static inline const QString kInactiveLinkedCode = QStringLiteral("CO-101");

	DeviceStatusService(ProjectSession* session, CatalogService* catalog, QObject* parent = nullptr);
	~DeviceStatusService() override;

	void setPlanner(DownloadPlanner planner);

	/// Starts tracking (also done by every query below).
	void activate();
	bool isActive() const { return m_active; }
	/// True while a check is scheduled or computing.
	bool isBusy() const;
	/// Processes events until idle (tests). False on timeout.
	bool waitForIdle(int timeoutMs = 30000);

	DownloadState downloadState(model::Id<model::Device> device);
	/// Short text for the Topology column: "", "Not loaded", "…", "Loaded", "Modified", "?".
	QString shortText(model::Id<model::Device> device);
	/// Sentence for the editor header and the commissioning dock.
	QString describe(model::Id<model::Device> device);
	/// The plan error, if any.
	QString detail(model::Id<model::Device> device) const;
	/// Linked com objects of the device that are not active (last result; links re-checked now).
	std::vector<model::Id<model::ComObject>> inactiveLinked(model::Id<model::Device> device);
	/// CO-101 warnings for every catalog device.
	std::vector<validate::Diagnostic> diagnostics();

Q_SIGNALS:
	/// The status of a device may have changed (views refresh its row).
	void deviceStatusChanged(kestrel::model::Id<kestrel::model::Device> device);
	/// diagnostics() changed.
	void diagnosticsChanged();
	void idle();

private:
	struct Entry
	{
		QByteArray fingerprint;      ///< inputs as of the last check
		QByteArray computedFrom;     ///< inputs of the stored result
		bool computed = false;
		bool planComputed = false;
		QString planHash;
		QString planError;
		std::vector<model::Id<model::ComObject>> inactive;
	};
	struct Job
	{
		model::Id<model::Device> device;
		QByteArray fingerprint;
		bool needPlan = false;
	};
	struct Outcome
	{
		QString planHash;
		QString planError;
		bool planComputed = false;
		std::vector<model::Id<model::ComObject>> inactive;
	};

	void onChanged(const model::ChangeSet& changeSet);
	void reset();
	void schedule(int delayMs = kDebounceMs);
	void check();
	void startBatch();
	void onResult(const Job& job, Outcome outcome);
	QByteArray fingerprint(const model::Device& device) const;
	static Outcome compute(const model::Project& project, const Job& job, CatalogService* catalog, const DownloadPlanner& planner);

	ProjectSession* m_session;
	CatalogService* m_catalog;
	DownloadPlanner m_planner;
	bool m_active = false;
	QHash<quint64, Entry> m_entries;
	std::vector<Job> m_queue;
	bool m_running = false;
	int m_generation = 0; ///< bumped by reset(): results of older batches are dropped
	QTimer m_timer;
	QThreadPool m_pool;
};

} // namespace kestrel::viewmodels
