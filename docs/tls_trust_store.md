# TLS trust store

Copy `include/config/tls_trust_store_example.h` to `include/config/tls_trust_store.h`.

Set `GOOGLE_SCRIPT_TRUSTED_CA_PEM` to an up-to-date PEM bundle that validates both the configured Google Apps Script host and its approved redirect host. Source and refresh the bundle through the managed Google trust-store guidance at `https://pki.goog/faq/` when preparing firmware releases.

Without the file or with an empty bundle, cloud uploads fail closed and the LCD reports `TLS: config`. If SNTP cannot establish UTC before a TLS connection, it reports `NTP: errore` and does not upload telemetry.
