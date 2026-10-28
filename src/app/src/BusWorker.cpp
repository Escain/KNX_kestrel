#include "BusWorker.hpp"

#include "kestrel/bus/Logging.hpp"

#include <QEventLoop>
#include <QHostInfo>

#include <algorithm>
#include <variant>

namespace kestrel::app::detail
{

using namespace Qt::StringLiterals;

namespace
{

constexpr int kTickMs = 50;

/// How long disconnect() waits for queued frames (e.g. a final T_Disconnect) to reach the bus.
constexpr std::chrono::milliseconds kDrainTimeout{1500};

QByteArray toByteArray(ByteView bytes)
{
	return QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
}

Unexpected notConnected()
{
	return makeError(ErrorCode::IoError, "not connected to a KNX interface");
}

std::uint16_t be16(const Bytes& bytes)
{
	return bytes.size() >= 2 ? static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]) : 0;
}

} // namespace

BusWorker::BusWorker(BusSession* session, std::shared_ptr<SharedState> shared)
	: m_session(session), m_shared(std::move(shared)), m_link(new bus::UdpTunnelLink(this)), m_timer(new QTimer(this))
{
	m_timer->setInterval(kTickMs);
	QObject::connect(m_timer, &QTimer::timeout, this, [this] { onTick(); });
	m_link->onState = [this](knx::LinkState state) { onLinkState(state); };
	m_link->onFrame = [this](const knx::cemi::LData& frame) { onLinkFrame(frame); };
	m_link->onConfirm = [this](bool ok) { onLinkConfirm(ok); };
	QObject::connect(m_link, &bus::UdpTunnelLink::connectionLost, this,
		[this](const QString& reason)
		{
			toSession([session = m_session, reason] { Q_EMIT session->connectionLost(reason); });
		});
}

BusWorker::~BusWorker()
{
	// The link's destructor sends DISCONNECT_REQUEST silently. Pending callbacks are dropped together
	// with the session.
	m_link->onState = nullptr;
	m_link->onFrame = nullptr;
	m_link->onConfirm = nullptr;
}

void BusWorker::toSession(std::function<void()> fn)
{
	QMetaObject::invokeMethod(m_session, std::move(fn), Qt::QueuedConnection);
}

void BusWorker::ensureTimer()
{
	if (!m_timer->isActive())
	{
		m_timer->start();
	}
}

// --- Connection --------------------------------------------------------------------------------------------

void BusWorker::connectTo(const QString& host, quint16 port, BusSession::StatusCallback done)
{
	if (m_link->state() != knx::LinkState::Disconnected)
	{
		deliver(done, Status(makeError(ErrorCode::Conflict, "already connected or connecting")));
		return;
	}
	QHostAddress address;
	if (!address.setAddress(host))
	{
		const QHostInfo info = QHostInfo::fromName(host); // bus thread: blocking lookup is fine here
		const auto addresses = info.addresses();
		const auto it = std::find_if(addresses.begin(), addresses.end(),
			[](const QHostAddress& a) { return a.protocol() == QAbstractSocket::IPv4Protocol; });
		if (it == addresses.end())
		{
			deliver(done, Status(makeError(ErrorCode::IoError, "cannot resolve host '" + host.toStdString() + "'")));
			return;
		}
		address = *it;
	}
	m_connectDone = std::move(done);
	const Status started = m_link->connectTo(address, port);
	if (!started)
	{
		deliver(std::exchange(m_connectDone, {}), started);
	}
}

void BusWorker::closeDeviceConnections()
{
	for (auto& op : m_infos)
	{
		if (!op->finished)
		{
			finishInfo(*op, Result<DeviceInfo>(makeError(ErrorCode::Cancelled, "disconnected from the interface")));
		}
	}
	std::erase_if(m_infos, [](const std::unique_ptr<InfoOp>& op) { return op->finished; });
	for (auto& op : m_mgmtReads)
	{
		finishManagementRead(*op);
	}
	m_mgmtReads.clear();
}

