// An Xbox 360 title, written the way one would be on Windows.
//
// There is no assembly entry stub and no hand-rolled runtime here. The XDK's own
// mainCRTStartup runs first, XapiInitProcess brings up the heap and TLS, and then this
// main is called, exactly the sequence a title built in Visual Studio goes through.

void DbgPrint(const char *fmt, ...);
void HalReturnToFirmware(unsigned int routine);

int main(void)
{
    DbgPrint("\n");
    DbgPrint("=================================================\n");
    DbgPrint(" OXDK360 title, linked against the real XDK\n");
    DbgPrint(" entry was mainCRTStartup from xapilib\n");
    DbgPrint(" built on macOS: clang -> ld.lld -> cxex\n");
    DbgPrint("=================================================\n");

    // If XapiInitProcess did not run, there would be no heap and this would fault.
    // Reaching the line after it is the whole point of the sample.
    DbgPrint(" the XDK runtime is initialised and main() is running\n");

    DbgPrint(" rebooting to the dashboard\n\n");
    HalReturnToFirmware(1);
    return 0;
}

// A second entry used only to bisect the loader failure: same objects, same libraries,
// same imports, but control starts here instead of in the XDK's startup.
void oxdk_probe_entry(void)
{
    DbgPrint("OXDK360: entered directly, XDK code is linked in but did not run\n");
    HalReturnToFirmware(1);
}
