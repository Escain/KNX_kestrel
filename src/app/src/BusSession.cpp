#include "kestrel/app/BusSession.hpp"

#include "BusWorker.hpp"

#include <QMetaObject>

#include <sstream>

namespace kestrel::app
{

using namespace Qt::StringLiterals;

BusSession::BusSession(QObject* parent) : QObject(parent), m_shared(std::make_shared<detail::SharedState>())
{
	qRegisterMetaType<kestrel::knx::LinkState>();
	m_thread.setObjectName(u"kestrel-bus"_s);
	m_worker = new detail::BusWorker(this, m_shared);
	m_worker->moveToThread(&m_thread);
	m_thread.start();
}

BusSession::~BusSession()
{
	// Let queued frames (the T_Disconnect of a device connection) go out, then delete the worker (and with it
	// the link, which says goodbye to the interface) on its own thread.
	QMetaObject::invokeMethod(m_worker,
		[worker = m_worker]
		{
			worker->shutdown();
			delete worker;
		},
		Qt::BlockingQueuedConnection);
	m_worker = nullptr;
	m_thread.quit();
	m_thread.wait();
}

void BusSession::post(std::function<void(detail::BusWorker&)> fn)
{
	QMetaObject::invokeMethod(m_worker, [worker = m_worker, fn = std::move(fn)] { fn(*worker); }, Qt::QueuedConnection);
}

void BusSession::connectToInterface(const QString& host, quint16 port, StatusCallback done)
{
	post([host, port, done = std::move(done)](detail::BusWorker& w) mutable { w.connectTo(host, port, std::move(done)); });
}

void BusSession::disconnectFromInterface()
{
	post([](detail::BusWorker& w) { w.disconnect(); });
}

void BusSession::groupWrite(knx::GroupAddress address, dpt::DptId dpt, dpt::DptValue value, StatusCallback done)
{
	setGroupAddressDpt(address, dpt);
	post([address, dpt, value = std::move(value), done = std::move(done)](detail::BusWorker& w) mutable
		{ w.groupWrite(address, dpt, value, std::move(done)); });
}

void BusSession::groupRead(
	knx::GroupAddress address, dpt::DptId dpt, GroupReadCallback done, std::chrono::milliseconds timeout)
{
	setGroupAddressDpt(address, dpt);
	post([address, dpt, timeout, done = std::move(done)](detail::BusWorker& w) mutable
		{ w.groupRead(address, dpt, timeout, std::move(done)); });
}

void BusSession::programIndividualAddress(
	knx::IndividualAddress target, ProgramOptions options, ProgramCallback done, ProgressCallback progress)
{
	post([target, options, done = std::move(done), progress = std::move(progress)](detail::BusWorker& w) mutable
		{ w.program(target, options, std::move(done), std::move(progress)); });
}

void BusSession::cancelProgramming()
{
	post([](detail::BusWorker& w) { w.cancelProgramming(); });
}

void BusSession::readDeviceInfo(knx::IndividualAddress device, DeviceInfoCallback done)
{
	post([device, done = std::move(done)](detail::BusWorker& w) mutable { w.readDeviceInfo(device, std::move(done)); });
}

void BusSession::readManagement(knx::IndividualAddress device, QList<ManagementRead> reads, ManagementReadCallback done)
{
	post([device, reads = std::move(reads), done = std::move(done)](detail::BusWorker& w) mutable
		{ w.readManagement(device, std::move(reads), std::move(done)); });
}

void BusSession::download(
	knx::IndividualAddress device, download::Plan plan, StatusCallback done, DownloadProgressCallback progress)
{
	post([device, plan = std::move(plan), done = std::move(done), progress = std::move(progress)](
			 detail::BusWorker& w) mutable { w.download(device, std::move(plan), std::move(done), std::move(progress)); });
}

void BusSession::cancelDownload()
{
	post([](detail::BusWorker& w) { w.cancelDownload(); });
}

void BusSession::readProgrammingMode(AddressListCallback done, std::chrono::milliseconds window)
{
	post([window, done = std::move(done)](detail::BusWorker& w) mutable { w.readProgrammingMode(window, std::move(done)); });
}

void BusSession::discover(DiscoveryOptions options, DiscoveryCallback done)
{
	post([options = std::move(options), done = std::move(done)](detail::BusWorker& w) mutable
		{ w.discover(options, std::move(done)); });
}

void BusSession::setGroupAddressDpt(knx::GroupAddress address, dpt::DptId dpt)
{
	std::lock_guard lock(m_shared->mutex);
	m_shared->dpts[address.raw()] = dpt;
}

void BusSession::clearGroupAddressDpts()
{
	std::lock_guard lock(m_shared->mutex);
	m_shared->dpts.clear();
}

void BusSession::setMonitorCapacity(std::size_t capacity)
{
	std::lock_guard lock(m_shared->mutex);
	m_shared->capacity = std::max<std::size_t>(capacity, 1);
	while (m_shared->records.size() > m_shared->capacity)
	{
		m_shared->records.pop_front();
		++m_shared->dropped;
	}
}

QList<TelegramRecord> BusSession::takeTelegrams()
{
	std::lock_guard lock(m_shared->mutex);
	QList<TelegramRecord> records;
	records.reserve(static_cast<qsizetype>(m_shared->records.size()));
	for (auto& record : m_shared->records)
	{
		records.append(std::move(record));
	}
	m_shared->records.clear();
	m_shared->notifyPending = false;
	return records;
}

std::uint64_t BusSession::droppedTelegrams() const
{
	std::lock_guard lock(m_shared->mutex);
	return m_shared->dropped;
}

// --- Value printers ----------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const GroupReadResult& result)
{
	return os << "GroupReadResult{" << result.responder << ", [" << result.raw.toHex(' ').toStdString() << "], "
			  << result.text.toStdString() << '}';
}

std::ostream& operator<<(std::ostream& os, const DeviceInfo& info)
{
	std::ostringstream mask;
	mask << std::hex;
	mask.width(4);
	mask.fill('0');
	mask << info.maskVersion;
	return os << "DeviceInfo{" << info.address << ", mask " << mask.str() << ", serial " << info.serial
			  << ", manufacturer " << info.manufacturerId << '}';
}

std::ostream& operator<<(std::ostream& os, const TelegramRecord& record)
{
	os << "TelegramRecord{" << record.timestamp.toString(Qt::ISODateWithMs).toStdString() << ", "
	   << record.source << " -> " << record.destinationText().toStdString() << ", "
	   << record.serviceText().toStdString() << ", [" << record.payload.toHex(' ').toStdString() << ']';
	if (!record.decoded.isEmpty())
	{
		os << ", " << record.decoded.toStdString();
	}
	if (record.outgoing)
	{
		os << ", outgoing";
	}
	return os << '}';
}

} // namespace kestrel::app
