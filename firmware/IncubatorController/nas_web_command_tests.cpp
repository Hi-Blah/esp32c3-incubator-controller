#include "nas_web_command.h"
static_assert(nasweb::token("0123456789abcdef01234567"));
static_assert(!nasweb::token("../../secret")&&!nasweb::token("0123456789abcdef0123456g"));
static_assert(nasweb::fresh(123000,129000)&&!nasweb::fresh(130000,129000)&&!nasweb::fresh(123000,133001));
static_assert(nasweb::command("daily 2 07:00:00-08:00:00,09:00:00-13:00:00"));
static_assert(!nasweb::command("on 1\narm")&&!nasweb::command("on \"1\""));
