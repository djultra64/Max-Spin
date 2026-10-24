# Troubleshooting

## Missing original game image

Use extracted files from the supported original Xbox game, retaining the exact filenames and directory layout. A disc image by itself is not the extracted asset directory. `native-linux/config/images.json` lists the executable filenames expected by the pipeline. Max Spin does not download game files.

## An image has not been built

Generate every required module before configuring the complete build. Building only the main game module is insufficient for menus and transitions into other modes. Use the same build directory for all modules and the launcher.

## CMake cannot find SDL3, Vulkan, or shaderc

Install development headers as well as the runtime libraries. A Vulkan loader alone is not the complete build dependency set. Configure `SDL3_DIR` with the directory containing your installation's SDL3 CMake package when it is outside the default search paths. Vulkan and shaderc also require compatible headers and libraries.

## Original prompts appear or Graphics Options is missing

Generate the local resources described in the build instructions. Those caches are derived from your game files and are deliberately excluded from the repository. Copying a previous user's cache is not a supported preparation step. Restart after preparing the resources.

## Controller prompts use the wrong layout

Select the controller family manually in Graphics Options. For Nintendo, Confirm on the bottom face button should show **B**, and Cancel on the right face button should show **A**. Include the controller model, selected family, physical button pressed, displayed label, and resulting action in an issue report.

## A graphics setting will not apply

Use Save and restart the whole application. Unsupported combinations are skipped or rejected. Reset the graphics preferences file with the application closed if a previous configuration prevents startup. Keep save files intact.

## Black screen, rendering defects, or a crash

Return to the default graphics settings and reproduce the problem. Include the selected game mode and the action immediately before the failure. Do not infer performance or game speed from a frame counter alone.

## Career appearance and saves

Some Career clothing or appearance materials can render black, and some clothing previews can omit items. A successful save and reload does not establish that every material or appearance is correct. Complete Career progression and visual recovery following a failed save are still being tested.

Keep a backup of the complete save folder with Max Spin closed. Report the screen and appearance affected, and whether the issue also occurs after closing and reloading. Do not upload your save files with a public issue report.

## Reporting a problem

Include the Max Spin revision, operating system, graphics driver version, selected settings, and reproduction steps. Linux launch logs are stored under `native-linux/logs/`. Inspect and redact logs before sharing: remove account names, personal file paths, network details, and any save contents. Do not attach executable images, disc images, extracted game assets, or generated translations.