void BusWorker::disconnect()
{
	closeDeviceConnections();
	m_disconnectDeadline = knx::Clock::now() + kDrainTimeout;
	ensureTimer();
	disconnectWhenDrained();
}

void BusWorker::disconnectWhenDrained()
{
	if (!m_disconnectDeadline)
	{
		return;
	}
	if (m_link->state() == knx::LinkState::Connected && m_link->pendingFrames() > 0
		&& knx::Clock::now() < *m_disconnectDeadline)
	{
		return;
	}
	m_disconnectDeadline.reset();
	m_link->disconnectFromInterface();
}

void BusWorker::shutdown()
{
	if (m_link->state() != knx::LinkState::Connected)
	{
		return;
	}
	disconnect();
	QEventLoop loop;
	QTimer poll;
	poll.setInterval(10);
	QObject::connect(&poll, &QTimer::timeout, &loop,
		[this, &loop]
		{
			disconnectWhenDrained();
			if (!m_disconnectDeadline)
			{
				loop.quit();
			}
		});
	poll.start();
	if (m_disconnectDeadline)
	{
		loop.exec();
	}
}

void BusWorker::onLinkState(knx::LinkState state)
{
	const knx::IndividualAddress address = m_link->address();
	toSession(
		[session = m_session, state, address]
		{
			session->m_state = state;
			session->m_localAddress = address;
			Q_EMIT session->stateChanged(state);
		});

	if (state == knx::LinkState::Connected)
	{
		ensureTimer();
		deliver(std::exchange(m_connectDone, {}), ok());
	}
	else if (state == knx::LinkState::Disconnected)
	{
		const QString reason = m_link->lastError().isEmpty() ? u"disconnected"_s : m_link->lastError();
		const Error error{ErrorCode::IoError, reason.toStdString()};
		deliver(std::exchange(m_connectDone, {}), Status(Unexpected{error}));
		failAll(error);
	}
}

void BusWorker::failAll(const Error& error)
{
	// Frames still waiting for L_Data.con were already answered negatively by the TunnelClient; this
	// only covers anything left over.
	for (auto& pending : std::exchange(m_pending, {}))
	{
		if (pending.done)
		{
			pending.done(false);
		}
	}
	for (auto& read : std::exchange(m_reads, {}))
	{
		deliver(read.done, Result<GroupReadResult>(Unexpected{error}));
	}
	if (m_programmer)
	{
		m_programmer.reset();
		deliver(std::exchange(m_programDone, {}), Result<knx::IndividualAddress>(Unexpected{error}));
		m_programProgress = {};
	}
	for (auto& op : std::exchange(m_infos, {}))
	{
		if (!op->finished)
		{
			deliver(op->done, Result<DeviceInfo>(Unexpected{error}));
		}
	}
	for (auto& scan : std::exchange(m_scans, {}))
	{
		deliver(scan.done, Result<QList<knx::IndividualAddress>>(Unexpected{error}));
	}
	for (auto& op : std::exchange(m_mgmtReads, {}))
	{
		if (!op->finished)
		{
			deliver(op->done, Result<QList<Result<QByteArray>>>(Unexpected{error}));
		}
	}
}

// --- Frames ------------------------------------------------------------------------------------------------

Status BusWorker::sendFrame(const knx::cemi::LData& frame, std::function<void(bool)> confirmed)
{
	// Register before sending: the confirmation may arrive synchronously.
	m_pending.push_back({frame, std::move(confirmed)});
	const Status status = m_link->send(frame);
	if (!status)
	{
		m_pending.pop_back();
	}
	return status;
}

void BusWorker::onLinkConfirm(bool ok)
{
	if (m_pending.empty())
	{
		return;
	}
	PendingConfirm pending = std::move(m_pending.front());
	m_pending.pop_front();
	if (ok)
	{
		knx::cemi::LData frame = pending.frame;
		if (frame.source == 0)
		{
			frame.source = m_link->address().raw();
		}
		record(frame, true);
	}
	if (pending.done)
	{
		pending.done(ok);
	}
	disconnectWhenDrained();
}

