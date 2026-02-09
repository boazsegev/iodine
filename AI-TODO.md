# Iodine 0.8.x Development TODO

> **Project**: iodine v0.8.0.dev  
> **Last Updated**: 2025-12-31

---

## 📋 Active Tasks

### T017: Performance Optimization Review 🔄
**Status**: In Progress | **Assignee**: @c-developer | **Effort**: M

Comprehensive review of all iodine-specific C code for performance optimization opportunities:
- Analyzing 20 C files in `ext/iodine/` (excluding fio-stl.h)
- Focus areas:
  - Hot paths (frequently executed code)
  - Memory allocation patterns
  - Cache efficiency
  - Lock contention / synchronization overhead
  - Ruby/C boundary crossing overhead
  - Algorithmic improvements
  - Compiler optimization opportunities

**Deliverable**: Prioritized list of optimization recommendations with impact/effort analysis.

---

## ✅ Completed

### T018: Benchmark Module Crypto Benchmarks ✓
Added 7 new benchmark methods to `lib/iodine/benchmark.rb`:
- **`crypto_symmetric`** - AEAD ciphers (ChaCha20-Poly1305, XChaCha20-Poly1305, AES-128-GCM, AES-256-GCM) vs OpenSSL
- **`crypto_asymmetric`** - Ed25519 signing/verification, X25519 key exchange vs OpenSSL/RbNaCl
- **`hashing`** - SHA-256, SHA-512, SHA3-256, SHA3-512, BLAKE2b, BLAKE2s vs OpenSSL/Digest
- **`crypto_ecies`** - X25519 ECIES encryption vs RbNaCl SimpleBox
- **`crypto_kdf`** - HKDF key derivation vs OpenSSL
- **`crypto_totp`** - TOTP generation/verification vs ROTP gem
- **`crypto_all`** - Convenience method to run all crypto benchmarks

Tests multiple payload sizes (64B, 1KB, 64KB) where meaningful. All optional dependencies handled gracefully.

### T016: Documentation Accuracy Review ✓
Reviewed `lib/iodine/documentation.rb` against C implementation in `ext/iodine/`:
- **Fixed**: `Iodine.threads` return type changed from `Integer, nil` to `Integer` (always returns value, default -4)
- **Fixed**: `Iodine.workers` return type changed from `Integer, nil` to `Integer` (always returns value, default -2)
- **Fixed**: `Iodine.threads=` added notes about error logging when called from worker or with nil
- **Fixed**: `Iodine.workers=` added notes about error logging when called from worker or with nil
- **Verified accurate**: `start`, `stop`, `master?`, `worker?`, `running?`, `verbosity`, `verbosity=`, `secret`, `secret=`, `shutdown_timeout`, `shutdown_timeout=`

### T012-T015: AES-GCM and ECIES Variants ✓
Added AES-GCM support and AES-based ECIES variants to `Iodine::Base::Crypto`:
- **AES128GCM**: `encrypt/decrypt` with 16-byte key, 12-byte nonce (hardware accelerated)
- **AES256GCM**: `encrypt/decrypt` with 32-byte key, 12-byte nonce (hardware accelerated)
- **X25519 ECIES variants**: `encrypt_aes128/decrypt_aes128`, `encrypt_aes256/decrypt_aes256`
- Updated `lib/iodine/documentation.rb` with full YARD documentation
- Updated `AI-MEMORY.json` with new crypto primitives

### T011: Ruby Documentation Update ✓
Comprehensive update to `lib/iodine/documentation.rb`:
- **New modules**: `Iodine::Base::Crypto` with all submodules (ChaCha20Poly1305, XChaCha20Poly1305, AES128GCM, AES256GCM, Ed25519, X25519, HKDF)
- **New classes**: `Iodine::PubSub::Engine::Redis` with `cmd` method
- **New Utils methods**: `sha256`, `sha512`, `sha3_256`, `sha3_512`, `blake2b`, `blake2s`, `totp_secret`, `totp_verify`
- **New TLS methods**: `default`, `default=`
- **New Connection methods**: `http?`, `peer_addr`, `from`
- Enhanced all existing documentation with proper YARD tags (`@param`, `@return`, `@example`, `@note`)

### T010: C Code Documentation Review ✓
Reviewed all 20 C extension files in `ext/iodine/`. Enhanced documentation for:
- `iodine_core.h` - IO reactor management (Ruby API docs)
- `iodine_listener.h` - Listener management (struct docs, Ruby API)
- `iodine_pubsub_eng.h` - PubSub engine abstraction (callbacks, Ruby API)
- `iodine_pubsub_msg.h` - PubSub message wrapper (enum, struct docs)
- `iodine_json.h` - JSON parsing (file header, API overview)
- `iodine_minimap.h` - Hash map (usage examples)
- `iodine_threads.h` - Ruby threading (GVL handling docs)
- `iodine_tls.h` - TLS/SSL (backend options, Ruby API)
- `iodine_connection.h` - Connection handling (comprehensive Ruby API docs)

### T008: XChaCha20-Poly1305 Support ✓
Added `Iodine::Base::Crypto::XChaCha20Poly1305` module with 24-byte nonce (safe for random generation).

### T003.3: Iodine::Base::Crypto Module ✓
- `ChaCha20Poly1305.encrypt/decrypt` - AEAD (12-byte nonce)
- `XChaCha20Poly1305.encrypt/decrypt` - AEAD (24-byte nonce, safe for random)
- `AES128GCM.encrypt/decrypt` - AEAD (16-byte key, 12-byte nonce)
- `AES256GCM.encrypt/decrypt` - AEAD (32-byte key, 12-byte nonce)
- `Ed25519.keypair/sign/verify` - Digital signatures
- `X25519.keypair/shared_secret/encrypt/decrypt` - Key exchange + ECIES (ChaCha20)
- `X25519.encrypt_aes128/decrypt_aes128` - ECIES with AES-128-GCM
- `X25519.encrypt_aes256/decrypt_aes256` - ECIES with AES-256-GCM
- `HKDF.derive` - Key derivation

### T003.1: Iodine::Utils Hashing Functions ✓
- `sha256`, `sha512`, `sha3_256`, `sha3_512`, `blake2b`, `blake2s`, `hmac256`

### T003.2: TOTP Utilities ✓
- `totp(secret:, offset:, interval:)` - Generate TOTP code
- `totp_secret(len: 20)` - Generate Base32 secret
- `totp_verify(secret:, code:, window:, interval:)` - Verify with time window

### T009: TOTP API Documentation Fix ✓
- Fixed incomplete documentation comments for `totp`, `totp_secret`, `totp_verify`
- Fixed bug: `totp` now correctly passes `interval` parameter to `fio_otp()`

### T001: Redis Pub/Sub Engine ✓
- Created `Iodine::PubSub::Engine::Redis` class
- 29 tests passing in `spec/redis_spec.rb`

### T002.1: Compile-Time TLS Selection ✓
- `IODINE_USE_EMBEDDED_TLS=1` environment variable

### T002.3: Runtime TLS Default Selection ✓
- `Iodine::TLS.default` / `Iodine::TLS.default=`

---

## Notes

1. **Security**: fio-stl.h crypto has not been independently audited
2. **API Stability**: `Iodine::Base::Crypto` is a "breakable" API
3. **Tests**: All 29 Redis tests pass
