#pragma once

// Fetches a product file from its manufacturer (specs/07-catalog.md §6): an index entry's download, checked
// against the SHA-256 the index recorded, and the .knxprod files taken out of it.

#include "kestrel/base/Result.hpp"
#include "kestrel/catalog/ProductIndex.hpp"

#include <QObject>
#include <QPointer>

#include <functional>

class QNetworkAccessManager;
#include <QNetworkReply>

namespace kestrel::app
{

struct FetchedProduct
{
	std::vector<catalog::ExtractedFile> files;
	QString sha256;
	/// False if the download differs from the one the index was made with (the manufacturer updated it).
	bool matchesIndex = true;
};

class ProductFetcher : public QObject
{
	Q_OBJECT

public:
	/// Downloads above this size are refused.
	static constexpr qint64 kMaxDownloadSize = qint64{256} << 20;

	explicit ProductFetcher(QObject* parent = nullptr);
	~ProductFetcher() override;

	/// One download at a time per fetcher; `done` is called once (Conflict if another download runs).
	/// `progress` gets received and total bytes (total -1 if unknown).
	void fetch(const catalog::IndexedProduct& product, std::function<void(Result<FetchedProduct>)> done,
		std::function<void(qint64, qint64)> progress = {});
	void cancel();
	bool isBusy() const { return m_reply != nullptr; }

private:
	QNetworkAccessManager* m_network;
	QPointer<QNetworkReply> m_reply;
};

} // namespace kestrel::app