void BusWorker::onLinkFrame(const knx::cemi::LData& frame)
{
	record(frame, false);
	const knx::TimePoint now = knx::Clock::now();

	if (frame.groupDestination && !m_reads.empty())
	{
		auto tpdu = knx::decodeTpdu(frame);
		if (tpdu && tpdu->isData() && tpdu->apdu.apci == knx::Apci::GroupValueResponse)
		{
			const auto it = std::find_if(m_reads.begin(), m_reads.end(),
				[&frame](const GroupReadOp& op) { return op.address.raw() == frame.destination; });
			if (it != m_reads.end())
			{
				GroupReadOp op = std::move(*it);
				m_reads.erase(it);
				const Bytes raw = tpdu->apdu.groupValue();
				auto value = dpt::decode(op.dpt, raw);
				if (!value)
				{
					deliver(op.done, Result<GroupReadResult>(Unexpected{value.error()}));
				}
				else
				{
					GroupReadResult result{frame.sourceAddress(), toByteArray(raw), *value,
						QString::fromStdString(dpt::format(op.dpt, *value))};
					deliver(op.done, Result<GroupReadResult>(std::move(result)));
				}
			}
		}
	}

	if (!m_scans.empty() && frame.groupDestination && frame.destination == 0)
	{
		auto tpdu = knx::decodeTpdu(frame);
		if (tpdu && tpdu->type == knx::TpduType::DataBroadcast && tpdu->apdu.apci == knx::Apci::IndividualAddressResponse)
		{
			for (auto& scan : m_scans)
			{
				scan.found.push_back(frame.sourceAddress());
			}
		}
	}

	if (m_programmer)
	{
		m_programmer->onFrame(frame, now);
		pumpProgrammer();
	}
	for (auto& op : m_infos)
	{
		if (!op->finished)
		{
			op->session->onFrame(frame, now);
			pumpInfo(*op);
		}
	}
	std::erase_if(m_infos, [](const std::unique_ptr<InfoOp>& op) { return op->finished; });
	for (auto& op : m_mgmtReads)
	{
		if (!op->finished)
		{
			op->session->onFrame(frame, now);
			pumpManagementRead(*op);
		}
	}
	std::erase_if(m_mgmtReads, [](const std::unique_ptr<ManagementReadOp>& op) { return op->finished; });
}

void BusWorker::record(const knx::cemi::LData& frame, bool outgoing)
{
	TelegramRecord record;
	record.timestamp = QDateTime::currentDateTimeUtc();
	record.source = frame.sourceAddress();
	record.destination = frame.destination;
	record.isGroup = frame.groupDestination;
	record.outgoing = outgoing;
	record.priority = frame.priority;

	auto tpdu = knx::decodeTpdu(frame);
	std::optional<dpt::DptId> dptId;
	if (tpdu)
	{
		record.tpduType = tpdu->type;
		if (tpdu->isData())
		{
			record.apci = tpdu->apdu.apci;
			const bool groupValue = tpdu->apdu.apci == knx::Apci::GroupValueWrite
				|| tpdu->apdu.apci == knx::Apci::GroupValueResponse;
			record.payload = toByteArray(groupValue ? tpdu->apdu.groupValue() : tpdu->apdu.data);
		}
	}

	bool notify = false;
	{
		std::lock_guard lock(m_shared->mutex);
		if (record.isGroup && record.apci
			&& (*record.apci == knx::Apci::GroupValueWrite || *record.apci == knx::Apci::GroupValueResponse))
		{
			const auto it = m_shared->dpts.find(record.destination);
			if (it != m_shared->dpts.end())
			{
				dptId = it->second;
			}
		}
	}
	if (dptId)
	{
		const auto* data = reinterpret_cast<const std::uint8_t*>(record.payload.constData());
		auto value = dpt::decode(*dptId, ByteView(data, static_cast<std::size_t>(record.payload.size())));
		if (value)
		{
			record.decoded = QString::fromStdString(dpt::format(*dptId, *value));
		}
	}
	{
		std::lock_guard lock(m_shared->mutex);
		m_shared->records.push_back(std::move(record));
		while (m_shared->records.size() > m_shared->capacity)
		{
			m_shared->records.pop_front();
			++m_shared->dropped;
		}
		if (!m_shared->notifyPending)
		{
			m_shared->notifyPending = true;
			notify = true;
		}
	}
	if (notify)
	{
		toSession([session = m_session] { Q_EMIT session->telegramsAvailable(); });
	}
}

