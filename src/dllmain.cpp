/*
 * MIT License
 *
 * Copyright (c) 2025 Dominik Protasewicz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// System includes
#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include <format>
#include <numeric>
#include <numbers>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <bit>

// Local includes
#include "utils.hpp"

// Version string
constexpr std::string VERSION = "1.0.0";

// .yml to struct
typedef struct resolution_t {
    u32 width;
    u32 height;
    f32 aspectRatio;
} resolution_t;

typedef struct hud_t {
    bool enable;
} hud_t;

typedef struct fixes_t {
    hud_t hud;
} fixes_t;

typedef struct yml_t {
    std::string name;
    bool masterEnable;
    resolution_t resolution;
    fixes_t fix;
} yml_t;

// Globals
namespace {
    Utils::ModuleInfo module(GetModuleHandle(nullptr));

    u32 nativeWidth = 0;
    u32 nativeOffset = 0;
    f32 nativeAspectRatio = (16.0f / 9.0f);
    f32 widthScalingFactor = 0;

    YAML::Node config = YAML::LoadFile("SamuraiWarriors4DXFix.yml");
    yml_t yml{};
}

/**
 * @brief Opens and initializes logging system.
 *
 * @return void
 */
void logOpen() {
    // spdlog initialisation
    auto logger = spdlog::basic_logger_mt("SamuraiWarriors4DXFix", "SamuraiWarriors4DXFix.log", true);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::debug);

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(module.address, exePath, MAX_PATH);
    std::filesystem::path exeFilePath = exePath;
    module.name = exeFilePath.filename().string();

    // Log module details
    LOG("-------------------------------------");
    LOG("Compiler: {:s}", Utils::getCompilerInfo());
    LOG("Compiled: {:s} at {:s}", __DATE__, __TIME__);
    LOG("Version: {:s}", VERSION);
    LOG("Module Name: {:s}", module.name);
    LOG("Module Path: {:s}", exeFilePath.string());
    LOG("Module Addr: 0x{:x}", reinterpret_cast<u64>(module.address));
    LOG("-------------------------------------");
}

/**
 * @brief Closes and cleans up the logging system.
 *
 * @details
 * Flushes all pending log messages, drops all loggers, and shuts down the spdlog system.
 * This function should be called when the DLL is being unloaded to ensure all log data
 * is properly written to disk.
 *
 * @return void
 */
void logClose() {
    spdlog::drop_all();
    spdlog::shutdown();
}

/**
 * @brief Reads and parses configuration settings from a YAML file.
 *
 * @return void
 */
void readYml() {
    yml.name = config["name"].as<std::string>();

    yml.masterEnable = config["masterEnable"].as<bool>();

    yml.resolution.width = config["resolution"]["width"].as<u32>();
    yml.resolution.height = config["resolution"]["height"].as<u32>();

    yml.fix.hud.enable = config["fixes"]["hud"]["enable"].as<bool>();

    if (yml.resolution.width == 0 || yml.resolution.height == 0) {
        std::pair<int, int> dimensions = Utils::getDesktopDimensions();
        yml.resolution.width  = dimensions.first;
        yml.resolution.height = dimensions.second;
    }
    yml.resolution.aspectRatio = static_cast<f32>(yml.resolution.width) / static_cast<f32>(yml.resolution.height);
    nativeWidth = (16.0f / 9.0f) * static_cast<f32>(yml.resolution.height);
    nativeOffset = static_cast<f32>(yml.resolution.width - nativeWidth) / 2.0f;
    widthScalingFactor = static_cast<f32>(yml.resolution.width) / static_cast<f32>(nativeWidth);

    // Get that info!
    LOG("Name: {}", yml.name);
    LOG("MasterEnable: {}", yml.masterEnable);
    LOG("Resolution.Width: {}", yml.resolution.width);
    LOG("Resolution.Height: {}", yml.resolution.height);
    LOG("Resolution.AspectRatio: {}", yml.resolution.aspectRatio);
    LOG("Normalized Width: {}", nativeWidth);
    LOG("Normalized Offset: {}", nativeOffset);
    LOG("Width Scaling Factor: {}", widthScalingFactor);
}

