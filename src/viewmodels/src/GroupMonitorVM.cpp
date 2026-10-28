#include "kestrel/viewmodels/GroupMonitorVM.hpp"

#include "kestrel/app/BusSession.hpp"
#include "kestrel/dpt/Dpt.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/TelegramModel.hpp"

#include <QPointer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

struct GroupMonitorVM::Target
{
	knx::GroupAddress address;
	dpt::DptId dpt;
};

GroupMonitorVM::GroupMonitorVM(const Services& services, app::BusSession* bus, QObject* parent)
	: PanelVM(services, u"groupMonitor"_s, parent), m_bus(bus), m_model(new TelegramModel(services.session, TelegramModel::kDefaultCapacity, this))
{
	m_timer.setInterval(kDrainIntervalMs);
	connect(&m_timer, &QTimer::timeout, this, &GroupMonitorVM::drain);
	m_timer.start();
	connect(m_bus, &app::BusSession::stateChanged, this, &GroupMonitorVM::connectedChanged);
	connect(m_model, &QAbstractItemModel::rowsInserted, this, &GroupMonitorVM::countChanged);
	connect(m_model, &QAbstractItemModel::rowsRemoved, this, &GroupMonitorVM::countChanged);
	connect(m_model, &QAbstractItemModel::modelReset, this, &GroupMonitorVM::countChanged);
}

GroupMonitorVM::~GroupMonitorVM() = default;

QAbstractItemModel* GroupMonitorVM::model() const
{
	return m_model;
}

bool GroupMonitorVM::isConnected() const
{
	return m_bus->isConnected();
}

QString GroupMonitorVM::filter() const
{
	return m_model->filter();
}

void GroupMonitorVM::setFilter(const QString& filter)
{
	if (filter.trimmed() == m_model->filter())
		return;
	m_model->setFilter(filter);
	Q_EMIT filterChanged();
}

int GroupMonitorVM::count() const
{
	return m_model->rowCount();
}

void GroupMonitorVM::setPaused(bool paused)
{
	if (paused == m_paused)
		return;
	m_paused = paused;
	if (!m_paused && !m_pending.isEmpty())
	{
		m_model->append(m_pending);
		m_pending.clear();
	}
	Q_EMIT pausedChanged();
	Q_EMIT countChanged();
}

void GroupMonitorVM::drain()
{
	QList<app::TelegramRecord> batch = m_bus->takeTelegrams();
	if (batch.isEmpty())
		return;
	if (m_paused)
	{
		m_pending.append(batch);
		const auto capacity = static_cast<qsizetype>(m_model->capacity());
		if (m_pending.size() > capacity)
			m_pending.remove(0, m_pending.size() - capacity);
		Q_EMIT countChanged();
		return;
	}
	m_model->append(batch);
}

void GroupMonitorVM::clear()
{
	m_pending.clear();
	m_model->clear();
	Q_EMIT countChanged();
}

QString GroupMonitorVM::dptOf(const QString& groupAddress) const
{
	const auto raw = parseGroupAddress(groupAddress);
	if (!raw)
		return {};
	for (const model::GroupAddress& address : project().groupAddresses())
	{
		if (address.raw == *raw)
			return address.dpt;
	}
	return {};
}

std::optional<GroupMonitorVM::Target> GroupMonitorVM::resolve(const QString& groupAddress, const QString& dpt)
{
	if (!m_bus->isConnected())
	{
		setError(tr("Not connected to the bus."));
		return std::nullopt;
	}
	const auto raw = parseGroupAddress(groupAddress);
	if (!raw || *raw == 0)
	{
		setError(tr("Not a group address: %1").arg(groupAddress));
		return std::nullopt;
	}
	const QString dptText = dpt.trimmed().isEmpty() ? dptOf(groupAddress) : dpt.trimmed();
	if (dptText.isEmpty())
	{
		setError(tr("Enter a datapoint type for %1.").arg(groupAddress));
		return std::nullopt;
	}
	const auto id = dpt::DptId::parse(dptText.toStdString());
	if (!id)
	{
		setError(tr("Not a datapoint type: %1").arg(dptText));
		return std::nullopt;
	}
	clearError();
	return Target{knx::GroupAddress(*raw), *id};
}

bool GroupMonitorVM::write(const QString& groupAddress, const QString& value, const QString& dpt)
{
	const auto target = resolve(groupAddress, dpt);
	if (!target)
		return false;
	auto parsed = dpt::parse(target->dpt, value.trimmed().toStdString());
	if (!parsed)
	{
		setError(tr("Invalid value for DPT %1: %2").arg(QString::fromStdString(target->dpt.toString()), errorText(parsed.error())));
		return false;
	}
	QPointer<GroupMonitorVM> self(this);
	m_bus->groupWrite(target->address, target->dpt, *parsed,
		[self](Status status)
		{
			if (self && !status)
				self->setError(errorText(status.error()));
		});
	return true;
}

bool GroupMonitorVM::read(const QString& groupAddress, const QString& dpt)
{
	const auto target = resolve(groupAddress, dpt);
	if (!target)
		return false;
	QPointer<GroupMonitorVM> self(this);
	const QString address = groupAddressText(project(), target->address.raw());
	setLastRead(tr("Reading %1…").arg(address));
	m_bus->groupRead(target->address, target->dpt,
		[self, address](Result<app::GroupReadResult> result)
		{
			if (!self)
				return;
			if (!result)
			{
				self->setLastRead(tr("%1: no answer").arg(address));
				self->setError(errorText(result.error()));
				return;
			}
			self->setLastRead(tr("%1 = %2 (from %3)").arg(address, result->text, QString::fromStdString(result->responder.toString())));
		});
	return true;
}

void GroupMonitorVM::setLastRead(const QString& text)
{
	if (text != m_lastRead)
	{
		m_lastRead = text;
		Q_EMIT lastReadChanged();
	}
}

} // namespace kestrel::viewmodels
