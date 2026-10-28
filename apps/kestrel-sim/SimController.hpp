#pragma once

// JSON-lines front end of the simulator (Kestrel spec 04 §5).
//
// stdout: {"event":"ready","port":N} once listening, then one {"event":"telegram",...} line per bus
// telegram, interleaved with exactly one response line per stdin command: {"ok":true,"cmd":...,...}
// or {"ok":false,"cmd":...,"error":"..."}. Every line is flushed immediately.
//
// stdin commands (one JSON object per line):
//   {"cmd":"press-prog-button","device":"switch-a"}            toggles programming mode
//   {"cmd":"device","device":"switch-a"}                       address, progMode, serial, maskVersion,
//                                                              restartCount, groupObjects
//   {"cmd":"memory","device":"d","address":"0x4000","length":4}           read (hex "data")
//   {"cmd":"memory","device":"d","address":16384,"data":"01 02"}          write
//   {"cmd":"group-value","device":"d","ga":"1/1/1"}                       read the object value (hex)
//   {"cmd":"group-value","device":"d","ga":"1/1/1","value":"01"}          application change (sends if T)
//   {"cmd":"telegrams","since":0}                                         log entries with index >= since
//   {"cmd":"status"}                                                      port, connected clients, telegrams
//   {"cmd":"quit"}

#include "kestrel/simnet/Simulation.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>

namespace kestrel::simapp
{

class SimController : public QObject
{
	Q_OBJECT

public:
	explicit SimController(simnet::Simulation& simulation, QObject* parent = nullptr);

	/// Executes one command line; returns the response object.
	QJsonObject execute(const QString& line);
	void handleLine(const QString& line);

	static void writeLine(const QJsonObject& object);
	static QJsonObject toJson(const sim::Telegram& telegram);

Q_SIGNALS:
	void quitRequested();

private:
	QJsonObject pressProgButton(const QJsonObject& request);
	QJsonObject deviceState(const QJsonObject& request);
	QJsonObject memory(const QJsonObject& request);
	QJsonObject groupValue(const QJsonObject& request);
	QJsonObject telegrams(const QJsonObject& request);
	QJsonObject status();

	simnet::Simulation& m_simulation;
};

} // namespace kestrel::simapp
