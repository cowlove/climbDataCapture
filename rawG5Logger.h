#pragma once

#include <Arduino.h>
#include <errno.h>
#include <stdint.h>
#include <string>

#ifdef CSIM
#include <chrono>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#else
#include <FS.h>
#include <LittleFS.h>
#include <esp_timer.h>
#endif

struct RawG5LogStatus {
  bool filesystemReady = false;
  bool recording = false;
  bool closing = false;
  uint32_t packetsWritten = 0;
  uint32_t packetsDropped = 0;
  uint32_t packetsQueued = 0;
  uint32_t writeErrors = 0;
  uint64_t bytesWritten = 0;
  char filename[32] = {0};
};

class RawG5Logger {
public:
  static const size_t maxPayloadBytes = 512;

  bool begin() {
#ifdef CSIM
    if (mkdir("spiffs", 0777) != 0 && errno != EEXIST) {
      Serial.println("Could not create simulator spiffs directory");
      return false;
    }
    filesystemReady = true;
    return true;
#else
    // Match esp32jimlib's current convention: the partition is historically
    // named "spiffs", but is mounted with LittleFS. Format only if mounting an
    // existing filesystem fails.
    filesystemReady = LittleFS.begin(true);
    if (!filesystemReady) {
      Serial.println("LittleFS mount/initialization failed");
      return false;
    }
    queue = xQueueCreate(queueEntries, sizeof(QueueEntry));
    if (queue == NULL) {
      Serial.println("Could not allocate raw G5 log queue");
      filesystemReady = false;
      return false;
    }
    BaseType_t taskResult = xTaskCreate(writerTaskEntry, "rawG5Writer", 6144,
                                        this, tskIDLE_PRIORITY, &writerTask);
    if (taskResult != pdPASS) {
      Serial.println("Could not start raw G5 writer task");
      vQueueDelete(queue);
      queue = NULL;
      filesystemReady = false;
      return false;
    }
    Serial.printf("LittleFS ready: %u used / %u bytes\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());
    return true;
#endif
  }

  bool start() {
    if (!filesystemReady || sessionOpen || closing) return false;
    if (!chooseFilename()) return false;

    packetsWritten = 0;
    packetsDropped = 0;
    writeErrors = 0;
    bytesWritten = 0;

#ifdef CSIM
    stream.open(hostPath(currentFilename), std::ios::out | std::ios::trunc);
    if (!stream.good()) {
      writeErrors++;
      return false;
    }
    writeHeader(stream);
    sessionOpen = true;
    recording = true;
    return true;
#else
    const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeBytes < minimumFreeBytes) {
      Serial.printf("Not enough LittleFS space for a new log: %u bytes free\n",
                    (unsigned)freeBytes);
      return false;
    }

    // Create the file before acquisition starts so a bad filename or full
    // filesystem cannot produce an apparently valid in-memory run.
    fs::File initial = LittleFS.open(currentFilename, "w");
    if (!initial) {
      writeErrors++;
      return false;
    }
    const char *header =
        "# climbDataCapture raw G5 payload log\n"
        "# timestamp_us\\tpayload_bytes\\tescaped_payload\n";
    size_t headerLength = strlen(header);
    if (initial.write((const uint8_t *)header, headerLength) != headerLength) {
      writeErrors++;
      initial.close();
      return false;
    }
    initial.close();

    QueueEntry command = {};
    command.type = QueueEntry::Open;
    strncpy(command.filename, currentFilename, sizeof(command.filename) - 1);
    sessionOpen = true;
    recording = true;
    if (xQueueSend(queue, &command, portMAX_DELAY) != pdTRUE) {
      writeErrors++;
      recording = false;
      sessionOpen = false;
      return false;
    }
    return true;
#endif
  }

  void log(uint64_t timestampUs, const std::string &payload) {
    if (!recording) return;
    if (payload.size() > maxPayloadBytes) {
      packetsDropped++;
      return;
    }
#ifdef CSIM
    writeRecord(stream, timestampUs, payload.data(), payload.size());
    if (!stream.good()) writeErrors++;
    else {
      packetsWritten++;
      bytesWritten = (uint64_t)stream.tellp();
    }
#else
    QueueEntry entry = {};
    entry.type = QueueEntry::Packet;
    entry.timestampUs = timestampUs;
    entry.length = payload.size();
    if (!payload.empty()) memcpy(entry.payload, payload.data(), payload.size());
    if (xQueueSend(queue, &entry, 0) != pdTRUE) packetsDropped++;
#endif
  }

  void stop() {
    if (!sessionOpen) return;
    recording = false;
    sessionOpen = false;
    closing = true;
#ifdef CSIM
    stream.flush();
    stream.close();
    closing = false;
#else
    QueueEntry command = {};
    command.type = QueueEntry::Close;
    if (xQueueSend(queue, &command, portMAX_DELAY) != pdTRUE) {
      writeErrors++;
      closing = false;
    }
#endif
  }

