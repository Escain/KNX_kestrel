#pragma once

// Group addresses dock (FR-GA-1/2/4).

#include "kestrel/viewmodels/PanelVM.hpp"

#include <cstdint>

namespace kestrel::viewmodels
{

class GroupAddressTreeModel;

class GroupAddressPanelVM : public TreePanelVM
{
	Q_OBJECT
	QML_ELEMENT
	QML_UNCREATABLE("View models are created by the application")

public:
	explicit GroupAddressPanelVM(const Services& services, QObject* parent = nullptr);

	GroupAddressTreeModel* groupAddressModel() const;

	/// Adds the next free main group (3-level / 2-level: a whole main group; free style: a block of 256).
	Q_INVOKABLE QString addMainGroup(const QString& name = {});
	/// Adds the next free middle group below the main group `parentRef` (3-level style only).
	Q_INVOKABLE QString addMiddleGroup(const QString& parentRef, const QString& name = {});
	/// Adds the next free group address in the range of `ref` (a range, or an address -> its range;
	/// empty -> outside every range).
	Q_INVOKABLE QString addGroupAddress(const QString& ref, const QString& name = {});
	/// Adds an explicit address ("1/2/3"); it goes into the innermost range containing it.
	Q_INVOKABLE QString addGroupAddressAt(const QString& address, const QString& name, const QString& dpt = {});
	/// Accepts "DPST-9-1", "9.001", "9" …; empty clears the DPT.
	Q_INVOKABLE bool setDpt(const QString& groupAddressRef, const QString& dpt);

	/// Range of a range / group address ref.
	std::optional<model::Id<model::GroupRange>> rangeFor(model::EntityRef ref) const;
	/// Innermost range containing `raw`.
	std::optional<model::Id<model::GroupRange>> innermostRange(std::uint16_t raw) const;

	QStringList contextActions(model::EntityRef ref) const override;
};

/// Normalises a DPT text to the stored form ("9.001" -> "DPST-9-1"); nullopt if malformed. Empty stays empty.
std::optional<QString> normalizeDpt(const QString& text);

} // namespace kestrel::viewmodels
