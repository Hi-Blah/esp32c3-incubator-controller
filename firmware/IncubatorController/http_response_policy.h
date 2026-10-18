#pragma once
namespace httppolicy {
// Only the tiny landing page probes the configured NAS. Local controls and
// APIs keep their existing same-origin restriction and CSRF authentication.
constexpr const char *localCsp="default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";
constexpr const char *entryCsp="default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; connect-src 'self' " INC_NAS_WEB_ORIGIN "; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";
}
