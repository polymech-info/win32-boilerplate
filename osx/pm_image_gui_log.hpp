#pragma once

/** `ui-log.log` next to the `pm-image-gui` binary (e.g. …/Contents/MacOS/) via spdlog. */
void pm_image_gui_log_init();
void pm_image_gui_log_info(const char* utf8_line);
