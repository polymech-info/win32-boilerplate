#ifndef PM_IMAGE_RUN_HPP
#define PM_IMAGE_RUN_HPP

int pm_image_run(int argc, char** argv);

#if defined(_WIN32)
/** False until after CLI parse; true iff `--console` was set (standalone chat may keep the console). */
bool pm_image_win_console_requested();
#endif

#endif
