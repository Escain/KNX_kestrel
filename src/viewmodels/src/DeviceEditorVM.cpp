#include "kestrel/viewmodels/DeviceEditorVM.hpp"

#include "kestrel/model/Operations.hpp"
#include "kestrel/viewmodels/CatalogComObjectModel.hpp"
#include "kestrel/viewmodels/CatalogService.hpp"
#include "kestrel/viewmodels/ComObjectTableModel.hpp"
#include "kestrel/viewmodels/DeviceParameterModel.hpp"
#include "kestrel/viewmodels/DeviceStatusService.hpp"
#include "kestrel/viewmodels/Format.hpp"
#include "kestrel/viewmodels/GroupAddressPanelVM.hpp"
#include "kestrel/viewmodels/ProjectSession.hpp"
#include "kestrel/viewmodels/SelectionService.hpp"

#include <QVariantMap>

#include <algorithm>
#include <set>

using namespace Qt::StringLiterals;

namespace kestrel::viewmodels
{

using model::EntityKind;

namespace
{

/// Model com object for a catalog com object (the first link creates it).
Result<model::ComObject> modelComObject(const catalog::EffectiveComObject& source)
{
	if (source.sizeInBit == 0)
	{
		return makeError(ErrorCode::Unsupported,
			QObject::tr("Unknown size \"%1\" of com object %2").arg(source.objectSize).arg(source.number).toStdString());
	}
	model::ComObject object;
	object.number = source.number;
	object.name = source.text.isEmpty() ? source.name : source.text;
	object.function = source.functionText;
	object.sizeInBit = source.sizeInBit;
	object.flags.communication = source.flags.communication;
	object.flags.read = source.flags.read;
	object.flags.write = source.flags.write;
	object.flags.transmit = source.flags.transmit;
	object.flags.update = source.flags.update;
	object.flags.readOnInit = source.flags.readOnInit;
	// Several DPTs may be listed: keep the first one the model accepts.
	for (const QString& dpt : source.datapointType.split(u' ', Qt::SkipEmptyParts))
	{
		if (model::isValidDptRef(dpt))
		{
			object.dpt = dpt;
			break;
		}
	}
	object.catalogRef = source.refId;
	return object;
}

} // namespace

DeviceEditorVM::DeviceEditorVM(const Services& services, model::Id<model::Device> device, CatalogService* catalog,
	ActiveComObjectFilter filter, QObject* parent)
	: PanelVM(services, u"deviceEditor"_s, parent)
	, m_device(device)
	, m_model(new ComObjectTableModel(services.session, device, this))
	, m_catalog(catalog)
	, m_filter(filter ? std::move(filter) : ActiveComObjectFilter(&activeComObjects))
	, m_catalogModel(new CatalogComObjectModel(services.session, device, this))
	, m_parameters(new DeviceParameterModel(this))
{
	connect(session(), &ProjectSession::changed, this, &DeviceEditorVM::onChanged);
	connect(session(), &ProjectSession::projectReplaced, this,
		[this]
		{
			if (!exists())
				Q_EMIT deviceRemoved();
		});
	if (m_catalog)
	{
		connect(m_catalog, &CatalogService::entriesChanged, this,
			[this]
			{
				if (m_catalogState != u"ready"_s)
					loadCatalog();
				else
					Q_EMIT catalogChanged();
			});
		connect(m_catalog, &CatalogService::busyChanged, this,
			[this]
			{
				if (m_catalogState == u"loading"_s && !m_catalog->isBusy())
					loadCatalog();
			});
		connect(m_catalog, &CatalogService::databaseReady, this,
			[this](const QString& file)
			{
				if (file == m_catalogFile && m_catalogState != u"ready"_s)
					loadCatalog();
			});
		connect(m_catalog, &CatalogService::databaseFailed, this,
			[this](const QString& file, const QString& message)
			{
				if (file == m_catalogFile && m_catalogState != u"ready"_s)
					setCatalogState(u"error"_s, tr("Cannot read the product file %1: %2").arg(file, message));
			});
	}
	loadCatalog();
}

DeviceEditorVM::~DeviceEditorVM() = default;

bool DeviceEditorVM::isCatalogDevice() const
{
	return exists() && !project().devices().at(m_device).appProgramRef.isEmpty();
}

void DeviceEditorVM::setCatalogState(const QString& state, const QString& message)
{
	if (state == m_catalogState && message == m_catalogMessage)
		return;
	m_catalogState = state;
	m_catalogMessage = message;
	Q_EMIT catalogChanged();
}

void DeviceEditorVM::loadCatalog()
{
	if (!isCatalogDevice())
	{
		m_program = nullptr;
		m_database.reset();
		m_loadedProgram.clear();
		m_catalogModel->setObjects({}, {});
		m_parameters->setRows({});
		setCatalogState(u"none"_s, {});
		return;
	}
	const QString programId = project().devices().at(m_device).appProgramRef;
	if (m_program && m_loadedProgram == programId)
	{
		refreshComObjects();
		setCatalogState(u"ready"_s, {});
		return;
	}
	m_program = nullptr;
	m_database.reset();
	if (!m_catalog)
	{
		setCatalogState(u"missing"_s, tr("No product catalog is available."));
		return;
	}
	m_catalog->ensureScanned();
	const CatalogEntry* entry = m_catalog->entryForProgram(programId);
	if (!entry || !entry->programAvailable)
	{
		if (m_catalog->isBusy())
			setCatalogState(u"loading"_s, tr("Reading the product catalog…"));
		else
			setCatalogState(u"missing"_s,
				tr("The application program %1 is not in the catalog. Import its product file (Catalog ▸ Import Product File…).")
					.arg(programId));
		return;
	}
	m_catalogFile = entry->file;
	const auto database = m_catalog->cachedDatabase(m_catalogFile);
	if (!database)
	{
		setCatalogState(u"loading"_s, tr("Reading %1…").arg(m_catalogFile));
		if (m_catalog->requestDatabase(m_catalogFile))
			loadCatalog();
		return;
	}
	const catalog::ApplicationProgram* program = database->database.findApplicationProgram(programId);
	if (!program)
	{
		setCatalogState(u"missing"_s, tr("The application program %1 is missing from %2.").arg(programId, m_catalogFile));
		return;
	}
	m_database = database;
	m_program = program;
	m_loadedProgram = programId;
	const QString language = project().settings().contentLanguage;
	m_channelTexts.clear();
	for (const catalog::Channel& channel : program->channels)
	{
		if (channel.id.isEmpty())
			continue;
		const QString text = program->text(channel.id, u"Text"_s, language);
		m_channelTexts.insert(channel.id, text.isEmpty() ? channel.name : text);
	}
	refreshComObjects();
	setCatalogState(u"ready"_s, {});
}

void DeviceEditorVM::refreshComObjects()
{
	if (!m_program || !exists())
		return;
	const model::Device& device = project().devices().at(m_device);
	const QString language = project().settings().contentLanguage;
	const auto evaluation = appconfig::evaluate(*m_program, parameterValues(*m_program, device));
	std::vector<catalog::EffectiveComObject> objects = m_filter(*m_program, device, language);

	// Linked com objects the parameters deactivated stay visible (greyed) so that their links can be removed.
	QSet<QString> shown;
	for (const catalog::EffectiveComObject& object : objects)
		shown.insert(object.refId);
	QSet<QString> inactive;
	std::vector<catalog::EffectiveComObject> all;
	for (const auto id : project().comObjectsOf(m_device))
	{
		const model::ComObject& object = project().comObjects().at(id);
		if (object.catalogRef.isEmpty() || shown.contains(object.catalogRef) || project().linksOfComObject(id).empty())
			continue;
		if (evaluation && evaluation->isComObjectRefActive(object.catalogRef))
			continue; // active, hidden by the filter (another ref of the same number is shown)
		if (all.empty())
			all = m_program->effectiveComObjects(language);
		const auto found = std::find_if(all.begin(), all.end(), [&](const auto& e) { return e.refId == object.catalogRef; });
		if (found == all.end())
			continue;
		inactive.insert(object.catalogRef);
		shown.insert(object.catalogRef);
		objects.push_back(*found);
	}
	std::stable_sort(objects.begin(), objects.end(), [](const auto& a, const auto& b) { return a.number < b.number; });
	m_inactiveLinkedCount = static_cast<int>(inactive.size());
	m_catalogModel->setObjects(std::move(objects), m_channelTexts, inactive);

	if (evaluation)
	{
		m_parameters->setRows(parameterRows(*m_program, device, *evaluation, language));
		m_parameterWarnings = evaluation->warnings.join(u'\n');
	}
	else
	{
		m_parameters->setRows({});
		m_parameterWarnings = errorText(evaluation.error());
	}
	Q_EMIT parametersChanged();
	if (!m_currentObject.isEmpty() && !m_catalogModel->object(m_currentObject))
	{
		m_currentObject.clear();
		Q_EMIT currentChanged();
	}
}

void DeviceEditorVM::setDeviceStatus(DeviceStatusService* status)
{
	m_status = status;
	if (!m_status)
		return;
	connect(m_status, &DeviceStatusService::deviceStatusChanged, this,
		[this](model::Id<model::Device> device)
		{
			if (device == m_device)
				Q_EMIT downloadStateChanged();
		});
	Q_EMIT downloadStateChanged();
}

QString DeviceEditorVM::downloadState() const
{
	return m_status && isCatalogDevice() ? m_status->describe(m_device) : QString();
}

bool DeviceEditorVM::needsDownload() const
{
	return m_status && isCatalogDevice() && m_status->downloadState(m_device) == DeviceStatusService::DownloadState::NeedsDownload;
}

QString DeviceEditorVM::productText() const
{
	if (!exists())
		return {};
	const model::Device& device = project().devices().at(m_device);
	if (m_catalog)
	{
		if (const CatalogEntry* entry = m_catalog->entryForProduct(device.productRef))
			return entry->productText;
		if (const CatalogEntry* entry = m_catalog->entryForProgram(device.appProgramRef))
			return entry->productText;
	}
	return device.productRef;
}

QString DeviceEditorVM::orderNumber() const
{
	if (!exists() || !m_catalog)
		return {};
	const model::Device& device = project().devices().at(m_device);
	const CatalogEntry* entry = m_catalog->entryForProduct(device.productRef);
	return entry ? entry->orderNumber : QString();
}

QString DeviceEditorVM::applicationText() const
{
	if (!exists())
		return {};
	const QString programId = project().devices().at(m_device).appProgramRef;
	if (m_catalog)
	{
		if (const CatalogEntry* entry = m_catalog->entryForProgram(programId))
			return u"%1 %2 (%3)"_s.arg(entry->programName, entry->versionText(), entry->maskVersion);
	}
	return programId;
}

QAbstractItemModel* DeviceEditorVM::catalogComObjects() const
{
	return m_catalogModel;
}

QVariantList DeviceEditorVM::groupAddressOptions() const
{
	std::vector<const model::GroupAddress*> sorted;
	for (const model::GroupAddress& address : project().groupAddresses())
		sorted.push_back(&address);
	std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return a->raw < b->raw; });
	QVariantList result;
	result.reserve(static_cast<qsizetype>(sorted.size()));
	for (const model::GroupAddress* address : sorted)
	{
		const QString text = groupAddressText(project(), address->raw);
		result.append(QVariantMap{
			{u"text"_s, address->name.isEmpty() ? text : u"%1  %2"_s.arg(text, address->name)},
			{u"address"_s, text},
			{u"ref"_s, model::EntityRef(address->id).toString()},
		});
	}
	return result;
}