/**
 * @brief Fixes the resolution.
 *
 * @details
 * This function updates all values in the resolution table, that the game uses for each available resolution
 * in the games settings, to the value that is provided in the YAML file.
 *
 * How was this found?
 * Doing a basic memory scan using cheat engine for the current resolution and then modifying each address to
 * another value will eventually cause visible destruction to the game rendering in general. From there you can
 * work backwards and see that the game does something unique, it actually queries the resolution table to get
 * the selected resolution value. This pattern is used everywhere in the code and is the main way that the game
 * does any calculations that require the resolution. Although there are many instances of the resolution being
 * cached and used elsewhere in the code this is not as widely used.
 *
 * Based on this information that the game makes extensive use of the resolution table a patch is applied to it
 * where each resolution is patched to whatever resolution is provided in the YAML file. Just this alone fixes
 * a plethera of other issues. Right from game start the window will be spawned with the correct resolution and
 * the game will render at the correct FOV, and movies and UI will be stretched to fill the screen.
 *
 * @return void
 */
void fixResolution() {
    u16 width16 = static_cast<u16>(yml.resolution.width);
    u16 height16 = static_cast<u16>(yml.resolution.height);
    std::string single = Utils::bytesToString(std::span<const u8>(reinterpret_cast<const u8*>(&width16), sizeof(width16)));
    single += " ";
    single += Utils::bytesToString(std::span<const u8>(reinterpret_cast<const u8*>(&height16), sizeof(height16)));
    single += " ";
    single += Utils::bytesToString(std::span<const u8>(reinterpret_cast<const u8*>(&height16), sizeof(height16)));
    single += " ";
    std::string entry{};
    for (int i = 0; i < 16; i++) {
        entry += single;
    }
    Utils::SignaturePatch patch1 {
        .tag = __func__,
        .signature = "80 02 68 01 E0 01",
        .patch = entry
    };

    bool enable = yml.masterEnable & yml.fix.hud.enable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");
    Utils::injectPatch(enable, module, patch1);
}

/**
 * @brief Fixes the HUD.
 *
 * @details
 * This function injects 6 hooks which all work together to unstretch, center, and constrain the entire HUD to
 * 16:9.
 *
 * How was this found?
 * Before this fix solution I was actually manually unstretching, centering, and constraining HUD elements one
 * at a time to the point where I had at least 20 hooks, and certain hooks would break parts of the UI. Overall
 * it was a messy solution and needed to come up with something better. I started looking for things that could
 * be a lead on a projection matrix used for the UI and started scanning for -1 (0xBF80_0000), as well as
 * 1/(resolution width) and 2/(resolution width). After some experimenting I landed upon a peice of code that
 * did exactly what I wanted it would make all parts of the UI unstretched, centered, and constrained to 16:9,
 * this is what hook1 accomplishes.
 *
 * Post that fix a new problem emerged the nameplates and healthbars of the enemies where now off since we just
 * scaled the UI from stretched ultrawide to 16:9 so it was no longer drawn in the correct location.
 *
 * First the health bars, what needed to be done is that we needed to scale the position back to where it was in
 * ultrawide while preserving the 16:9 textures so it doesn't get stretched. So we needed to find where this
 * calculation is handled by the game and this was thankfully very easy because as mentioned in the resolution fix
 * the game constantly indexes that table to get the current resolution, from here and after some iterations I
 * noticed that most of the functions that call the function that index the resolution table are scaling the
 * resolution by dividing by the hardcoded value 1280.0f found in the read only section of the binary. Playing
 * around with this value made me see a visible change in the health bar scaling and position on the screen so
 * I knew I could use this value to track down the code that was responsible for the health bars. Once I found
 * that function I saw that it was actually being called a total of 3 times which makes sense. It was called
 * first to do the calculations for the black outline of the health bar, then the grey track of the health bar
 * that red health would sit on top of and the final call was for the actual red health bar. I placed a hook
 * inside that function which would do some fancy math which would scale the health bar position to the right
 * coordinates on the screen. In game while testing it I observed a slight issue: as the health of enemies got
 * slower the red health bar would anchor itself from the whole health bar entity. This behavior made sense,
 * because as the health got lower and lower its center point would float away from the rest of the health bar
 * entity. This is where hooks 2 and 3 come in, they are placed just before entering the function responsible
 * for all the math for the healthbars. Hook2 is placed before the call of the grey part of the health bar and
 * hook3 just before the red part of the health bar. Hook4 is placed inside the function that does all the
 * calculations. Hook2 is responsible for letting us know that we are about to do the calculation for the grey
 * part and that hook4 needs to record the left side X coordinate of the grey part so that we know where to
 * anchor the red part of the health bar. Hook3 tells us that we are about to do the red part of the health bar
 * calculation and that we need to anchor it to left X coordinate recorded earlier. And with that health bars
 * are working as intended.
 *
 * And finally the nameplates, this was not the most trivial. The nameplates themselves are not one full entity
 * but instead each character is its own thing. Hook5 has the same math theory as that for the health bars so
 * I wont explain it, but the problem is that each character has significant spacing now and things just look
 * weird on the screen. It took me significant time to find a solution where I initially thought I needed to
 * change my approach for this, but thankfully that was not the case. So basically first the game does calculations
 * on each character one by one, this is where hook5 lives, and outside the function it takes the 4 x,y coordinates
 * and places them in some arbitrary memory that is basically a doubly linked list, where each character has a
 * previous and next pointer. Making this discovery was massive and from this point on I basically just recorded
 * each address of the doubly linked list for each character, and I knew when to stop recording that because in
 * hook5 I make a check to the structure that holds a lot of data including the string of the name of whatever
 * character is currently being processed, and thankfully this same structure also had pointers to the current
 * and next characters. If the next character was NULL (0), then I knew that the string was terminated and the
 * current character was the last character that needed to be processed. This is where hook6 comes in, I already
 * mentioned that I record the address of the doubly linked list nodes where the 4 x,y coordinate pairs are
 * written. I basically take the first and last elements and using the X coordinates I find out the center of
 * the nameplate, and then I scale each characters position back to where it would be in ultrawide while
 * preserving the unstretched nature from thanks to hook1. The nameplates basically required 2 passes, the
 * first would scale their positions from 16:9 to ultrawide, which would result in individual characters being
 * spaced way to far apart and then the second pass would scale the nameplates from ultrawide back to 16:9 by
 * using the nameplates center point as a reference. In other words, the first pass fixes their position on the
 * screen and second pass fixes the spacing between characters caused by the first pass.
 *
 * @return void
 */
