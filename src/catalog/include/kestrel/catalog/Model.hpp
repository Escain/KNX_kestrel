#pragma once

// knxprod product-database model (specs/07-catalog.md): manufacturer hardware, products, catalog items and
// application programs as plain copyable values. Ids are the KNX string ids of the XML ("M-FFFF_A-0001-10-0000",
// "..._O-0_R-1"); cross references stay ids, resolved through the find*() helpers.
//
// Only what the "link a group address to a com object, download the tables" slice needs is typed; every element
// keeps its raw attributes so later milestones can interpret more without re-parsing.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

namespace kestrel::catalog
{

/// Raw XML attributes by local name.
using AttributeMap = std::map<QString, QString>;

std::ostream& operator<<(std::ostream& os, const AttributeMap& attributes);

/// A non-fatal problem found while reading a product file.
struct Diagnostic
{
	QString file;     ///< path inside the archive, e.g. "M-FFFF/Hardware.xml"
	qint64 line = 0;  ///< 1-based, 0 when not tied to a line
	QString message;

	friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

std::ostream& operator<<(std::ostream& os, const Diagnostic& value);

// --- translations ------------------------------------------------------------------------------------------

/// Texts of translatable attributes (Text, FunctionText, Name, SuffixText, ...) by element id, plus the
/// Languages/Language/TranslationUnit/TranslationElement/Translation overrides.
class Translations
{
public:
	/// The attribute value written on the element itself (in the file's DefaultLanguage).
	void setDefault(const QString& id, const QString& attribute, const QString& text);
	void add(const QString& language, const QString& id, const QString& attribute, const QString& text);
	void merge(const Translations& other);

	/// Lookup order: exact language (case-insensitive), then another region of the same language ("de" for
	/// "de-CH" → "de-DE"), then the default text. An empty language returns the default text.
	QString text(const QString& id, const QString& attribute, const QString& language = {}) const;
	/// Exact-language translation only.
	std::optional<QString> translation(const QString& id, const QString& attribute, const QString& language) const;
	/// Languages with at least one translation, sorted.
	QStringList languages() const;
	bool empty() const { return m_defaults.empty() && m_byLanguage.empty(); }

	friend bool operator==(const Translations&, const Translations&) = default;

private:
	using Key = std::pair<QString, QString>; ///< (element id, attribute)
	std::map<QString, std::map<Key, QString>> m_byLanguage;
	std::map<Key, QString> m_defaults;
};

std::ostream& operator<<(std::ostream& os, const Translations& value);

// --- hardware, products, catalog ---------------------------------------------------------------------------

struct Product
{
	QString id;
	QString text;
	QString orderNumber;
	AttributeMap attributes;

	friend bool operator==(const Product&, const Product&) = default;
};

struct Hardware2Program
{
	QString id;
	QStringList applicationProgramRefs; ///< ApplicationProgramRef RefIds
	AttributeMap attributes;

	friend bool operator==(const Hardware2Program&, const Hardware2Program&) = default;
};

struct Hardware
{
	QString id;
	QString name;
	QString serialNumber;
	QString versionNumber;
	std::vector<Product> products;
	std::vector<Hardware2Program> hardware2Programs;
	AttributeMap attributes;

	friend bool operator==(const Hardware&, const Hardware&) = default;
};

struct CatalogItem
{
	QString id;
	QString name;
	QString number;
	QString productRefId;
	QString hardware2ProgramRefId;
	QStringList sectionPath; ///< enclosing CatalogSection names, outermost first
	AttributeMap attributes;

	friend bool operator==(const CatalogItem&, const CatalogItem&) = default;
};

std::ostream& operator<<(std::ostream& os, const Product& value);
std::ostream& operator<<(std::ostream& os, const Hardware2Program& value);
std::ostream& operator<<(std::ostream& os, const Hardware& value);
std::ostream& operator<<(std::ostream& os, const CatalogItem& value);

// --- application program: Static ---------------------------------------------------------------------------

struct AbsoluteSegment
{
	QString id;
	unsigned address = 0;
	unsigned size = 0;
	std::optional<QByteArray> data; ///< decoded <Data> (base64 in the XML)
	AttributeMap attributes;