std::optional<model::Id<model::ComObject>> DeviceEditorVM::shownComObject() const
{
	if (isCatalogDevice() && m_catalogState == u"ready"_s)
		return m_catalogModel->modelObject(m_currentObject);
	if (m_current && project().comObjects().contains(*m_current))
		return m_current;
	return std::nullopt;
}

void DeviceEditorVM::setCurrentObjectRow(int row)
{
	setCurrentObject(m_catalogModel->keyAt(row));
}

void DeviceEditorVM::setCurrentObject(const QString& refId)
{
	const QString next = m_catalogModel->object(refId) ? refId : QString();
	if (next != m_currentObject)
	{
		m_currentObject = next;
		Q_EMIT currentChanged();
	}
	if (const auto object = m_catalogModel->modelObject(m_currentObject))
		selection()->select(*object, origin());
}

std::optional<model::Id<model::GroupAddress>> DeviceEditorVM::findGroupAddress(const QString& text, std::optional<std::uint16_t>& raw)
{
	raw.reset();
	const QString trimmed = text.trimmed();
	if (const auto ref = model::EntityRef::parse(trimmed); ref && ref->is<model::GroupAddress>())
	{
		if (project().groupAddresses().contains(ref->as<model::GroupAddress>()))
			return ref->as<model::GroupAddress>();
		setError(tr("The group address no longer exists."));
		return std::nullopt;
	}
	// "1/2/3" or "1/2/3  Name" (an entry of groupAddressOptions).
	const QString first = trimmed.section(u' ', 0, 0, QString::SectionSkipEmpty);
	const auto parsed = parseGroupAddress(first);
	if (!parsed)
	{
		setError(tr("Not a group address: %1").arg(text));
		return std::nullopt;
	}
	for (const model::GroupAddress& address : project().groupAddresses())
	{
		if (address.raw == *parsed)
			return address.id;
	}
	raw = *parsed;
	return std::nullopt;
}

