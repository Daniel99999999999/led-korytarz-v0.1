"""Windows fallback for the available Embarcadero C++17 compiler; no package installs."""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="C:/Program Files (x86)/Embarcadero/Studio/23.0/bin/bcc64.exe")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = root / ".pio" / "host-bcc"
    output.mkdir(parents=True, exist_ok=True)
    cmake = (root / "test/host/CMakeLists.txt").read_text(encoding="utf-8")
    # The first executable is the pure logic suite; adapter sources belong to the second target.
    first_target = cmake.split("add_executable(", 1)[1].split("target_include_directories", 1)[0]
    sources = re.findall(r'"\$\{PROJECT_ROOT\}/(src/[^"]+\.cpp)"', first_target)
    def run(command):
        subprocess.run(command, cwd=output, check=True)
    run([args.compiler, "-c", "-ocjson.o", str(root / "managed_components/espressif__cjson/cJSON/cJSON.c")])
    flags = [args.compiler, "-tM", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti"]
    run(flags + ["-I" + str(root / "src"), "-I" + str(root / "managed_components/espressif__cjson/cJSON"),
        "-DCONFIG_LED_CONTROLLER_AT24C08=1", "-DCONFIG_LED_CONTROLLER_INA228=1",
        "-DCONFIG_LED_CONTROLLER_DS18B20=1", "-ologic.exe", str(root / "test/host/test_main.cpp")]
        + [str(root / source) for source in sources] + ["cjson.o"])
    run([str(output / "logic.exe")])
    run(flags + ["-I" + str(root / "test/host/stubs"), "-I" + str(root / "src"), "-oadapters.exe"]
        + [str(root / source) for source in ["test/host/test_adapters.cpp", "src/LcdDisplay.cpp",
           "src/LedChannel.cpp", "src/ChannelState.cpp", "src/DutyTransition.cpp", "src/DebugLog.cpp", "src/StackMonitor.cpp"]])
    run([str(output / "adapters.exe")])


if __name__ == "__main__":
    main()
