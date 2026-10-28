#include "kestrel/base/Result.hpp"

namespace kestrel
{

const char* toString(ErrorCode code)
{
	switch (code)
	{
		case ErrorCode::InvalidArgument: return "InvalidArgument";
		case ErrorCode::ParseError:      return "ParseError";
		case ErrorCode::Unsupported:     return "Unsupported";
		case ErrorCode::NotFound:        return "NotFound";
		case ErrorCode::Conflict:        return "Conflict";
		case ErrorCode::IoError:         return "IoError";
		case ErrorCode::Timeout:         return "Timeout";
		case ErrorCode::ProtocolError:   return "ProtocolError";
		case ErrorCode::Cancelled:       return "Cancelled";
	}
	return "Unknown";
}

std::ostream& operator<<(std::ostream& os, ErrorCode code)
{
	return os << toString(code);
}

std::ostream& operator<<(std::ostream& os, const Error& error)
{
	return os << toString(error.code) << ": " << error.message;
}

} // namespace kestrel