	friend bool operator==(const AbsoluteSegment&, const AbsoluteSegment&) = default;
};

struct RelativeSegment
{
	QString id;
	QString name;
	unsigned offset = 0;
	unsigned size = 0;
	unsigned loadStateMachine = 0;
	std::optional<QByteArray> data; ///< decoded <Data>: the initial content (ETS overlays parameters on it)
	AttributeMap attributes;

	friend bool operator==(const RelativeSegment&, const RelativeSegment&) = default;
};

enum class ParameterTypeKind
{
	Number,      ///< TypeNumber
	Restriction, ///< TypeRestriction (enumeration)
	Text,        ///< TypeText
	Float,       ///< TypeFloat
	None,        ///< TypeNone
	Unsupported, ///< any other Type* element (IP address, picture, colour, date, ...): see `element`
};

const char* toString(ParameterTypeKind kind);
std::ostream& operator<<(std::ostream& os, ParameterTypeKind kind);

struct EnumerationEntry
{
	QString id;
	QString text;
	QString value;
	AttributeMap attributes;

	friend bool operator==(const EnumerationEntry&, const EnumerationEntry&) = default;
};

struct ParameterType
{
	QString id;
	QString name;
	ParameterTypeKind kind = ParameterTypeKind::Unsupported;
	QString element;        ///< local name of the type element, e.g. "TypeNumber", "TypeIPAddress"
	unsigned sizeInBit = 0; ///< SizeInBit (0 if absent)
	QString numberType;     ///< TypeNumber/@Type ("unsignedInt", "signedInt", ...)
	QString base;           ///< TypeRestriction/@Base ("Value", "BinaryValue")
	QString encoding;       ///< TypeFloat/@Encoding
	std::optional<qint64> minimum;      ///< TypeNumber minInclusive
	std::optional<qint64> maximum;      ///< TypeNumber maxInclusive
	std::optional<double> minimumFloat; ///< TypeFloat minInclusive
	std::optional<double> maximumFloat; ///< TypeFloat maxInclusive
	std::vector<EnumerationEntry> enumeration;
	AttributeMap attributes; ///< raw attributes of the type element

	friend bool operator==(const ParameterType&, const ParameterType&) = default;
};

/// <Memory CodeSegment Offset BitOffset>: CodeSegment is an AbsoluteSegment or RelativeSegment id.
struct MemoryLocation
{
	QString codeSegment;
	unsigned offset = 0;
	unsigned bitOffset = 0;

	friend bool operator==(const MemoryLocation&, const MemoryLocation&) = default;
};

/// <Property ObjectIndex|ObjectType PropertyId Offset BitOffset>.
struct PropertyLocation
{
	std::optional<unsigned> objectIndex;
	std::optional<unsigned> objectType;
	unsigned propertyId = 0;
	unsigned offset = 0;
	unsigned bitOffset = 0;

	friend bool operator==(const PropertyLocation&, const PropertyLocation&) = default;
};

/// Position of a Parameter inside its Union.
struct UnionMember
{
	std::size_t unionIndex = 0; ///< into ApplicationProgram::unions
	unsigned offset = 0;
	unsigned bitOffset = 0;
	bool isDefault = false; ///< DefaultUnionParameter

	friend bool operator==(const UnionMember&, const UnionMember&) = default;
};

struct Parameter
{
	QString id;
	QString name;
	QString parameterType; ///< ParameterType id
	QString text;
	QString value;  ///< default value
	QString access; ///< "None", "Read", "ReadWrite" (empty: schema default ReadWrite)
	std::optional<MemoryLocation> memory;
	std::optional<PropertyLocation> property;
	std::optional<UnionMember> unionMember;
	AttributeMap attributes;

	friend bool operator==(const Parameter&, const Parameter&) = default;
};

struct Union
{
	unsigned sizeInBit = 0;
	std::optional<MemoryLocation> memory;
	std::optional<PropertyLocation> property;
	QStringList parameters; ///< member Parameter ids
	AttributeMap attributes;

