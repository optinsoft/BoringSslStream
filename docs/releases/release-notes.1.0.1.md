## Changes

### macOS support & fixes
- Added macOS support: `libproxymap_boringssl.dylib` is now built and packaged into the NuGet package (`runtimes/osx-arm64/native`, RID configurable via `MacNativeRuntimeIdentifier`).
- Fixed `Certificate chain validation failed: An incomplete certificate revocation check occurred` when validating server certificates on macOS (`X509RevocationMode.Online` is unsupported on macOS, so it is automatically downgraded to `NoCheck`).
- Fixed `TLSV1_ALERT_PROTOCOL_VERSION` test failure: SslStream on macOS does not support TLS 1.3 server-side.

NuGet: https://www.nuget.org/packages/BoringSslStream/1.0.1