// --- Group communication -----------------------------------------------------------------------------------

void BusWorker::groupWrite(knx::GroupAddress address, dpt::DptId dpt, const dpt::DptValue& value,
	BusSession::StatusCallback done)
{
	auto bytes = dpt::encode(dpt, value);
	if (!bytes)
	{
		deliver(done, Status(Unexpected{bytes.error()}));
		return;
	}
	if (m_link->state() != knx::LinkState::Connected)
	{
		deliver(done, Status(notConnected()));
		return;
	}
	const knx::Apdu apdu = dpt::isShortForm(dpt) ? knx::groupValueWriteShort(bytes->at(0)) : knx::groupValueWrite(*bytes);
	const Status sent = sendFrame(knx::makeGroupFrame(address, apdu),
		[this, done](bool ok)
		{
			deliver(done,
				ok ? Status() : Status(makeError(ErrorCode::ProtocolError, "the interface did not confirm the frame")));
		});
	if (!sent)
	{
		deliver(done, sent);
	}
}

void BusWorker::groupRead(knx::GroupAddress address, dpt::DptId dpt, std::chrono::milliseconds timeout,
	BusSession::GroupReadCallback done)
{
	if (m_link->state() != knx::LinkState::Connected)
	{
		deliver(done, Result<GroupReadResult>(notConnected()));
		return;
	}
	const Status sent = sendFrame(knx::makeGroupFrame(address, knx::groupValueRead()));
	if (!sent)
	{
		deliver(done, Result<GroupReadResult>(Unexpected{sent.error()}));
		return;
	}
	m_reads.push_back({address, dpt, knx::Clock::now() + timeout, std::move(done)});
	ensureTimer();
}

void BusWorker::readProgrammingMode(std::chrono::milliseconds window, BusSession::AddressListCallback done)
{
	if (m_link->state() != knx::LinkState::Connected)
	{
		deliver(done, Result<QList<knx::IndividualAddress>>(notConnected()));
		return;
	}
	const Status sent = sendFrame(knx::makeBroadcastFrame(knx::individualAddressRead()));
	if (!sent)
	{
		deliver(done, Result<QList<knx::IndividualAddress>>(Unexpected{sent.error()}));
		return;
	}
	m_scans.push_back({knx::Clock::now() + window, {}, std::move(done)});
	ensureTimer();
}

// --- IA programming ----------------------------------------------------------------------------------------

void BusWorker::program(knx::IndividualAddress target, const BusSession::ProgramOptions& options,
	BusSession::ProgramCallback done, BusSession::ProgressCallback progress)
{
	if (m_link->state() != knx::LinkState::Connected)
	{
		deliver(done, Result<knx::IndividualAddress>(notConnected()));
		return;
	}
	if (m_programmer)
	{
		deliver(done, Result<knx::IndividualAddress>(
						  makeError(ErrorCode::Conflict, "an individual address is already being programmed")));
		return;
	}
	knx::IaProgrammerConfig config;
	config.progModeTimeout = options.timeout;
	config.allowOverwrite = options.allowOverwrite;
	m_programmer = std::make_unique<knx::IaProgrammer>(config);
	m_programDone = std::move(done);
	m_programProgress = std::move(progress);
	m_programTimeout = options.timeout;
	m_programmer->start(target, knx::Clock::now());
	ensureTimer();
	pumpProgrammer();
}

void BusWorker::cancelProgramming()
{
	if (m_programmer)
	{
		m_programmer->cancel(knx::Clock::now());
		pumpProgrammer();
	}
}

