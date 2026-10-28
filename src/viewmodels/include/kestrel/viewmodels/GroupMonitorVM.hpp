#pragma once

// Group monitor tab (FR-BUS-5/6). Drains app::BusSession's telegram buffer on a ~30 Hz timer into a
// TelegramModel (one insert per tick). Pause keeps capturing into a side buffer and shows it on resume.
// The read/write bar parses the group address in any style and the value with the GA's DPT from the
// project (or an explicit DPT).

#include "kestrel/viewmodels/PanelVM.hpp"

#include "kestrel/app/TelegramRecord.hpp"

#include <QList>
#include <QTimer>

namespace kestrel::app
{
class BusSession;
}

namespace kestrel::viewmodels
{

class TelegramModel;

class GroupMonitorVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)
	Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
	Q_PROPERTY(bool paused READ isPaused WRITE setPaused NOTIFY pausedChanged)
	Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
	Q_PROPERTY(int count READ count NOTIFY countChanged)
	Q_PROPERTY(int pending READ pending NOTIFY countChanged)
	Q_PROPERTY(QString lastRead READ lastRead NOTIFY lastReadChanged)

public:
	static constexpr int kDrainIntervalMs = 33;

	GroupMonitorVM(const Services& services, app::BusSession* bus, QObject* parent = nullptr);
	~GroupMonitorVM() override;

	QAbstractItemModel* model() const;
	TelegramModel* telegramModel() const { return m_model; }
	bool isConnected() const;
	bool isPaused() const { return m_paused; }
	void setPaused(bool paused);
	QString filter() const;
	void setFilter(const QString& filter);
	/// Visible rows.
	int count() const;
	/// Telegrams captured while paused.
	int pending() const { return static_cast<int>(m_pending.size()); }
	QString lastRead() const { return m_lastRead; }

	/// Takes the session's buffered telegrams now (the timer calls this).
	void drain();

	Q_INVOKABLE void clear();
	/// DPT stored for a group address in the project ("DPST-9-1"), empty if unknown.
	Q_INVOKABLE QString dptOf(const QString& groupAddress) const;
	/// Sends A_GroupValue_Write. `dpt` empty = the project's DPT for that address.
	Q_INVOKABLE bool write(const QString& groupAddress, const QString& value, const QString& dpt = {});
	/// Sends A_GroupValue_Read; the answer arrives in lastRead.
	Q_INVOKABLE bool read(const QString& groupAddress, const QString& dpt = {});

Q_SIGNALS:
	void connectedChanged();
	void pausedChanged();
	void filterChanged();
	void countChanged();
	void lastReadChanged();

private:
	struct Target;
	std::optional<Target> resolve(const QString& groupAddress, const QString& dpt);
	void setLastRead(const QString& text);

	app::BusSession* m_bus;
	TelegramModel* m_model;
	QTimer m_timer;
	bool m_paused = false;
	QList<app::TelegramRecord> m_pending;
	QString m_lastRead;
};

} // namespace kestrel::viewmodels
