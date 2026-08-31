#pragma once
#ifndef AUTOSTART_H
#define AUTOSTART_H

#include <stdbool.h>

/*
 * "Run at sign-in" via the per-user Run key. Nothing is written until the
 * user asks for it, and disabling removes the value rather than blanking it.
 */
bool Autostart_IsEnabled(void);
bool Autostart_SetEnabled(bool enable);

#endif /* AUTOSTART_H */