bool DeviceEditorVM::linkObject(const QString& refId, const QString& address)
{
	const catalog::EffectiveComObject* source = m_catalogModel->object(refId);
	if (!source)
	{
		setError(tr("Not an active com object of this device: %1").arg(refId));
		return false;
	}
	std::optional<std::uint16_t> newRaw;
	const auto existing = findGroupAddress(address, newRaw);
	if (!existing && !newRaw)
		return false;
	auto object = modelComObject(*source);
	if (!object)
	{
		setError(object.error());
		return false;
	}
	const auto present = m_catalogModel->modelObject(refId);
	const bool ok = check(project().edit(tr("Link group address"),
		[&](model::Tx&) -> Status
		{
			model::Id<model::GroupAddress> target;
			if (existing)
			{
				target = *existing;
			}
			else
			{
				QString name = object->name;
				if (!object->function.isEmpty())
					name = u"%1 %2"_s.arg(name, object->function).trimmed();
				KESTREL_TRY(target, model::addGroupAddress(project(), knx::GroupAddress(*newRaw), name, object->dpt,
										innermostGroupRange(project(), *newRaw)));
			}
			model::Id<model::ComObject> comObject;
			if (present)
			{
				comObject = *present;
			}
			else
			{
				KESTREL_TRY(comObject, model::addComObject(project(), m_device, *object));
			}
			KESTREL_TRY(const auto link, model::link(project(), comObject, target));
			(void)link;
			return kestrel::ok();
		}));
	if (ok)
		setCurrentObject(refId);
	return ok;
}

