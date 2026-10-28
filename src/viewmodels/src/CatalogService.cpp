#include "kestrel/viewmodels/CatalogService.hpp"

#include "kestrel/viewmodels/Format.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimeZone>

#include <utility>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcCatalog, "kestrel.viewmodels.catalog")

namespace kestrel::viewmodels
{

namespace
{

const QString kIndexFile = u"index.json"_s;

QJsonObject toJson(const CatalogEntry& e)
{
	return QJsonObject{
		{u"manufacturerId"_s, e.manufacturerId},
		{u"productId"_s, e.productId},
		{u"productText"_s, e.productText},
		{u"orderNumber"_s, e.orderNumber},
		{u"hardwareName"_s, e.hardwareName},
		{u"programId"_s, e.programId},
		{u"programName"_s, e.programName},
		{u"maskVersion"_s, e.maskVersion},
		{u"applicationNumber"_s, static_cast<qint64>(e.applicationNumber)},
		{u"applicationVersion"_s, static_cast<qint64>(e.applicationVersion)},
		{u"programAvailable"_s, e.programAvailable},
	};
}

CatalogEntry entryFromJson(const QString& file, const QJsonObject& o)
{
	CatalogEntry e;
	e.file = file;
	e.manufacturerId = o.value(u"manufacturerId"_s).toString();
	e.productId = o.value(u"productId"_s).toString();
	e.productText = o.value(u"productText"_s).toString();
	e.orderNumber = o.value(u"orderNumber"_s).toString();
	e.hardwareName = o.value(u"hardwareName"_s).toString();
	e.programId = o.value(u"programId"_s).toString();
	e.programName = o.value(u"programName"_s).toString();
	e.maskVersion = o.value(u"maskVersion"_s).toString();
	e.applicationNumber = static_cast<unsigned>(o.value(u"applicationNumber"_s).toInteger());
	e.applicationVersion = static_cast<unsigned>(o.value(u"applicationVersion"_s).toInteger());
	e.programAvailable = o.value(u"programAvailable"_s).toBool();
	e.key = file + u'|' + e.productId + u'|' + e.programId;
	return e;
}

/// Index entries of a parsed product file.
std::vector<CatalogEntry> entriesOf(const QString& file, const catalog::ProductDatabase& database, const QString& language)
{
	std::vector<CatalogEntry> result;
	for (const catalog::ProductSummary& product : catalog::listProducts(database, language))
	{
		CatalogEntry base;
		base.file = file;
		base.manufacturerId = database.manufacturerId;
		base.productId = product.productId;
		base.productText = product.text.isEmpty() ? product.hardwareName : product.text;
		base.orderNumber = product.orderNumber;
		base.hardwareName = product.hardwareName;
		if (product.programs.empty())
		{
			base.key = file + u'|' + base.productId + u'|';
			result.push_back(base);
			continue;
		}
		for (const catalog::ProgramSummary& program : product.programs)
		{
			CatalogEntry e = base;
			e.programId = program.id;
			e.programName = program.name;
			e.maskVersion = program.maskVersion;
			e.applicationNumber = program.applicationNumber;
			e.applicationVersion = program.applicationVersion;
			e.programAvailable = program.available;
			e.key = file + u'|' + e.productId + u'|' + e.programId;
			result.push_back(std::move(e));
		}
	}
	return result;
}

bool sameContent(const QString& a, const QString& b)
{
	QFile fa(a);
	QFile fb(b);
	if (fa.size() != fb.size() || !fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly))
		return false;
	while (!fa.atEnd())
	{
		if (fa.read(1 << 20) != fb.read(1 << 20))
			return false;
	}
	return true;
}

} // namespace

QString CatalogEntry::versionText() const
{
	if (programId.isEmpty())
		return {};
	return u"%1.%2"_s.arg(applicationVersion >> 4).arg(applicationVersion & 0x0F);
}

CatalogService::CatalogService(const QString& directory, QObject* parent)
	: QObject(parent), m_directory(directory.isEmpty() ? defaultDirectory() : directory)
{
	m_pool.setMaxThreadCount(2);
}

CatalogService::~CatalogService()
{
	// Workers post their results to this object; Qt drops those events once it is gone.
	m_pool.waitForDone();
}

