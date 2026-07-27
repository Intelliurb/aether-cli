/*
 * Copyright (c) 2026 caq@intelliurb.com
 */

#pragma once

namespace cli {
// Versão injetada pelo CMake (PROJECT_VERSION); fallback para builds manuais.
#ifdef AETHERCLI_VERSION
inline constexpr const char* VERSION = AETHERCLI_VERSION;
#else
inline constexpr const char* VERSION = "0.6.0";
#endif
} // namespace cli
