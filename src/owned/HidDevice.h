#pragma once
#include "../GigaHid.h"
#include <atomic>
#include <cstring>
extern "C" {
#include "core/bth_sdp.h"
#include "core/bth_control_tx.h"
}
namespace giga_bredr { namespace detail {
// One Bluetooth HID Device (HIDP 1.1) endpoint: immutable SDP HID + Device ID
// records, a parsed report layout, the control/interrupt channel pair and a
// fixed input-report queue. No allocation or clock. Every method except
// connected() runs under the radio owner's serialization (the actor itself,
// or a foreground call holding the radio mutex).
class HidDevice {
public:
    static constexpr uint16_t ControlPsm = 0x11, InterruptPsm = 0x13, ReceiveMtu = 256, MinimumMtu = 48;
    static constexpr size_t DescriptorLimit = 512, TextLimit = 32, ReportBytes = 64, ReportSlots = 12, QueueSlots = 8;
    static constexpr size_t RecordBytes = DescriptorLimit + 288, DeviceIdBytes = 80, FrameBytes = ReportBytes + 2;
    static constexpr uint32_t RecordHandle = 0x10007, DeviceIdHandle = 0x10008;
    // Free per-peer Classic control slots required before a reply / a report
    // enters the shared FIFO: signaling, AVDTP and AVRCP replies never wait
    // behind an input report (same rule as SPP bulk data).
    static constexpr unsigned ReplyReserve = 2, ReportReserve = 3;
    enum : uint8_t { Successful = 0, NotReady = 1, InvalidReportId = 2, UnsupportedRequest = 3, InvalidParameter = 4 };
    enum : uint8_t { Boot = 0, Report = 1 };
    struct Layout {
        struct Entry { uint8_t type = 0, id = 0; uint16_t bits = 0; };
        Entry entries[ReportSlots];
        size_t count = 0;
        bool ids = false;
        const Entry* find(uint8_t type, uint8_t id) const {
            for (size_t i = 0; i < count; ++i) if (entries[i].type == type && entries[i].id == id) return &entries[i];
            return nullptr;
        }
    };
    HidDevice() = default;
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    // Short-item report descriptor walk: sizes of every (type, report ID).
    static bool parseDescriptor(const uint8_t* d, size_t n, Layout& out) {
        out = Layout();
        if (!d || !n || n > DescriptorLimit) return false;
        struct Globals { uint32_t size = 0, count = 0; uint8_t id = 0; } g, stack[4];
        unsigned depth = 0, collections = 0; bool unnumbered = false;
        for (size_t at = 0; at < n;) {
            const uint8_t b = d[at];
            if (b == 0xfe) { if (n - at < 3 || n - at - 3 < d[at + 1]) return false; at += 3u + d[at + 1]; continue; }
            const size_t len = (b & 3) == 3 ? 4 : (b & 3);
            if (n - at - 1 < len) return false;
            uint32_t v = 0; for (size_t i = 0; i < len; ++i) v |= uint32_t(d[at + 1 + i]) << (8 * i);
            const uint8_t type = (b >> 2) & 3, tag = b >> 4;
            if (type == 3) return false;
            if (type == 1) {
                if (tag == 7) g.size = v;
                else if (tag == 9) g.count = v;
                else if (tag == 8) { if (!v || v > 255) return false; g.id = uint8_t(v); out.ids = true; }
                else if (tag == 10) { if (depth == 4) return false; stack[depth++] = g; }
                else if (tag == 11) { if (!depth) return false; g = stack[--depth]; }
            } else if (type == 0) {
                if (tag == 10) { if (++collections > 16) return false; }
                else if (tag == 12) { if (!collections) return false; --collections; }
                else if (tag == 8 || tag == 9 || tag == 11) {
                    const uint8_t report = tag == 8 ? 1 : tag == 9 ? 2 : 3;
                    if (g.size > ReportBytes * 8 || g.count > ReportBytes * 8) return false;
                    const uint32_t bits = g.size * g.count;
                    if (!g.id) unnumbered = true;
                    Layout::Entry* e = nullptr;
                    for (size_t i = 0; i < out.count; ++i) if (out.entries[i].type == report && out.entries[i].id == g.id) e = &out.entries[i];
                    if (!e) { if (out.count == ReportSlots) return false; e = &out.entries[out.count++]; e->type = report; e->id = g.id; }
                    if (e->bits + bits > ReportBytes * 8) return false;
                    e->bits = uint16_t(e->bits + bits);
                } else return false;
            }
            at += 1 + len;
        }
        if (collections || (out.ids && unnumbered)) return false;
        size_t inputs = 0;
        for (size_t i = 0; i < out.count; ++i) { if (!out.entries[i].bits) return false; if (out.entries[i].type == 1) ++inputs; }
        return inputs != 0;
    }
    static size_t text(const char* s) { size_t n = 0; if (s) while (n <= TextLimit && s[n]) ++n; return n; }
    static uint8_t keyboardId(const Layout& l) { return l.ids ? HidBootKeyboardReportId : 0; }
    static uint8_t mouseId(const Layout& l) { return l.ids ? HidBootMouseReportId : 0; }
    static bool validate(const HidConfig& c, Layout* layout = nullptr) {
        Layout l;
        const size_t name = text(c.service_name), description = text(c.description), provider = text(c.provider);
        if (!c.report_descriptor || !c.report_descriptor_bytes || c.report_descriptor_bytes > DescriptorLimit ||
            !name || name > TextLimit || (c.description && (!description || description > TextLimit)) ||
            (c.provider && (!provider || provider > TextLimit)) || (c.subclass & 3) ||
            !parseDescriptor(c.report_descriptor, c.report_descriptor_bytes, l)) return false;
        if (c.boot_keyboard) {
            const auto* in = l.find(1, keyboardId(l)); const auto* out = l.find(2, keyboardId(l));
            if (!in || in->bits != 64 || (out && out->bits > 64)) return false;
        }
        if (c.boot_mouse) {
            const auto* in = l.find(1, mouseId(l));
            if (!in || in->bits < 24 || (!l.ids && c.boot_keyboard)) return false;
        }
        if (layout) *layout = l;
        return true;
    }
    // Exact SDP attribute bytes (HID 1.1 Table 5.3 mandatory set + options).
    static size_t buildRecord(uint8_t* out, size_t capacity, const HidConfig& c) {
        Writer w{out, capacity};
        const size_t n = c.report_descriptor_bytes;
        w.handle(RecordHandle);
        w.attr(0x0001); w.seq(3); w.uuid(0x1124);
        w.attr(0x0004); w.seq(13); w.seq(6); w.uuid(0x0100); w.u16(ControlPsm); w.seq(3); w.uuid(0x0011);
        w.attr(0x0005); w.seq(3); w.uuid(0x1002);
        w.attr(0x0006); w.seq(9); w.u16(0x656e); w.u16(0x006a); w.u16(0x0100);
        w.attr(0x0009); w.seq(8); w.seq(6); w.uuid(0x1124); w.u16(0x0101);
        w.attr(0x000d); w.seq(15); w.seq(13); w.seq(6); w.uuid(0x0100); w.u16(InterruptPsm); w.seq(3); w.uuid(0x0011);
        w.attr(0x0100); w.string(c.service_name, text(c.service_name));
        if (c.description) { w.attr(0x0101); w.string(c.description, text(c.description)); }
        if (c.provider) { w.attr(0x0102); w.string(c.provider, text(c.provider)); }
        w.attr(0x0201); w.u16(0x0111);
        w.attr(0x0202); w.u8(c.subclass);
        w.attr(0x0203); w.u8(c.country_code);
        w.attr(0x0204); w.boolean(c.virtual_cable);
        w.attr(0x0205); w.boolean(c.reconnect_initiate);
        const size_t entry = 2 + (n < 256 ? 2 : 3) + n;
        w.attr(0x0206); w.seq(entry + (entry < 256 ? 2 : 3)); w.seq(entry); w.u8(0x22); w.string(c.report_descriptor, n);
        w.attr(0x0207); w.seq(8); w.seq(6); w.u16(0x0409); w.u16(0x0100);
        w.attr(0x020d); w.boolean(c.normally_connectable);
        w.attr(0x020e); w.boolean(c.boot_keyboard || c.boot_mouse);
        return w.ok ? w.n : 0;
    }
    static size_t buildDeviceId(uint8_t* out, size_t capacity, const HidConfig& c) {
        Writer w{out, capacity};
        w.handle(DeviceIdHandle);
        w.attr(0x0001); w.seq(3); w.uuid(0x1200);
        w.attr(0x0005); w.seq(3); w.uuid(0x1002);
        w.attr(0x0009); w.seq(8); w.seq(6); w.uuid(0x1200); w.u16(0x0103);
        w.attr(0x0200); w.u16(0x0103);
        w.attr(0x0201); w.u16(c.vendor_id);
        w.attr(0x0202); w.u16(c.product_id);
        w.attr(0x0203); w.u16(c.version);
        w.attr(0x0204); w.boolean(true);
        w.attr(0x0205); w.u16(c.vendor_id_source);
        return w.ok ? w.n : 0;
    }

