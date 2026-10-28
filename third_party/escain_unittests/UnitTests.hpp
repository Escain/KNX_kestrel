/* Copyright (c) 2024 Adrian Maire
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#ifndef ESCAIN_UNIT_TEST_HPP
#define ESCAIN_UNIT_TEST_HPP

#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <string>

// Platform detection to manage termination and recover.
//
// Two implementations for catching abort() / SIGSEGV / SIGFPE / SIGILL:
//
//   - MSVC (`_MSC_VER`): Structured Exception Handling.
//       `_set_se_translator` rewrites SEH exceptions (access violation,
//       divide by zero, ...) into a C++ TerminationException, and
//       `__try`/`__except` catches them. Required on MSVC because the
//       CRT delivers hardware faults via SEH, not C signals —
//       `signal(SIGSEGV, ...)` is essentially a no-op there.
//
//   - MinGW-w64 + POSIX: C signal() + setjmp/longjmp.
//       MinGW g++ doesn't accept the `__try`/`__except` keywords; and
//       its CRT does translate hardware faults into signals, so the
//       POSIX-style approach catches everything. The only piece that's
//       genuinely POSIX-only is sigprocmask() (POSIX auto-blocks the
//       delivered signal during handler; Windows CRT does not), gated
//       below by `PLATFORM_UNIX`.
#include <csignal>   // signal, SIGABRT, SIGSEGV, ...
#include <csetjmp>   // jmp_buf, setjmp, longjmp
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
    #include <io.h>      // _dup / _dup2
    #include <cstdio>    // _fileno, freopen
    #if defined(_MSC_VER)
        #include <windows.h>  // EXCEPTION_POINTERS, STATUS_*
        #include <eh.h>       // _set_se_translator
    #endif
#else //Posix
    #define PLATFORM_UNIX
    #include <unistd.h>  // dup / dup2 / STDERR_FILENO
#endif

/*
 * Create tests in the following way:
 * UNIT_TEST(NameOfTheTest)
 * {
 *     ASSERT(myFunction()==expectedResult);
 * }
 *
 * Create unit_tests with a base clase like
 * UNIT_TEST(NameOfTheTest, Fixture)
 * ...
 *
 * You may use the constructor to setup element, and use
 * the Fixture member in your tests.
 */


//*-----------------------------------------------------------------------
/// Base class for all unit-test
class TEST
{
	bool m_result=true; ///save the result of the test: true==pass, false==Fail
	[[maybe_unused]] char _unused[7];       /// Avoid alignment warning
public:
	virtual std::string getName() const =0; /// Return the textual name of the test
	virtual ~TEST() = default;
	virtual void run()=0;                   /// Run the test

	void setFail(){m_result=false;}         /// Save the result of the test Pass/true, Fail/false
	bool isPassed() const {return m_result;}       /// Retrieve the result of the test
};

//*------------------------------------------------------------------------
/// Registry to save all classes
class Registry
{
public:
	using factoryMethod = std::function<TEST*()>;
	using Entry         = std::pair<std::string, factoryMethod>;

	static void registerClass(const std::string& name, factoryMethod meth)
	{
		registry().push_back(std::make_pair(name, meth));
	}
	static const std::list<Entry>& getRegistry() { return registry(); }

private:
	// Function-local static (Meyers singleton): the list is constructed
	// on the very first call and torn down at program exit.
	//
	// Previously we used `inline static std::list<...>` as a class
	// member. That works on Linux by luck — UNIT_TEST macros expand to
	// static initializers in every `*_test.cpp` that call
	// `registerClass()`. With a class-scope `inline static`, the order
	// in which the list itself is dynamically initialized relative to
	// those callers is unspecified across translation units. On
	// MinGW-w64 the callers can fire first, push_back-ing into a list
	// whose internal pointers haven't been default-constructed yet,
	// producing 0xC0000374 (STATUS_HEAP_CORRUPTION) before main even
	// starts. The function-local static is guaranteed initialized on
	// first call, sidestepping the static-init-order fiasco.
	static std::list<Entry>& registry()
	{
		static std::list<Entry> instance;
		return instance;
	}
};

//*-------------------------------------------------------------------------
/// ASSERT MACROS
#define ASSERT(cond)                                         \
	if( !(cond) )                                            \
	{                                                        \
	    std::cout <<  "    " << __LINE__ << " FAIL ASSERT: " \
	        << getName() << " : " << #cond << std::endl;     \
	    setFail();                                           \
	    return;                                              \
	}

