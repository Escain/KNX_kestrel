#pragma once

// One bus telegram as seen by the group monitor (FR-BUS-5). Plain value, cheap to copy in batches.

#include "kestrel/knx/Address.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/Cemi.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>

#include <cstdint>
#include <optional>

namespace kestrel::app
{

struct TelegramRecord
{
	/// Reception time (UTC, ms precision); for outgoing frames the time of the L_Data.con.
	QDateTime timestamp;
	knx::IndividualAddress source;
	/// Raw destination: a group address when isGroup, an individual address otherwise.
	std::uint16_t destination = 0;
	bool isGroup = true;
	/// true for frames sent by this session (confirmed by the interface), false for L_Data.ind.
	bool outgoing = false;
	knx::cemi::Priority priority = knx::cemi::Priority::Low;
	knx::TpduType tpduType = knx::TpduType::DataGroup;
	/// nullopt for transport control frames (T_Connect, T_ACK...) and undecodable TPDUs.
	std::optional<knx::Apci> apci;
	/// Group value (short form expanded to one byte) for group services, the APDU data otherwise.
	QByteArray payload;
	/// Formatted value ("21.5 °C") when the destination's DPT is known and the value decodes, else empty.
	QString decoded;

	knx::GroupAddress groupAddress() const { return knx::GroupAddress(destination); }
	knx::IndividualAddress individualDestination() const { return knx::IndividualAddress(destination); }
	/// "1/1/1" (3-level) or "1.1.5".
	QString destinationText(knx::GroupAddressStyle style = knx::GroupAddressStyle::ThreeLevel) const;
	/// "GroupValueWrite", "T_Connect", ...
	QString serviceText() const;

	friend bool operator==(const TelegramRecord&, const TelegramRecord&) = default;
};

} // namespace kestrel::app

Q_DECLARE_METATYPE(kestrel::app::TelegramRecord)
