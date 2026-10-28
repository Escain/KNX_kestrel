#include "kestrel/viewmodels/CommissioningVM.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/viewmodels/Format.hpp"

#include <QPointer>
#include <QTime>

#include <utility>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

QString CommissioningVM::noPlannerText()
{
	return tr("The configuration generator is not available yet: the download cannot be prepared.");
}

CommissioningVM::CommissioningVM(const Services& services, app::BusSession* bus, QObject* parent)
	: PanelVM(services, u"commissioning"_s, parent), m_bus(bus)
{
	m_pool.setMaxThreadCount(1);
	connect(m_bus, &app::BusSession::stateChanged, this, &CommissioningVM::connectedChanged);
	connect(selection(), &SelectionService::selectionChanged, this, &CommissioningVM::targetChanged);
	connect(session(), &ProjectSession::changed, this, &CommissioningVM::targetChanged);
	connect(session(), &ProjectSession::projectReplaced, this, &CommissioningVM::targetChanged);
}

CommissioningVM::~CommissioningVM()
{
	// A running planner posts its result to this object; wait so it never outlives the planner's captures.
	m_pool.waitForDone();
}

void CommissioningVM::setDownloadPlanner(DownloadPlanner planner)
{
	m_planner = std::move(planner);
	Q_EMIT plannerChanged();
}

bool CommissioningVM::isCancellable() const
{
	return m_kind == u"programAddress"_s || m_kind == u"download"_s;
}

std::optional<model::Id<model::Device>> CommissioningVM::targetDevice() const
{
	const auto ref = selection()->current();
	if (!ref || !project().contains(*ref))
		return std::nullopt;
	if (ref->is<model::Device>())
		return ref->as<model::Device>();
	if (ref->is<model::ComObject>())
		return project().comObjects().at(ref->as<model::ComObject>()).device;
	return std::nullopt;
}

QString CommissioningVM::deviceRef() const
{
	const auto device = targetDevice();
	return device ? model::EntityRef(*device).toString() : QString();
}

QString CommissioningVM::deviceLabel() const
{
	const auto device = targetDevice();
	return device ? entityLabel(project(), *device) : QString();
}

QString CommissioningVM::deviceStatus() const
{
	const auto device = targetDevice();
	if (!device)
		return {};
	const model::DeviceState& state = project().deviceState(*device);
	const model::Device& entity = project().devices().at(*device);
	QStringList parts;
	parts.append(state.iaProgrammed ? tr("Address programmed") : tr("Address not programmed"));
	if (!state.serial.isEmpty())
		parts.append(tr("Serial %1").arg(state.serial));
	if (entity.appProgramRef.isEmpty())
		parts.append(tr("No application program"));
	else
		parts.append(state.downloadedHash.isEmpty() ? tr("Application not downloaded") : tr("Application downloaded"));
	return parts.join(u" · "_s);
}

bool CommissioningVM::isConnected() const
{
	return m_bus->isConnected();
}

std::optional<knx::IndividualAddress> CommissioningVM::addressOf(const QString& deviceRef)
{
	const auto ref = parseRef(deviceRef);
	if (!ref)
		return std::nullopt;
	if (!ref->is<model::Device>())
	{
		setError(tr("Select a device."));
		return std::nullopt;
	}
	const auto address = project().individualAddress(ref->as<model::Device>());
	if (!address)
		setError(tr("The device has no individual address yet."));
	return address;
}

bool CommissioningVM::canStart(const QString& deviceRef, std::optional<model::Id<model::Device>>& device,
	std::optional<knx::IndividualAddress>& address)
{
	if (isOperationActive())
	{
		setError(tr("Another bus operation is running."));
		return false;
	}
	if (!m_bus->isConnected())
	{
		setError(tr("Not connected to the bus."));
		return false;
	}
	const QString ref = deviceRef.isEmpty() ? this->deviceRef() : deviceRef;
	if (ref.isEmpty())
	{
		setError(tr("Select a device."));
		return false;
	}
	address = addressOf(ref);
	if (!address)
		return false;
	device = model::EntityRef::parse(ref)->as<model::Device>();
	clearError();
	return true;
}

void CommissioningVM::start(const QString& kind, const QString& text, int total)
{
	m_kind = kind;
	m_operation = text;
	m_cancelRequested = false;
	m_waitingForButton = false;
	m_progressValue = 0;
	m_progressTotal = total;
	Q_EMIT operationChanged();
	Q_EMIT operationStarted();
}

void CommissioningVM::setProgress(const QString& text, int value, int total)
{
	m_operation = text;
	m_progressValue = value;
	m_progressTotal = total;
	Q_EMIT operationChanged();
}

