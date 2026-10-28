#pragma once

// The user's product catalog (specs/08-gui.md §2): a folder of imported .knxprod files and an index of their
// products, plus a cache of parsed product databases.
//
// - Folder: $KESTREL_CATALOG_DIR if set (tests), else QStandardPaths::AppDataLocation + "/catalog".
// - Index: one CatalogEntry per (product, application program) pair, persisted in <folder>/index.json and keyed
//   by file name, size and modification time, so starting the application never re-parses unchanged files.
//   New or changed files are parsed on a worker thread (real files take about 0.7 s each).
// - Lazy: nothing touches the folder until ensureScanned() (the catalog dock, a device editor or an import).
// - Databases: parsed off the GUI thread by requestDatabase() (signals databaseReady / databaseFailed), or
//   synchronously by loadDatabase(). The last kCacheSize databases stay in memory (LRU).
//
// Threading: the object lives on the GUI thread; entries(), the scan and the import are GUI-thread only.
// cachedDatabase(), loadDatabase() and databaseForProgram() are thread-safe (a planner on a worker thread may
// call them).

#include "kestrel/base/Result.hpp"
#include "kestrel/catalog/Knxprod.hpp"

#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <vector>

namespace kestrel::viewmodels
{

/// One product with one of its application programs, as listed by the catalog dock.
struct CatalogEntry
{
	QString key;  ///< "<file>|<productId>|<programId>": unique within the catalog
	QString file; ///< file name inside the catalog folder
	QString manufacturerId;
	QString productId;
	QString productText;
	QString orderNumber;
	QString hardwareName;
	QString programId; ///< empty for a product without application program
	QString programName;
	QString maskVersion;
	unsigned applicationNumber = 0;
	unsigned applicationVersion = 0;
	bool programAvailable = false; ///< the program is part of the file

	/// "1.0" style version text of the application.
	QString versionText() const;

	friend bool operator==(const CatalogEntry&, const CatalogEntry&) = default;
};

class CatalogService : public QObject
{
	Q_OBJECT

public:
	static constexpr std::size_t kCacheSize = 4;
	static constexpr int kIndexFormat = 1;

	/// `directory` empty = defaultDirectory().
	explicit CatalogService(const QString& directory = {}, QObject* parent = nullptr);
	~CatalogService() override;

	/// $KESTREL_CATALOG_DIR, else <AppDataLocation>/catalog.
	static QString defaultDirectory();
	QString directory() const { return m_directory; }
	/// Language of the indexed texts ("en-US").
	QString language() const { return m_language; }

	/// Reads the persisted index and the folder once; files that are new or changed are indexed in the background.
	void ensureScanned();
	/// Scans the folder again (files added or removed by hand).
	void rescan();
	bool isScanned() const { return m_scanned; }
	/// True while files are being indexed or databases parsed.
	bool isBusy() const;
	/// Files being indexed right now.
	QStringList pendingFiles() const;

	const std::vector<CatalogEntry>& entries() const { return m_entries; }
	const CatalogEntry* entry(const QString& key) const;
	/// First entry of an application program id.
	const CatalogEntry* entryForProgram(const QString& programId) const;
	/// First entry of a product id.
	const CatalogEntry* entryForProduct(const QString& productId) const;
	/// "file: message" for files that could not be read.
	QStringList problems() const;

	/// Copies a .knxprod into the folder (a unique name if another file has the same name) and indexes it in
	/// the background. Returns the file name inside the folder. A file whose content is already in the
	/// catalog is not copied twice. If the copy cannot be parsed it is removed again (importFailed).
	Result<QString> importFile(const QString& path);

	// --- Product databases -----------------------------------------------------------------------------------
	/// Already parsed (no I/O). Thread-safe.
	std::shared_ptr<const catalog::LoadedDatabase> cachedDatabase(const QString& file) const;
	/// Parses on the calling thread if needed; nullptr on failure. Thread-safe.
	std::shared_ptr<const catalog::LoadedDatabase> loadDatabase(const QString& file);
	/// loadDatabase() of the file holding an application program; nullptr if unknown. Thread-safe.
	std::shared_ptr<const catalog::LoadedDatabase> databaseForProgram(const QString& programId);
	/// True if the database is cached (databaseReady is not emitted then). Otherwise parses it on a worker
	/// thread and emits databaseReady or databaseFailed later.
	bool requestDatabase(const QString& file);
	/// Last parse error of a file (empty if none).
	QString databaseError(const QString& file) const;

	/// Blocks until no background work is left (tests, shutdown). Returns false on timeout.
	bool waitForIdle(int timeoutMs = 30000);

Q_SIGNALS:
	/// entries() changed (files indexed, removed or imported).
	void entriesChanged();
	void busyChanged();
	void databaseReady(const QString& file);
	void databaseFailed(const QString& file, const QString& message);
	/// A file finished indexing (whether imported or found by a scan).
	void fileIndexed(const QString& file);
	/// An imported file could not be parsed and was removed again.
	void importFailed(const QString& file, const QString& message);

private:
	struct FileRecord
	{
		QString file;
		qint64 size = 0;
		qint64 modified = 0; ///< ms since epoch
		QString language;
		std::vector<CatalogEntry> entries;
		QString error;
		std::size_t warnings = 0;
	};

	QString pathOf(const QString& file) const;
	void loadIndex();
	void saveIndex() const;
	void scanFolder();
	void startIndexing(const QString& file);
	void onIndexed(const QString& file, FileRecord record, std::shared_ptr<const catalog::LoadedDatabase> database);
	void rebuildEntries();
	void store(const QString& file, std::shared_ptr<const catalog::LoadedDatabase> database) const;
	void setBusyState();

	QString m_directory;
	QString m_language = QStringLiteral("en-US");
	bool m_scanned = false;
	bool m_wasBusy = false;
	std::map<QString, FileRecord> m_files; ///< by file name (sorted: stable entry order)
	std::vector<CatalogEntry> m_entries;
	QHash<QString, std::size_t> m_entryIndex;
	QSet<QString> m_indexing;
	QSet<QString> m_importing;
	QSet<QString> m_loading;
	QHash<QString, QString> m_loadErrors;

	mutable QMutex m_mutex; ///< guards the cache and m_programFiles
	mutable std::list<std::pair<QString, std::shared_ptr<const catalog::LoadedDatabase>>> m_cache; ///< MRU first
	QHash<QString, QString> m_programFiles; ///< program id -> file

	QThreadPool m_pool;
};

} // namespace kestrel::viewmodels
