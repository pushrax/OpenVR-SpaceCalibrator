#pragma once

typedef void (*InitErrorCallback)(char const * );

int MainApplication(char const * const cwd, InitErrorCallback cb);

