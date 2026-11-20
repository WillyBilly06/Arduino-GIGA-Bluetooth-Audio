#pragma once
// Persistent Classic link-key storage on the GIGA R1's 16 MiB QSPI flash.
//
// Bonds survive resets and sketch uploads, so a phone that paired once
// reconnects with its stored key instead of pairing again. The store is a Mbed
// TDBStore in the last 1 MiB of the QSPI device (0xF00000..0xFFFFFF). Arduino's
// QSPIFormat layout (WiFi 0-1 MiB, OTA 1-6, KV 6-7, user 7-14) never uses that
// range, so no formatting is needed and existing partitions are not touched.
//
// Usage:
//   #include <GigaQspiBondStorage.h>
//   giga_bredr::GigaQspiBondStorage bonds;
//   config.classic_bonds = bonds.storage();   // before receiver.begin(config)
//
// Writes happen only when a peer pairs or removes its bond (never per packet).
// A QSPI program/erase can take tens of milliseconds; that cost is paid on the
// host actor during pairing, not on the audio path. Reads (every reconnect) are
// sub-millisecond. Call clear() to forget every stored peer.
#include "BondStorage.h"
#if defined(ARDUINO_GIGA) && __has_include("QSPIFBlockDevice.h") && __has_include("TDBStore.h")
#include "QSPIFBlockDevice.h"
#include "SlicingBlockDevice.h"
#include "TDBStore.h"
#include <stdio.h>
#include <string.h>

namespace giga_bredr {
class GigaQspiBondStorage {
public:
    static constexpr uint32_t RegionStart = 0xF00000u, RegionEnd = 0x1000000u;

    // Lazy: the QSPI device is opened on first use, so a sketch that never
    // touches bonds pays nothing at boot.
    ClassicBondStorage storage() {
        ClassicBondStorage s;
        s.context = this;
        s.load = &GigaQspiBondStorage::load;
        s.save = &GigaQspiBondStorage::save;
        s.remove = &GigaQspiBondStorage::remove;
        return s;
    }
    bool begin() {
        if (ready_) return true;
        if (failed_) return false;
        if (store_.init() != 0) { failed_ = true; return false; }
        ready_ = true;
        return true;
    }
    bool clear() { return begin() && store_.reset() == 0; }

private:
    // Explicit little-endian layout; independent of C++ struct padding.
    enum : uint32_t { Magic = 0x31424247u /* "GBB1" */ };
    enum : size_t { RecordBytes = 4 + 4 + 6 + 6 + 16 + 1 + 1 };

    static void keyName(const ClassicBondIdentity& id, char out[32]) {
        // "b" + local(12 hex) + peer(12 hex): 25 chars, valid TDBStore key.
        static const char hex[] = "0123456789abcdef";
        out[0] = 'b';
        for (unsigned i = 0; i < 6; ++i) {
            out[1 + 2 * i] = hex[id.local_address[i] >> 4];
            out[2 + 2 * i] = hex[id.local_address[i] & 15];
            out[13 + 2 * i] = hex[id.peer_address[i] >> 4];
            out[14 + 2 * i] = hex[id.peer_address[i] & 15];
        }
        out[25] = 0;
    }
    static void put32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }
    static uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
    static bool validType(uint8_t t, uint8_t pin) {
        return (t == 0 && pin >= 1 && pin <= 16) || ((t == 4 || t == 5 || t == 7 || t == 8) && pin == 0);
    }

    static BondStorageResult load(void* c, const ClassicBondIdentity& id, ClassicBondRecord& r) {
        auto& self = *static_cast<GigaQspiBondStorage*>(c);
        if (!self.begin()) return BondStorageResult::Error;
        char key[32]; keyName(id, key);
        uint8_t b[RecordBytes]; size_t got = 0;
        const int e = self.store_.get(key, b, sizeof b, &got);
        if (e == MBED_ERROR_ITEM_NOT_FOUND) return BondStorageResult::NotFound;
        if (e != 0 || got != RecordBytes || get32(b) != Magic || get32(b + 4) != 1 ||
            memcmp(b + 8, id.local_address, 6) || memcmp(b + 14, id.peer_address, 6) ||
            !validType(b[36], b[37])) {
            memset(b, 0, sizeof b);
            return BondStorageResult::NotFound;  // Corrupt/foreign record: pair again.
        }
        r = ClassicBondRecord();
        r.format = 1; r.identity = id;
        memcpy(r.link_key, b + 20, 16);
        r.key_type = ClassicLinkKeyType(b[36]); r.pin_length = b[37];
        volatile uint8_t* wipe = b; for (size_t i = 0; i < sizeof b; ++i) wipe[i] = 0;
        return BondStorageResult::Ok;
    }
    static BondStorageResult save(void* c, const ClassicBondRecord& r) {
        auto& self = *static_cast<GigaQspiBondStorage*>(c);
        if (r.format != 1 || !validType(uint8_t(r.key_type), r.pin_length)) return BondStorageResult::Error;
        if (!self.begin()) return BondStorageResult::Error;
        uint8_t b[RecordBytes];
        put32(b, Magic); put32(b + 4, 1);
        memcpy(b + 8, r.identity.local_address, 6); memcpy(b + 14, r.identity.peer_address, 6);
        memcpy(b + 20, r.link_key, 16); b[36] = uint8_t(r.key_type); b[37] = r.pin_length;
        char key[32]; keyName(r.identity, key);
        const int e = self.store_.set(key, b, sizeof b, 0);
        volatile uint8_t* wipe = b; for (size_t i = 0; i < sizeof b; ++i) wipe[i] = 0;
        return e == 0 ? BondStorageResult::Ok : BondStorageResult::Error;
    }
    static BondStorageResult remove(void* c, const ClassicBondIdentity& id) {
        auto& self = *static_cast<GigaQspiBondStorage*>(c);
        if (!self.begin()) return BondStorageResult::Error;
        char key[32]; keyName(id, key);
        const int e = self.store_.remove(key);
        return (e == 0 || e == MBED_ERROR_ITEM_NOT_FOUND) ? BondStorageResult::Ok : BondStorageResult::Error;
    }

    QSPIFBlockDevice root_;
    mbed::SlicingBlockDevice slice_{&root_, RegionStart, RegionEnd};
    mbed::TDBStore store_{&slice_};
    bool ready_ = false, failed_ = false;
};
}  // namespace giga_bredr
#else
namespace giga_bredr {
// Off-target (host tests, other boards): no persistent store. storage()
// returns an empty ClassicBondStorage, i.e. the receiver's RAM-only default.
class GigaQspiBondStorage {
public:
    ClassicBondStorage storage() { return ClassicBondStorage(); }
    bool begin() { return false; }
    bool clear() { return false; }
};
}  // namespace giga_bredr
#endif