QString CatalogService::defaultDirectory()
{
	const QString fromEnv = qEnvironmentVariable("KESTREL_CATALOG_DIR");
	if (!fromEnv.isEmpty())
		return QDir::cleanPath(fromEnv);
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/catalog"_s;
}

QString CatalogService::pathOf(const QString& file) const
{
	return QDir(m_directory).filePath(file);
}

// --- Index -------------------------------------------------------------------------------------------------

void CatalogService::ensureScanned()
{
	if (m_scanned)
		return;
	m_scanned = true;
	loadIndex();
	scanFolder();
}

void CatalogService::rescan()
{
	if (!m_scanned)
	{
		ensureScanned();
		return;
	}
	scanFolder();
}

void CatalogService::loadIndex()
{
	QFile file(pathOf(kIndexFile));
	if (!file.open(QIODevice::ReadOnly))
		return;
	const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	if (root.value(u"format"_s).toInt() != kIndexFormat)
		return;
	for (const QJsonValue& value : root.value(u"files"_s).toArray())
	{
		const QJsonObject o = value.toObject();
		FileRecord record;
		record.file = o.value(u"file"_s).toString();
		if (record.file.isEmpty() || record.file.contains(u'/'))
			continue;
		record.size = o.value(u"size"_s).toInteger();
		record.modified = o.value(u"modified"_s).toInteger();
		record.language = o.value(u"language"_s).toString();
		record.error = o.value(u"error"_s).toString();
		record.warnings = static_cast<std::size_t>(o.value(u"warnings"_s).toInteger());
		for (const QJsonValue& entry : o.value(u"entries"_s).toArray())
			record.entries.push_back(entryFromJson(record.file, entry.toObject()));
		m_files[record.file] = std::move(record);
	}
}

void CatalogService::saveIndex() const
{
	if (!QDir().mkpath(m_directory))
		return;
	QJsonArray files;
	for (const auto& [name, record] : m_files)
	{
		QJsonArray entries;
		for (const CatalogEntry& e : record.entries)
			entries.append(toJson(e));
		files.append(QJsonObject{
			{u"file"_s, record.file},
			{u"size"_s, record.size},
			{u"modified"_s, record.modified},
			{u"language"_s, record.language},
			{u"error"_s, record.error},
			{u"warnings"_s, static_cast<qint64>(record.warnings)},
			{u"entries"_s, entries},
		});
	}
	QSaveFile out(pathOf(kIndexFile));
	if (!out.open(QIODevice::WriteOnly))
	{
		qCWarning(lcCatalog) << "cannot write" << out.fileName();
		return;
	}
	out.write(QJsonDocument(QJsonObject{{u"format"_s, kIndexFormat}, {u"files"_s, files}}).toJson(QJsonDocument::Indented));
	out.commit();
}

void CatalogService::scanFolder()
{
	const QDir dir(m_directory);
	const QFileInfoList found = dir.entryInfoList({u"*.knxprod"_s}, QDir::Files | QDir::Readable, QDir::Name);
	QSet<QString> present;
	bool changed = false;
	for (const QFileInfo& info : found)
	{
		const QString name = info.fileName();
		present.insert(name);
		const auto it = m_files.find(name);
		const qint64 modified = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
		if (it != m_files.end() && it->second.size == info.size() && it->second.modified == modified
			&& it->second.language == m_language)
		{
			continue;
		}
		startIndexing(name);
	}
	for (auto it = m_files.begin(); it != m_files.end();)
	{
		if (!present.contains(it->first) && !m_indexing.contains(it->first))
		{
			it = m_files.erase(it);
			changed = true;
		}
		else
		{
			++it;
		}
	}
	rebuildEntries();
	if (changed)
		saveIndex();
	setBusyState();
}

