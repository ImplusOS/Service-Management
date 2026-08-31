/* libextra.so — reachable at runtime ONLY through LD_LIBRARY_PATH
   (it lives in the app's lib/ subdirectory, outside every default
   search location). */

int extra_value(void)
{
    return 555;
}