void BusWorker::pumpProgrammer()
{
	while (m_programmer)
	{
		auto actions = m_programmer->takeActions();
		if (actions.empty())
		{
			return;
		}
		for (auto& action : actions)
		{
			if (const auto* send = std::get_if<knx::IaProgrammer::SendFrame>(&action))
			{
				if (const Status sent = sendFrame(send->frame); !sent)
				{
					qCWarning(bus::lcBus) << "IA programming: send failed:" << sent.error().message.c_str();
				}
			}
			else if (const auto* changed = std::get_if<knx::IaProgrammer::StateChanged>(&action))
			{
				deliver(m_programProgress, changed->state);
			}
			else if (const auto* finished = std::get_if<knx::IaProgrammer::Finished>(&action))
			{
				Result<knx::IndividualAddress> result = finished->previousAddress;
				switch (finished->failure)
				{
					case knx::IaProgrammerFailure::None: break;
					case knx::IaProgrammerFailure::Timeout:
						result = makeError(ErrorCode::Timeout,
							"no device in programming mode within "
								+ std::to_string(std::chrono::duration_cast<std::chrono::seconds>(m_programTimeout).count())
								+ " s");
						break;
					case knx::IaProgrammerFailure::MultipleDevicesInProgMode:
						result = makeError(ErrorCode::Conflict, "more than one device is in programming mode");
						break;
					case knx::IaProgrammerFailure::VerifyFailed:
						result = makeError(ErrorCode::ProtocolError, "the device did not confirm the new address");
						break;
					case knx::IaProgrammerFailure::Cancelled:
						result = makeError(ErrorCode::Cancelled, "programming cancelled");
						break;
					case knx::IaProgrammerFailure::AddressInUse:
						result = makeError(ErrorCode::Conflict,
							"address " + m_programmer->target().toString() + " is already used by another device");
						break;
				}
				// Keep the programmer alive until its last frames (A_Restart) were handed to the link.
				auto programmer = std::move(m_programmer);
				for (auto& rest : programmer->takeActions())
				{
					if (const auto* sendRest = std::get_if<knx::IaProgrammer::SendFrame>(&rest))
					{
						(void)sendFrame(sendRest->frame);
					}
				}
				deliver(std::exchange(m_programDone, {}), std::move(result));
				m_programProgress = {};
				return;
			}
		}
	}
}

// --- Device info -------------------------------------------------------------------------------------------

void BusWorker::readDeviceInfo(knx::IndividualAddress device, BusSession::DeviceInfoCallback done)
{
	if (m_link->state() != knx::LinkState::Connected)
	{
		deliver(done, Result<DeviceInfo>(notConnected()));
		return;
	}
	auto op = std::make_unique<InfoOp>();
	op->session = std::make_unique<knx::ManagementSession>(device);
	op->session->setLocalAddress(m_link->address());
	op->info.address = device;
	op->done = std::move(done);
	const knx::TimePoint now = knx::Clock::now();
	op->descriptorId = op->session->deviceDescriptorRead(now, 0);
	op->serialId = op->session->propertyValueRead(0, 11, 1, 1, now);
	op->manufacturerId = op->session->propertyValueRead(0, 12, 1, 1, now);
	InfoOp& ref = *op;
	m_infos.push_back(std::move(op));
	ensureTimer();
	pumpInfo(ref);
	std::erase_if(m_infos, [](const std::unique_ptr<InfoOp>& o) { return o->finished; });
}

void BusWorker::finishInfo(InfoOp& op, Result<DeviceInfo> result)
{
	if (op.finished)
	{
		return;
	}
	op.finished = true;
	op.session->close(knx::Clock::now());
	for (auto& action : op.session->takeActions())
	{
		if (const auto* send = std::get_if<knx::ManagementSession::SendFrame>(&action))
		{
			(void)sendFrame(send->frame);
		}
	}
	deliver(op.done, std::move(result));
}

