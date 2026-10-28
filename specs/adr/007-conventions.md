# ADR-007 — Coding conventions

Status: Accepted

- Style follows `UnitTests.hpp`: tabs, Allman braces, PascalCase types, camelCase functions, `m_` members, `k` constants,
  `KESTREL_` macros. Namespaces `kestrel::<lib>`.
- Public headers live in `src/<lib>/include/kestrel/<lib>/`.
- Expected failures return `Result<T>` or `Status`. Contract violations use `assert`. Exceptions never cross a public API or a Qt slot.
- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion…`, with `-Werror` in presets/CI.
  Qt and UnitTests.hpp are SYSTEM includes.
- Qt: `QT_NO_KEYWORDS` (`Q_SIGNALS`/`Q_EMIT`), `QT_NO_CAST_FROM_ASCII` (`u"…"_s`). Logging uses a `QLoggingCategory` per library (`kestrel.<lib>`).
- Every value type has `operator==` and `operator<<`.
