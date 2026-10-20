// English (United States): the fallback language.
#include "ui/i18n.h"

namespace bs::builder::ui {

namespace {
const Entry kEntries[] = {
    {"app.title", L"Blue Stinger Recompiler"},
    {"app.intro", L"Builds Blue Stinger for Windows from your copy of the game (CDI image)."},
    {"lang.label", L"Language:"},

    {"disc.label", L"Game image (your Blue Stinger NTSC-U CDI):"},
    {"out.label", L"Installation folder (the game goes to the BlueStinger subfolder):"},
    {"browse", L"Browse..."},
    {"portable.check", L"Portable mode"},
    {"portable.off", L"Saved games go to this user's Documents\\DC-Recomp\\Blue Stinger\\saves folder."},
    {"portable.on", L"Saved games go to the saves folder inside the installation: the game can be moved to another drive or computer."},

    {"btn.build", L"Build"},
    {"btn.cancel", L"Cancel"},
    {"btn.open", L"Open folder"},
    {"btn.play", L"Play"},
    {"btn.help", L"Help"},

    {"status.idle", L"Choose the game image and the installation folder."},
    {"status.progress", L"{0}% - {1}"},
    {"status.cancelling", L"Cancelling..."},
    {"status.done", L"Done: the game is installed in {0}."},
    {"status.failed", L"Could not complete the \"{0}\" step."},
    {"status.cancelled", L"Cancelled. The previous installation was kept."},

    {"step.validate", L"Checking your choices"},
    {"step.toolchain", L"Looking for the compiler"},
    {"step.extract", L"Preparing the game data"},
    {"step.recompile", L"Recompiling the game code"},
    {"step.configure", L"Configuring the build"},
    {"step.compile", L"Compiling"},
    {"step.package", L"Assembling the package"},
    {"step.verify", L"Testing the built game"},
    {"step.export", L"Exporting the project"},
    {"step.install", L"Installing"},
    {"step.done", L"Done"},

    {"dlg.disc_title", L"Blue Stinger CDI image (NTSC-U)"},
    {"dlg.cdi_filter", L"CDI disc image (*.cdi)"},
    {"dlg.all_files", L"All files"},
    {"dlg.out_title", L"Folder where the game will be installed"},

    {"msg.missing_input", L"Choose the game image and the installation folder."},
    {"msg.success", L"Blue Stinger was recompiled, tested and installed in:\n{0}\n\nUse \"Play\" or \"Open folder\". The game contains code derived from your copy: use it only on your computer and do not publish it."},
    {"msg.previous_not_archived", L"\n\nNote: the previous version could not be stored as BlueStinger.anterior. It remains in a folder next to the installation; details in: {0}"},
    {"msg.close_running", L"The build is in progress. Cancel and exit?"},
    {"msg.kit_missing", L"Recompiler files are missing (kit folder). Extract the complete distribution again."},
    {"msg.existing", L"There is already an installation in:\n{0}\n\nIt will be updated. Saved games, mods and your other files are kept, and the current version is stored in {1}.\n\nContinue?"},
    {"msg.foreign", L"The folder {0} already exists and was not installed by this recompiler.\n\nNothing is deleted: its current contents are stored in {1}, and files that are not part of the game stay in the new installation.\n\nContinue?"},
    {"msg.mode_change", L"The current installation keeps saved games in {0}. After this build they will be kept in {1}.\n\nExisting saved games are NOT transferred: they stay where they are and nothing is deleted. To continue a game, copy the save files from one folder to the other.\n\nContinue?"},
    {"loc.documents", L"Documents\\DC-Recomp\\Blue Stinger\\saves"},
    {"loc.portable", L"saves, inside the installation"},

    {"err.footer", L"\n\nNo previous installation was changed.\n\nDiagnostic details: {0}"},
    {"err.invalid_disc", L"This image cannot be used. The Blue Stinger CDI image for Dreamcast, US version (NTSC-U, T13001N V1.000), is required."},
    {"err.disc_not_found", L"The game image was not found. Check the chosen file."},
    {"err.no_space", L"There is not enough free space on the installation folder's drive. Free some space or choose another folder."},
    {"err.no_permission", L"No permission to write to the chosen folder. Choose another folder, for example inside Documents."},
    {"err.path_too_long", L"The installation folder path is too long. Choose a folder with a shorter path."},
    {"err.busy", L"Another build is using this folder. Wait for it to finish and try again."},
    {"err.toolchain", L"The Visual Studio C++ compiler was not found. Install Visual Studio Build Tools with the \"Desktop development with C++\" workload and try again."},
    {"err.missing_input", L"Choose the game image and the installation folder."},
    {"err.data", L"The game data prepared in this folder is incomplete or changed. Choose the game image to prepare it again."},
    {"err.build", L"Compiling the game failed."},
    {"err.verify", L"The built game did not pass the startup test."},
    {"err.install", L"The new version could not be installed. Is the game running? Close it and try again. The previous version was kept."},
    {"err.other", L"An unexpected error occurred."},

    {"help.title", L"Help"},
    {"help.body", L"1. Choose the CDI image of your Blue Stinger copy (Dreamcast, NTSC-U).\n2. Choose the installation folder. The game goes to the BlueStinger subfolder.\n3. Click Build. The first time takes a few minutes; later builds reuse what was already done.\n4. When it finishes, use Play or Open folder.\n\nPortable mode: unchecked, saved games go to Documents\\DC-Recomp\\Blue Stinger\\saves. Checked, they go to the saves folder of the installation, and the game can be moved to another drive. Changing the mode does not move existing saved games.\n\nBuilding again in the same folder keeps saved games and mods. The language chosen here only applies to the recompiler, not to the game.\n\nKeyboard: Tab moves between fields; Enter builds; Esc cancels; F1 opens this help."},
    {"tip.lang", L"Language of this window. It does not change the game's language."},
    {"tip.disc", L"The .cdi file of your copy of the game."},
    {"tip.out", L"The game is installed in the BlueStinger subfolder of this folder."},
    {"tip.portable", L"Keeps saved games inside the installation instead of Documents."},
    {"tip.build", L"Recompiles the game and installs it in the chosen folder."},
};
} // namespace

extern const Language kEnUs{"en-US", L"English", kEntries};

} // namespace bs::builder::ui
