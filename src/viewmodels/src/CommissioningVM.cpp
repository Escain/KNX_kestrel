#include "kestrel/viewmodels/CommissioningVM.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/appconfig/Images.hpp"
#include "kestrel/appconfig/Import.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/DeviceStatusService.hpp"
#include "kestrel/viewmodels/Format.hpp"

#include <QPointer>
#include <QTime>

#include <array>
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
	return m_kind == u"programAddress"_s || m_kind == u"download"_s || m_kind == u"readConfiguration"_s;
}

void CommissioningVM::setDeviceStatus(DeviceStatusService* status)
{
	m_status = status;
	if (m_status)
	{
		connect(m_status, &DeviceStatusService::deviceStatusChanged, this,
			[this](model::Id<model::Device> device)
			{
				if (targetDevice() == device)
					Q_EMIT targetChanged();
			});
	}
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
	else if (m_status)
		parts.append(m_status->describe(*device));
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

// --- Read configuration (System B) -----------------------------------------------------------------------

namespace
{

std::uint32_t beValue(const QByteArray& bytes, qsizetype count)
{
	std::uint32_t value = 0;
	for (qsizetype i = 0; i < std::min(count, bytes.size()); ++i)
		value = (value << 8) | static_cast<std::uint8_t>(bytes[i]);
	return value;
}

} // namespace

struct CommissioningVM::ReadContext
{
	model::Id<model::Device> device;
	knx::IndividualAddress address;
	QString label;
	QString programId;
	std::shared_ptr<const catalog::LoadedDatabase> database;
	const catalog::ApplicationProgram* app = nullptr;
	std::array<std::uint32_t, 4> where{};
	std::array<std::uint32_t, 4> size{};
	QStringList notes;
};

bool CommissioningVM::stopIfCancelled(const QString& label)
{
	if (m_kind != u"readConfiguration"_s)
		return true; // finished meanwhile (cannot happen: one operation at a time)
	if (!m_cancelRequested)
		return false;
	finish(false, tr("Reading the configuration of %1 cancelled.").arg(label));
	return true;
}

bool CommissioningVM::readConfiguration(const QString& deviceRef)
{
	std::optional<model::Id<model::Device>> device;
	std::optional<knx::IndividualAddress> target;
	if (!canStart(deviceRef, device, target))
		return false;
	const QString programId = project().devices().at(*device).appProgramRef;
	if (programId.isEmpty())
	{
		setError(tr("The device has no application program. Add it from the catalog first."));
		return false;
	}
	if (!m_catalog)
	{
		setError(tr("No product catalog is available."));
		return false;
	}
	auto context = std::make_shared<ReadContext>();
	context->device = *device;
	context->address = *target;
	context->label = entityLabel(project(), *device);
	context->programId = programId;
	start(u"readConfiguration"_s, tr("%1: reading the product file…").arg(context->label), 4);
	// The product file may need parsing (about 0.7 s for real files): off the GUI thread.
	QPointer<CommissioningVM> self(this);
	CatalogService* catalog = m_catalog;
	m_pool.start(
		[self, catalog, context, receiver = this]
		{
			auto database = catalog->databaseForProgram(context->programId);
			QMetaObject::invokeMethod(
				receiver,
				[self, context, database]
				{
					if (!self || self->stopIfCancelled(context->label))
						return;
					context->database = database;
					context->app = database ? database->database.findApplicationProgram(context->programId) : nullptr;
					if (!context->app)
					{
						self->setError(tr("The application program %1 is not in the catalog.").arg(context->programId));
						self->finish(false, tr("Reading the configuration of %1 failed.").arg(context->label));
						return;
					}
					if (context->app->maskVersion != u"MV-07B0"_s)
					{
						self->setError(tr("Only System B (MV-07B0) devices can be read yet; %1 uses %2.")
										   .arg(context->label, context->app->maskVersion));
						self->finish(false, tr("Reading the configuration of %1 failed.").arg(context->label));
						return;
					}
					self->readProperties(context);
				},
				Qt::QueuedConnection);
		});
	return true;
}

void CommissioningVM::readProperties(std::shared_ptr<ReadContext> context)
{
	// 1. Where and how large the segments are: PID_TABLE_REFERENCE (7) and PID_MCB_TABLE (27) of objects 1..4,
	//    and PID_PROGRAM_VERSION (13) of the application object.
	setProgress(tr("%1: reading the segment table…").arg(context->label), 1, 4);
	QList<app::ManagementRead> reads;
	for (std::uint8_t object = 1; object <= 4; ++object)
	{
		reads.append(app::ManagementRead::property(object, download::kPidTableReference));
		reads.append(app::ManagementRead::property(object, download::kPidMcbTable));
	}
	reads.append(app::ManagementRead::property(4, download::kPidProgramVersion));
	QPointer<CommissioningVM> self(this);
	m_bus->readManagement(context->address, reads,
		[self, context](Result<QList<Result<QByteArray>>> results)
		{
			if (!self || self->stopIfCancelled(context->label))
				return;
			const QString failed = tr("Reading the configuration of %1 failed.").arg(context->label);
			if (!results)
			{
				self->setError(errorText(results.error()));
				self->finish(false, failed);
				return;
			}
			const auto& r = *results;
			const catalog::ApplicationProgram& app = *context->app;
			const Bytes expected = appconfig::programVersion(context->database->database.manufacturerId, app);
			if (r[8] && r[8]->size() == 5 && expected.size() == 5)
			{
				const QByteArray& version = *r[8];
				for (int i = 0; i < 4; ++i)
				{
					if (static_cast<std::uint8_t>(version[i]) != expected[static_cast<std::size_t>(i)])
					{
						self->setError(tr("The device runs another application (%1), not %2.")
										   .arg(QString::fromLatin1(version.toHex(' ')), app.id));
						self->finish(false, failed);
						return;
					}
				}
				if (static_cast<std::uint8_t>(version[4]) != expected[4])
				{
					context->notes.append(tr("The device has application version %1, the product file %2.")
											  .arg(static_cast<std::uint8_t>(version[4]), 2, 16, QLatin1Char('0'))
											  .arg(expected[4], 2, 16, QLatin1Char('0')));
				}
			}
			else
			{
				context->notes.append(tr("The device did not report its application version."));
			}
			// Sizes: PID_MCB_TABLE when the load procedure wrote one; otherwise the table header (u16 count) for
			// objects 1..3 and the product file's segment size for the parameters.
			QList<app::ManagementRead> headers;
			QList<int> headerObjects;
			for (int object = 0; object < 4; ++object)
			{
				const auto o = static_cast<std::size_t>(object);
				if (!r[2 * object])
				{
					self->setError(tr("Object %1: the table reference is not readable.").arg(object + 1));
					self->finish(false, failed);
					return;
				}
				context->where[o] = beValue(*r[2 * object], 4);
				context->size[o] = r[2 * object + 1] ? beValue(*r[2 * object + 1], 4) : 0;
				if (context->size[o] != 0)
					continue;
				if (object == 3)
				{
					for (const catalog::RelativeSegment& segment : app.relativeSegments)
					{
						if (segment.loadStateMachine == 4)
							context->size[3] = segment.size;
					}
				}
				else
				{
					headers.append(app::ManagementRead::memory(static_cast<std::uint16_t>(context->where[o]), 2));
					headerObjects.append(object);
				}
			}
			if (headers.isEmpty())
			{
				self->readSegments(context);
				return;
			}
			self->setProgress(tr("%1: reading the table sizes…").arg(context->label), 2, 4);
			QPointer<CommissioningVM> again = self;
			self->m_bus->readManagement(context->address, headers,
				[again, context, headerObjects, failed](Result<QList<Result<QByteArray>>> counts)
				{
					if (!again || again->stopIfCancelled(context->label))
						return;
					if (!counts)
					{
						again->setError(errorText(counts.error()));
						again->finish(false, failed);
						return;
					}
					for (qsizetype i = 0; i < headerObjects.size(); ++i)
					{
						if (!(*counts)[i])
						{
							again->setError(errorText((*counts)[i].error()));
							again->finish(false, failed);
							return;
						}
						const int object = headerObjects[i];
						const std::uint32_t entries = beValue(*(*counts)[i], 2);
						const std::uint32_t entrySize = object == 1 ? 4u : 2u; // association pairs: 4 octets
						context->size[static_cast<std::size_t>(object)] = 2 + entries * entrySize;
					}
					again->readSegments(context);
				});
		});
}

void CommissioningVM::readSegments(std::shared_ptr<ReadContext> context)
{
	// 2. The segments themselves.
	const QString failed = tr("Reading the configuration of %1 failed.").arg(context->label);
	QList<app::ManagementRead> memory;
	for (std::size_t object = 0; object < 4; ++object)
	{
		if (context->where[object] + context->size[object] > 0x10000 || context->size[object] == 0)
		{
			setError(tr("Object %1: segment at 0x%2 with %3 bytes cannot be read.")
						 .arg(object + 1)
						 .arg(context->where[object], 0, 16)
						 .arg(context->size[object]));
			finish(false, failed);
			return;
		}
		memory.append(app::ManagementRead::memory(
			static_cast<std::uint16_t>(context->where[object]), static_cast<std::uint16_t>(context->size[object])));
	}
	setProgress(tr("%1: reading the memory…").arg(context->label), 3, 4);
	QPointer<CommissioningVM> self(this);
	m_bus->readManagement(context->address, memory,
		[self, context, failed](Result<QList<Result<QByteArray>>> data)
		{
			if (!self || self->stopIfCancelled(context->label))
				return;
			if (!data)
			{
				self->setError(errorText(data.error()));
				self->finish(false, failed);
				return;
			}
			std::map<std::uint8_t, Bytes> images;
			for (int object = 0; object < 4; ++object)
			{
				if (!(*data)[object])
				{
					self->setError(tr("Object %1: %2").arg(object + 1).arg(errorText((*data)[object].error())));
					self->finish(false, failed);
					return;
				}
				const QByteArray& bytes = *(*data)[object];
				images[static_cast<std::uint8_t>(object + 1)] = Bytes(bytes.begin(), bytes.end());
			}
			self->importImages(context, std::move(images));
		});
}

void CommissioningVM::importImages(std::shared_ptr<ReadContext> context, std::map<std::uint8_t, Bytes> images)
{
	// 3. Into the project, on the GUI thread: one undo step.
	if (!project().devices().contains(context->device)
		|| project().devices().at(context->device).appProgramRef != context->programId)
	{
		setError(tr("The device changed while its configuration was read."));
		finish(false, tr("Reading the configuration of %1 failed.").arg(context->label));
		return;
	}
	setProgress(tr("%1: applying the configuration…").arg(context->label), 4, 4);
	auto warnings = appconfig::importSystemBImages(
		project(), context->device, *context->app, images, project().settings().contentLanguage);
	if (!warnings)
	{
		setError(tr("Cannot apply the configuration of %1: %2").arg(context->label, errorText(warnings.error())));
		finish(false, tr("Reading the configuration of %1 failed.").arg(context->label));
		return;
	}
	const QStringList notes = context->notes + *warnings;
	for (const QString& note : notes)
		appendLog(tr("%1: %2").arg(context->label, note));
	if (!notes.isEmpty())
		setError(notes.join(u'\n'));
	finish(true,
		notes.isEmpty() ? tr("Configuration of %1 read from the device.").arg(context->label)
						: tr("Configuration of %1 read from the device, with %n warning(s).", nullptr, static_cast<int>(notes.size()))
							  .arg(context->label));
}

void CommissioningVM::cancel()
{
	if (m_kind == u"readConfiguration"_s)
	{
		m_cancelRequested = true;
		setProgress(tr("Cancelling…"), m_progressValue, m_progressTotal);
		return;
	}
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
