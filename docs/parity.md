# Parity status

Reference: Mozilla Readability commit
`ab4027a8b37669745016869a37a504727992b2ba` (`0.6.0-15-gab4027a`).

The unmodified upstream `test/test-pages` corpus is stored under
`tests/fixtures`: 130 source documents, 130 expected HTML files, and 130
expected metadata/configuration files. The C++ suite parses every source with
the lightweight DOM, checks `isProbablyReaderable`, compares the extracted DOM
node-by-node (including attributes and normalized text), and checks every
metadata field used upstream.

The API cases from `test-readability.js`, option/threshold/callback cases from
`test-isProbablyReaderable.js`, and DOM mutation, escaping, script parsing,
case, namespace, recovery, and base-URI cases from `test-jsdomparser.js` are
also represented in the C++ test executable.

Current result: all 130 fixture content comparisons, all 130 metadata
comparisons, all 130 readerability expectations, parser/API tests, and sanitizer
runs pass. There are no known algorithm-output divergences for the upstream
fixture corpus.

The test comparator mirrors upstream's `prettyPrint` step by collapsing HTML
whitespace and disregarding formatting-only trailing whitespace at the end of
equivalent parent nodes. Expected files are never rewritten.
