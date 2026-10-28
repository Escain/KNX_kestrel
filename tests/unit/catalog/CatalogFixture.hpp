#pragma once

// Access to the synthetic M-FFFF product data (tests/data/catalog/M-FFFF/*.xml and M-FFFF.knxprod).

#include "kestrel/catalog/Knxprod.hpp"

#include <QDir>
#include <QFile>
#include <QString>

namespace fixture
{

inline constexpr char16_t kButton[] = u"M-FFFF_A-0001-10-0001";
inline constexpr char16_t kActuator[] = u"M-FFFF_A-0002-21-0001";

inline QString dataDir()
{
	return QStringLiteral(KESTREL_TEST_DATA_DIR);
}

inline QString knxprodPath()
{
	return dataDir() + QStringLiteral("/catalog/M-FFFF.knxprod");
}

/// The fixture folder as the files of an unpacked product file ("M-FFFF/Hardware.xml" → content).
inline kestrel::catalog::PackageFiles folderFiles()
{
	kestrel::catalog::PackageFiles files;
	const QDir dir(dataDir() + QStringLiteral("/catalog/M-FFFF"));
	for (const QString& name : dir.entryList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name))
	{
		QFile file(dir.filePath(name));
		if (file.open(QIODevice::ReadOnly))
		{
			files[QStringLiteral("M-FFFF/") + name] = file.readAll();
		}
	}
	return files;
}

inline QString buttonFile()
{
	return QStringLiteral("M-FFFF/M-FFFF_A-0001-10-0001.xml");
}

inline QString actuatorFile()
{
	return QStringLiteral("M-FFFF/M-FFFF_A-0002-21-0001.xml");
}

} // namespace fixture
