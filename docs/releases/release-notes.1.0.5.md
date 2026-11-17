# BoringSslStream 1.0.5

Reliability and hardening release. It brings BoringSslStream in line with the
lifecycle, shutdown and input-validation fixes recently made to NssStream.
Deploy the native library (`proxymap_boringssl`) and the managed assembly
together.

## Highlights

- Native connections are now released deterministically on `Dispose` /
  `DisposeAsync` instead of waiting for the finalizer.
- Fixed hangs and masked exceptions caused by disposing the internal
  semaphores while operations were still waiting on them.
- Fixed spurious `PMS_SSL_ERROR` results caused by stale entries in the
  thread-local OpenSSL error queue.
- Native API now validates its inputs and no longer lets C++ exceptions cross
  the P/Invoke boundary.

## Fixed

### Native wrapper (`proxymap_boringssl`)

- **Spurious SSL errors.** `ERR_clear_error()` is now called before
  `SSL_connect`, `SSL_read` and `SSL_write`, and the rest of the error queue
  is cleared after an error is captured. Previously, leftover errors from an
  earlier failed operation on the same thread could make `SSL_get_error`
  report `SSL_ERROR_SSL` for an unrelated connection that merely needed more
  data.
- **C++ exceptions no longer escape `pms_ssl_create`.** Allocation failures
  (`bad_alloc` from `std::string` / `std::vector`) are caught and reported as
  a null result instead of terminating the process.
- **Simpler, safer cleanup.** `PmsSslConnection` now owns its `SSL` and
  `SSL_CTX` (RAII) and `pms_ssl_create` uses `std::unique_ptr`, so no error
  path can leak or double-release them.
- **`trust_anchors` is validated.** The hex string must have an even length
  and contain only hex digits. Previously an odd length or non-hex characters
  silently produced wrong bytes.
- **Input length validation.** `pms_ssl_feed_read` and `pms_ssl_take_write`
  reject negative lengths (zero is a no-op returning `0`); `pms_ssl_read` and
  `pms_ssl_write` reject non-positive lengths. All of them check for null
  buffers.

### Managed (`BoringSslStream`)

- **Native memory is freed on dispose.** `Dispose` / `DisposeAsync` now
  close the native handle under the native-call lock. Before, the `SSL*` and
  `SSL_CTX*` were released only when the `SafeHandle` was finalized.
- **No more semaphore disposal.** `_readLock` / `_writeLock` are no longer
  disposed. Disposing them left waiters hanging and made `Release()` in
  `finally` blocks throw `ObjectDisposedException`, hiding the original error.
- **Quiet pump shutdown.** The write pump no longer surfaces an
  `ObjectDisposedException` caused by disposal as a spurious "Failed to write
  TLS data" `IOException`; its `CancellationTokenSource` is now disposed.
- **`ThrowIfDisposed()` after every wait** in `AuthenticateAsClientAsync`,
  `ReadAsync`, `WriteAsync` and `FlushAsync`, and at the start of
  `ApplyClientOptions`.
- **`ApplyClientOptions` is atomic.** Unsupported options
  (`EnabledSslProtocols` without TLS 1.2/1.3, client certificates, local
  certificate selection) are rejected before any state is changed.
- **Calling convention.** All native imports are explicitly `cdecl`
  (`[UnmanagedCallConv(CallConvs = [typeof(CallConvCdecl)])]`).

## Changed

- **The native connection is created lazily**, at the start of
  `AuthenticateAsClientAsync`, instead of in the constructor. This removes the
  duplicate native connection that was created and thrown away whenever
  `TargetHost` or `ApplicationProtocols` overrode the constructor values, and
  removes the unsafe re-creation path.
- **Pooled I/O buffer for the write pump.** A buffer is rented from
  `ArrayPool` per drain and pinned only for the duration of the native call,
  replacing a permanently pinned 16 KB buffer per connection.

## Behavior changes

- Failure to create the native connection (for example an invalid cipher list
  or `trust_anchors`) is now reported by `AuthenticateAsClientAsync`, not by
  the constructor.
- `ProtocolVersion`, `CipherName` and `AlpnSelected` return an empty string
  before the handshake has started.
- If the underlying stream ends, `ReadAsync` returns `0`, matching `SslStream`.
  During the handshake, an end of stream still throws `EndOfStreamException`.
- An invalid `trust_anchors` value now fails connection creation with a
  descriptive error instead of being silently mis-parsed.
- Native `read` / `write` calls with a length of `0` or less now return an
  error instead of being forwarded to OpenSSL.

## Upgrade notes

- Ship `proxymap_boringssl` and the managed assembly from the same build.
- If your code depended on the constructor throwing when the native connection
  could not be created, move that error handling to
  `AuthenticateAsClientAsync`.
- Verify that callers treat a `0` result from `ReadAsync` as a normal end of
  stream.
- Recommended before rollout: a concurrent-connection stress test, including
  disposal during an active handshake and during an active read.