void CommissioningVM::finish(bool success, const QString& result)
{
	m_kind.clear();
	m_operation.clear();
	m_waitingForButton = false;
	m_cancelRequested = false;
	m_progressValue = 0;
	m_progressTotal = 0;
	m_lastResult = result;
	m_lastSucceeded = success;
	appendLog(result);
	Q_EMIT operationChanged();
	Q_EMIT targetChanged();
}

void CommissioningVM::appendLog(const QString& line)
{
	m_log.prepend(u"%1  %2"_s.arg(QTime::currentTime().toString(u"HH:mm:ss"_s), line));
	while (m_log.size() > kMaxLogLines)
		m_log.removeLast();
	Q_EMIT logChanged();
}

void CommissioningVM::clearLog()
{
	m_log.clear();
	Q_EMIT logChanged();
}

// --- Operations --------------------------------------------------------------------------------------------

bool CommissioningVM::programAddress(const QString& deviceRef)
{
	std::optional<model::Id<model::Device>> device;
	std::optional<knx::IndividualAddress> target;
	if (!canStart(deviceRef, device, target))
		return false;
	const QString address = QString::fromStdString(target->toString());
	start(u"programAddress"_s, tr("Programming %1: press the programming button of the device.").arg(address));
	m_waitingForButton = true;
	Q_EMIT operationChanged();
	QPointer<CommissioningVM> self(this);
	const model::Id<model::Device> id = *device;
	m_bus->programIndividualAddress(*target, {},
		[self, id, address](Result<knx::IndividualAddress> result)
		{
			if (!self)
				return;
			if (!result)
			{
				self->setError(tr("Programming %1 failed: %2").arg(address, errorText(result.error())));
				self->finish(false,
					result.error().code == ErrorCode::Cancelled ? tr("Programming %1 cancelled.").arg(address)
																: tr("Programming %1 failed.").arg(address));
				return;
			}
			if (self->project().devices().contains(id))
			{
				// A commissioning fact: dirty, but not undoable.
				model::DeviceState state = self->project().deviceState(id);
				state.iaProgrammed = true;
				self->project().recordState(id, state);
			}
			self->finish(true, tr("Individual address %1 programmed.").arg(address));
		},
		[self, address](knx::IaProgrammerState progress)
		{
			if (!self)
				return;
			switch (progress)
			{
				case knx::IaProgrammerState::WaitProgMode:
					self->m_waitingForButton = true;
					self->setProgress(tr("Programming %1: press the programming button of the device.").arg(address), 0, 0);
					break;
				case knx::IaProgrammerState::CheckAddress:
					self->m_waitingForButton = false;
					self->setProgress(tr("Programming %1: checking that the address is free…").arg(address), 1, 3);
					break;
				case knx::IaProgrammerState::Verify:
					self->m_waitingForButton = false;
					self->setProgress(tr("Programming %1: verifying…").arg(address), 2, 3);
					break;
				default: break;
			}
		});
	return true;
}

bool CommissioningVM::readDeviceInfo(const QString& deviceRef)
{
	std::optional<model::Id<model::Device>> device;
	std::optional<knx::IndividualAddress> target;
	if (!canStart(deviceRef, device, target))
		return false;
	const QString address = QString::fromStdString(target->toString());
	start(u"readInfo"_s, tr("Reading device info of %1…").arg(address));
	QPointer<CommissioningVM> self(this);
	const model::Id<model::Device> id = *device;
	m_bus->readDeviceInfo(*target,
		[self, id, address](Result<app::DeviceInfo> result)
		{
			if (!self)
				return;
			if (!result)
			{
				self->setError(tr("Reading %1 failed: %2").arg(address, errorText(result.error())));
				self->finish(false, tr("No device info from %1.").arg(address));
				return;
			}
			const QString serial = QString::fromStdString(result->serial.toString());
			if (self->project().devices().contains(id))
			{
				model::DeviceState state = self->project().deviceState(id);
				state.serial = serial;
				self->project().recordState(id, state);
			}
			self->finish(true,
				tr("%1: mask version %2, manufacturer %3, serial %4")
					.arg(address)
					.arg(result->maskVersion, 4, 16, QLatin1Char('0'))
					.arg(result->manufacturerId, 4, 16, QLatin1Char('0'))
					.arg(serial));
		});
	return true;
}