void CatalogService::startIndexing(const QString& file)
{
	if (m_indexing.contains(file))
		return;
	m_indexing.insert(file);
	const QString path = pathOf(file);
	const QString language = m_language;
	m_pool.start(
		[this, file, path, language]
		{
			FileRecord record;
			record.file = file;
			record.language = language;
			const QFileInfo info(path);
			record.size = info.size();
			record.modified = info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
			std::shared_ptr<const catalog::LoadedDatabase> database;
			auto loaded = catalog::loadKnxprodFile(path);
			if (!loaded)
			{
				record.error = errorText(loaded.error());
			}
			else
			{
				record.warnings = loaded->warnings.size();
				record.entries = entriesOf(file, loaded->database, language);
				database = std::make_shared<const catalog::LoadedDatabase>(std::move(loaded).value());
			}
			QMetaObject::invokeMethod(
				this, [this, file, record = std::move(record), database]() mutable
				{ onIndexed(file, std::move(record), std::move(database)); },
				Qt::QueuedConnection);
		});
	setBusyState();
}

void CatalogService::onIndexed(const QString& file, FileRecord record, std::shared_ptr<const catalog::LoadedDatabase> database)
{
	m_indexing.remove(file);
	const bool imported = m_importing.remove(file);
	if (imported && !record.error.isEmpty())
	{
		QFile::remove(pathOf(file));
		m_files.erase(file);
		rebuildEntries();
		saveIndex();
		Q_EMIT importFailed(file, record.error);
		setBusyState();
		return;
	}
	if (!record.error.isEmpty())
		qCWarning(lcCatalog) << file << record.error;
	if (database)
		store(file, std::move(database));
	m_files[file] = std::move(record);
	rebuildEntries();
	saveIndex();
	Q_EMIT fileIndexed(file);
	setBusyState();
}

void CatalogService::rebuildEntries()
{
	std::vector<CatalogEntry> entries;
	QHash<QString, QString> programFiles;
	for (const auto& [name, record] : m_files)
	{
		for (const CatalogEntry& e : record.entries)
		{
			entries.push_back(e);
			if (!e.programId.isEmpty() && e.programAvailable && !programFiles.contains(e.programId))
				programFiles.insert(e.programId, e.file);
		}
	}
	{
		const QMutexLocker lock(&m_mutex);
		m_programFiles = std::move(programFiles);
	}
	if (entries == m_entries)
		return;
	m_entries = std::move(entries);
	m_entryIndex.clear();
	for (std::size_t i = 0; i < m_entries.size(); ++i)
		m_entryIndex.insert(m_entries[i].key, i);
	Q_EMIT entriesChanged();
}

const CatalogEntry* CatalogService::entry(const QString& key) const
{
	const auto it = m_entryIndex.constFind(key);
	return it == m_entryIndex.cend() ? nullptr : &m_entries[*it];
}

const CatalogEntry* CatalogService::entryForProgram(const QString& programId) const
{
	if (programId.isEmpty())
		return nullptr;
	const CatalogEntry* fallback = nullptr;
	for (const CatalogEntry& e : m_entries)
	{
		if (e.programId != programId)
			continue;
		if (e.programAvailable)
			return &e;
		if (!fallback)
			fallback = &e;
	}
	return fallback;
}

const CatalogEntry* CatalogService::entryForProduct(const QString& productId) const
{
	for (const CatalogEntry& e : m_entries)
	{
		if (e.productId == productId)
			return &e;
	}
	return nullptr;
}

QStringList CatalogService::problems() const
{
	QStringList result;
	for (const auto& [name, record] : m_files)
	{
		if (!record.error.isEmpty())
			result.append(u"%1: %2"_s.arg(name, record.error));
	}
	return result;
}

bool CatalogService::isBusy() const
{
	return !m_indexing.isEmpty() || !m_loading.isEmpty();
}

QStringList CatalogService::pendingFiles() const
{
	QStringList files(m_indexing.begin(), m_indexing.end());
	files.sort();
	return files;
}

void CatalogService::setBusyState()
{
	const bool busy = isBusy();
	if (busy != m_wasBusy)
	{
		m_wasBusy = busy;
		Q_EMIT busyChanged();
	}
}

// --- Import ------------------------------------------------------------------------------------------------

