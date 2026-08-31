/* libpreload.so — loaded via LD_PRELOAD before any DT_NEEDED library.
   Its symbols interpose in the global scope; the test executable
   calls preload_marker() even though no link-time definition exists. */

int preload_marker(void)
{
    return 777;
}
