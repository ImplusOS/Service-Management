/* dynlib.c — shared library used by the dynamically-linked test app. */

const char *dynlib_greet(void)
{
    return "hello from libdynlib.so!";
}

const int dynlib_version = 7;