void BusWorker::pumpInfo(InfoOp& op)
{
	while (!op.finished)
	{
		auto actions = op.session->takeActions();
		if (actions.empty())
		{
			return;
		}
		for (auto& action : actions)
		{
			if (op.finished)
			{
				break;
			}
			if (const auto* send = std::get_if<knx::ManagementSession::SendFrame>(&action))
			{
				if (const Status sent = sendFrame(send->frame); !sent)
				{
					finishInfo(op, Result<DeviceInfo>(Unexpected{sent.error()}));
				}
			}
			else if (const auto* completed = std::get_if<knx::ManagementSession::Completed>(&action))
			{
				if (completed->id == op.descriptorId)
				{
					op.info.maskVersion = be16(completed->data);
				}
				else if (completed->id == op.serialId)
				{
					if (completed->data.size() >= 6)
					{
						std::copy_n(completed->data.begin(), 6, op.info.serial.bytes.begin());
					}
				}
				else if (completed->id == op.manufacturerId)
				{
					op.info.manufacturerId = be16(completed->data);
				}
				if (--op.remaining == 0)
				{
					finishInfo(op, op.info);
				}
			}
			else if (const auto* failed = std::get_if<knx::ManagementSession::Failed>(&action))
			{
				finishInfo(op, Result<DeviceInfo>(Unexpected{failed->error}));
			}
			else if (std::holds_alternative<knx::ManagementSession::Disconnected>(action))
			{
				finishInfo(op, Result<DeviceInfo>(makeError(ErrorCode::ProtocolError,
								   "the device closed the connection before answering")));
			}
		}
	}
}

// --- Generic management reads ------------------------------------------------------------------------------

void BusWorker::readManagement(knx::IndividualAddress device, QList<ManagementRead> reads,
	BusSession::ManagementReadCallback done)
{
	if (m_link->state() != knx::LinkState::Connected)
	{
		deliver(done, Result<QList<Result<QByteArray>>>(notConnected()));
		return;
	}
	if (reads.isEmpty())
	{
		deliver(done, Result<QList<Result<QByteArray>>>(QList<Result<QByteArray>>{}));
		return;
	}
	auto op = std::make_unique<ManagementReadOp>();
	op->session = std::make_unique<knx::ManagementSession>(device);
	op->session->setLocalAddress(m_link->address());
	op->done = std::move(done);
	op->remaining = reads.size();
	op->results.resize(reads.size(), Result<QByteArray>(makeError(ErrorCode::Cancelled, "not run")));
	const knx::TimePoint now = knx::Clock::now();
	for (const ManagementRead& read : std::as_const(reads))
	{
		op->ids.push_back(read.kind == ManagementRead::Kind::Property
				? op->session->propertyValueRead(read.objectIndex, read.propertyId, read.count, read.startIndex, now)
				: op->session->memoryRead(read.address, read.length, now));
	}
	ManagementReadOp& ref = *op;
	m_mgmtReads.push_back(std::move(op));
	ensureTimer();
	pumpManagementRead(ref);
	std::erase_if(m_mgmtReads, [](const std::unique_ptr<ManagementReadOp>& o) { return o->finished; });
}

void BusWorker::finishManagementRead(ManagementReadOp& op)
{
	if (op.finished)
	{
		return;
	}
	op.finished = true;
	op.session->close(knx::Clock::now());
	for (auto& action : op.session->takeActions())
	{
		if (const auto* send = std::get_if<knx::ManagementSession::SendFrame>(&action))
		{
			(void)sendFrame(send->frame);
		}
	}
	deliver(op.done, Result<QList<Result<QByteArray>>>(std::move(op.results)));
}