bool CommissioningVM::readProgrammingMode()
{
	if (isOperationActive())
	{
		setError(tr("Another bus operation is running."));
		return false;
	}
	if (!m_bus->isConnected())
	{
		setError(tr("Not connected to the bus."));
		return false;
	}
	clearError();
	start(u"programmingMode"_s, tr("Looking for devices in programming mode…"));
	QPointer<CommissioningVM> self(this);
	m_bus->readProgrammingMode(
		[self](Result<QList<knx::IndividualAddress>> result)
		{
			if (!self)
				return;
			if (!result)
			{
				self->setError(errorText(result.error()));
				self->finish(false, tr("Could not look for devices in programming mode."));
				return;
			}
			if (result->isEmpty())
			{
				self->finish(true, tr("No device is in programming mode."));
				return;
			}
			QStringList addresses;
			for (const knx::IndividualAddress& address : *result)
				addresses.append(QString::fromStdString(address.toString()));
			self->finish(true, tr("In programming mode: %1").arg(addresses.join(u", "_s)));
		});
	return true;
}

bool CommissioningVM::download(const QString& deviceRef)
{
	std::optional<model::Id<model::Device>> device;
	std::optional<knx::IndividualAddress> target;
	if (!canStart(deviceRef, device, target))
		return false;
	if (project().devices().at(*device).appProgramRef.isEmpty())
	{
		setError(tr("The device has no application program. Add devices from the catalog to download them."));
		return false;
	}
	const QString label = entityLabel(project(), *device);
	if (!m_planner)
	{
		setError(noPlannerText());
		m_lastResult = tr("Download to %1: %2").arg(label, noPlannerText());
		m_lastSucceeded = false;
		appendLog(m_lastResult);
		Q_EMIT operationChanged();
		return false;
	}
	start(u"download"_s, tr("Preparing the configuration of %1…").arg(label));
	// The planner works on a snapshot copy, off the GUI thread (specs/02 §3.2: workers use copies).
	auto snapshot = std::make_shared<const model::Project>(project());
	QPointer<CommissioningVM> self(this);
	const model::Id<model::Device> id = *device;
	const knx::IndividualAddress address = *target;
	DownloadPlanner planner = m_planner;
	m_pool.start(
		[self, planner, snapshot, id, address, label, receiver = this]
		{
			Result<download::Plan> plan = planner(*snapshot, id);
			QMetaObject::invokeMethod(
				receiver,
				[self, id, address, label, plan = std::move(plan)]() mutable
				{
					if (self)
						self->runDownload(id, address, label, std::move(plan));
				},
				Qt::QueuedConnection);
		});
	return true;
}

void CommissioningVM::runDownload(model::Id<model::Device> device, knx::IndividualAddress address, QString label,
	Result<download::Plan> plan)
{
	if (m_kind != u"download"_s)
		return;
	if (m_cancelRequested)
	{
		finish(false, tr("Download to %1 cancelled.").arg(label));
		return;
	}
	if (!plan)
	{
		setError(tr("Cannot prepare the download of %1: %2").arg(label, errorText(plan.error())));
		finish(false, tr("Download to %1 failed: the configuration could not be prepared.").arg(label));
		return;
	}
	if (!m_bus->isConnected())
	{
		setError(tr("Not connected to the bus."));
		finish(false, tr("Download to %1 failed: not connected.").arg(label));
		return;
	}
	const QString hash = planHash(*plan);
	const int total = static_cast<int>(plan->steps.size());
	setProgress(tr("Downloading to %1…").arg(label), 0, total);
	QPointer<CommissioningVM> self(this);
	m_bus->download(address, std::move(plan).value(),
		[self, device, label, hash, total](Status status)
		{
			if (!self)
				return;
			if (!status)
			{
				const bool cancelled = status.error().code == ErrorCode::Cancelled;
				if (!cancelled)
					self->setError(tr("Download to %1 failed: %2").arg(label, errorText(status.error())));
				self->finish(false, cancelled ? tr("Download to %1 cancelled.").arg(label)
											  : tr("Download to %1 failed.").arg(label));
				return;
			}
			if (self->project().devices().contains(device))
			{
				model::DeviceState state = self->project().deviceState(device);
				state.downloadedHash = hash;
				self->project().recordState(device, state);
			}
			self->finish(true, tr("Download to %1 completed (%2 steps).").arg(label).arg(total));
		},
		[self, label](std::size_t step, std::size_t steps, QString text)
		{
			if (!self)
				return;
			self->setProgress(tr("%1: step %2 of %3, %4").arg(label).arg(step + 1).arg(steps).arg(text),
				static_cast<int>(step), static_cast<int>(steps));
		});
}

void CommissioningVM::cancel()
{
	if (m_kind == u"programAddress"_s)
	{
		m_bus->cancelProgramming();
	}
	else if (m_kind == u"download"_s)
	{
		m_cancelRequested = true;
		m_bus->cancelDownload();
		setProgress(tr("Cancelling…"), m_progressValue, m_progressTotal);
	}
}

} // namespace kestrel::viewmodels
