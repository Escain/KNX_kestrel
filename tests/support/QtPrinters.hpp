#pragma once

// operator<< for QtCore types used in ASSERT_EQ. Global namespace on purpose (see Printers.hpp).
// Include before <UnitTests.hpp>.

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <ostream>

inline std::ostream& operator<<(std::ostream& os, const QString& value)
{
	return os << '"' << value.toStdString() << '"';
}

// Hex bytes separated by spaces: [0a ff 10]
inline std::ostream& operator<<(std::ostream& os, const QByteArray& value)
{
	return os << '[' << value.toHex(' ').toStdString() << ']';
}

inline std::ostream& operator<<(std::ostream& os, const QStringList& values)
{
	os << '[';
	for (qsizetype i = 0; i < values.size(); ++i)
	{
		os << (i ? ", " : "") << values[i];
	}
	return os << ']';
}
