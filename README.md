# BoringSslStream

A .NET library that provides an `SslStream`-compatible TLS stream backed by
**BoringSSL** — the TLS library used by Chromium — with full control over the
ClientHello, enabling byte-perfect Chrome JA4/JA3 fingerprinting and native
HTTP/2 support.

All BoringSSL native read/write operations are routed through a managed .NET
`NetworkStream` (or any `Stream`) via custom BIO callbacks:

```text
TcpClient
   |
NetworkStream  (innerStream)
   |
BoringSslStream (C#): pumps bytes between the stream and the buffers
   |
BoringSSL, memory BIO (pure TLS state, no network)
   |
HTTP/1.1 or HTTP/2
```

The repository also contains a **demo console application**
(`BoringSslConsole`) that showcases the library by connecting to real HTTPS
endpoints, and an **xUnit test suite** (`BoringSslStream.Tests`).

## Key Features

- **Full-Duplex Architecture:** Independent read/write and network
  synchronization locks (`_readLock`, `_writeLock`, `_networkReadLock`,
  `_networkWriteLock`). This allows the .NET application to read and write
  over TLS concurrently without risking memory corruption or race conditions
  within the native BoringSSL `SSL*` state machine.
- **Production-Grade Certificate Validation:** Built-in cryptographic chain
  validation (`X509Chain`) utilizing the host operating system's trusted root
  certificate store. It natively enforces online revocation checks
  (`X509RevocationMode.Online`).
- **Secure Hostname & Wildcard Matching:** Uses the modern .NET 8 API
  (`certificate.MatchesHostname`) to safely validate Subject Alternative Names
  (SAN) and Common Names (CN), including strict adherence to RFC 6125 wildcard
  rules (preventing sub-domain bypasses).
- **Flawless Chrome JA4 Fingerprinting:** Achieves a 100% perfect JA4 match
  with Google Chrome. The C++ native layer and C# presets coordinate to
  replicate browser features seamlessly, including GREASE orchestration,
  dynamic ALPN/ALPS context slicing, precise `status_request` padding,
  post-quantum ML-KEM/ML-DSA groups, and extension permutation.
- **Native HTTP/2 Support:** Fully supports HTTP/2 protocol negotiation via
  ALPN ("h2"). Features a lightweight frame processing pipeline, HPACK binary
  payload encoding for pseudo-headers, and frame-level stream flow handling
  (DATA, HEADERS, SETTINGS, RST_STREAM, GOAWAY).

## Installation

The library ships as a self-contained NuGet package: the managed assembly and
the prebuilt native binary (`proxymap_boringssl`) travel together under
`runtimes/{RID}/native/`, so NuGet picks the correct binary for the consumer's
platform automatically — no external dependencies, no manual DLL copying.

```powershell
dotnet add package BoringSslStream
```

## Usage

```csharp
using System.Net.Sockets;
using BoringSsl;

using var client = new TcpClient(host, 443);
await using NetworkStream networkStream = client.GetStream();

using var sslStream = new BoringSslStream(networkStream, host);
await sslStream.AuthenticateAsClientAsync();

Console.WriteLine($"TLS version:   {sslStream.ProtocolVersion}");
Console.WriteLine($"Cipher:        {sslStream.CipherName}");
Console.WriteLine($"ALPN selected: {sslStream.AlpnSelected}");
```

The stream exposes the same surface as `System.Net.Security.SslStream`
(`ReadAsync`, `WriteAsync`, certificate validation callbacks, ALPN/ALPS
options), so it can be dropped in wherever a TLS stream is expected.

## Building from source

This repository incorporates BoringSSL and Google Brotli as Git submodules to
ensure consistent, standalone compilation:

```text
BoringSslStream.sln
  BoringSslStream/        (Managed C# class library, NuGet-packable)
  BoringSslConsole/       (Demo console application)
  BoringSslStream.Tests/  (xUnit test suite)
  Native/                 (Native C++ Wrapper & CMake configuration)
  boringssl/              <-- Git submodule
  brotli/                 <-- Git submodule (Required for certificate compression)
```

To clone this repository along with the BoringSSL dependency, run:

```powershell
git clone --recursive https://github.com/optinsoft/BoringSslStream.git
```

### Prerequisites

Windows:

- .NET 8 SDK
- Visual Studio 2022 with C++ tools (x64 toolset)
- CMake 3.22+ and Ninja
- Git, NASM, Go

Linux (tested on Ubuntu with WSL): `cmake`, `ninja-build`, `g++`, `perl`, `go`.

BoringSSL documents its current build requirements in BUILDING.md:
https://github.com/google/boringssl/blob/main/BUILDING.md

### Native library

Windows — open a **Developer PowerShell for VS 2022 (x64)** and run:

```powershell
cd Native
.\build.ps1
```

Linux:

```bash
cd Native
./build.sh
```

The project builds its own copy of BoringSSL and links the Brotli
decoder/encoder components statically into a single, fully autonomous
`proxymap_boringssl.dll` (Windows) / `libproxymap_boringssl.so` (Linux) to
prevent runtime `DllNotFoundException` errors.

> **Note:** build both platforms from the same source revision so that the
> shipped binaries are in sync with the managed code.

### .NET build, tests and packing

```powershell
dotnet build BoringSslStream.sln
dotnet test  BoringSslStream.Tests
dotnet pack  BoringSslStream/BoringSslStream.csproj -c Release   # -> BoringSslStream.1.0.0.nupkg
```

The native binaries are copied into the .NET output directories automatically
during local builds, and embedded into the NuGet package under
`runtimes/win-x64/native/` and `runtimes/linux-x64/native/` when packing.

## Demo console application

`BoringSslConsole` demonstrates the end-to-end flow against a real server
(`tls.peet.ws/api/tls` by default):

```powershell
dotnet run --project BoringSslConsole
```

What it does:

1. Opens a standard TCP connection to the target server (e.g. `tls.peet.ws:443`).
2. Creates a BoringSSL `SSL*` object and configures context parameters passed
   dynamically from C# (cipher list, ALPN, ALPS protocols, signature
   algorithms, and GREASE flags).
3. Connects the TLS state machine to custom, memory-isolated static read and
   write BIOs (`BIO_s_mem`).
4. Performs `SSL_connect()` via asynchronous native callbacks.
5. **Validates the server certificate:** immediately after a successful TLS
   handshake, it extracts the server's DER-encoded X509 certificate via the
   native wrapper, runs a full OS-level chain validation and matches the
   connection hostname.
6. Prints the negotiated TLS version, selected ALPN protocol, and cipher suite
   name.
7. **Dispatches the HTTP request:**
   - **If HTTP/2 is negotiated ("h2"):** sends the mandatory 24-byte connection
     preface, transmits initial `SETTINGS` frames, encodes request metadata
     using the HPACK encoder, wraps the binary payload into standard 9-byte
     `HEADERS` frame envelopes, and handles bidirectional settings
     acknowledgment.
   - **If HTTP/1.1 is negotiated:** falls back to dispatching plain-text
     HTTP/1.1 requests.
8. Streams the incoming protocol data (automatically demuxing framed binary
   envelopes for HTTP/2 payloads), prints response outcomes to the console,
   and flushes output records into the `./output/` directory.

### Testing certificate validation

The `Program.cs` file includes a set of pre-configured, commented-out test
hosts from the **badssl.com** project to verify secure error-handling:

- `tls.peet.ws` / `badssl.com` - **Standard Verification:** Connection should succeed.
- `expired.badssl.com` - **Expiration Failure:** Handshake must fail during `X509Chain` building.
- `untrusted-root.badssl.com` - **Trust Failure:** Handshake must fail due to an unknown root authority.
- `revoked.badssl.com` - **Revocation Failure:** Handshake must fail because the certificate was revoked by the CA.

## Tests

The xUnit suite in `BoringSslStream.Tests` runs a local TLS echo/ALPN server
and exercises the library end-to-end: chunked read/write, concurrent
read+write, dispose during concurrent I/O, ALPN options handling, and
certificate policy error reporting.

```powershell
dotnet test BoringSslStream.Tests
```

## Current Status

The underlying architecture is fully complete, completely thread-safe, and
passes advanced fingerprinting verifications (JA3/JA4/JA5 engines). The client
appears identical to an official Google Chrome browser build to edge
infrastructure (Cloudflare, Akamai, etc.) and seamlessly handles modern HTTP/2
communication streams.

## License

See [LICENSE](LICENSE).