QAbstractItemModel* DeviceEditorVM::parameters() const
{
	return m_parameters;
}

int DeviceEditorVM::parameterCount() const
{
	return m_parameters->parameterCount();
}

bool DeviceEditorVM::setParameter(const QString& refId, const QString& value)
{
	const ParameterRow* row = m_parameters->row(refId);
	if (!row || row->header || !m_program)
	{
		setError(tr("Not an active parameter of this device: %1").arg(refId));
		return false;
	}
	if (row->readOnly)
	{
		setError(tr("The parameter \"%1\" is read-only.").arg(row->text));
		return false;
	}
	QString canonical = value.trimmed();
	if (row->editor == u"enum"_s)
	{
		const bool known = std::any_of(row->options.begin(), row->options.end(),
			[&](const QVariant& option) { return option.toMap().value(u"value"_s).toString() == canonical; });
		if (!known)
		{
			setError(tr("\"%1\" is not a choice of \"%2\".").arg(value, row->text));
			return false;
		}
	}
	else if (row->editor == u"number"_s)
	{
		bool ok = false;
		const qint64 number = canonical.toLongLong(&ok);
		if (!ok || number < row->minimum || number > row->maximum)
		{
			setError(tr("\"%1\" accepts whole numbers from %2 to %3.").arg(row->text).arg(row->minimum).arg(row->maximum));
			return false;
		}
		canonical = QString::number(number);
	}
	else if (row->editor == u"float"_s)
	{
		bool ok = false;
		const double number = canonical.toDouble(&ok);
		if (!ok || number < row->minimumFloat || number > row->maximumFloat)
		{
			setError(tr("\"%1\" accepts numbers from %2 to %3.").arg(row->text).arg(row->minimumFloat).arg(row->maximumFloat));
			return false;
		}
	}
	else if (row->editor != u"text"_s)
	{
		setError(tr("The parameter \"%1\" cannot be edited.").arg(row->text));
		return false;
	}
	// Values belong to the Parameter: other refs of the same parameter must not keep a stale value.
	const catalog::ParameterRef* ref = m_program->findParameterRef(refId);
	QStringList siblings;
	for (const auto& [otherRef, otherValue] : project().devices().at(m_device).params)
	{
		const catalog::ParameterRef* other = m_program->findParameterRef(otherRef);
		if (otherRef != refId && other && ref && other->refId == ref->refId)
			siblings.append(otherRef);
	}
	const std::optional<QString> stored = canonical == row->defaultValue ? std::nullopt : std::optional<QString>(canonical);
	if (siblings.isEmpty())
		return check(model::setDeviceParam(project(), m_device, refId, stored));
	return check(project().edit(tr("Set parameter"),
		[&](model::Tx&) -> Status
		{
			for (const QString& sibling : siblings)
				KESTREL_CHECK(model::setDeviceParam(project(), m_device, sibling, std::nullopt));
			return model::setDeviceParam(project(), m_device, refId, stored);
		}));
}