#define ASSERT_FALSE(cond)                                   \
	if( cond )                                               \
	{                                                        \
		std::cout <<  "    " << __LINE__ << " FAIL ASSERT_FALSE: " \
			<< getName() << " : " << #cond << std::endl;     \
		setFail();                                           \
		return;                                              \
	}

#define ASSERT_EQ(a,b)                                       \
	{                                                        \
		auto aVal = a;                                       \
		decltype(aVal) bVal = b;                             \
		if(!(aVal==bVal))                                    \
		{                                                    \
			std::cout << "    " << __LINE__ << " FAIL EQ: "  \
				<< getName() << " : " << #a << "("<< aVal << \
				") != "<<#b <<"(" <<bVal<<")" << std::endl;  \
			setFail();                                       \
			return;                                          \
		}                                                    \
	}

#define ASSERT_NEQ(a,b)                                      \
	{                                                        \
		auto aVal = a;                                       \
		decltype(aVal) bVal = b;                             \
		if(aVal==bVal)                                       \
		{                                                    \
			std::cout << "    " << __LINE__ << " FAIL NEQ: " \
				<< getName() << " : " << #a << "("<< aVal << \
				") == "<<#b <<"(" <<bVal<<")" << std::endl;  \
			setFail();                                       \
			return;                                          \
		}                                                    \
	}

#define MUST_THROW(a)                                        \
	{                                                        \
	    bool throwed = false;                                \
	    try                                                  \
	    {                                                    \
	        a;                                               \
	    }                                                    \
	    catch(...)                                           \
	    {                                                    \
	        throwed = true;                                  \
	    }                                                    \
	    if (!throwed)                                        \
	    {                                                    \
	        std::cout << "    " << __LINE__ << " FAIL TO THROW: " \
	            << getName() << std::endl;                   \
	        setFail();                                       \
	        return;                                          \
	    }                                                    \
	}

#define MUST_NOT_THROW(a)                                    \
	{                                                        \
	    bool throwed = false;                                \
	    try                                                  \
	    {                                                    \
	        a;                                               \
	    }                                                    \
	    catch(...)                                           \
	    {                                                    \
	        throwed = true;                                  \
	    }                                                    \
	    if (throwed)                                        \
	    {                                                    \
	        std::cout << "    " << __LINE__ << " FAIL TO NOT THROW: " \
	            << getName() << std::endl;                   \
	        setFail();                                       \
	        return;                                          \
	    }                                                    \
	}


//*-------------------------------------------------------------------------
// Manage assert and terminations

// Exception thrown when termination is caught
class TerminationException : public std::exception
{
private:
	std::string message_;

public:
	explicit TerminationException(const char* msg) : message_(msg) {}
	const char* what() const noexcept override { return message_.c_str(); }
};


// Temporarily redirect stderr to suppress assertion messages
class StderrMuter final
{
private:
	int old_stderr = 0;
	bool m_mute = false;
public:
	StderrMuter( bool mute )
		: m_mute(mute)
	{
		if (m_mute)
		{
#ifdef PLATFORM_WINDOWS
			old_stderr = _dup(_fileno(stderr));
			freopen("NUL", "w", stderr);
#else
			old_stderr = dup(STDERR_FILENO);
			freopen("/dev/null", "w", stderr);
#endif
		}
	}

	~StderrMuter()
	{
		if (m_mute)
		{
			fflush(stderr);
#ifdef PLATFORM_WINDOWS
			_dup2(old_stderr, _fileno(stderr));
#else
			dup2(old_stderr, STDERR_FILENO);
#endif
		}
	}
};

// Termination capture — two backends, picked by the active compiler.

#if defined(PLATFORM_WINDOWS) && defined(_MSC_VER)

// MSVC SEH backend.
// The CRT delivers hardware faults via SEH exceptions, not C signals.
// _set_se_translator converts each SEH code into a TerminationException
// the user-level try/catch can catch. `inline` keeps it ODR-safe across
// translation units that include this header.
namespace detail
{
	inline void se_translator(unsigned int code, EXCEPTION_POINTERS*)
	{
		const char* exception_name = "Unknown";
		switch (code)
		{
			case STATUS_ACCESS_VIOLATION:        exception_name = "Access Violation";        break;
			case STATUS_ILLEGAL_INSTRUCTION:     exception_name = "Illegal Instruction";     break;
			case STATUS_PRIVILEGED_INSTRUCTION:  exception_name = "Privileged Instruction";  break;
			case STATUS_FLOAT_DIVIDE_BY_ZERO:
			case STATUS_INTEGER_DIVIDE_BY_ZERO:  exception_name = "Divide by Zero";          break;
			case STATUS_STACK_OVERFLOW:          exception_name = "Stack Overflow";          break;
			case 0xC0000409: /* STATUS_STACK_BUFFER_OVERRUN */
			                                     exception_name = "Buffer Overrun";          break;
			default:                             exception_name = "Fatal Application Exit";  break;
		}
		throw TerminationException(exception_name);
	}
}

