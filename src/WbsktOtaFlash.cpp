#include "WbsktOtaFlash.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/md.h>

#include "wbskt/Ota.h"

namespace wbskt_ota {

namespace {

constexpr uint16_t kReadTimeoutMs = 15000;

// Takes the body from HTTPClient::writeToStream, which undoes chunked encoding, and passes each
// piece to Update and the hash.
class FlashSink : public Stream {
public:
    FlashSink(size_t totalBytes, const Progress& progress) : total_(totalBytes), progress_(progress) {
        mbedtls_md_init(&md_);
        ok_ = mbedtls_md_setup(&md_, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0) == 0 &&
              mbedtls_md_starts(&md_) == 0;
    }
    ~FlashSink() override { mbedtls_md_free(&md_); }

    size_t write(uint8_t byte) override { return write(&byte, 1); }

    size_t write(const uint8_t* data, size_t length) override {
        if (!ok_ || Update.write(const_cast<uint8_t*>(data), length) != length) {
            ok_ = false;
            return 0;
        }
        ok_ = mbedtls_md_update(&md_, data, length) == 0;
        written_ += length;
        if (total_ > 0 && progress_) {
            const uint8_t percent = static_cast<uint8_t>(written_ >= total_ ? 100 : written_ * 100 / total_);
            if (percent != lastPercent_) {
                lastPercent_ = percent;
                progress_(percent);
            }
        }
        return ok_ ? length : 0;
    }

    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    // Pure in some cores' Stream, virtual with a body in others.
    void flush() {}

    bool ok() const { return ok_; }
    size_t written() const { return written_; }

    std::string digest() {
        uint8_t out[32];
        if (mbedtls_md_finish(&md_, out) != 0) {
            return "";
        }
        return wbskt_core::toHex(out, sizeof(out));
    }

private:
    mbedtls_md_context_t md_;
    bool ok_ = false;
    size_t total_;
    size_t written_ = 0;
    uint8_t lastPercent_ = 255;
    const Progress& progress_;
};

std::string updateError() {
    return std::string("flash write failed: ") + Update.errorString();
}

}  // namespace

Partitions partitions() {
    Partitions out;
    if (const esp_partition_t* running = esp_ota_get_running_partition()) {
        out.running = running->label;
    }
    if (const esp_partition_t* boot = esp_ota_get_boot_partition()) {
        out.boot = boot->label;
    }
    if (const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr)) {
        out.next = next->label;
        out.nextBytes = next->size;
    }
    return out;
}

std::string install(const char* url, const char* rootCa, const std::string& sha256, size_t expectedBytes,
                    const Progress& progress) {
    const Partitions slots = partitions();
    if (slots.nextBytes == 0) {
        return "this build has no OTA partition";
    }

    WiFiClientSecure tls;
    tls.setCACert(rootCa);
    HTTPClient http;
    // GitHub release assets and most object stores answer with a redirect.
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(kReadTimeoutMs);
    if (!http.begin(tls, url)) {
        return "could not open the URL";
    }
    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        http.end();
        return status < 0 ? std::string("download failed: ") + HTTPClient::errorToString(status).c_str()
                          : "download answered HTTP " + std::to_string(status);
    }

    const int length = http.getSize();
    if (length > 0 && static_cast<size_t>(length) > slots.nextBytes) {
        http.end();
        return "the image is " + std::to_string(length) + " bytes and the OTA partition holds " +
               std::to_string(slots.nextBytes);
    }
    if (length > 0 && expectedBytes > 0 && static_cast<size_t>(length) != expectedBytes) {
        http.end();
        return "the server sent " + std::to_string(length) + " bytes, the command said " +
               std::to_string(expectedBytes);
    }
    if (!Update.begin(length > 0 ? static_cast<size_t>(length) : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        http.end();
        return updateError();
    }

    FlashSink sink(length > 0 ? static_cast<size_t>(length) : expectedBytes, progress);
    const int copied = http.writeToStream(&sink);
    http.end();
    if (!sink.ok()) {
        Update.abort();
        return updateError();
    }
    if (copied < 0) {
        Update.abort();
        return std::string("download failed: ") + HTTPClient::errorToString(copied).c_str();
    }
    if (length > 0 && sink.written() != static_cast<size_t>(length)) {
        Update.abort();
        return "download stopped at " + std::to_string(sink.written()) + " of " + std::to_string(length) + " bytes";
    }
    if (!wbskt_core::sameDigest(sink.digest(), sha256)) {
        Update.abort();
        return "SHA-256 does not match";
    }
    // Checks the image header and makes the partition the boot one.
    if (!Update.end(true)) {
        return updateError();
    }
    return "";
}

bool bootFrom(const std::string& label) {
    const esp_partition_t* partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, label.c_str());
    return partition != nullptr && esp_ota_set_boot_partition(partition) == ESP_OK;
}

void restart() {
    ESP.restart();
    while (true) {
    }
}

}  // namespace wbskt_ota