bool DeviceEditorVM::resetParameter(const QString& refId)
{
	const ParameterRow* row = m_parameters->row(refId);
	if (!row || row->header)
	{
		setError(tr("Not an active parameter of this device: %1").arg(refId));
		return false;
	}
	return setParameter(refId, row->defaultValue);
}

bool DeviceEditorVM::dropOnObjectRow(int row, const QString& ref)
{
	const QString refId = m_catalogModel->keyAt(row);
	if (refId.isEmpty())
		return false;
	const auto parsed = model::EntityRef::parse(ref.trimmed());
	if (!parsed || !parsed->is<model::GroupAddress>())
	{
		setError(tr("Only group addresses can be dropped on a com object."));
		return false;
	}
	return linkObject(refId, ref.trimmed());
}

bool DeviceEditorVM::dropOnComObjectRow(int row, const QString& ref)
{
	const QString object = m_model->refString(m_model->index(row, 0));
	const auto parsed = model::EntityRef::parse(ref.trimmed());
	if (object.isEmpty() || !parsed || !parsed->is<model::GroupAddress>()
		|| !project().groupAddresses().contains(parsed->as<model::GroupAddress>()))
	{
		setError(tr("Only group addresses can be dropped on a group object."));
		return false;
	}
	const auto id = comObjectFor(object);
	return id && check(model::link(project(), *id, parsed->as<model::GroupAddress>()));
}

bool DeviceEditorVM::exists() const
{
	return project().devices().contains(m_device);
}

QString DeviceEditorVM::title() const
{
	return exists() ? entityLabel(project(), m_device) : QString();
}

QString DeviceEditorVM::address() const
{
	return individualAddressText(project(), m_device);
}

QString DeviceEditorVM::name() const
{
	return exists() ? project().devices().at(m_device).name : QString();
}

QAbstractItemModel* DeviceEditorVM::comObjects() const
{
	return m_model;
}

int DeviceEditorVM::nextNumber() const
{
	std::set<unsigned> used;
	for (const auto id : project().comObjectsOf(m_device))
		used.insert(project().comObjects().at(id).number);
	unsigned number = 0;
	while (used.contains(number))
		++number;
	return static_cast<int>(number);
}

QString DeviceEditorVM::current() const
{
	return m_current ? model::EntityRef(*m_current).toString() : QString();
}

QString DeviceEditorVM::currentLabel() const
{
	if (isCatalogDevice() && m_catalogState == u"ready"_s)
	{
		const catalog::EffectiveComObject* object = m_catalogModel->object(m_currentObject);
		if (!object)
			return {};
		const QString text = object->text.isEmpty() ? object->name : object->text;
		return object->functionText.isEmpty() ? u"%1: %2"_s.arg(object->number).arg(text)
											  : u"%1: %2 – %3"_s.arg(object->number).arg(text, object->functionText);
	}
	if (!m_current || !project().comObjects().contains(*m_current))
		return {};
	const model::ComObject& object = project().comObjects().at(*m_current);
	return u"%1: %2"_s.arg(object.number).arg(object.name);
}

