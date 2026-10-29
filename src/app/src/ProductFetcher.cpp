#include "kestrel/app/ProductFetcher.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace kestrel::app
{

ProductFetcher::ProductFetcher(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

ProductFetcher::~ProductFetcher()
{
	cancel();
}

void ProductFetcher::fetch(const catalog::IndexedProduct& product, std::function<void(Result<FetchedProduct>)> done,
	std::function<void(qint64, qint64)> progress)
{
	if (m_reply)
	{
		done(makeError(ErrorCode::Conflict, "another product file is being downloaded"));
		return;
	}
	const QUrl url(product.url);
	if (!url.isValid() || (url.scheme() != u"https" && url.scheme() != u"http" && url.scheme() != u"file"))
	{
		done(makeError(ErrorCode::InvalidArgument, "invalid download address: " + product.url.toStdString()));
		return;
	}
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	// Some manufacturers' servers refuse requests without a browser-like agent.
	request.setHeader(QNetworkRequest::UserAgentHeader,
		u"Mozilla/5.0 (compatible; Kestrel/%1; +KNX configuration tool)"_s.arg(QCoreApplication::applicationVersion()));
	request.setTransferTimeout(60000);
	QNetworkReply* reply = m_network->get(request);
	m_reply = reply;
	connect(reply, &QNetworkReply::downloadProgress, this,
		[reply, progress](qint64 received, qint64 total)
		{
			if (received > kMaxDownloadSize || total > kMaxDownloadSize)
				reply->abort();
			else if (progress)
				progress(received, total);
		});
	connect(reply, &QNetworkReply::finished, this,
		[this, reply, product, done = std::move(done)]
		{
			reply->deleteLater();
			m_reply.clear();
			if (reply->error() != QNetworkReply::NoError)
			{
				const bool cancelled = reply->error() == QNetworkReply::OperationCanceledError;
				done(makeError(cancelled ? ErrorCode::Cancelled : ErrorCode::IoError,
					cancelled ? std::string("download cancelled or too large")
							  : "download failed: " + reply->errorString().toStdString()));
				return;
			}
			const QByteArray data = reply->readAll();
			FetchedProduct fetched;
			fetched.sha256 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
			fetched.matchesIndex = product.sha256.isEmpty() || product.sha256 == fetched.sha256;
			QString name = reply->url().fileName();
			const QString disposition = reply->header(QNetworkRequest::ContentDispositionHeader).toString();
			if (const qsizetype at = disposition.indexOf(u"filename="_s); at >= 0)
				name = disposition.mid(at + 9).remove(u'"').section(u';', 0, 0).trimmed();
			if (name.isEmpty() || name == u"files")
				name = product.orderNumber.isEmpty() ? u"download"_s : product.orderNumber;
			auto files = catalog::extractProductFiles(data, name);
			if (!files)
			{
				done(makeError(files.error().code, files.error().message));
				return;
			}
			fetched.files = std::move(*files);
			done(std::move(fetched));
		});
}

void ProductFetcher::cancel()
{
	if (m_reply)
		m_reply->abort();
}

} // namespace kestrel::app