    bool begin(const HidConfig& c) {
        end();
        if (!validate(c, &layout_)) return false;
        const size_t n = buildRecord(record_bytes_, sizeof record_bytes_, c);
        if (!n) return false;
        records_[0] = {RecordHandle, {record_bytes_, n}}; record_count_ = 1;
        if (c.device_id) {
            const size_t d = buildDeviceId(device_id_bytes_, sizeof device_id_bytes_, c);
            if (!d) return false;
            records_[record_count_++] = {DeviceIdHandle, {device_id_bytes_, d}};
        }
        for (size_t i = 0; i < layout_.count; ++i) {
            auto& r = cache_[i]; r = Cached();
            r.type = layout_.entries[i].type; r.id = layout_.entries[i].id; r.bytes = uint8_t((layout_.entries[i].bits + 7) / 8);
        }
        boot_keyboard_ = c.boot_keyboard; boot_mouse_ = c.boot_mouse; cod_ = c.class_of_device;
        stats_ = {}; active_ = true; return true;
    }
    void end() {
        reset(0);
        active_ = false; replies_ = nullptr; record_count_ = 0; cod_ = 0; boot_keyboard_ = boot_mouse_ = false;
        memset(record_bytes_, 0, sizeof record_bytes_); memset(device_id_bytes_, 0, sizeof device_id_bytes_);
        memset(records_, 0, sizeof records_); layout_ = Layout();
        for (auto& r : cache_) r = Cached();
        output_ = Pending(); stats_ = {};
    }
    bool enabled() const { return active_; }
    const bth_sdp_record_t* records() const { return active_ ? records_ : nullptr; }
    size_t recordCount() const { return active_ ? record_count_ : 0; }
    uint32_t classOfDevice() const { return active_ ? cod_ : 0; }
    const Layout& layout() const { return layout_; }
    void bind(bth_control_tx_t& queue, uint8_t peer = 0) { replies_ = &queue; peer_ = peer; }

