#pragma once

// Reversible primitive operations recorded by Tx (implementation detail of Project/History).

#include "kestrel/model/Entities.hpp"

#include <cstddef>
#include <variant>

namespace kestrel::model::detail
{

/// Insert: `after` holds the inserted entity. Erase: `before` holds the erased entity.
/// Replace: whole-entity before/after. `pos` is the row of the entity (insert/erase only).
template <class T>
struct RowOp
{
	enum class Kind
	{
		Insert,
		Erase,
		Replace,
	};

	Kind kind = Kind::Insert;
	T before;
	T after;
	std::size_t pos = 0;
};

struct SettingsOp
{
	ProjectSettings before;
	ProjectSettings after;
};

using Op = std::variant<RowOp<BuildingPart>, RowOp<Area>, RowOp<Line>, RowOp<Device>, RowOp<ComObject>,
	RowOp<GroupRange>, RowOp<GroupAddress>, RowOp<Link>, SettingsOp>;

} // namespace kestrel::model::detail
