#pragma once

// Website exclusions in exactly the forms adguardvpn-cli accepts (see "site-exclusions add --help"):
//   domain.net, www.domain.net, *.domain.net (any subdomain), IPv4[:port], IPv6, [IPv6][:port],
//   IPv4/mask, IPv6/mask.
// Shared by agvd (which refuses anything else) and the GUI (which normalizes what people paste).

#include <QString>
#include <QStringList>

namespace agv::site {

enum class Kind {
    Domain,    // domain.net or www.domain.net: the domain and its www subdomain
    Wildcard,  // *.domain.net or *.net: every subdomain, not the domain itself
    Address,   // an IP address, optionally with a port
    Range,     // CIDR
};

// Why an input was refused, so a UI can explain it in its own language.
enum class Problem {
    None,
    StarWithoutDot,  // "*domain.net" → suggestion "*.domain.net"
    StarInside,      // "a*.domain.net", "domain.*"
    BareZone,        // "ru" → suggestion "*.ru"
    Invalid,
};

struct Parsed {
    QString input;   // as typed (trimmed)
    QString value;   // normalized: lower case, punycode, no scheme/path
    Kind kind = Kind::Domain;
    QString error;   // English explanation; empty when valid
    Problem problem = Problem::None;
    QString suggestion;  // a valid form of what was probably meant, if any

    bool ok() const { return error.isEmpty(); }
};

// Strict check of one entry as the CLI accepts it (already normalized).
bool isValid(const QString &entry);

// Turns one pasted token into an entry: strips a URL's scheme, path and query, lower-cases,
// converts international names to punycode, and reads ".ru" as the whole zone "*.ru".
// Anything that still does not fit the CLI's syntax comes back with an error.
Parsed parse(const QString &input);

// Splits pasted text (spaces, commas, new lines) and parses each token.
QList<Parsed> parseList(const QString &text);

// The entries to add for one parsed domain: with includeSubdomains, "domain.net" also brings
// "*.domain.net". Other kinds come back unchanged.
QStringList expand(const Parsed &entry, bool includeSubdomains);

// Groups related entries: "domain.net", then "*.domain.net", then other domains.
QString sortKey(const QString &entry);

} // namespace agv::site
