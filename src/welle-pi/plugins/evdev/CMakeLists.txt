# Remote control that appears as a keyboard (2.4 GHz or infrared dongle),
# read through /dev/input/event*. No dependency beyond the Linux input API,
# nlohmann/json comes from src/libs.
welle_pi_plugin(evdev evdev.cpp remote_config.cpp)