	friend bool operator==(const Union&, const Union&) = default;
};

struct ParameterRef
{
	QString id;
	QString refId; ///< Parameter id
	std::optional<QString> text;
	std::optional<QString> value;
	std::optional<QString> access;
	AttributeMap attributes;

	friend bool operator==(const ParameterRef&, const ParameterRef&) = default;
};

struct ComObjectFlags
{
	bool communication = false; ///< C
	bool read = false;          ///< R
	bool write = false;         ///< W
	bool transmit = false;      ///< T
	bool update = false;        ///< U
	bool readOnInit = false;    ///< I

	/// Letters of the set flags in "CRWTUI" order, e.g. "CWT".
	QString toString() const;

	friend bool operator==(const ComObjectFlags&, const ComObjectFlags&) = default;
};

struct ComObject
{
	QString id;
	QString name;
	QString text;
	QString functionText;
	unsigned number = 0;
	QString objectSize;     ///< raw, e.g. "1 Bit", "2 Bytes"
	unsigned sizeInBit = 0; ///< 0 if objectSize is not understood
	ComObjectFlags flags;   ///< "Enabled"/"Disabled"; absent = disabled
	QString datapointType;  ///< raw, e.g. "DPST-1-1" (may list several, space separated)
	AttributeMap attributes;

	friend bool operator==(const ComObject&, const ComObject&) = default;
};

/// ComObjectRef: every field except id/refId is an optional override of the ComObject.
struct ComObjectRef
{
	QString id;
	QString refId; ///< ComObject id
	std::optional<QString> name;
	std::optional<QString> text;
	std::optional<QString> functionText;
	std::optional<QString> objectSize;
	std::optional<unsigned> sizeInBit;
	std::optional<bool> communication;
	std::optional<bool> read;
	std::optional<bool> write;
	std::optional<bool> transmit;
	std::optional<bool> update;
	std::optional<bool> readOnInit;
	std::optional<QString> datapointType;
	AttributeMap attributes;

	friend bool operator==(const ComObjectRef&, const ComObjectRef&) = default;
};

/// Placement of the address, association or group-object table.
struct TablePlacement
{
	QString codeSegment;
	unsigned offset = 0;
	std::optional<unsigned> maxEntries;
	AttributeMap attributes;

	friend bool operator==(const TablePlacement&, const TablePlacement&) = default;
};

/// One LdCtrl* element, uninterpreted (the download planner reads them, ADR-008).
struct LoadControl
{
	QString element; ///< e.g. "LdCtrlRelSegment"
	AttributeMap attributes;
	std::vector<LoadControl> children;

	friend bool operator==(const LoadControl&, const LoadControl&) = default;
};

struct LoadProcedure
{
	std::optional<unsigned> mergeId;
	std::vector<LoadControl> controls; ///< document order
	AttributeMap attributes;

	friend bool operator==(const LoadProcedure&, const LoadProcedure&) = default;
};

/// knx_master.xml MaskVersion/Procedures/Procedure: the mask's own load procedure, into which an application
/// with LoadProcedureStyle="MergedProcedure" merges its LoadProcedures by MergeId.
struct MaskProcedure
{
	QString type;    ///< ProcedureType, e.g. "Load"
	QString subType; ///< ProcedureSubType, e.g. "all", "ap1", "par"
	QString access;
	std::vector<LoadControl> controls;

	friend bool operator==(const MaskProcedure&, const MaskProcedure&) = default;
};

/// knx_master.xml MaskVersion (only what downloads need).
struct MaskVersionInfo
{
	QString id; ///< "MV-07B0"
	unsigned maskVersion = 0;
	QString name;
	QString managementModel;
	std::vector<MaskProcedure> procedures;