QVariantList DeviceEditorVM::currentLinks() const
{
	QVariantList result;
	const auto shown = shownComObject();
	if (!shown)
		return result;
	auto links = project().linksOfComObject(*shown);
	std::stable_partition(links.begin(), links.end(), [&](auto id) { return project().links().at(id).sending; });
	for (const auto id : links)
	{
		const model::Link& link = project().links().at(id);
		const model::GroupAddress& address = project().groupAddresses().at(link.groupAddress);
		result.append(QVariantMap{
			{u"ref"_s, model::EntityRef(id).toString()},
			{u"address"_s, groupAddressText(project(), address.raw)},
			{u"name"_s, address.name},
			{u"sending"_s, link.sending},
		});
	}
	return result;
}

QVariantList DeviceEditorVM::sizeOptions()
{
	QVariantList options;
	for (const unsigned bits : {1u, 2u, 4u, 8u, 16u, 24u, 32u, 48u, 64u, 112u})
		options.append(QVariantMap{{u"text"_s, sizeText(bits)}, {u"value"_s, bits}});
	return options;
}

std::optional<model::Id<model::ComObject>> DeviceEditorVM::comObjectFor(const QString& ref)
{
	const auto parsed = parseRef(ref);
	if (!parsed)
		return std::nullopt;
	if (!parsed->is<model::ComObject>() || project().comObjects().at(parsed->as<model::ComObject>()).device != m_device)
	{
		setError(tr("Not a group object of this device: %1").arg(ref));
		return std::nullopt;
	}
	return parsed->as<model::ComObject>();
}

std::optional<model::Id<model::Link>> DeviceEditorVM::linkFor(const QString& ref)
{
	const auto parsed = parseRef(ref);
	if (!parsed)
		return std::nullopt;
	if (!parsed->is<model::Link>())
	{
		setError(tr("Not an association: %1").arg(ref));
		return std::nullopt;
	}
	return parsed->as<model::Link>();
}

QString DeviceEditorVM::addComObject(const QString& name, const QString& function, int sizeInBit, const QString& flags, const QString& dpt)
{
	const auto parsedFlags = model::ComObjectFlags::parse(flags.trimmed().toUpper());
	if (!parsedFlags)
	{
		setError(tr("Flags are letters of CRWTUI: %1").arg(flags));
		return {};
	}
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return {};
	}
	if (sizeInBit <= 0)
	{
		setError(tr("Invalid size."));
		return {};
	}
	model::ComObject object;
	object.number = static_cast<unsigned>(nextNumber());
	object.name = name.trimmed().isEmpty() ? tr("Object %1").arg(object.number) : name.trimmed();
	object.function = function.trimmed();
	object.sizeInBit = static_cast<unsigned>(sizeInBit);
	object.flags = *parsedFlags;
	object.dpt = *normalized;
	const auto id = check(model::addComObject(project(), m_device, std::move(object)));
	if (!id)
		return {};
	setCurrent(model::EntityRef(*id).toString());
	return model::EntityRef(*id).toString();
}

bool DeviceEditorVM::removeComObject(const QString& ref)
{
	const auto id = comObjectFor(ref);
	return id && check(model::removeComObject(project(), *id));
}

void DeviceEditorVM::setCurrentIndex(const QModelIndex& index)
{
	setCurrent(m_model->refString(m_model->index(index.row(), 0)));
}

void DeviceEditorVM::setCurrent(const QString& ref)
{
	std::optional<model::Id<model::ComObject>> next;
	if (const auto parsed = model::EntityRef::parse(ref); parsed && parsed->is<model::ComObject>()
		&& project().comObjects().contains(parsed->as<model::ComObject>()))
	{
		next = parsed->as<model::ComObject>();
	}
	if (next != m_current)
	{
		m_current = next;
		Q_EMIT currentChanged();
	}
	if (m_current)
	{
		selection()->select(*m_current, origin());
	}
}