    // Actor side.
    void reset(uint32_t owner) {
        disconnect();
        control_ = interrupt_ = Channel(); reply_ = Frame(); close_ = unplug_ = false;
        owner_ = active_ ? owner : 0;
    }
    bool owns(uint16_t psm) const { return active_ && (psm == ControlPsm || psm == InterruptPsm); }
    uint16_t controlCid() const { return control_.local; }
    uint16_t interruptCid() const { return interrupt_.local; }
    bool linked() const { return control_.local || interrupt_.local; }
    bool closeRequested() const { return close_; }
    // Next channel to retire after a close request: interrupt before control.
    uint16_t closingCid() const { return close_ ? (interrupt_.local ? interrupt_.local : control_.local) : 0; }
    bool takeUnplug() { const bool u = unplug_; unplug_ = false; return u; }
    void requestClose() { close_ = linked(); disconnect(); }
    bool channelOpened(uint32_t owner, uint16_t psm, uint16_t local, uint16_t remote, uint16_t mtu) {
        if (!ownerValid() || owner != owner_ || local < 0x40 || remote < 0x40 || mtu < MinimumMtu || close_) return false;
        Channel* c = psm == ControlPsm ? &control_ : psm == InterruptPsm ? &interrupt_ : nullptr;
        if (!c || c->local || serial_ == UINT32_MAX) return false;
        *c = {local, remote, mtu};
        if (control_.local && interrupt_.local) {
            generation_ = ++serial_; ++stats_.connections; protocol_ = Report; suspended_ = false;
            connected_.store(true, std::memory_order_release);
        }
        return true;
    }
    bool channelMtuUpdated(uint32_t owner, uint16_t psm, uint16_t local, uint16_t remote, uint16_t mtu) {
        if (!ownerValid() || owner != owner_ || mtu < MinimumMtu) return false;
        Channel* c = psm == ControlPsm ? &control_ : psm == InterruptPsm ? &interrupt_ : nullptr;
        if (!c || c->local != local || c->remote != remote) return false;
        c->mtu = mtu; return true;
    }
    void channelClosed(uint16_t local) {
        if (!local) return;
        if (local == interrupt_.local) { interrupt_ = Channel(); disconnect(); }
        else if (local == control_.local) {
            control_ = Channel(); reply_ = Frame(); disconnect();
            if (interrupt_.local) close_ = true; // Interrupt alone is meaningless.
        } else return;
        if (!linked()) close_ = false;
    }
    // Always consumes: HID input never retains L2CAP storage that fences media.
    bool receive(uint16_t cid, const uint8_t* p, size_t n, bool secure, uint32_t) {
        if (!ownerValid() || !cid || (cid != control_.local && cid != interrupt_.local)) return true;
        if (!secure) { requestClose(); return true; }
        if (!p || !n) { ++stats_.malformed; return true; }
        if (cid == interrupt_.local) {
            if (p[0] == 0xa2) store(2, p + 1, n - 1, false); else ++stats_.ignored_requests;
            return true;
        }
        control(p, n); return true;
    }
    void poll(bool allow_transmit) {
        if (!ownerValid()) return;
        if (reply_.length && control_.local && bth_control_tx_available(replies_, peer_) >= ReplyReserve) {
            if (submit(control_, reply_)) reply_ = Frame();
        }
        if (!allow_transmit || !generation_ || !count_ || bth_control_tx_available(replies_, peer_) < ReportReserve) return;
        if (submit(interrupt_, queue_[read_])) ++stats_.reports_sent; else ++stats_.reports_dropped;
        queue_[read_] = Frame(); read_ = uint8_t((read_ + 1) % QueueSlots); --count_;
    }