	const MaskProcedure* procedure(QStringView type, QStringView subType) const;
	friend bool operator==(const MaskVersionInfo&, const MaskVersionInfo&) = default;
};

std::ostream& operator<<(std::ostream& os, const AbsoluteSegment& value);
std::ostream& operator<<(std::ostream& os, const RelativeSegment& value);
std::ostream& operator<<(std::ostream& os, const EnumerationEntry& value);
std::ostream& operator<<(std::ostream& os, const ParameterType& value);
std::ostream& operator<<(std::ostream& os, const MemoryLocation& value);
std::ostream& operator<<(std::ostream& os, const PropertyLocation& value);
std::ostream& operator<<(std::ostream& os, const UnionMember& value);
std::ostream& operator<<(std::ostream& os, const Parameter& value);
std::ostream& operator<<(std::ostream& os, const Union& value);
std::ostream& operator<<(std::ostream& os, const ParameterRef& value);
std::ostream& operator<<(std::ostream& os, const ComObjectFlags& value);
std::ostream& operator<<(std::ostream& os, const ComObject& value);
std::ostream& operator<<(std::ostream& os, const ComObjectRef& value);
std::ostream& operator<<(std::ostream& os, const TablePlacement& value);
std::ostream& operator<<(std::ostream& os, const LoadControl& value);
std::ostream& operator<<(std::ostream& os, const LoadProcedure& value);

// --- application program: Dynamic --------------------------------------------------------------------------

enum class DynamicKind
{
	Root, ///< the <Dynamic> element
	ChannelIndependentBlock,
	Channel,
	ParameterBlock,
	Choose,
	When,
	ParameterRefRef,
	ComObjectRefRef,
	Other, ///< ParameterSeparator, Assign, Rename, Button, ... (kept for the evaluator)
};

const char* toString(DynamicKind kind);
std::ostream& operator<<(std::ostream& os, DynamicKind kind);

/// The <Dynamic> section as a tree, conditions (choose ParamRefId / when test|default) included.
struct DynamicNode
{
	DynamicKind kind = DynamicKind::Root;
	QString element;
	AttributeMap attributes;
	std::vector<DynamicNode> children;

	QString attribute(const QString& name) const;

	friend bool operator==(const DynamicNode&, const DynamicNode&) = default;
};

struct ParameterBlockInfo
{
	QString id;
	QString name;
	QString text;

	friend bool operator==(const ParameterBlockInfo&, const ParameterBlockInfo&) = default;
};

/// Per-channel index of the Dynamic section: everything reachable under the channel, conditions ignored.
struct Channel
{
	QString id;     ///< empty for the channel-independent part
	QString name;
	QString text;
	QString number;
	bool independent = false; ///< ChannelIndependentBlock(s) and refs outside any channel
	std::vector<ParameterBlockInfo> parameterBlocks; ///< document order, nested blocks flattened
	QStringList comObjectRefs; ///< ComObjectRefRef RefIds, document order, unique
	QStringList parameterRefs; ///< ParameterRefRef RefIds, document order, unique

	friend bool operator==(const Channel&, const Channel&) = default;
};

std::ostream& operator<<(std::ostream& os, const DynamicNode& value);
std::ostream& operator<<(std::ostream& os, const ParameterBlockInfo& value);
std::ostream& operator<<(std::ostream& os, const Channel& value);

// --- application program -----------------------------------------------------------------------------------

/// A com object as the user sees it: ComObjectRef overrides applied to its ComObject, texts translated.
struct EffectiveComObject
{
	QString refId;       ///< ComObjectRef id (what a project links to)
	QString comObjectId; ///< ComObject id
	unsigned number = 0;
	QString name;
	QString text;
	QString functionText;
	QString objectSize;
	unsigned sizeInBit = 0;
	ComObjectFlags flags;
	QString datapointType;
	QString channelId;       ///< first channel whose subtree references the ref; empty if channel-independent
	bool referenced = false; ///< reachable from Dynamic at all (under some condition)

	friend bool operator==(const EffectiveComObject&, const EffectiveComObject&) = default;
};

std::ostream& operator<<(std::ostream& os, const EffectiveComObject& value);

struct ApplicationProgram
{
	QString id;
	QString name;
	unsigned applicationNumber = 0;
	unsigned applicationVersion = 0;
	QString programType;        ///< "ApplicationProgram", "PluginProgram", ...
	QString maskVersion;        ///< "MV-07B0"
	QString loadProcedureStyle; ///< "MergedProcedure", "ProductProcedure", "DefaultProcedure"
	unsigned peiType = 0;
	bool dynamicTableManagement = false;
	QString defaultLanguage;
	AttributeMap attributes; ///< every attribute of <ApplicationProgram>

