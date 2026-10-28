#include "kestrel/catalog/Model.hpp"

#include <QHash>

#include <algorithm>

namespace kestrel::catalog
{

using namespace Qt::StringLiterals;

namespace
{

std::ostream& quoted(std::ostream& os, const QString& text)
{
	return os << '"' << text.toStdString() << '"';
}

template <class T>
std::ostream& printOptional(std::ostream& os, const std::optional<T>& value)
{
	if (!value)
	{
		return os << "-";
	}
	if constexpr (std::is_same_v<T, QString>)
	{
		return quoted(os, *value);
	}
	else
	{
		return os << *value;
	}
}

std::ostream& printList(std::ostream& os, const QStringList& values)
{
	os << '[';
	for (qsizetype i = 0; i < values.size(); ++i)
	{
		os << (i ? ", " : "") << values[i].toStdString();
	}
	return os << ']';
}

template <class T>
const T* findById(const std::vector<T>& items, QStringView id)
{
	const auto it = std::find_if(items.begin(), items.end(), [id](const T& item) { return item.id == id; });
	return it == items.end() ? nullptr : &*it;
}

QStringView primaryLanguage(QStringView language)
{
	const qsizetype dash = language.indexOf(u'-');
	return dash < 0 ? language : language.left(dash);
}

} // namespace

std::ostream& operator<<(std::ostream& os, const AttributeMap& attributes)
{
	os << '{';
	bool first = true;
	for (const auto& [name, value] : attributes)
	{
		os << (first ? "" : " ") << name.toStdString() << '=';
		quoted(os, value);
		first = false;
	}
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const Diagnostic& value)
{
	return os << value.file.toStdString() << ':' << value.line << ": " << value.message.toStdString();
}

// --- Translations ------------------------------------------------------------------------------------------

void Translations::setDefault(const QString& id, const QString& attribute, const QString& text)
{
	m_defaults[{id, attribute}] = text;
}

void Translations::add(const QString& language, const QString& id, const QString& attribute, const QString& text)
{
	m_byLanguage[language][{id, attribute}] = text;
}

void Translations::merge(const Translations& other)
{
	for (const auto& [key, text] : other.m_defaults)
	{
		m_defaults[key] = text;
	}
	for (const auto& [language, texts] : other.m_byLanguage)
	{
		auto& target = m_byLanguage[language];
		for (const auto& [key, text] : texts)
		{
			target[key] = text;
		}
	}
}

std::optional<QString> Translations::translation(const QString& id, const QString& attribute,
	const QString& language) const
{
	const Key key{id, attribute};
	for (const auto& [candidate, texts] : m_byLanguage)
	{
		if (candidate.compare(language, Qt::CaseInsensitive) == 0)
		{
			if (const auto it = texts.find(key); it != texts.end())
			{
				return it->second;
			}
		}
	}
	return std::nullopt;
}

QString Translations::text(const QString& id, const QString& attribute, const QString& language) const
{
	const Key key{id, attribute};
	if (!language.isEmpty())
	{
		if (auto exact = translation(id, attribute, language))
		{
			return *exact;
		}
		const QStringView primary = primaryLanguage(language);
		for (const auto& [candidate, texts] : m_byLanguage)
		{
			if (primaryLanguage(candidate).compare(primary, Qt::CaseInsensitive) == 0)
			{
				if (const auto it = texts.find(key); it != texts.end())
				{
					return it->second;
				}
			}
		}
	}
	const auto it = m_defaults.find(key);
	return it == m_defaults.end() ? QString() : it->second;
}

QStringList Translations::languages() const
{
	QStringList out;
	for (const auto& [language, texts] : m_byLanguage)
	{
		if (!texts.empty())
		{
			out.append(language);
		}
	}
	return out;
}

std::ostream& operator<<(std::ostream& os, const Translations& value)
{
	os << "Translations{languages=";
	return printList(os, value.languages()) << '}';
}

// --- hardware ----------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const Product& value)
{
	os << "Product{" << value.id.toStdString() << " text=";
	quoted(os, value.text) << " order=";
	return quoted(os, value.orderNumber) << '}';
}

std::ostream& operator<<(std::ostream& os, const Hardware2Program& value)
{
	os << "Hardware2Program{" << value.id.toStdString() << " apps=";
	return printList(os, value.applicationProgramRefs) << '}';
}

std::ostream& operator<<(std::ostream& os, const Hardware& value)
{
	os << "Hardware{" << value.id.toStdString() << " name=";
	quoted(os, value.name) << " serial=";
	quoted(os, value.serialNumber) << " version=" << value.versionNumber.toStdString() << " products=[";
	for (std::size_t i = 0; i < value.products.size(); ++i)
	{
		os << (i ? ", " : "") << value.products[i];
	}
	os << "] h2p=[";
	for (std::size_t i = 0; i < value.hardware2Programs.size(); ++i)
	{
		os << (i ? ", " : "") << value.hardware2Programs[i];
	}
	return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const CatalogItem& value)
{
	os << "CatalogItem{" << value.id.toStdString() << " name=";
	quoted(os, value.name) << " product=" << value.productRefId.toStdString()
						   << " h2p=" << value.hardware2ProgramRefId.toStdString() << " section=";
	return printList(os, value.sectionPath) << '}';
}

// --- static ------------------------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const AbsoluteSegment& value)
{
	os << "AbsoluteSegment{" << value.id.toStdString() << " address=" << value.address << " size=" << value.size
	   << " data=";
	if (value.data)
	{
		os << value.data->size() << " bytes";
	}
	else
	{
		os << '-';
	}
	return os << '}';
}

std::ostream& operator<<(std::ostream& os, const RelativeSegment& value)
{
	os << "RelativeSegment{" << value.id.toStdString() << " name=";
	return quoted(os, value.name) << " offset=" << value.offset << " size=" << value.size
								  << " lsm=" << value.loadStateMachine << '}';
}

const char* toString(ParameterTypeKind kind)
{
	switch (kind)
	{
		case ParameterTypeKind::Number: return "Number";
		case ParameterTypeKind::Restriction: return "Restriction";
		case ParameterTypeKind::Text: return "Text";
		case ParameterTypeKind::Float: return "Float";
		case ParameterTypeKind::None: return "None";
		case ParameterTypeKind::Unsupported: return "Unsupported";
	}
	return "?";
}

std::ostream& operator<<(std::ostream& os, ParameterTypeKind kind)
{
	return os << toString(kind);
}

std::ostream& operator<<(std::ostream& os, const EnumerationEntry& value)
{
	os << "Enumeration{" << value.id.toStdString() << " value=" << value.value.toStdString() << " text=";
	return quoted(os, value.text) << '}';
}

std::ostream& operator<<(std::ostream& os, const ParameterType& value)
{
	os << "ParameterType{" << value.id.toStdString() << ' ' << value.kind << '(' << value.element.toStdString()
	   << ") bits=" << value.sizeInBit << " min=";
	printOptional(os, value.minimum) << " max=";
	printOptional(os, value.maximum) << " enum=" << value.enumeration.size() << '}';
	return os;
}

std::ostream& operator<<(std::ostream& os, const MemoryLocation& value)
{
	return os << "Memory{" << value.codeSegment.toStdString() << " +" << value.offset << '.' << value.bitOffset << '}';
}

std::ostream& operator<<(std::ostream& os, const PropertyLocation& value)
{
	os << "Property{index=";
	printOptional(os, value.objectIndex) << " type=";
	return printOptional(os, value.objectType) << " pid=" << value.propertyId << " +" << value.offset << '.'
											   << value.bitOffset << '}';
}

std::ostream& operator<<(std::ostream& os, const UnionMember& value)
{
	return os << "UnionMember{union=" << value.unionIndex << " +" << value.offset << '.' << value.bitOffset
			  << (value.isDefault ? " default" : "") << '}';
}

std::ostream& operator<<(std::ostream& os, const Parameter& value)
{
	os << "Parameter{" << value.id.toStdString() << " name=";
	quoted(os, value.name) << " type=" << value.parameterType.toStdString() << " value=";
	quoted(os, value.value) << " access=" << value.access.toStdString() << " memory=";
	printOptional(os, value.memory) << " property=";
	printOptional(os, value.property) << " union=";
	return printOptional(os, value.unionMember) << '}';
}

std::ostream& operator<<(std::ostream& os, const Union& value)
{
	os << "Union{bits=" << value.sizeInBit << " memory=";
	printOptional(os, value.memory) << " members=";
	return printList(os, value.parameters) << '}';
}

std::ostream& operator<<(std::ostream& os, const ParameterRef& value)
{
	os << "ParameterRef{" << value.id.toStdString() << " ref=" << value.refId.toStdString() << " text=";
	printOptional(os, value.text) << " value=";
	printOptional(os, value.value) << " access=";
	return printOptional(os, value.access) << '}';
}

QString ComObjectFlags::toString() const
{
	QString out;
	const std::pair<bool, char16_t> letters[] = {{communication, u'C'}, {read, u'R'}, {write, u'W'},
		{transmit, u'T'}, {update, u'U'}, {readOnInit, u'I'}};
	for (const auto& [set, letter] : letters)
	{
		if (set)
		{
			out.append(QChar(letter));
		}
	}
	return out;
}

std::ostream& operator<<(std::ostream& os, const ComObjectFlags& value)
{
	return os << value.toString().toStdString();
}

std::ostream& operator<<(std::ostream& os, const ComObject& value)
{
	os << "ComObject{" << value.id.toStdString() << " #" << value.number << " text=";
	quoted(os, value.text) << " function=";
	quoted(os, value.functionText) << " size=" << value.sizeInBit << " flags=" << value.flags
								   << " dpt=" << value.datapointType.toStdString() << '}';
	return os;
}

std::ostream& operator<<(std::ostream& os, const ComObjectRef& value)
{
	os << "ComObjectRef{" << value.id.toStdString() << " ref=" << value.refId.toStdString() << " text=";
	printOptional(os, value.text) << " function=";
	printOptional(os, value.functionText) << " size=";
	printOptional(os, value.sizeInBit) << " dpt=";
	return printOptional(os, value.datapointType) << '}';
}

std::ostream& operator<<(std::ostream& os, const TablePlacement& value)
{
	os << "Table{" << value.codeSegment.toStdString() << " +" << value.offset << " max=";
	return printOptional(os, value.maxEntries) << '}';
}

std::ostream& operator<<(std::ostream& os, const LoadControl& value)
{
	os << value.element.toStdString() << value.attributes;
	if (!value.children.empty())
	{
		os << '[';
		for (std::size_t i = 0; i < value.children.size(); ++i)
		{
			os << (i ? ", " : "") << value.children[i];
		}
		os << ']';
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const LoadProcedure& value)
{
	os << "LoadProcedure{merge=";
	printOptional(os, value.mergeId) << " [";
	for (std::size_t i = 0; i < value.controls.size(); ++i)
	{
		os << (i ? ", " : "") << value.controls[i];
	}
	return os << "]}";
}

// --- dynamic -----------------------------------------------------------------------------------------------

const char* toString(DynamicKind kind)
{
	switch (kind)
	{
		case DynamicKind::Root: return "Root";
		case DynamicKind::ChannelIndependentBlock: return "ChannelIndependentBlock";
		case DynamicKind::Channel: return "Channel";
		case DynamicKind::ParameterBlock: return "ParameterBlock";
		case DynamicKind::Choose: return "Choose";
		case DynamicKind::When: return "When";
		case DynamicKind::ParameterRefRef: return "ParameterRefRef";
		case DynamicKind::ComObjectRefRef: return "ComObjectRefRef";
		case DynamicKind::Other: return "Other";
	}
	return "?";
}

std::ostream& operator<<(std::ostream& os, DynamicKind kind)
{
	return os << toString(kind);
}

QString DynamicNode::attribute(const QString& name) const
{
	const auto it = attributes.find(name);
	return it == attributes.end() ? QString() : it->second;
}

std::ostream& operator<<(std::ostream& os, const DynamicNode& value)
{
	os << value.element.toStdString() << value.attributes;
	if (!value.children.empty())
	{
		os << '[';
		for (std::size_t i = 0; i < value.children.size(); ++i)
		{
			os << (i ? ", " : "") << value.children[i];
		}
		os << ']';
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const ParameterBlockInfo& value)
{
	os << "ParameterBlock{" << value.id.toStdString() << " text=";
	return quoted(os, value.text) << '}';
}

std::ostream& operator<<(std::ostream& os, const Channel& value)
{
	os << "Channel{" << (value.independent ? "independent" : value.id.toStdString()) << " number="
	   << value.number.toStdString() << " text=";
	quoted(os, value.text) << " blocks=" << value.parameterBlocks.size() << " objects=";
	printList(os, value.comObjectRefs) << " parameters=" << value.parameterRefs.size();
	return os << '}';
}

// --- application program -----------------------------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const EffectiveComObject& value)
{
	os << "EffectiveComObject{" << value.refId.toStdString() << " #" << value.number << " text=";
	quoted(os, value.text) << " function=";
	quoted(os, value.functionText) << " size=" << value.sizeInBit << " flags=" << value.flags
								   << " dpt=" << value.datapointType.toStdString()
								   << " channel=" << value.channelId.toStdString() << '}';
	return os;
}

const AbsoluteSegment* ApplicationProgram::findAbsoluteSegment(QStringView segmentId) const
{
	return findById(absoluteSegments, segmentId);
}

const RelativeSegment* ApplicationProgram::findRelativeSegment(QStringView segmentId) const
{
	return findById(relativeSegments, segmentId);
}

const ParameterType* ApplicationProgram::findParameterType(QStringView typeId) const
{
	return findById(parameterTypes, typeId);
}

const Parameter* ApplicationProgram::findParameter(QStringView parameterId) const
{
	return findById(parameters, parameterId);
}

const ParameterRef* ApplicationProgram::findParameterRef(QStringView refId) const
{
	return findById(parameterRefs, refId);
}

const ComObject* ApplicationProgram::findComObject(QStringView objectId) const
{
	return findById(comObjects, objectId);
}

const ComObjectRef* ApplicationProgram::findComObjectRef(QStringView refId) const
{
	return findById(comObjectRefs, refId);
}

const Channel* ApplicationProgram::findChannel(QStringView channelId) const
{
	if (channelId.isEmpty())
	{
		return nullptr;
	}
	return findById(channels, channelId);
}

const Channel* ApplicationProgram::channelOfComObjectRef(QStringView refId) const
{
	for (const Channel& channel : channels)
	{
		if (std::any_of(channel.comObjectRefs.begin(), channel.comObjectRefs.end(),
				[refId](const QString& ref) { return ref == refId; }))
		{
			return &channel;
		}
	}
	return nullptr;
}

std::optional<MemoryLocation> ApplicationProgram::memoryOf(const Parameter& parameter) const
{
	if (parameter.memory)
	{
		return parameter.memory;
	}
	if (!parameter.unionMember || parameter.unionMember->unionIndex >= unions.size())
	{
		return std::nullopt;
	}
	const Union& owner = unions[parameter.unionMember->unionIndex];
	if (!owner.memory)
	{
		return std::nullopt;
	}
	const std::uint64_t bits = (std::uint64_t{owner.memory->offset} + parameter.unionMember->offset) * 8
		+ owner.memory->bitOffset + parameter.unionMember->bitOffset;
	return MemoryLocation{owner.memory->codeSegment, static_cast<unsigned>(bits / 8), static_cast<unsigned>(bits % 8)};
}

std::vector<EffectiveComObject> ApplicationProgram::effectiveComObjects(const QString& language) const
{
	QHash<QString, const ComObject*> objects;
	for (const ComObject& object : comObjects)
	{
		objects.insert(object.id, &object);
	}
	// First channel per ref, the independent part (listed first) losing against a real channel.
	QHash<QString, const Channel*> channelOf;
	for (const Channel& channel : channels)
	{
		for (const QString& ref : channel.comObjectRefs)
		{
			const auto it = channelOf.constFind(ref);
			if (it == channelOf.constEnd() || (it.value()->independent && !channel.independent))
			{
				channelOf.insert(ref, &channel);
			}
		}
	}

	std::vector<EffectiveComObject> out;
	out.reserve(comObjectRefs.size());
	for (const ComObjectRef& ref : comObjectRefs)
	{
		const ComObject* object = objects.value(ref.refId, nullptr);
		if (!object)
		{
			continue;
		}
		EffectiveComObject effective;
		effective.refId = ref.id;
		effective.comObjectId = object->id;
		effective.number = object->number;
		effective.name = ref.name.value_or(object->name);
		effective.text = ref.text ? text(ref.id, u"Text"_s, language) : text(object->id, u"Text"_s, language);
		effective.functionText = ref.functionText ? text(ref.id, u"FunctionText"_s, language)
												  : text(object->id, u"FunctionText"_s, language);
		effective.objectSize = ref.objectSize.value_or(object->objectSize);
		effective.sizeInBit = ref.sizeInBit.value_or(object->sizeInBit);
		effective.flags.communication = ref.communication.value_or(object->flags.communication);
		effective.flags.read = ref.read.value_or(object->flags.read);
		effective.flags.write = ref.write.value_or(object->flags.write);
		effective.flags.transmit = ref.transmit.value_or(object->flags.transmit);
		effective.flags.update = ref.update.value_or(object->flags.update);
		effective.flags.readOnInit = ref.readOnInit.value_or(object->flags.readOnInit);
		effective.datapointType = ref.datapointType.value_or(object->datapointType);
		if (const Channel* channel = channelOf.value(ref.id, nullptr))
		{
			effective.referenced = true;
			effective.channelId = channel->independent ? QString() : channel->id;
		}
		out.push_back(std::move(effective));
	}
	std::stable_sort(out.begin(), out.end(), [](const EffectiveComObject& a, const EffectiveComObject& b) {
		return a.number != b.number ? a.number < b.number : a.refId < b.refId;
	});
	return out;
}

std::ostream& operator<<(std::ostream& os, const ApplicationProgram& value)
{
	os << "ApplicationProgram{" << value.id.toStdString() << " name=";
	quoted(os, value.name) << " number=" << value.applicationNumber << " version=" << value.applicationVersion
						   << " mask=" << value.maskVersion.toStdString() << " parameters=" << value.parameters.size()
						   << " comObjects=" << value.comObjects.size() << " channels=" << value.channels.size();
	return os << '}';
}

// --- product database --------------------------------------------------------------------------------------

const Hardware* ProductDatabase::findHardware(QStringView id) const
{
	return findById(hardware, id);
}

const Product* ProductDatabase::findProduct(QStringView id) const
{
	for (const Hardware& item : hardware)
	{
		if (const Product* product = findById(item.products, id))
		{
			return product;
		}
	}
	return nullptr;
}

const ApplicationProgram* ProductDatabase::findApplicationProgram(QStringView id) const
{
	return findById(applicationPrograms, id);
}

QString ProductDatabase::text(const QString& id, const QString& attribute, const QString& language) const
{
	QString found = translations.text(id, attribute, language);
	for (auto it = applicationPrograms.begin(); found.isEmpty() && it != applicationPrograms.end(); ++it)
	{
		found = it->text(id, attribute, language);
	}
	return found;
}

std::ostream& operator<<(std::ostream& os, const ProductDatabase& value)
{
	os << "ProductDatabase{" << value.manufacturerId.toStdString() << " hardware=" << value.hardware.size()
	   << " catalogItems=" << value.catalogItems.size() << " apps=[";
	for (std::size_t i = 0; i < value.applicationPrograms.size(); ++i)
	{
		os << (i ? ", " : "") << value.applicationPrograms[i].id.toStdString();
	}
	return os << "]}";
}

std::optional<unsigned> parseObjectSize(QStringView text)
{
	const QStringView trimmed = text.trimmed();
	const qsizetype space = trimmed.indexOf(u' ');
	if (space <= 0)
	{
		return std::nullopt;
	}
	bool ok = false;
	const uint count = trimmed.left(space).toUInt(&ok);
	if (!ok || count == 0 || count > 65535)
	{
		return std::nullopt;
	}
	const QStringView unit = trimmed.mid(space + 1).trimmed();
	if (unit.compare(u"Bit", Qt::CaseInsensitive) == 0 || unit.compare(u"Bits", Qt::CaseInsensitive) == 0)
	{
		return count;
	}
	if (unit.compare(u"Byte", Qt::CaseInsensitive) == 0 || unit.compare(u"Bytes", Qt::CaseInsensitive) == 0)
	{
		return count * 8;
	}
	return std::nullopt;
}

} // namespace kestrel::catalog