  RawG5LogStatus status() const {
    RawG5LogStatus result;
    result.filesystemReady = filesystemReady;
    result.recording = recording;
    result.closing = closing;
    result.packetsWritten = packetsWritten;
    result.packetsDropped = packetsDropped;
#ifndef CSIM
    result.packetsQueued = queue == NULL ? 0 : uxQueueMessagesWaiting(queue);
#endif
    result.writeErrors = writeErrors;
    result.bytesWritten = bytesWritten;
    strncpy(result.filename, currentFilename, sizeof(result.filename) - 1);
    return result;
  }

  uint64_t timestampUs() const {
#ifdef CSIM
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
#else
    return (uint64_t)esp_timer_get_time();
#endif
  }

  bool handleCommand(const std::string &line) {
    if (line == "LOG LIST") {
      if (sessionOpen || closing) Serial.println("LOG_ERROR\tcapture active");
      else listFiles();
      return true;
    }
    const std::string dumpPrefix = "LOG DUMP ";
    if (line.compare(0, dumpPrefix.size(), dumpPrefix) == 0) {
      std::string filename = normalizeFilename(line.substr(dumpPrefix.size()));
      if (!validFilename(filename)) Serial.println("LOG_ERROR\tinvalid filename");
      else dumpFile(filename);
      return true;
    }
    const std::string deletePrefix = "LOG DELETE ";
    if (line.compare(0, deletePrefix.size(), deletePrefix) == 0) {
      std::string filename = normalizeFilename(line.substr(deletePrefix.size()));
      if (!validFilename(filename)) Serial.println("LOG_ERROR\tinvalid filename");
      else deleteFile(filename);
      return true;
    }
    if (line == "LOG HELP") {
      Serial.println("LOG LIST | LOG DUMP /G5_001.TSV | LOG DELETE /G5_001.TSV");
      return true;
    }
    return false;
  }

private:
  static const uint32_t minimumFreeBytes = 64 * 1024;
  volatile bool filesystemReady = false;
  volatile bool recording = false;
  volatile bool sessionOpen = false;
  volatile bool closing = false;
  volatile uint32_t packetsWritten = 0;
  volatile uint32_t packetsDropped = 0;
  volatile uint32_t writeErrors = 0;
  volatile uint64_t bytesWritten = 0;
  char currentFilename[32] = {0};

  static std::string normalizeFilename(const std::string &name) {
    if (name.empty()) return name;
    if (name[0] == '/') return name;
    return "/" + name;
  }

  static bool validFilename(const std::string &name) {
    return name.size() == 11 && name.compare(0, 4, "/G5_") == 0 &&
           name[4] >= '0' && name[4] <= '9' &&
           name[5] >= '0' && name[5] <= '9' &&
           name[6] >= '0' && name[6] <= '9' &&
           name.compare(7, 4, ".TSV") == 0;
  }

  static std::string escaped(const char *payload, size_t length) {
    std::string output;
    output.reserve(length + 32);
    for (size_t i = 0; i < length; i++) {
      switch (payload[i]) {
        case '\\': output += "\\\\"; break;
        case '\t': output += "\\t"; break;
        case '\r': output += "\\r"; break;
        case '\n': output += "\\n"; break;
        default: output += payload[i]; break;
      }
    }
    return output;
  }

  static std::string recordLine(uint64_t timestampUs, const char *payload,
                                size_t length) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%llu\t%u\t",
             (unsigned long long)timestampUs, (unsigned)length);
    return std::string(prefix) + escaped(payload, length) + "\n";
  }

  bool chooseFilename() {
    for (int number = 1; number <= 999; number++) {
      snprintf(currentFilename, sizeof(currentFilename), "/G5_%03d.TSV", number);
#ifdef CSIM
      std::ifstream test(hostPath(currentFilename));
      if (!test.good()) return true;
#else
      if (!LittleFS.exists(currentFilename)) return true;
#endif
    }
    Serial.println("No unused G5 log filenames remain");
    currentFilename[0] = 0;
    return false;
  }