bool DeviceEditorVM::linkGroupAddress(const QString& comObjectRef, const QString& address)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	const auto raw = parseGroupAddress(address);
	if (!raw)
	{
		setError(tr("Not a group address: %1").arg(address));
		return false;
	}
	const auto& addresses = project().groupAddresses();
	const auto found = std::find_if(addresses.begin(), addresses.end(), [&](const auto& ga) { return ga.raw == *raw; });
	if (found == addresses.end())
	{
		setError(tr("Group address %1 does not exist.").arg(groupAddressText(project(), *raw)));
		return false;
	}
	return static_cast<bool>(check(model::link(project(), *object, found->id)));
}

bool DeviceEditorVM::unlink(const QString& linkRef)
{
	const auto id = linkFor(linkRef);
	if (!id)
		return false;
	const model::Id<model::ComObject> object = project().links().at(*id).comObject;
	const bool catalogObject = !project().comObjects().at(object).catalogRef.isEmpty();
	return check(project().edit(tr("Unlink group address"),
		[&](model::Tx&) -> Status
		{
			KESTREL_CHECK(model::unlink(project(), *id));
			// A catalog com object exists in the model only while it has links.
			if (catalogObject && project().linksOfComObject(object).empty())
				KESTREL_CHECK(model::removeComObject(project(), object));
			return kestrel::ok();
		}));
}

bool DeviceEditorVM::setSending(const QString& linkRef)
{
	const auto id = linkFor(linkRef);
	return id && check(model::setSendingLink(project(), *id));
}

bool DeviceEditorVM::setFlags(const QString& comObjectRef, const QString& flags)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	const auto parsed = model::ComObjectFlags::parse(flags.trimmed().toUpper());
	if (!parsed)
	{
		setError(tr("Flags are letters of CRWTUI: %1").arg(flags));
		return false;
	}
	return check(project().edit(tr("Set flags"),
		[&](model::Tx& tx)
		{
			tx.set(*object, &model::ComObject::flags, *parsed);
			return ok();
		}));
}

bool DeviceEditorVM::setDpt(const QString& comObjectRef, const QString& dpt)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	const auto normalized = normalizeDpt(dpt);
	if (!normalized)
	{
		setError(tr("Not a datapoint type: %1").arg(dpt));
		return false;
	}
	return check(project().edit(tr("Set datapoint type"),
		[&](model::Tx& tx)
		{
			tx.set(*object, &model::ComObject::dpt, *normalized);
			return ok();
		}));
}

bool DeviceEditorVM::setSize(const QString& comObjectRef, int sizeInBit)
{
	const auto object = comObjectFor(comObjectRef);
	if (!object)
		return false;
	if (sizeInBit <= 0 || static_cast<unsigned>(sizeInBit) > model::kMaxComObjectSizeInBit)
	{
		setError(tr("Invalid size."));
		return false;
	}
	return check(project().edit(tr("Set size"),
		[&](model::Tx& tx)
		{
			tx.set(*object, &model::ComObject::sizeInBit, static_cast<unsigned>(sizeInBit));
			return ok();
		}));
}

void DeviceEditorVM::onChanged(const model::ChangeSet& changeSet)
{
	if (changeSet.contains(model::EntityRef(m_device)) && !exists())
	{
		Q_EMIT deviceRemoved();
		return;
	}
	if (m_current && !project().comObjects().contains(*m_current))
	{
		m_current.reset();
	}
	if (changeSet.contains(model::EntityRef(m_device)) || changeSet.touches(model::EntityKind::Link)
		|| changeSet.touches(model::EntityKind::ComObject))
	{
		// Application changed: reload; parameters or links changed: the active (and greyed) objects may differ.
		const QString programId = project().devices().at(m_device).appProgramRef;
		if (programId != m_loadedProgram || m_catalogState != u"ready"_s)
			loadCatalog();
		else
			refreshComObjects();
	}
	if (changeSet.touches(model::EntityKind::GroupAddress) || changeSet.touches(model::EntityKind::Project))
		Q_EMIT groupAddressesChanged();
	Q_EMIT deviceChanged();
	Q_EMIT currentChanged();
}

} // namespace kestrel::viewmodels
