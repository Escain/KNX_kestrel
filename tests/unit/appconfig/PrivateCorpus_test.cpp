// Opt-in golden test against real vendor data (ADR-009: never committed). Runs when KESTREL_PRIVATE_CORPUS
// points to a directory with meanwell/KAA-8R_R21.knxprod and meanwell/backup-15.15.2-*/obj<N>*.bin, the
// segments ETS6 downloaded to a MEAN WELL KAA-8R. Kestrel must rebuild all four segments byte for byte.

#include "kestrel/appconfig/DownloadPlan.hpp"
#include "kestrel/appconfig/Images.hpp"
#include "kestrel/appconfig/Import.hpp"
#include "kestrel/model/Operations.hpp"
#include "kestrel/catalog/Knxprod.hpp"

#include <Printers.hpp>
#include <QtPrinters.hpp>
#include <UnitTests.hpp>

#include <QDir>
#include <QFile>

#include <iostream>

using namespace kestrel;
using namespace kestrel::appconfig;
using namespace Qt::StringLiterals;

namespace
{

std::map<std::uint8_t, Bytes> readImages(const QString& directory)
{
	std::map<std::uint8_t, Bytes> images;
	const QDir dir(directory);
	for (const QString& name : dir.entryList({u"obj*.bin"_s}, QDir::Files))
	{
		QFile file(dir.filePath(name));
		if (file.open(QIODevice::ReadOnly))
		{
			const QByteArray content = file.readAll();
			images[static_cast<std::uint8_t>(name.mid(3, 1).toUInt())] = Bytes(content.begin(), content.end());
		}
	}
	return images;
}

std::uint16_t u16(const Bytes& b, std::size_t at)
{
	return static_cast<std::uint16_t>((b[at] << 8) | b[at + 1]);
}

} // namespace

UNIT_TEST(FR_DEV_2__rebuildsEts6ImagesOfRealDevice)
{
	const QString corpus = qEnvironmentVariable("KESTREL_PRIVATE_CORPUS");
	if (corpus.isEmpty())
	{
		std::cout << "  (KESTREL_PRIVATE_CORPUS not set: golden test skipped)\n";
		return;
	}
	const QDir meanwell(corpus + u"/meanwell"_s);
	const QStringList backups = meanwell.entryList({u"backup-15.15.2-*"_s}, QDir::Dirs, QDir::Name);
	ASSERT(!backups.isEmpty());
	const auto golden = readImages(meanwell.filePath(backups.front()));
	ASSERT_EQ(golden.size(), std::size_t{4});

	auto loaded = catalog::loadKnxprodFile(meanwell.filePath(u"KAA-8R_R21.knxprod"_s));
	ASSERT(loaded.has_value());
	const catalog::ApplicationProgram& app = loaded->database.applicationPrograms.front();

	// Parameter values from the ETS6 parameter segment.
	QStringList warnings;
	const auto values = decodeParameterSegments(app, {{app.relativeSegments.front().id, golden.at(4)}}, warnings);
	ASSERT(values.has_value());
	ASSERT(!values->empty()); // what the installer changed in ETS6 (channel modes, functions, ...)
	std::cout << "  " << values->size() << " non-default parameter values decoded\n";

	// Links from the ETS6 tables: the first association of an object is its sending one.
	const Bytes& addresses = golden.at(1);
	const Bytes& associations = golden.at(2);
	Links links;
	for (std::size_t i = 0; i < u16(associations, 0); ++i)
	{
		const std::uint16_t index = u16(associations, 2 + 4 * i);
		const std::uint16_t object = u16(associations, 4 + 4 * i);
		links[object].push_back(u16(addresses, 2 * std::size_t{index}));
	}

	const auto rebuilt = buildSystemB(app, *values, links);
	ASSERT(rebuilt.has_value());
	for (const std::uint8_t object : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
	{
		ASSERT_EQ(toHex(rebuilt->images.at(object)), toHex(golden.at(object)));
	}
	ASSERT_EQ(programVersion(loaded->database.manufacturerId, app), fromHex("01 62 00 01 21").value());

	// The same through a project: import the device, plan its download, compare the planned images.
	model::Project project(model::IdGenerator::sequential());
	const auto area = model::addArea(project, 15, u"Area"_s).value();
	const auto line = model::addLine(project, area, 15, u"Line"_s).value();
	const auto device = model::addDevice(project, line, 2, u"KAA-8R"_s, u"M-0162_H-KAA.2D8R-1_P-KAA.2D8R"_s, app.id).value();
	ASSERT(importSystemBImages(project, device, app, golden).has_value());
	ASSERT_EQ(project.links().rows().size(), std::size_t{18});
	const auto planned = planDeviceDownload(loaded->database, app, project, device);
	ASSERT(planned.has_value());
	for (const std::uint8_t object : {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}})
	{
		ASSERT_EQ(toHex(planned->images.at(object)), toHex(golden.at(object)));
	}
	ASSERT_EQ(planned->plan.steps.size(), std::size_t{28});
}
