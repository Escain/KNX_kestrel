#pragma once

// Commissioning dock (FR-COM-1/2/4; specs/08-gui.md §4): bus operations on the selected device through
// app::BusSession, one at a time, with progress, cancel, the result and a log.
//
// - Program individual address (FR-COM-1): waits for the device in programming mode ("press the programming
//   button"), writes and verifies the address. Success records iaProgrammed.
// - Read device info (FR-COM-4): mask version, manufacturer, serial number. Success records the serial.
// - Download (FR-COM-2): the injected DownloadPlanner builds a download::Plan from a snapshot copy of the project
//   on a worker thread, then BusSession::download() runs it with step progress. Success records
//   downloadedHash = planHash(plan). Without a planner the download reports kNoPlanner.
// - Read configuration (System B): reads the segment references, sizes and contents of interface objects 1..4
//   and PID_PROGRAM_VERSION, then appconfig::importSystemBImages() replaces the device's parameters, com objects
//   and links (one undo step). Cancel stops at the next bus answer (a running management request completes).
// - Devices in programming mode: lists the individual addresses that answer A_IndividualAddress_Read.
//
// Commissioning facts are written with Project::recordState: the project becomes dirty, but nothing is undoable.
// Every BusSession callback is guarded and ignored once this view model is gone.

#include "kestrel/viewmodels/ConfigurationHooks.hpp"
#include "kestrel/viewmodels/PanelVM.hpp"

#include "kestrel/knx/Address.hpp"

#include <QStringList>
#include <QThreadPool>

#include <map>
#include <memory>
#include <optional>

namespace kestrel::app
{
class BusSession;
}

namespace kestrel::viewmodels
{

class CatalogService;
class DeviceStatusService;

class CommissioningVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QString device READ deviceRef NOTIFY targetChanged)
	Q_PROPERTY(QString deviceLabel READ deviceLabel NOTIFY targetChanged)
	Q_PROPERTY(QString deviceStatus READ deviceStatus NOTIFY targetChanged)
	Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
	Q_PROPERTY(bool operationActive READ isOperationActive NOTIFY operationChanged)
	Q_PROPERTY(QString operationKind READ operationKind NOTIFY operationChanged)
	Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
	Q_PROPERTY(bool cancellable READ isCancellable NOTIFY operationChanged)
	Q_PROPERTY(bool waitingForButton READ isWaitingForButton NOTIFY operationChanged)
	Q_PROPERTY(int progressValue READ progressValue NOTIFY operationChanged)
	Q_PROPERTY(int progressTotal READ progressTotal NOTIFY operationChanged)
	Q_PROPERTY(QString lastResult READ lastResult NOTIFY operationChanged)
	Q_PROPERTY(bool lastSucceeded READ lastSucceeded NOTIFY operationChanged)
	Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
	Q_PROPERTY(bool plannerAvailable READ isPlannerAvailable NOTIFY plannerChanged)

public:
	static constexpr int kMaxLogLines = 200;
	/// Download result while no planner is injected.
	static QString noPlannerText();

	CommissioningVM(const Services& services, app::BusSession* bus, QObject* parent = nullptr);
	~CommissioningVM() override;

	/// Injects the plan generator (kestrel::appconfig). An empty function removes it.
	void setDownloadPlanner(DownloadPlanner planner);
	/// Product databases for "read configuration" (and the device status text).
	void setCatalog(CatalogService* catalog) { m_catalog = catalog; }
	/// Optional: "needs download" in deviceStatus().
	void setDeviceStatus(DeviceStatusService* status);
	bool isPlannerAvailable() const { return static_cast<bool>(m_planner); }

	/// The device the actions apply to: the selected device, or the device of a selected group object.
	std::optional<model::Id<model::Device>> targetDevice() const;
	QString deviceRef() const;
	QString deviceLabel() const;
	/// Commissioning facts of the target device ("Address programmed · Serial …").
	QString deviceStatus() const;
	bool isConnected() const;
	bool isOperationActive() const { return !m_kind.isEmpty(); }
	/// "", "programAddress", "readInfo", "download", "readConfiguration", "programmingMode"
	QString operationKind() const { return m_kind; }
	QString operation() const { return m_operation; }
	bool isCancellable() const;
	bool isWaitingForButton() const { return m_waitingForButton; }
	int progressValue() const { return m_progressValue; }
	/// 0 = indeterminate.
	int progressTotal() const { return m_progressTotal; }
	QString lastResult() const { return m_lastResult; }
	bool lastSucceeded() const { return m_lastSucceeded; }
	/// Newest first: "12:03:04  Individual address 1.1.5 programmed."
	QStringList log() const { return m_log; }

	/// Individual address of a device ref; nullopt (and lastError) if it has none.
	std::optional<knx::IndividualAddress> addressOf(const QString& deviceRef);

	/// FR-COM-1. `deviceRef` empty = target device.
	Q_INVOKABLE bool programAddress(const QString& deviceRef = {});
	/// FR-COM-4. `deviceRef` empty = target device.
	Q_INVOKABLE bool readDeviceInfo(const QString& deviceRef = {});
	/// FR-COM-2. `deviceRef` empty = target device.
	Q_INVOKABLE bool download(const QString& deviceRef = {});
	/// Reads the configuration of a System B catalog device into the project. `deviceRef` empty = target device.
	Q_INVOKABLE bool readConfiguration(const QString& deviceRef = {});
	/// Lists the devices in programming mode.
	Q_INVOKABLE bool readProgrammingMode();
	/// Cancels IA programming, a download (also while the plan is being prepared) or a configuration read.
	Q_INVOKABLE void cancel();
	Q_INVOKABLE void clearLog();

Q_SIGNALS:
	void targetChanged();
	void connectedChanged();
	void operationChanged();
	/// An operation started (the shell shows the commissioning dock).
	void operationStarted();
	void logChanged();
	void plannerChanged();

private:
	bool canStart(const QString& deviceRef, std::optional<model::Id<model::Device>>& device,
		std::optional<knx::IndividualAddress>& address);
	void start(const QString& kind, const QString& text, int total = 0);
	void setProgress(const QString& text, int value, int total);
	void finish(bool success, const QString& result);
	void appendLog(const QString& line);
	void runDownload(model::Id<model::Device> device, knx::IndividualAddress address, QString label,
		Result<download::Plan> plan);
	struct ReadContext;
	void readProperties(std::shared_ptr<ReadContext> context);
	void readSegments(std::shared_ptr<ReadContext> context);
	void importImages(std::shared_ptr<ReadContext> context, std::map<std::uint8_t, Bytes> images);
	/// True (and finished as cancelled) if the user cancelled the running read.
	bool stopIfCancelled(const QString& label);

	app::BusSession* m_bus;
	CatalogService* m_catalog = nullptr;
	DeviceStatusService* m_status = nullptr;
	DownloadPlanner m_planner;
	QThreadPool m_pool; ///< runs the planner
	QString m_kind;
	QString m_operation;
	bool m_waitingForButton = false;
	bool m_cancelRequested = false;
	int m_progressValue = 0;
	int m_progressTotal = 0;
	QString m_lastResult;
	bool m_lastSucceeded = false;
	QStringList m_log;
};

} // namespace kestrel::viewmodels