#define TERMINATE_2_EXCEPT(code, mute)                              \
	do                                                              \
	{                                                               \
		StderrMuter suppress(mute);                                 \
		_se_translator_function old_translator =                    \
			_set_se_translator(detail::se_translator);              \
		try                                                         \
		{                                                           \
			__try                                                   \
			{                                                       \
				code;                                               \
			}                                                       \
			__except(EXCEPTION_EXECUTE_HANDLER)                     \
			{                                                       \
				/* Already converted to TerminationException */     \
			}                                                       \
		}                                                           \
		catch (...)                                                 \
		{                                                           \
			_set_se_translator(old_translator);                     \
			throw;                                                  \
		}                                                           \
		_set_se_translator(old_translator);                         \
	}                                                               \
	while(0)

#else // POSIX + MinGW-w64 — signal()/setjmp/longjmp backend.

// `inline thread_local` / `inline void` so each TU including this header
// gets the same definition (C++17). `__attribute__((weak))` previously
// worked on ELF but produced multiple-definition link errors on MinGW
// PE/COFF.
inline thread_local std::jmp_buf* active_jump_buffer = nullptr;

inline void termination_signal_handler(int signum)
{
	if (active_jump_buffer != nullptr)
	{
#ifdef PLATFORM_UNIX
		// POSIX auto-blocks the delivered signal during handler; unblock
		// before longjmp so a second termination can be caught.
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, signum);
		sigprocmask(SIG_UNBLOCK, &mask, nullptr);
#endif
		std::longjmp(*active_jump_buffer, signum);
	}
	// If not caught by our handler, restore default and re-raise
	signal(signum, SIG_DFL);
	raise(signum);
}


#define TERMINATE_2_EXCEPT(code, mute)                                                      \
	{                                                                                       \
		StderrMuter suppress(mute);                                                         \
		do                                                                                  \
		{                                                                                   \
			std::jmp_buf jump_buf;                                                          \
			active_jump_buffer = &jump_buf;                                                 \
																							\
			void (*old_sigabrt)(int) = signal(SIGABRT, termination_signal_handler);         \
			void (*old_sigsegv)(int) = signal(SIGSEGV, termination_signal_handler);         \
			void (*old_sigill)(int) = signal(SIGILL, termination_signal_handler);           \
			void (*old_sigfpe)(int) = signal(SIGFPE, termination_signal_handler);           \
																							\
			int sig = setjmp(jump_buf);                                                     \
			if (sig == 0)                                                                   \
			{                                                                               \
				code;                                                                       \
				active_jump_buffer = nullptr;                                               \
				signal(SIGABRT, old_sigabrt);                                               \
				signal(SIGSEGV, old_sigsegv);                                               \
				signal(SIGILL, old_sigill);                                                 \
				signal(SIGFPE, old_sigfpe);                                                 \
			}                                                                               \
			else                                                                            \
			{                                                                               \
				active_jump_buffer = nullptr;                                               \
				signal(SIGABRT, old_sigabrt);                                               \
				signal(SIGSEGV, old_sigsegv);                                               \
				signal(SIGILL, old_sigill);                                                 \
				signal(SIGFPE, old_sigfpe);                                                 \
																							\
				const char* sig_name = "Unknown";                                           \
				if (sig == SIGABRT) sig_name = "SIGABRT (abort)";                           \
				else if (sig == SIGSEGV) sig_name = "SIGSEGV (segmentation fault)";         \
				else if (sig == SIGILL) sig_name = "SIGILL (illegal instruction)";          \
				else if (sig == SIGFPE) sig_name = "SIGFPE (floating point exception)";     \
																							\
				throw TerminationException(sig_name);                                       \
			}                                                                               \
		}                                                                                   \
		while(0);                                                                           \
	}

#endif // _MSC_VER selection

