#pragma once
#include <esp_transport.h>
namespace rawmqtt {
struct Failure {uint32_t count,atMs,elapsedMs,requested,sent,heap,largest;int error,fd,rssi;char stage[24];};
Failure failureView();
esp_transport_handle_t create();bool live(esp_transport_handle_t);int error();void abort();}
