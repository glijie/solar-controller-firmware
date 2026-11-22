#pragma once

#include "HomeyP1Telegram.h"

void homeyP1Update(const char* address);
bool homeyP1Read(HomeyP1Sample& sample, unsigned long& receivedMs);