    // Application side (radio mutex held).
    Status send(uint8_t report_id, const uint8_t* data, size_t length) {
        if (!active_) return Status::Unsupported;
        if ((!data && length) || !generation_) return Status::InvalidArgument;
        Cached* r = cached(1, report_id);
        if (!r || length != r->bytes) return Status::InvalidArgument;
        Frame f; f.bytes[0] = 0xa1;
        if (protocol_ == Boot) {
            uint8_t boot = 0; size_t bytes = 0;
            if (boot_keyboard_ && report_id == keyboardId(layout_)) { boot = HidBootKeyboardReportId; bytes = 8; }
            else if (boot_mouse_ && report_id == mouseId(layout_)) { boot = HidBootMouseReportId; bytes = 3; }
            if (!boot) { memcpy(r->data, data, length); r->valid = true; ++stats_.reports_filtered; return Status::Ok; }
            f.bytes[1] = boot; memcpy(f.bytes + 2, data, bytes); f.length = uint8_t(2 + bytes);
        } else {
            size_t at = 1; if (layout_.ids) f.bytes[at++] = report_id;
            memcpy(f.bytes + at, data, length); f.length = uint8_t(at + length);
        }
        if (f.length > interrupt_.mtu) return Status::InvalidArgument;
        if (count_ == QueueSlots) { ++stats_.reports_rejected; return Status::OutputFull; }
        queue_[(read_ + count_) % QueueSlots] = f; ++count_; ++stats_.reports_queued;
        memcpy(r->data, data, length); r->valid = true;
        return Status::Ok;
    }
    bool connected() const { return connected_.load(std::memory_order_acquire); }
    bool takeOutput(HidReportType& type, uint8_t& id, uint8_t* data, size_t& length) {
        if (!active_ || !output_.pending) return false;
        type = HidReportType(output_.type); id = output_.id; length = output_.length;
        memcpy(data, output_.data, output_.length); output_.pending = false; return true;
    }
    HidStats stats() const {
        HidStats s = stats_;
        s.enabled = active_; s.connected = generation_ != 0; s.control_open = control_.local != 0; s.interrupt_open = interrupt_.local != 0;
        s.suspended = suspended_; s.boot_protocol = protocol_ == Boot; s.generation = generation_; s.tx_pending = count_;
        return s;
    }
private:
    struct Writer {
        uint8_t* p; size_t capacity; size_t n = 0; bool ok = true;
        void byte(uint8_t v) { if (p && n < capacity) p[n] = v; else ok = false; ++n; }
        void attr(uint16_t id) { byte(0x09); byte(uint8_t(id >> 8)); byte(uint8_t(id)); }
        void handle(uint32_t h) { attr(0); byte(0x0a); byte(uint8_t(h >> 24)); byte(uint8_t(h >> 16)); byte(uint8_t(h >> 8)); byte(uint8_t(h)); }
        void u8(uint8_t v) { byte(0x08); byte(v); }
        void u16(uint16_t v) { byte(0x09); byte(uint8_t(v >> 8)); byte(uint8_t(v)); }
        void boolean(bool v) { byte(0x28); byte(v ? 1 : 0); }
        void uuid(uint16_t v) { byte(0x19); byte(uint8_t(v >> 8)); byte(uint8_t(v)); }
        void seq(size_t length) { if (length < 256) { byte(0x35); byte(uint8_t(length)); } else { byte(0x36); byte(uint8_t(length >> 8)); byte(uint8_t(length)); } }
        void string(const void* s, size_t length) {
            if (length < 256) { byte(0x25); byte(uint8_t(length)); } else { byte(0x26); byte(uint8_t(length >> 8)); byte(uint8_t(length)); }
            for (size_t i = 0; i < length; ++i) byte(static_cast<const uint8_t*>(s)[i]);
        }
    };
    struct Channel { uint16_t local = 0, remote = 0, mtu = 0; };
    struct Frame { uint8_t bytes[FrameBytes] = {}; uint8_t length = 0; };
    struct Cached { uint8_t type = 0, id = 0, bytes = 0; bool valid = false; uint8_t data[ReportBytes] = {}; };
    struct Pending { uint8_t type = 0, id = 0, length = 0; bool pending = false; uint8_t data[ReportBytes] = {}; };
    bool ownerValid() const {
        return active_ && owner_ && replies_ && replies_->initialized && replies_->peers[peer_].attached && replies_->peers[peer_].owner == owner_;
    }
    void disconnect() {
        if (generation_) { stats_.reports_dropped += count_; generation_ = 0; }
        connected_.store(false, std::memory_order_release);
        for (auto& f : queue_) f = Frame();
        read_ = count_ = 0;
    }
    Cached* cached(uint8_t type, uint8_t id) {
        for (size_t i = 0; i < layout_.count; ++i) if (cache_[i].type == type && cache_[i].id == id) return &cache_[i];
        return nullptr;
    }
    // Boot protocol reports map onto the descriptor's boot-layout reports.
    Cached* boot(uint8_t type, uint8_t id, size_t& bytes) {
        bytes = 0;
        if (id == HidBootKeyboardReportId && boot_keyboard_ && (type == 1 || type == 2)) {
            Cached* r = cached(type, keyboardId(layout_)); if (r) bytes = type == 1 ? 8 : r->bytes; return r;
        }
        if (id == HidBootMouseReportId && boot_mouse_ && type == 1) { Cached* r = cached(1, mouseId(layout_)); if (r) bytes = 3; return r; }
        return nullptr;
    }
    bool submit(const Channel& c, const Frame& f) {
        if (!c.local || !f.length) return false;
        bth_control_reservation_t slot{};
        if (bth_control_tx_reserve(replies_, peer_, &slot) != BTH_OK) return false;
        const size_t n = f.length < c.mtu ? f.length : c.mtu; // Truncate to the host's MTU.
        memcpy(slot.data, f.bytes, n);
        if (bth_control_tx_commit(replies_, slot.token, c.remote, n, c.mtu) != BTH_OK) { bth_control_tx_cancel(replies_, slot.token); return false; }
        return true;
    }
    bool replyFree() { if (reply_.length) { ++stats_.ignored_requests; return false; } return true; }
    void handshake(uint8_t result) {
        if (!replyFree()) return;
        reply_.bytes[0] = result; reply_.length = 1;
        if (result) ++stats_.error_handshakes;
        if (result == UnsupportedRequest) ++stats_.unsupported_requests;
    }
    void data(uint8_t header, const uint8_t* p, size_t n) {
        if (!replyFree()) return;
        reply_.bytes[0] = header; if (n) memcpy(reply_.bytes + 1, p, n); reply_.length = uint8_t(1 + n);
    }
    void control(const uint8_t* p, size_t n) {
        const uint8_t type = p[0] >> 4, param = p[0] & 15;
        switch (type) {
        case 0: return; // HANDSHAKE from the host: no device response.
        case 1: // HID_CONTROL: no response.
            if (n != 1) { ++stats_.malformed; return; }
            if (param == 1 || param == 2) { protocol_ = Report; suspended_ = false; } // Hard/soft reset.
            else if (param == 3) { suspended_ = true; ++stats_.suspends; }
            else if (param == 4) suspended_ = false;
            else if (param == 5) { ++stats_.unplugs; unplug_ = true; requestClose(); }
            else if (param) ++stats_.ignored_requests;
            return;
        case 4: getReport(param, p + 1, n - 1); return;
        case 5: ++stats_.set_reports;
            if (param & 0x0c || (param & 3) < 2) { handshake(InvalidParameter); return; }
            handshake(store(param & 3, p + 1, n - 1, true)); return;
        case 6: if (n != 1 || param) { handshake(InvalidParameter); return; }
            { const uint8_t mode = protocol_; data(0xa0, &mode, 1); } return;
        case 7:
            if (n != 1 || param > 1 || (!param && !boot_keyboard_ && !boot_mouse_)) { handshake(InvalidParameter); return; }
            if (param != protocol_) { stats_.reports_dropped += count_; for (auto& f : queue_) f = Frame(); read_ = count_ = 0; ++stats_.protocol_changes; }
            protocol_ = param; handshake(Successful); return;
        case 8: if (n != 1) { handshake(InvalidParameter); return; } data(0xa0, &idle_, 1); return; // Deprecated GET_IDLE.
        case 9: if (n != 2) { handshake(InvalidParameter); return; } idle_ = p[1]; handshake(Successful); return; // Deprecated SET_IDLE.
        case 10: if (param == 2) store(2, p + 1, n - 1, false); else ++stats_.ignored_requests; return; // Deprecated control DATA.
        default: handshake(UnsupportedRequest); return;
        }
    }
    void getReport(uint8_t param, const uint8_t* p, size_t n) {
        ++stats_.get_reports;
        const uint8_t type = param & 3; const bool sized = param & 8;
        if (!type || (param & 4)) { handshake(InvalidParameter); return; }
        const bool with_id = protocol_ == Boot || layout_.ids;
        size_t at = 0; uint8_t id = 0; size_t limit = FrameBytes;
        if (with_id) { if (at == n) { handshake(InvalidParameter); return; } id = p[at++]; }
        if (sized) { if (n - at < 2) { handshake(InvalidParameter); return; } limit = size_t(p[at]) | (size_t(p[at + 1]) << 8); at += 2; }
        if (at != n) { handshake(InvalidParameter); return; }
        size_t bytes = 0; Cached* r = protocol_ == Boot ? boot(type, id, bytes) : cached(type, id);
        if (!r) { handshake(InvalidReportId); return; }
        if (protocol_ != Boot) bytes = r->bytes;
        uint8_t payload[FrameBytes]; size_t length = 0;
        if (with_id) payload[length++] = id;
        memcpy(payload + length, r->data, bytes); length += bytes; // Zeros until a value exists.
        data(uint8_t(0xa0 | type), payload, length < limit ? length : limit);
    }
    // SET_REPORT / output DATA. Returns the HANDSHAKE result for SET_REPORT.
    uint8_t store(uint8_t type, const uint8_t* p, size_t n, bool request) {
        const bool with_id = protocol_ == Boot || layout_.ids;
        if (with_id && !n) { ++stats_.malformed; return InvalidParameter; }
        const uint8_t id = with_id ? p[0] : 0; p += with_id ? 1 : 0; n -= with_id ? 1 : 0;
        size_t bytes = 0; Cached* r = protocol_ == Boot ? boot(type, id, bytes) : cached(type, id);
        if (!r) { if (!request) ++stats_.malformed; return InvalidReportId; }
        if (protocol_ != Boot) bytes = r->bytes;
        if (!n || n > bytes) { if (!request) ++stats_.malformed; return InvalidParameter; }
        memset(r->data, 0, r->bytes); memcpy(r->data, p, n); r->valid = true;
        if (output_.pending) ++stats_.output_overwritten;
        output_.type = type; output_.id = r->id; output_.length = uint8_t(n); memcpy(output_.data, p, n); output_.pending = true;
        ++stats_.output_reports; return Successful;
    }
    uint8_t record_bytes_[RecordBytes] = {};
    uint8_t device_id_bytes_[DeviceIdBytes] = {};
    bth_sdp_record_t records_[2] = {};
    size_t record_count_ = 0;
    Layout layout_;
    Cached cache_[ReportSlots];
    Frame queue_[QueueSlots];
    Frame reply_;
    Pending output_;
    HidStats stats_;
    bth_control_tx_t* replies_ = nullptr;
    Channel control_, interrupt_;
    uint32_t owner_ = 0, generation_ = 0, serial_ = 0, cod_ = 0;
    uint8_t peer_ = 0, read_ = 0, count_ = 0, protocol_ = Report, idle_ = 0;
    bool active_ = false, close_ = false, unplug_ = false, suspended_ = false, boot_keyboard_ = false, boot_mouse_ = false;
    std::atomic<bool> connected_{false};
};
}}
