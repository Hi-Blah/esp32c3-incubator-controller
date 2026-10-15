#pragma once
#include <stddef.h>
#include <stdint.h>
struct SampleJournalView {bool enabled,ready;uint16_t pending,capacity;uint32_t covered,thinned,failures,damaged,intervalMs,flashTotal,flashUsed;};
// Called exclusively by inc-log while holding its filesystem mutex.
void sampleJournalTick();
// loop task: nonblocking filesystem lock, serialize directly into shared MQTT buffer.
bool sampleJournalJson(char*json,size_t capacity);
// MQTT callback: queues an exact NAS commit receipt; no filesystem work here.
void sampleJournalReceipt(const char*json,bool retained);
SampleJournalView sampleJournalView();
void sampleJournalPrint();
// event_log owns the shared LittleFS instance and mutex.
bool eventStorageTake();void eventStorageGive();

