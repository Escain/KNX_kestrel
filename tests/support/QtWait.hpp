#pragma once

// QTRY_* of Qt 6.8 does not compile warning-free under -Wconversion; poll with the event loop instead.

#include <QDeadlineTimer>
#include <QTest>

#include <functional>

namespace kestrel::test
{

inline bool waitFor(const std::function<bool()>& condition, int timeoutMs = 5000)
{
	QDeadlineTimer deadline(timeoutMs);
	while (!condition())
	{
		if (deadline.hasExpired())
		{
			return false;
		}
		QTest::qWait(10);
	}
	return true;
}

} // namespace kestrel::test
