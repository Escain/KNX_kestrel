#pragma once

// Bus connection dock (FR-BUS-1/2): wraps app::BusSession's discovery list, manual host:port,
// connect/disconnect and link state. Commissioning (FR-COM-*) lives in CommissioningVM.
// Every BusSession callback is guarded: it is ignored once this view model is gone.

#include "kestrel/viewmodels/PanelVM.hpp"

#include "kestrel/knx/Address.hpp"

#include <QVariantList>

#include <optional>

namespace kestrel::app
{
class BusSession;
}

namespace kestrel::viewmodels
{

class BusConnectionVM : public PanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")
	Q_PROPERTY(QString state READ stateText NOTIFY stateChanged)
	Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)
	Q_PROPERTY(bool connecting READ isConnecting NOTIFY stateChanged)
	Q_PROPERTY(QString localAddress READ localAddress NOTIFY stateChanged)
	Q_PROPERTY(QString host READ host WRITE setHost NOTIFY endpointChanged)
	Q_PROPERTY(int port READ port WRITE setPort NOTIFY endpointChanged)
	Q_PROPERTY(bool discovering READ isDiscovering NOTIFY interfacesChanged)
	Q_PROPERTY(QVariantList interfaces READ interfaces NOTIFY interfacesChanged)
	Q_PROPERTY(bool discovered READ wasDiscovered NOTIFY interfacesChanged)

public:
	static constexpr int kDefaultPort = 3671;

	BusConnectionVM(const Services& services, app::BusSession* bus, QObject* parent = nullptr);

	app::BusSession* bus() const { return m_bus; }
	QString stateText() const;
	bool isConnected() const;
	bool isConnecting() const;
	QString localAddress() const;
	QString host() const { return m_host; }
	void setHost(const QString& host);
	int port() const { return m_port; }
	void setPort(int port);
	bool isDiscovering() const { return m_discovering; }
	/// [{name, host, port, tunnelling}]
	QVariantList interfaces() const { return m_interfaces; }
	/// A search has completed at least once.
	bool wasDiscovered() const { return m_discovered; }

	Q_INVOKABLE void discover();
	/// Connects to host()/port().
	Q_INVOKABLE void connectToBus();
	/// Copies the endpoint of a discovered interface and connects.
	Q_INVOKABLE void connectToInterface(int index);
	Q_INVOKABLE void disconnectFromBus();

Q_SIGNALS:
	void stateChanged();
	void endpointChanged();
	void interfacesChanged();

private:
	app::BusSession* m_bus;
	QString m_host;
	int m_port = kDefaultPort;
	bool m_discovering = false;
	bool m_discovered = false;
	QVariantList m_interfaces;
};

} // namespace kestrel::viewmodels
