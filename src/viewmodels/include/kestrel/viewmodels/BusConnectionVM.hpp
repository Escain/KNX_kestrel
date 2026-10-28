#pragma once

// Bus connection dock (FR-BUS-1/2) and commissioning of the selected device (FR-COM-1/4).
// Wraps app::BusSession: discovery list, manual host:port, connect/disconnect, link state; programming an
// individual address with progress and cancel (success records iaProgrammed via Project::recordState, which
// marks the project dirty but is not undoable); reading device info (records the serial number).
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
	Q_PROPERTY(bool operationActive READ isOperationActive NOTIFY operationChanged)
	Q_PROPERTY(bool cancellable READ isCancellable NOTIFY operationChanged)
	Q_PROPERTY(QString operation READ operation NOTIFY operationChanged)
	Q_PROPERTY(QString lastResult READ lastResult NOTIFY operationChanged)

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
	bool isOperationActive() const { return m_operationActive; }
	bool isCancellable() const { return m_programming; }
	QString operation() const { return m_operation; }
	QString lastResult() const { return m_lastResult; }

	/// Individual address of a device ref, nullopt (and lastError) if it has none.
	std::optional<knx::IndividualAddress> addressOf(const QString& deviceRef);

	Q_INVOKABLE void discover();
	/// Connects to host()/port().
	Q_INVOKABLE void connectToBus();
	/// Copies the endpoint of a discovered interface and connects.
	Q_INVOKABLE void connectToInterface(int index);
	Q_INVOKABLE void disconnectFromBus();
	/// FR-COM-1: waits for the device in programming mode and writes the device's individual address.
	Q_INVOKABLE bool programAddress(const QString& deviceRef);
	Q_INVOKABLE void cancelProgramming();
	/// FR-COM-4
	Q_INVOKABLE bool readDeviceInfo(const QString& deviceRef);

Q_SIGNALS:
	void stateChanged();
	void endpointChanged();
	void interfacesChanged();
	void operationChanged();

private:
	void setOperation(bool active, const QString& text);
	void finish(const QString& result);

	app::BusSession* m_bus;
	QString m_host;
	int m_port = kDefaultPort;
	bool m_discovering = false;
	QVariantList m_interfaces;
	bool m_operationActive = false;
	bool m_programming = false;
	QString m_operation;
	QString m_lastResult;
};

} // namespace kestrel::viewmodels