void fixHud() {
    static bool isHealthBarTrack = false;
    static bool isHealthBarFill = false;
    static u64 healthBarTrackX = 0;
    static bool glpyhStringEnd = false;
    static std::vector<u64> glyphString{};
    Utils::SignatureHook hook1 {
        .tag = __func__,
        .signature = "41 0F 10 4C 10 F0    0F 11 01",
    };
    Utils::SignatureHook hook2 {
        .tag = __func__,
        .signature = "E8 ?? ?? ?? ??    F3 44 0F 58 54 24 70",
    };
    Utils::SignatureHook hook3 {
        .tag = __func__,
        .signature = "E8 ?? ?? ?? ??    83 7B 10 00    74 11",
    };
    Utils::SignatureHook hook4 {
        .tag = __func__,
        .signature = "F3 44 0F 11 54 24 30    F3 44 0F 11 4C 24 34",
    };
    Utils::SignatureHook hook5 {
        .tag = __func__,
        .signature = "F2 0F 11 47 18    48 83 C4 50",
        .offset = 5
    };
    Utils::SignatureHook hook6 {
        .tag = __func__,
        .signature = "F3 0F 11 73 58    48 8B 84 24 00 01 00 00",
    };

    bool enable = yml.masterEnable & yml.fix.hud.enable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");
    Utils::injectHook(enable, module, hook1,
        [](SafetyHookContext& ctx) {
            f32 normalizedHalfWidth = 1.0f / (static_cast<f32>(yml.resolution.width) / 2.0f);
            f32 normalizedHalfHeight = -1.0f / (static_cast<f32>(yml.resolution.height) / 2.0f);
            if (ctx.xmm0.f32[0] == normalizedHalfWidth && ctx.xmm0.f32[1] == normalizedHalfHeight) {
                f32 normalizedWidth = 1.0f / static_cast<f32>(yml.resolution.width);
                if (*reinterpret_cast<u32*>(ctx.rdx + 0x10) == 0x3F800000 && *reinterpret_cast<u32*>(ctx.rsp + 0x20) != 0 && (ctx.xmm2.f32[0] != normalizedWidth)) {
                    f32 scaler = static_cast<f32>(yml.resolution.aspectRatio) / static_cast<f32>(nativeAspectRatio);
                    ctx.xmm0.f32[0] = 2.0f / static_cast<f32>(yml.resolution.width * scaler);
                    ctx.xmm0.f32[2] = -1.0f / scaler;
                }
            }
        }
    );
    Utils::injectHook(enable, module, hook2,
        [](SafetyHookContext& ctx) {
            isHealthBarTrack = true;
        }
    );
    Utils::injectHook(enable, module, hook3,
        [](SafetyHookContext& ctx) {
            isHealthBarFill = true;
        }
    );
    Utils::injectHook(enable, module, hook4,
        [](SafetyHookContext& ctx) {
            // Read the current X positions (bar edges)
            f32 x1 = ctx.xmm10.f32[0];
            f32 x2 = ctx.xmm6.f32[0];
            // Compute bar center and half-width (radius)
            f32 center = (x1 + x2) * 0.5f;
            f32 radius = (x2 - x1) * 0.5f;
            // --- Compute new center ---
            // Apply aspect ratio correction: ultrawide (32:9, 21:9, etc.) -> 16:9
            f32 renderAspect = yml.resolution.aspectRatio;      // internal render aspect
            f32 targetAspect = nativeAspectRatio;               // final viewport aspect
            f32 screenCenter = static_cast<f32>(yml.resolution.width) * 0.5f;
            // Adjust center based on aspect ratio difference
            f32 newCenter = screenCenter + (center - screenCenter) * (renderAspect / targetAspect);
            // --- Reconstruct X edges using radius ---
            ctx.xmm10.f32[0] = newCenter - radius;   // new x1
            ctx.xmm6.f32[0]  = newCenter + radius;   // new x2
            // Record absolute X of the health bar track that will be used to realign health bar fill to itself
            if (isHealthBarTrack == true) {
                isHealthBarTrack = false;
                healthBarTrackX = ctx.xmm10.f32[0];
            }

            if (isHealthBarFill == true) {
                isHealthBarFill = false;
                f32 healthBarFillLength = ctx.xmm6.f32[0] - ctx.xmm10.f32[0];
                ctx.xmm10.f32[0] = healthBarTrackX;
                ctx.xmm6.f32[0] = healthBarTrackX + healthBarFillLength;
            }
        }
    );
    Utils::injectHook(enable, module, hook5,
        [](SafetyHookContext& ctx) {
            u64 ptr1 = *reinterpret_cast<u64*>(ctx.rdi + 0x1C0);
            u64 ptr2 = *reinterpret_cast<u64*>(ctx.rdi + 0x1C8);
            u64 ptr3 = *reinterpret_cast<u64*>(ctx.rdi + 0x1E0);
            u64 ptr4 = *reinterpret_cast<u64*>(ctx.rdi + 0x1F8);
            if ((ptr1 == (ptr2 + 1)) || (ptr1 == (ptr2 + 2))) {
                if ((ptr3 + 1) == ptr4) {
                    if (*reinterpret_cast<const char*>(ptr1) == 0) {
                        glpyhStringEnd = true;
                    }
                    // First pass - scale nameplate glyphs to correct locations
                    // Read the current X positions (bar edges)
                    f32 x1 = *reinterpret_cast<f32*>(ctx.rdi + 0x00);
                    f32 x2 = *reinterpret_cast<f32*>(ctx.rdi + 0x08);
                    f32 x3 = *reinterpret_cast<f32*>(ctx.rdi + 0x10);
                    f32 x4 = *reinterpret_cast<f32*>(ctx.rdi + 0x18);
                    // Compute bar center and half-width (radius)
                    f32 center1 = (x1 + x2) * 0.5f;
                    f32 radius1 = (x2 - x1) * 0.5f;
                    f32 center2 = (x3 + x4) * 0.5f;
                    f32 radius2 = (x4 - x3) * 0.5f;
                    // --- Compute new center ---
                    // Apply aspect ratio correction: ultrawide (32:9, 21:9, etc.) -> 16:9
                    f32 renderAspect = yml.resolution.aspectRatio;      // internal render aspect
                    f32 targetAspect = nativeAspectRatio;               // final viewport aspect
                    f32 screenCenter = static_cast<f32>(yml.resolution.width) * 0.5f;
                    // Adjust center based on aspect ratio difference
                    f32 newCenter1 = screenCenter + (center1 - screenCenter) * (renderAspect / targetAspect);
                    f32 newCenter2 = screenCenter + (center2 - screenCenter) * (renderAspect / targetAspect);
                    // --- Reconstruct X edges using radius ---
                    *reinterpret_cast<f32*>(ctx.rdi + 0x00) = newCenter1 - radius1;   // new x1
                    *reinterpret_cast<f32*>(ctx.rdi + 0x08) = newCenter1 + radius1;   // new x2
                    *reinterpret_cast<f32*>(ctx.rdi + 0x10) = newCenter2 - radius2;   // new x3
                    *reinterpret_cast<f32*>(ctx.rdi + 0x18) = newCenter2 + radius2;   // new x4
                }
            }
        }
    );
    Utils::injectHook(enable, module, hook6,
        [](SafetyHookContext& ctx) {
            glyphString.push_back(ctx.rbx);
            if (glpyhStringEnd == true) {
                glpyhStringEnd = false;
                u64 first = glyphString.front();
                u64 last = glyphString.back();
                // At the front of the string get start X positions
                f32 x1 = *reinterpret_cast<f32*>(first + 0x38);
                f32 x3 = *reinterpret_cast<f32*>(first + 0x48);
                // At the end of the string get end X positions
                f32 x2 = *reinterpret_cast<f32*>(last + 0x40);
                f32 x4 = *reinterpret_cast<f32*>(last + 0x50);
                // Compute bar center and half-width (radius)
                f32 trueCenter1 = (x1 + x2) * 0.5f;
                f32 trueCenter2 = (x3 + x4) * 0.5f;
                for (u64 glyph : glyphString) {
                    // Read the current X positions
                    f32 glyphX1 = *reinterpret_cast<f32*>(glyph + 0x38);
                    f32 glyphX2 = *reinterpret_cast<f32*>(glyph + 0x40);
                    f32 glyphX3 = *reinterpret_cast<f32*>(glyph + 0x48);
                    f32 glyphX4 = *reinterpret_cast<f32*>(glyph + 0x50);
                    // Compute bar center and half-width (radius)
                    f32 glyphCenter1 = (glyphX1 + glyphX2) * 0.5f;
                    f32 glyphRadius1 = (glyphX2 - glyphX1) * 0.5f;
                    f32 glyphCenter2 = (glyphX3 + glyphX4) * 0.5f;
                    f32 glyphRadius2 = (glyphX4 - glyphX3) * 0.5f;
                    // --- Compute new center ---
                    // Apply aspect ratio correction: 16:9 -> ultrawide (32:9, 21:9, etc.)
                    f32 renderAspect = nativeAspectRatio;          // internal render aspect
                    f32 targetAspect = yml.resolution.aspectRatio; // final viewport aspect
                    // Adjust center based on aspect ratio difference
                    f32 newCenter1 = trueCenter1 + (glyphCenter1 - trueCenter1) * (renderAspect / targetAspect);
                    f32 newCenter2 = trueCenter2 + (glyphCenter2 - trueCenter2) * (renderAspect / targetAspect);
                    // --- Reconstruct X edges using radius ---
                    *reinterpret_cast<f32*>(glyph + 0x38) = newCenter1 - glyphRadius1;   // new x1
                    *reinterpret_cast<f32*>(glyph + 0x40) = newCenter1 + glyphRadius1;   // new x2
                    *reinterpret_cast<f32*>(glyph + 0x48) = newCenter2 - glyphRadius2;   // new x3
                    *reinterpret_cast<f32*>(glyph + 0x50) = newCenter2 + glyphRadius2;   // new x4
                }
                glyphString.clear();
            }
        }
    );
}

