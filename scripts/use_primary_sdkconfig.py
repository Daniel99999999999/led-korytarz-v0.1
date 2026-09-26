Import("env")

import os


project_dir = env.subst("$PROJECT_DIR")
configured_path = env.GetProjectOption("custom_sdkconfig_source")
sdkconfig_path = os.path.abspath(os.path.join(project_dir, configured_path))

if not os.path.isfile(sdkconfig_path):
    print(
        "ERROR: Profil OTA wymaga konfiguracji bazowej: %s. "
        "Najpierw zbuduj srodowisko esp32doit-devkit-v1." % sdkconfig_path
    )
    env.Exit(1)

env.BoardConfig().update("build.esp-idf.sdkconfig_path", sdkconfig_path)