#ifdef CSIM
  std::ofstream stream;

  static std::string hostPath(const std::string &filename) {
    return std::string("spiffs") + filename;
  }

  static void writeHeader(std::ofstream &output) {
    output << "# climbDataCapture raw G5 payload log\n"
           << "# timestamp_us\\tpayload_bytes\\tescaped_payload\n";
  }

  static void writeRecord(std::ofstream &output, uint64_t timestampUs,
                          const char *payload, size_t length) {
    output << recordLine(timestampUs, payload, length);
  }

  void listFiles() {
    Serial.println("LOG_LIST_BEGIN");
    DIR *directory = opendir("spiffs");
    if (directory != NULL) {
      struct dirent *entry;
      while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        std::string path = std::string("spiffs/") + entry->d_name;
        struct stat details;
        if (stat(path.c_str(), &details) == 0)
          Serial.printf("LOG_FILE\t/%s\t%llu\n", entry->d_name,
                        (unsigned long long)details.st_size);
      }
      closedir(directory);
    }
    Serial.println("LOG_LIST_END");
  }

  void dumpFile(const std::string &filename) {
    if (sessionOpen || closing) {
      Serial.println("LOG_ERROR\tcapture active");
      return;
    }
    std::ifstream input(hostPath(filename), std::ios::binary);
    if (!input.good()) {
      Serial.println("LOG_ERROR\tfile not found");
      return;
    }
    input.seekg(0, std::ios::end);
    uint64_t size = input.tellg();
    input.seekg(0);
    Serial.printf("LOG_DUMP_BEGIN\t%s\t%llu\n", filename.c_str(),
                  (unsigned long long)size);
    char buffer[256];
    while (input.good()) {
      input.read(buffer, sizeof(buffer));
      std::streamsize count = input.gcount();
      if (count > 0) Serial.write((const uint8_t *)buffer, count);
    }
    Serial.printf("\nLOG_DUMP_END\t%s\n", filename.c_str());
  }

  void deleteFile(const std::string &filename) {
    if (sessionOpen || closing) {
      Serial.println("LOG_ERROR\tcapture active");
      return;
    }
    if (remove(hostPath(filename).c_str()) == 0)
      Serial.printf("LOG_DELETED\t%s\n", filename.c_str());
    else
      Serial.println("LOG_ERROR\tdelete failed");
  }

#else
  static const int queueEntries = 32;

  struct QueueEntry {
    enum Type : uint8_t { Open, Packet, Close } type;
    uint64_t timestampUs;
    uint16_t length;
    char filename[32];
    char payload[maxPayloadBytes];
  };

  QueueHandle_t queue = NULL;
  TaskHandle_t writerTask = NULL;

  static void writerTaskEntry(void *context) {
    ((RawG5Logger *)context)->writerLoop();
  }

  void writerLoop() {
    fs::File file;
    std::string batch;
    batch.reserve(4096);
    uint32_t lastFlushMs = millis();
    while (true) {
      QueueEntry entry = {};
      bool received = xQueueReceive(queue, &entry, pdMS_TO_TICKS(250)) == pdTRUE;
      if (received && entry.type == QueueEntry::Open) {
        if (file) file.close();
        file = LittleFS.open(entry.filename, "a");
        if (!file) {
          writeErrors++;
          recording = false;
        }
        batch.clear();
        lastFlushMs = millis();
      } else if (received && entry.type == QueueEntry::Packet) {
        if (!file) {
          writeErrors++;
          continue;
        }
        std::string line = recordLine(entry.timestampUs, entry.payload, entry.length);
        if (batch.size() + line.size() > 4096) flushBatch(file, batch, false);
        batch += line;
        packetsWritten++;
      } else if (received && entry.type == QueueEntry::Close) {
        flushBatch(file, batch, true);
        if (file) file.close();
        closing = false;
      }

      uint32_t nowMs = millis();
      if (file && !batch.empty() && (uint32_t)(nowMs - lastFlushMs) >= 1000) {
        flushBatch(file, batch, true);
        lastFlushMs = nowMs;
      }
    }
  }

  void flushBatch(fs::File &file, std::string &batch, bool flushFile) {
    if (!batch.empty()) {
      size_t written = file.write((const uint8_t *)batch.data(), batch.size());
      bytesWritten += written;
      if (written != batch.size()) {
        writeErrors++;
        recording = false;
      }
      batch.clear();
    }
    if (flushFile && file) file.flush();
  }

  void listFiles() {
    Serial.println("LOG_LIST_BEGIN");
    fs::File root = LittleFS.open("/");
    fs::File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory())
        Serial.printf("LOG_FILE\t%s\t%llu\n", file.name(),
                      (unsigned long long)file.size());
      file = root.openNextFile();
    }
    Serial.println("LOG_LIST_END");
  }

  void dumpFile(const std::string &filename) {
    if (sessionOpen || closing) {
      Serial.println("LOG_ERROR\tcapture active");
      return;
    }
    fs::File file = LittleFS.open(filename.c_str(), "r");
    if (!file) {
      Serial.println("LOG_ERROR\tfile not found");
      return;
    }
    Serial.printf("LOG_DUMP_BEGIN\t%s\t%llu\n", filename.c_str(),
                  (unsigned long long)file.size());
    uint8_t buffer[256];
    while (file.available()) {
      size_t count = file.read(buffer, sizeof(buffer));
      if (count > 0) Serial.write(buffer, count);
    }
    file.close();
    Serial.printf("\nLOG_DUMP_END\t%s\n", filename.c_str());
  }

  void deleteFile(const std::string &filename) {
    if (sessionOpen || closing) {
      Serial.println("LOG_ERROR\tcapture active");
      return;
    }
    if (LittleFS.remove(filename.c_str()))
      Serial.printf("LOG_DELETED\t%s\n", filename.c_str());
    else
      Serial.println("LOG_ERROR\tdelete failed");
  }
#endif
};
