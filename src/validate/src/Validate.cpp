#include "kestrel/validate/Validate.hpp"

#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>

using namespace Qt::StringLiterals;
using namespace kestrel::model;

namespace kestrel::validate
{

namespace
{

Diagnostic make(const char* code, Severity severity, QString message, std::vector<EntityRef> subjects)
{
	return Diagnostic{QString::fromLatin1(code), severity, std::move(message), std::move(subjects)};
}

/// Reports one diagnostic per group of entities sharing a key (only groups of 2+), subjects in table order.
template <class Key>
void reportDuplicates(const std::vector<std::pair<Key, EntityRef>>& entries, const char* code, const QString& message,
	DiagnosticSink& sink)
{
	std::map<Key, std::vector<EntityRef>> groups;
	for (const auto& [key, ref] : entries)
		groups[key].push_back(ref);
	for (auto& [key, refs] : groups)
	{
		if (refs.size() > 1)
			sink.report(make(code, Severity::Error, message, std::move(refs)));
	}
}

} // namespace

const char* toString(Severity severity)
{
	switch (severity)
	{
		case Severity::Error: return "Error";
		case Severity::Warning: return "Warning";
		case Severity::Info: return "Info";
	}
	return "?";
}

std::ostream& operator<<(std::ostream& os, Severity severity)
{
	return os << toString(severity);
}

std::ostream& operator<<(std::ostream& os, const Diagnostic& diagnostic)
{
	os << diagnostic.severity << ' ' << diagnostic.code.toStdString() << ' ' << diagnostic.message.toStdString()
	   << " [";
	for (std::size_t i = 0; i < diagnostic.subjects.size(); ++i)
		os << (i ? ", " : "") << diagnostic.subjects[i];
	return os << ']';
}

std::optional<unsigned> dptSizeInBits(QStringView dpt)
{
	// Main number -> size in bits (KNX 3/7/2).
	static const std::map<unsigned, unsigned> kSizes = {
		{1, 1}, {2, 2}, {3, 4}, {4, 8}, {5, 8}, {6, 8}, {7, 16}, {8, 16}, {9, 16}, {10, 24}, {11, 24}, {12, 32},
		{13, 32}, {14, 32}, {15, 32}, {16, 112}, {17, 8}, {18, 8}, {19, 64}, {20, 8}, {232, 24}};

	QStringView mainText;
	if (dpt.startsWith(u"DPST-"))
	{
		const QStringView rest = dpt.mid(5);
		const qsizetype dash = rest.indexOf(u'-');
		if (dash <= 0)
			return std::nullopt;
		mainText = rest.left(dash);
	}
	else if (dpt.startsWith(u"DPT-"))
	{
		mainText = dpt.mid(4);
	}
	else
	{
		const qsizetype dot = dpt.indexOf(u'.');
		mainText = dot < 0 ? dpt : dpt.left(dot);
	}
	bool ok = false;
	const unsigned mainNumber = mainText.toUInt(&ok);
	if (!ok)
		return std::nullopt;
	const auto it = kSizes.find(mainNumber);
	if (it == kSizes.end())
		return std::nullopt;
	return it->second;
}

void checkDuplicateIndividualAddresses(const Project& project, DiagnosticSink& sink)
{
	std::vector<std::pair<std::uint16_t, EntityRef>> entries;
	for (const Device& device : project.devices())
	{
		if (const auto address = project.individualAddress(device.id))
			entries.emplace_back(address->raw(), device.id);
	}
	reportDuplicates(entries, "TOP-001", u"Duplicate individual address"_s, sink);
}

void checkDeviceNumberZero(const Project& project, DiagnosticSink& sink)
{
	for (const Device& device : project.devices())
	{
		if (device.number == 0u)
			sink.report(make("TOP-002", Severity::Error, u"Device number 0 is reserved for couplers"_s, {device.id}));
	}
}

void checkDevicesWithoutAddress(const Project& project, DiagnosticSink& sink)
{
	for (const Device& device : project.devices())
	{
		if (!device.number)
			sink.report(make("TOP-003", Severity::Info, u"Device has no individual address yet"_s, {device.id}));
	}
}

void checkDuplicateGroupAddresses(const Project& project, DiagnosticSink& sink)
{
	std::vector<std::pair<std::uint16_t, EntityRef>> entries;
	for (const GroupAddress& address : project.groupAddresses())
		entries.emplace_back(address.raw, address.id);
	reportDuplicates(entries, "GA-001", u"Duplicate group address"_s, sink);
}

void checkGroupAddressesWithoutDpt(const Project& project, DiagnosticSink& sink)
{
	for (const GroupAddress& address : project.groupAddresses())
	{
		if (address.dpt.isEmpty())
			sink.report(make("GA-002", Severity::Warning, u"Group address has no datapoint type"_s, {address.id}));
	}
}

void checkDptSizeMismatch(const Project& project, DiagnosticSink& sink)
{
	for (const Link& link : project.links())
	{
		const GroupAddress* address = project.groupAddresses().find(link.groupAddress);
		const ComObject* object = project.comObjects().find(link.comObject);
		if (!address || !object)
			continue;
		const auto size = dptSizeInBits(address->dpt);
		if (size && *size != object->sizeInBit)
		{
			sink.report(make("GA-003", Severity::Error,
				u"Linked communication object size differs from the group address datapoint type"_s,
				{address->id, object->id}));
		}
	}
}

void checkGroupAddressesWithoutLinks(const Project& project, DiagnosticSink& sink)
{
	std::unordered_map<std::uint64_t, bool> linked;
	for (const Link& link : project.links())
		linked[link.groupAddress.value()] = true;
	for (const GroupAddress& address : project.groupAddresses())
	{
		if (!linked.contains(address.id.value()))
			sink.report(make("GA-004", Severity::Warning, u"Group address has no links"_s, {address.id}));
	}
}

void checkGroupAddressesWithoutSender(const Project& project, DiagnosticSink& sink)
{
	// group address -> has a linked com object with the T flag
	std::unordered_map<std::uint64_t, bool> hasSender;
	for (const Link& link : project.links())
	{
		const ComObject* object = project.comObjects().find(link.comObject);
		bool& sender = hasSender[link.groupAddress.value()];
		sender = sender || (object && object->flags.transmit);
	}
	for (const GroupAddress& address : project.groupAddresses())
	{
		const auto it = hasSender.find(address.id.value());
		if (it != hasSender.end() && !it->second)
		{
			sink.report(make("GA-005", Severity::Warning,
				u"Group address has links but no sender (no linked communication object with the T flag)"_s,
				{address.id}));
		}
	}
}

void checkMultipleSendingLinks(const Project& project, DiagnosticSink& sink)
{
	std::unordered_map<std::uint64_t, unsigned> sending;
	for (const Link& link : project.links())
	{
		if (link.sending)
			++sending[link.comObject.value()];
	}
	for (const ComObject& object : project.comObjects())
	{
		const auto it = sending.find(object.id.value());
		if (it != sending.end() && it->second > 1)
		{
			sink.report(make("CO-001", Severity::Warning,
				u"Communication object has more than one sending group address"_s, {object.id}));
		}
	}
}

void checkDevicesNotPlaced(const Project& project, DiagnosticSink& sink)
{
	for (const Device& device : project.devices())
	{
		if (!device.place)
			sink.report(make("BLD-001", Severity::Info, u"Device is not placed in a building part"_s, {device.id}));
	}
}

void checkDuplicateAreaAndLineNumbers(const Project& project, DiagnosticSink& sink)
{
	std::vector<std::pair<unsigned, EntityRef>> areas;
	for (const Area& area : project.areas())
		areas.emplace_back(area.number, area.id);
	reportDuplicates(areas, "TOP-004", u"Duplicate area number"_s, sink);

	std::vector<std::pair<std::pair<std::uint64_t, unsigned>, EntityRef>> lines;
	for (const Line& line : project.lines())
		lines.emplace_back(std::pair(line.area.value(), line.number), line.id);
	reportDuplicates(lines, "TOP-004", u"Duplicate line number in area"_s, sink);
}

void checkGroupAddressZero(const Project& project, DiagnosticSink& sink)
{
	for (const GroupAddress& address : project.groupAddresses())
	{
		if (address.raw == 0)
			sink.report(make("GA-006", Severity::Error, u"Group address 0/0/0 is reserved"_s, {address.id}));
	}
}

void checkGroupRangeConsistency(const Project& project, DiagnosticSink& sink)
{
	for (const GroupAddress& address : project.groupAddresses())
	{
		const GroupRange* range = address.range ? project.groupRanges().find(*address.range) : nullptr;
		if (range && !range->contains(address.raw))
		{
			sink.report(make("GA-007", Severity::Error, u"Group address is outside its group range"_s,
				{address.id, range->id}));
		}
	}
	for (const GroupRange& range : project.groupRanges())
	{
		const GroupRange* parent = range.parent ? project.groupRanges().find(*range.parent) : nullptr;
		if (parent && (!parent->contains(range.start) || !parent->contains(range.end)))
		{
			sink.report(make("GA-007", Severity::Error, u"Group range is outside its parent range"_s,
				{range.id, parent->id}));
		}
	}
	// Overlapping siblings: compare each pair once (per parent; ranges are few).
	const auto& ranges = project.groupRanges().rows();
	for (std::size_t i = 0; i < ranges.size(); ++i)
	{
		for (std::size_t j = i + 1; j < ranges.size(); ++j)
		{
			const GroupRange& a = ranges[i];
			const GroupRange& b = ranges[j];
			if (a.parent == b.parent && a.start <= b.end && b.start <= a.end)
			{
				sink.report(
					make("GA-007", Severity::Error, u"Sibling group ranges overlap"_s, {EntityRef(a.id), EntityRef(b.id)}));
			}
		}
	}
}

void checkDuplicateLinks(const Project& project, DiagnosticSink& sink)
{
	std::vector<std::pair<std::pair<std::uint64_t, std::uint64_t>, EntityRef>> entries;
	for (const Link& link : project.links())
		entries.emplace_back(std::pair(link.comObject.value(), link.groupAddress.value()), link.id);
	reportDuplicates(entries, "CO-002", u"Communication object is linked twice to the same group address"_s, sink);
}

void checkDuplicateComObjectNumbers(const Project& project, DiagnosticSink& sink)
{
	std::vector<std::pair<std::pair<std::uint64_t, unsigned>, EntityRef>> entries;
	for (const ComObject& object : project.comObjects())
		entries.emplace_back(std::pair(object.device.value(), object.number), object.id);
	reportDuplicates(entries, "CO-003", u"Duplicate communication object number on device"_s, sink);
}

const std::vector<RuleInfo>& rules()
{
	static const std::vector<RuleInfo> kRules = {
		{"BLD-001", Severity::Info, &checkDevicesNotPlaced},
		{"CO-001", Severity::Warning, &checkMultipleSendingLinks},
		{"CO-002", Severity::Error, &checkDuplicateLinks},
		{"CO-003", Severity::Error, &checkDuplicateComObjectNumbers},
		{"GA-001", Severity::Error, &checkDuplicateGroupAddresses},
		{"GA-002", Severity::Warning, &checkGroupAddressesWithoutDpt},
		{"GA-003", Severity::Error, &checkDptSizeMismatch},
		{"GA-004", Severity::Warning, &checkGroupAddressesWithoutLinks},
		{"GA-005", Severity::Warning, &checkGroupAddressesWithoutSender},
		{"GA-006", Severity::Error, &checkGroupAddressZero},
		{"GA-007", Severity::Error, &checkGroupRangeConsistency},
		{"TOP-001", Severity::Error, &checkDuplicateIndividualAddresses},
		{"TOP-002", Severity::Error, &checkDeviceNumberZero},
		{"TOP-003", Severity::Info, &checkDevicesWithoutAddress},
		{"TOP-004", Severity::Error, &checkDuplicateAreaAndLineNumbers},
	};
	return kRules;
}

void sortDiagnostics(std::vector<Diagnostic>& diagnostics)
{
	std::sort(diagnostics.begin(), diagnostics.end(),
		[](const Diagnostic& a, const Diagnostic& b)
		{
			return std::tie(a.severity, a.code, a.subjects, a.message)
				< std::tie(b.severity, b.code, b.subjects, b.message);
		});
}

std::vector<Diagnostic> run(const Project& project)
{
	DiagnosticSink sink;
	for (const RuleInfo& info : rules())
		info.rule(project, sink);
	std::vector<Diagnostic> diagnostics = sink.take();
	sortDiagnostics(diagnostics);
	return diagnostics;
}

} // namespace kestrel::validate
