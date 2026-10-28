#pragma once

// Ordered entity table: rows in insertion (sibling) order plus an id -> row index.
// Copy-on-write: copying a Table is O(1); the first mutation of a shared table detaches it.
// Only Project mutates tables; everybody else gets const access.

#include "kestrel/model/Id.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kestrel::model
{

class Project;

template <class T>
class Table
{
	struct Data
	{
		std::vector<T> rows;
		std::unordered_map<std::uint64_t, std::size_t> index;
	};

	std::shared_ptr<Data> m_data = std::make_shared<Data>();

	Data& mutableData()
	{
		if (m_data.use_count() > 1)
		{
			m_data = std::make_shared<Data>(*m_data);
		}
		return *m_data;
	}

	static void reindexFrom(Data& data, std::size_t from)
	{
		for (std::size_t i = from; i < data.rows.size(); ++i)
		{
			data.index[data.rows[i].id.value()] = i;
		}
	}

	friend class Project;

	void insert(T value, std::size_t pos)
	{
		Data& data = mutableData();
		assert(pos <= data.rows.size());
		assert(!data.index.contains(value.id.value()));
		data.rows.insert(data.rows.begin() + static_cast<std::ptrdiff_t>(pos), std::move(value));
		reindexFrom(data, pos);
	}

	void erase(Id<T> id)
	{
		Data& data = mutableData();
		const auto it = data.index.find(id.value());
		assert(it != data.index.end());
		const std::size_t pos = it->second;
		data.index.erase(it);
		data.rows.erase(data.rows.begin() + static_cast<std::ptrdiff_t>(pos));
		reindexFrom(data, pos);
	}

	void replace(T value)
	{
		Data& data = mutableData();
		const auto it = data.index.find(value.id.value());
		assert(it != data.index.end());
		data.rows[it->second] = std::move(value);
	}

public:
	using value_type = T;
	using const_iterator = typename std::vector<T>::const_iterator;

	// Moves are copies on purpose: a moved-from table stays valid (and empty-shared, not null).
	Table() = default;
	Table(const Table&) = default;
	Table& operator=(const Table&) = default;

	std::size_t size() const { return m_data->rows.size(); }
	bool empty() const { return m_data->rows.empty(); }
	const_iterator begin() const { return m_data->rows.begin(); }
	const_iterator end() const { return m_data->rows.end(); }
	const std::vector<T>& rows() const { return m_data->rows; }

	/// Row by position (sibling order is insertion order).
	const T& operator[](std::size_t row) const
	{
		assert(row < m_data->rows.size());
		return m_data->rows[row];
	}

	bool contains(Id<T> id) const { return m_data->index.contains(id.value()); }

	/// nullptr when absent. The pointer is invalidated by the next mutation of the project.
	const T* find(Id<T> id) const
	{
		const auto it = m_data->index.find(id.value());
		return it == m_data->index.end() ? nullptr : &m_data->rows[it->second];
	}

	/// The entity; asserts that it exists.
	const T& at(Id<T> id) const
	{
		const T* entity = find(id);
		assert(entity);
		return *entity;
	}

	std::optional<std::size_t> indexOf(Id<T> id) const
	{
		const auto it = m_data->index.find(id.value());
		if (it == m_data->index.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	friend bool operator==(const Table& a, const Table& b)
	{
		return a.m_data == b.m_data || a.m_data->rows == b.m_data->rows;
	}
};

} // namespace kestrel::model