// MUST_DIE macro - expects code to terminate, not throw
#define MUST_DIE(code)                                              \
	do                                                              \
	{                                                               \
		bool throwed = false;                                       \
		try                                                         \
		{                                                           \
			TERMINATE_2_EXCEPT(code, true);                         \
		}                                                           \
		catch (const TerminationException& e)                       \
		{                                                           \
			throwed = true;                                         \
		}                                                           \
		if (!throwed)                                               \
		{                                                           \
			std::cout << "    " << __LINE__ << " FAIL TO DIE: "     \
				<< getName() << std::endl;                          \
			setFail();                                              \
			return;                                                 \
		}                                                           \
	}                                                               \
	while(0)

// MUST_THROW_OR_DIE macro - expects code to terminate/throw
#define MUST_THROW_OR_DIE(code)                                     \
	do                                                              \
	{                                                               \
		bool throwed = false;                                       \
		try                                                         \
		{                                                           \
			TERMINATE_2_EXCEPT(code, true);                         \
		}                                                           \
		catch (const TerminationException& e)                       \
		{                                                           \
			throwed = true;                                         \
		}                                                           \
		catch (const std::exception& e)                             \
		{                                                           \
			throwed = true;                                         \
		}                                                           \
		if (!throwed)                                               \
		{                                                           \
			std::cout << "    " << __LINE__ << " FAIL TO THROW/DIE: "   \
				<< getName() << std::endl;                          \
			setFail();                                              \
			return;                                                 \
		}                                                           \
	}                                                               \
	while(0)