Result<QString> CatalogService::importFile(const QString& path)
{
	ensureScanned();
	const QFileInfo source(path);
	if (!source.isFile() || !source.isReadable())
		return makeError(ErrorCode::NotFound, tr("Cannot read %1").arg(QDir::toNativeSeparators(path)).toStdString());
	if (source.suffix().compare(u"knxprod"_s, Qt::CaseInsensitive) != 0)
		return makeError(ErrorCode::InvalidArgument,
			tr("%1 is not a product file (*.knxprod)").arg(source.fileName()).toStdString());
	if (!QDir().mkpath(m_directory))
		return makeError(ErrorCode::IoError,
			tr("Cannot create the catalog folder %1").arg(QDir::toNativeSeparators(m_directory)).toStdString());

	// Already there (same content, any name)?
	const QDir dir(m_directory);
	for (const QFileInfo& existing : dir.entryInfoList({u"*.knxprod"_s}, QDir::Files))
	{
		if (existing.size() == source.size() && sameContent(existing.absoluteFilePath(), source.absoluteFilePath()))
		{
			if (!m_files.contains(existing.fileName()))
				startIndexing(existing.fileName());
			return existing.fileName();
		}
	}

	QString name = source.fileName();
	for (int n = 2; dir.exists(name); ++n)
		name = u"%1-%2.%3"_s.arg(source.completeBaseName()).arg(n).arg(source.suffix());
	if (!QFile::copy(source.absoluteFilePath(), dir.filePath(name)))
		return makeError(ErrorCode::IoError, tr("Cannot copy %1 into the catalog").arg(source.fileName()).toStdString());
	m_importing.insert(name);
	startIndexing(name);
	return name;
}

// --- Databases ---------------------------------------------------------------------------------------------

void CatalogService::store(const QString& file, std::shared_ptr<const catalog::LoadedDatabase> database) const
{
	const QMutexLocker lock(&m_mutex);
	m_cache.remove_if([&](const auto& item) { return item.first == file; });
	m_cache.emplace_front(file, std::move(database));
	while (m_cache.size() > kCacheSize)
		m_cache.pop_back();
}

std::shared_ptr<const catalog::LoadedDatabase> CatalogService::cachedDatabase(const QString& file) const
{
	const QMutexLocker lock(&m_mutex);
	for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
	{
		if (it->first == file)
		{
			m_cache.splice(m_cache.begin(), m_cache, it);
			return m_cache.front().second;
		}
	}
	return nullptr;
}

std::shared_ptr<const catalog::LoadedDatabase> CatalogService::loadDatabase(const QString& file)
{
	if (auto cached = cachedDatabase(file))
		return cached;
	auto loaded = catalog::loadKnxprodFile(pathOf(file));
	if (!loaded)
		return nullptr;
	auto database = std::make_shared<const catalog::LoadedDatabase>(std::move(loaded).value());
	store(file, database);
	return database;
}

std::shared_ptr<const catalog::LoadedDatabase> CatalogService::databaseForProgram(const QString& programId)
{
	QString file;
	{
		const QMutexLocker lock(&m_mutex);
		file = m_programFiles.value(programId);
	}
	return file.isEmpty() ? nullptr : loadDatabase(file);
}

bool CatalogService::requestDatabase(const QString& file)
{
	if (cachedDatabase(file))
		return true;
	if (m_loading.contains(file))
		return false;
	m_loading.insert(file);
	const QString path = pathOf(file);
	m_pool.start(
		[this, file, path]
		{
			auto loaded = catalog::loadKnxprodFile(path);
			std::shared_ptr<const catalog::LoadedDatabase> database;
			QString error;
			if (loaded)
				database = std::make_shared<const catalog::LoadedDatabase>(std::move(loaded).value());
			else
				error = errorText(loaded.error());
			QMetaObject::invokeMethod(
				this,
				[this, file, database, error]
				{
					m_loading.remove(file);
					if (database)
					{
						m_loadErrors.remove(file);
						store(file, database);
						Q_EMIT databaseReady(file);
					}
					else
					{
						m_loadErrors.insert(file, error);
						Q_EMIT databaseFailed(file, error);
					}
					setBusyState();
				},
				Qt::QueuedConnection);
		});
	setBusyState();
	return false;
}

QString CatalogService::databaseError(const QString& file) const
{
	return m_loadErrors.value(file);
}

bool CatalogService::waitForIdle(int timeoutMs)
{
	const QDeadlineTimer deadline(timeoutMs);
	while (isBusy())
	{
		if (deadline.hasExpired())
			return false;
		m_pool.waitForDone(10);
		QCoreApplication::sendPostedEvents(this);
		QCoreApplication::processEvents();
	}
	return true;
}

} // namespace kestrel::viewmodels
