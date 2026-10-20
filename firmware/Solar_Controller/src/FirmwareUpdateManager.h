#pragma once

#include <WebServer.h>

bool startFirmwareUpdateTaskNow();
void processFirmwareUpdateTask();
void firmwareUpdateAutoCheckTick();
void registerFirmwareUpdateRoutes(WebServer& server);