//*-------------------------------------------------------------------------
#define REGISTER_CLASS(ClassType)                            \
	static TEST* createInstance()                            \
	{                                                        \
	    return new ClassType();                              \
	}                                                        \
	static int initRegistry()                                \
	{                                                        \
	    Registry::registerClass(                             \
	        std::string(#ClassType),                         \
	        ClassType::createInstance);                      \
	    return 0;                                            \
	}                                                        \
	static const int regToken;                               \


#define DEFINE_REG_CLASS(ClassType)                          \
	const int ClassType::regToken = ClassType::initRegistry();

/// UNIT_TEST standalone, without base class
#define UNIT_TEST1(name)                                     \
	struct name: public TEST                                 \
	{                                                        \
	    std::string getName() const override                 \
	    { return #name;}                                     \
	    void run() override;                                 \
	    REGISTER_CLASS(name)                                 \
	};                                                       \
	DEFINE_REG_CLASS(name)                                   \
	void name::run()

/// UNIT_TEST with base-class
#define UNIT_TEST2(name,Base)                                \
	struct name: public TEST, public Base                    \
	{                                                        \
	    std::string getName() const override                 \
	    { return #name;}                                     \
	    void run() override;                                 \
	    REGISTER_CLASS(name)                                 \
	};                                                       \
	DEFINE_REG_CLASS(name)                                   \
	void name::run()


//*----------------------------------------------------------------------
// Make UNIT_TEST to be overloaded for one or two parameters
#define GET_MACRO(_1,_2,NAME,...)                            \
	NAME

#define UNIT_TEST(...)                                       \
	GET_MACRO(__VA_ARGS__, UNIT_TEST2, UNIT_TEST1)(__VA_ARGS__)


//*----------------------------------------------------------------------
// Function to print the result of individual tests
// (Registry::registry is now an inline-static member of the class.)

inline void printResult( const TEST& obj, size_t testIndex, size_t registrySize, size_t maxNameLength)
{
	auto digits = []( size_t n)
	{
		size_t numDigits=1;
		for (size_t comparer=10; comparer<=n; comparer*=10, numDigits++) continue;

		return numDigits;
	};

	std::cout << std::string(digits(registrySize) - digits(testIndex+1), ' ');
	std::cout << "\033[37m" << testIndex+1 << "/" << registrySize << "\033[0m";
	std::cout << std::string(2, ' ');
	std::cout << obj.getName() << ' ';
	std::cout << std::string(maxNameLength - obj.getName().size(), '.');
	std::cout << (obj.isPassed()?"\033[32m OK\033[0m" : "\033[31m\033[1m FAIL\033[0m") << std::endl;
}


//*----------------------------------------------------------------------
// Main function, calling automatically all UNIT_TEST
//
// Only emitted in the TU that defines ESCAIN_DEFINE_MAIN before
// including this header (test_main.cpp). main() cannot be `inline`,
// so the previous `__attribute__((weak))` approach was used to allow
// every translation unit to carry a copy and let the linker pick one;
// that fails on MinGW PE/COFF (weak semantics differ from ELF). The
// macro gate keeps the header drop-in for every test .cpp while
// ensuring exactly one definition lands in the executable.
//
// -Wclobbered (enabled under -Wextra) flags counters that live across
// TERMINATE_2_EXCEPT's setjmp. In our control flow each iteration is
// either successful (no longjmp) or the signal handler longjmps once
// and the macro throws — main()'s catch handler then resumes the loop,
// so the counters are never mutated between setjmp and longjmp.
// volatile would silence the warning but C++20 deprecates `++` on
// volatile, so suppress the warning locally instead. The pragma is
// GCC-only (Clang accepts and ignores it); MinGW also accepts it.
#ifdef ESCAIN_DEFINE_MAIN
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wclobbered"
#endif
int main(int argn, char* argv[])
{
	using namespace std::string_literals;

	size_t count = 0; // Count all tests, including those filtered
	size_t countMustPass = 0; // Count tests that are not filtered
	size_t countPassed = 0;   // Count tests that are passed
	[[maybe_unused]] size_t registrySize = Registry::getRegistry().size();

	// Catch terminate to print an error. It should ideally never happens if properly handled.
	std::set_terminate([]()
	{
		std::cerr << "Unhandled terminate!" << std::endl;
		std::abort();
	});

	// Manage arguments
	struct Arguments
	{
		bool verbose=false;
		std::string filter;
	};
	Arguments args;

	for (int i=1; i<argn; ++i)
	{
		const std::string rawArg = argv[i];
		if (rawArg == "-v" || rawArg == "--verbose")
		{
			args.verbose = true;
		}

		if (rawArg.starts_with("-f=") || rawArg.starts_with("--filter="))
		{
			args.filter = rawArg.substr(rawArg.find_first_of("=")+1);
		}

		if (rawArg.starts_with("-h") || rawArg.starts_with("--help"))
		{
			std::cout << "Unit test arguments:" << std::endl;
			std::cout << " * -v | --verbose                      : show verbose output with each test result, default FALSE" << std::endl;
			std::cout << " * -h | --help                         : show this help message" << std::endl;
			std::cout << " * -f=<TestName> | --filter=<TestName> : runs only that specific test" << std::endl;
			std::cout << std::endl;
			return 0;
		}
	}

	// Compute the length of the longest test name, to have aligned results
	[[maybe_unused]] size_t maxNameLength=0;
	for (auto& entry : Registry::getRegistry())
	{
		if (!args.filter.empty() && std::string::npos == entry.first.find(args.filter))
		{
			continue;
		}

		if (entry.first.size() > maxNameLength)
		{
			maxNameLength = entry.first.size();
		}
	}

	// Iterate tests and execute them.
	for(auto entry : Registry::getRegistry())
	{
		if (!args.filter.empty() && std::string::npos == entry.first.find(args.filter))
		{
			count++;
			continue;
		}

		const auto& constructor = entry.second;
		auto obj = std::unique_ptr<TEST>(constructor());

		try
		{
			TERMINATE_2_EXCEPT ( obj->run(), false );
		}
		catch( const std::exception& e)
		{
			std::cout << "    " << "THROW: " << obj->getName() << " : " << e.what() << std::endl;
			obj->setFail();
		}
		catch(...)
		{
			std::cout << "    " << "THROW a non-standard exception: " << obj->getName() << std::endl;
			obj->setFail();
		}

		if (args.verbose)
		{
			printResult(*obj, count, registrySize, maxNameLength);
		}

		countPassed += obj->isPassed()?1:0;
		count++;
		countMustPass++;
	}

	// Show the result summary
	const std::string skippedStr = countMustPass==count ? "" : " (skipped: "s + std::to_string(count-countMustPass) + ")";
	std::string summaryStr = "";
	if (countMustPass != countPassed)
	{
		summaryStr = "\033[31m\033[1mFAILURE\033[0m: "s + std::to_string(countPassed) + " passed of " + std::to_string(countMustPass) + skippedStr;
	}
	else
	{
		summaryStr = "ALL TEST \033[32mPASSED\033[0m: " + std::to_string(countMustPass) + skippedStr;
	}

	std::cout << std::string(summaryStr.size(), '-') << std::endl;
	std::cout << summaryStr << std::endl;

	if (countMustPass != countPassed)
	{
		return 1;
	}

	return 0;
}
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif
#endif // ESCAIN_DEFINE_MAIN

#endif //ESCAIN_UNIT_TEST_HPP