	std::vector<AbsoluteSegment> absoluteSegments;
	std::vector<RelativeSegment> relativeSegments;
	std::vector<ParameterType> parameterTypes;
	std::vector<Parameter> parameters;
	std::vector<Union> unions;
	std::vector<ParameterRef> parameterRefs;
	std::optional<TablePlacement> comObjectTable;
	std::vector<ComObject> comObjects;
	std::vector<ComObjectRef> comObjectRefs;
	std::optional<TablePlacement> addressTable;
	std::optional<TablePlacement> associationTable;
	std::vector<LoadProcedure> loadProcedures;

	std::vector<Channel> channels; ///< the channel-independent part (if any) first, then channels in order
	DynamicNode dynamic;
	bool hasModuleDefs = false; ///< ModuleDefs present (not interpreted yet: module channels are missing)

	Translations translations;

	const AbsoluteSegment* findAbsoluteSegment(QStringView id) const;
	const RelativeSegment* findRelativeSegment(QStringView id) const;
	const ParameterType* findParameterType(QStringView id) const;
	const Parameter* findParameter(QStringView id) const;
	const ParameterRef* findParameterRef(QStringView id) const;
	const ComObject* findComObject(QStringView id) const;
	const ComObjectRef* findComObjectRef(QStringView id) const;
	const Channel* findChannel(QStringView id) const;
	/// The first channel whose subtree references this ComObjectRef (independent part included).
	const Channel* channelOfComObjectRef(QStringView refId) const;

	/// Memory of a parameter: its own <Memory>, or its union's memory shifted by the member offset.
	std::optional<MemoryLocation> memoryOf(const Parameter& parameter) const;

	/// Every ComObjectRef merged with its ComObject, ordered by number then ref id. Refs whose ComObject
	/// is missing are skipped (the loader reported them).
	std::vector<EffectiveComObject> effectiveComObjects(const QString& language = {}) const;

	QString text(const QString& elementId, const QString& attribute, const QString& language = {}) const
	{
		return translations.text(elementId, attribute, language);
	}

	friend bool operator==(const ApplicationProgram&, const ApplicationProgram&) = default;
};

std::ostream& operator<<(std::ostream& os, const ApplicationProgram& value);

// --- product database --------------------------------------------------------------------------------------

/// The content of one .knxprod.
struct ProductDatabase
{
	QString manufacturerId; ///< "M-FFFF"
	QString schemaNamespace; ///< e.g. "http://knx.org/xml/project/20" (of Hardware.xml)
	std::vector<Hardware> hardware;
	std::vector<CatalogItem> catalogItems;
	std::vector<ApplicationProgram> applicationPrograms;
	Translations translations; ///< hardware, product and catalog texts
	QStringList files;         ///< archive paths that were parsed
	bool hasMasterData = false; ///< knx_master.xml present
	std::vector<MaskVersionInfo> maskVersions; ///< from knx_master.xml

	const Hardware* findHardware(QStringView id) const;
	const MaskVersionInfo* findMaskVersion(QStringView id) const;
	const Product* findProduct(QStringView id) const;
	const ApplicationProgram* findApplicationProgram(QStringView id) const;
	/// Looks in the package translations, then in each application program.
	QString text(const QString& elementId, const QString& attribute, const QString& language = {}) const;

	friend bool operator==(const ProductDatabase&, const ProductDatabase&) = default;
};

std::ostream& operator<<(std::ostream& os, const ProductDatabase& value);

/// "1 Bit" → 1, "4 Bit" → 4, "1 Byte" → 8, "14 Bytes" → 112. nullopt if not understood.
std::optional<unsigned> parseObjectSize(QStringView text);

} // namespace kestrel::catalog