void BusWorker::pumpManagementRead(ManagementReadOp& op)
{
	const auto indexOf = [&op](knx::ManagementSession::RequestId id) -> qsizetype
	{
		const auto it = std::find(op.ids.begin(), op.ids.end(), id);
		return it == op.ids.end() ? -1 : static_cast<qsizetype>(it - op.ids.begin());
	};
	while (!op.finished)
	{
		auto actions = op.session->takeActions();
		if (actions.empty())
		{
			return;
		}
		for (auto& action : actions)
		{
			if (op.finished)
			{
				break;
			}
			if (const auto* send = std::get_if<knx::ManagementSession::SendFrame>(&action))
			{
				if (const Status sent = sendFrame(send->frame); !sent)
				{
					for (auto& result : op.results)
					{
						if (!result && result.error().code == ErrorCode::Cancelled)
						{
							result = Result<QByteArray>(Unexpected{sent.error()});
						}
					}
					finishManagementRead(op);
				}
			}
			else if (const auto* completed = std::get_if<knx::ManagementSession::Completed>(&action))
			{
				if (const qsizetype i = indexOf(completed->id); i >= 0)
				{
					op.results[i] = toByteArray(completed->data);
					if (--op.remaining == 0)
					{
						finishManagementRead(op);
					}
				}
			}
			else if (const auto* failed = std::get_if<knx::ManagementSession::Failed>(&action))
			{
				if (const qsizetype i = indexOf(failed->id); i >= 0)
				{
					op.results[i] = Result<QByteArray>(Unexpected{failed->error});
					if (--op.remaining == 0)
					{
						finishManagementRead(op);
					}
				}
			}
			else if (std::holds_alternative<knx::ManagementSession::Disconnected>(action))
			{
				// Requests still queued were failed by the session before it reported the disconnect.
				finishManagementRead(op);
			}
		}
	}
}

// --- Timer -------------------------------------------------------------------------------------------------

void BusWorker::onTick()
{
	const knx::TimePoint now = knx::Clock::now();
	disconnectWhenDrained();
	for (auto it = m_reads.begin(); it != m_reads.end();)
	{
		if (now >= it->deadline)
		{
			deliver(it->done, Result<GroupReadResult>(makeError(ErrorCode::Timeout,
								  "no response to the group read of " + it->address.toString())));
			it = m_reads.erase(it);
		}
		else
		{
			++it;
		}
	}
	for (auto it = m_scans.begin(); it != m_scans.end();)
	{
		if (now >= it->deadline)
		{
			deliver(it->done, Result<QList<knx::IndividualAddress>>(std::move(it->found)));
			it = m_scans.erase(it);
		}
		else
		{
			++it;
		}
	}
	if (m_programmer)
	{
		m_programmer->tick(now);
		pumpProgrammer();
	}
	for (auto& op : m_infos)
	{
		if (!op->finished)
		{
			op->session->tick(now);
			pumpInfo(*op);
		}
	}
	std::erase_if(m_infos, [](const std::unique_ptr<InfoOp>& op) { return op->finished; });
	for (auto& op : m_mgmtReads)
	{
		if (!op->finished)
		{
			op->session->tick(now);
			pumpManagementRead(*op);
		}
	}
	std::erase_if(m_mgmtReads, [](const std::unique_ptr<ManagementReadOp>& op) { return op->finished; });
}

// --- Discovery ---------------------------------------------------------------------------------------------

void BusWorker::discover(const BusSession::DiscoveryOptions& options, BusSession::DiscoveryCallback done)
{
	auto* discovery = new bus::Discovery(this);
	discovery->setTimeout(options.timeout);
	discovery->setMulticastEnabled(options.multicast);
	if (!options.unicastHost.isEmpty())
	{
		QHostAddress host;
		if (!host.setAddress(options.unicastHost))
		{
			const auto addresses = QHostInfo::fromName(options.unicastHost).addresses();
			const auto it = std::find_if(addresses.begin(), addresses.end(),
				[](const QHostAddress& a) { return a.protocol() == QAbstractSocket::IPv4Protocol; });
			if (it == addresses.end())
			{
				delete discovery;
				deliver(done, Result<QList<bus::DiscoveredInterface>>(makeError(
								  ErrorCode::IoError, "cannot resolve host '" + options.unicastHost.toStdString() + "'")));
				return;
			}
			host = *it;
		}
		discovery->addUnicastTarget(host, options.unicastPort);
	}
	QObject::connect(discovery, &bus::Discovery::finished, this,
		[this, discovery, done](const QList<bus::DiscoveredInterface>& results)
		{
			deliver(done, Result<QList<bus::DiscoveredInterface>>(results));
			discovery->deleteLater();
		});
	const Status started = discovery->start();
	if (!started)
	{
		delete discovery;
		deliver(done, Result<QList<bus::DiscoveredInterface>>(Unexpected{started.error()}));
	}
}

} // namespace kestrel::app::detail