/**
 * @brief This function serves as the entry point for the DLL. It performs the following tasks:
 * 1. Initializes the logging system.
 * 2. Reads the configuration from a YAML file.
 * 3. Applies a center UI fix.
 *
 * @param lpParameter Unused parameter.
 * @return Always returns TRUE to indicate successful execution.
 */
DWORD WINAPI Main(void* lpParameter) {
    logOpen();
    readYml();
    fixResolution();
    fixHud();
    logClose();
    return true;
}

/**
 * @brief Entry point for a DLL, called by the system when the DLL is loaded or unloaded.
 *
 * This function handles various events related to the DLL's lifetime and performs actions
 * based on the reason for the call. Specifically, it creates a new thread when the DLL is
 * attached to a process.
 *
 * @details
 * The `DllMain` function is called by the system when the DLL is loaded or unloaded. It handles
 * different reasons for the call specified by `ul_reason_for_call`. In this implementation:
 *
 * - **DLL_PROCESS_ATTACH**: When the DLL is loaded into the address space of a process, it
 *   creates a new thread to run the `Main` function. The thread priority is set to the highest,
 *   and the thread handle is closed after creation.
 *
 * - **DLL_THREAD_ATTACH**: Called when a new thread is created in the process. No action is taken
 *   in this implementation.
 *
 * - **DLL_THREAD_DETACH**: Called when a thread exits cleanly. No action is taken in this implementation.
 *
 * - **DLL_PROCESS_DETACH**: Called when the DLL is unloaded from the address space of a process.
 *   No action is taken in this implementation.
 *
 * @param hModule Handle to the DLL module. This parameter is used to identify the DLL.
 * @param ul_reason_for_call Indicates the reason for the call (e.g., process attach, thread attach).
 * @param lpReserved Reserved for future use. This parameter is typically NULL.
 * @return BOOL Always returns TRUE to indicate successful execution.
 */
BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
) {
    HANDLE mainHandle;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
