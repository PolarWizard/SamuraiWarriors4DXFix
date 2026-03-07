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
constexpr std::string VERSION = "1.1.0";

// .yml to struct
typedef struct resolution_t {
    u32 width;
    u32 height;
    f32 aspectRatio;
} resolution_t;

typedef struct hud_t {
    bool enable;
} hud_t;

typedef struct fullscreen_t {
    bool enable;
} fullscreen_t;

typedef struct yml_t {
    std::string name;
    bool masterEnable;
    resolution_t resolution;
    fullscreen_t fullscreen;
    hud_t hud;
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

    std::atomic<bool> isCostumeView{false};
    std::atomic<bool> isCutscene{false};
    std::atomic<bool> isGameplay{false};
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

    yml.fullscreen.enable = config["fullscreen"]["enable"].as<bool>();

    yml.hud.enable = config["hud"]["enable"].as<bool>();

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
    LOG("-------------------------------------");
    LOG("Fullscreen.Enable: {}", yml.fullscreen.enable);
    LOG("HUD.Enable: {}", yml.hud.enable);
    LOG("-------------------------------------");
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

    bool enable = yml.masterEnable;
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
 * EDIT: A user found an issue that when the mini map is changed to its alternative, the rendering is not correct
 * where it would either be partially rendered or not rendered at all. This was simple to fix, as we already know
 * the game abuses the 1280.0f value for scaling, we just needed to find a place where this value is used for the
 * mini map and then after experimenting I just needed to add in a large enough value and the alternative mini map
 * would render correctly. This is what hook7 accomplishes. I can only assume that the larger value caused the
 * clipping planes for the mini map to be further apart which is why the entire mini map would render properly, not
 * that this has been confirmed but it is the most likely explanation.
 *
 * @return void
 */
void fixHud() {
    static bool isHealthBarTrack = false;
    static bool isHealthBarFill = false;
    static f32 healthBarTrackX = 0;
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
    Utils::SignatureHook hook7 {
        .tag = __func__,
        .signature = "0F 59 07    F3 0F 11 44 24 20",
    };
    Utils::SignatureHook hook8 {
        .tag = __func__,
        .signature = "0F 57 F6    8B C0    F3 48 0F 2A F0    F3 41 0F 5E F1    F3 0F 59 35 ?? ?? ?? ??    E8 ?? ?? ?? ??    F3 44 0F 10 05 ?? ?? ?? ??",
    };
    Utils::SignatureHook hook9 {
        .tag = __func__,
        .signature = "F3 0F 59 47 08    F3 0F 11 44 24 28",
    };
    Utils::SignatureHook hook10 {
        .tag = __func__,
        .signature = "48 8D 15 ?? ?? ?? ??    48 8B CB    E8 ?? ?? ?? ??    80 BB ?? ?? ?? ?? 01",
    };

    bool enable = yml.masterEnable & yml.hud.enable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");

    static bool resetTimer{false};
    static std::mutex mtx;
    static std::condition_variable cv;

    std::thread([&]() {
        std::unique_lock<std::mutex> lock(mtx);
        while (true) {
            resetTimer = false;
            if (cv.wait_for(lock, std::chrono::milliseconds(20), [&] { return resetTimer; }) == false) {
                isCostumeView.store(false);
            }
        }
    }).detach();

    Utils::injectHook(enable, module, hook1,
        [](SafetyHookContext& ctx) {
            f32 normalizedHalfWidth = 1.0f / (static_cast<f32>(yml.resolution.width) / 2.0f);
            f32 normalizedHalfHeight = -1.0f / (static_cast<f32>(yml.resolution.height) / 2.0f);
            if (ctx.xmm0.f32[0] == normalizedHalfWidth && ctx.xmm0.f32[1] == normalizedHalfHeight) {
                f32 normalizedWidth = 1.0f / static_cast<f32>(yml.resolution.width);
                if (*reinterpret_cast<u32*>(ctx.rdx + 0x10) == 0x3F800000 && *reinterpret_cast<u32*>(ctx.rsp + 0x20) != 0 && (ctx.xmm2.f32[0] != normalizedWidth)) {
                    if (isCostumeView.load() == true) {
                        if (ctx.xmm3.f32[0] != static_cast<f32>(yml.resolution.width)) {
                            return;
                        }
                        else if (ctx.xmm3.f32[0] == static_cast<f32>(yml.resolution.width)) {
                            if (*reinterpret_cast<u64*>(ctx.rsp + 0x08) == 0 || *reinterpret_cast<f32*>(ctx.rsp + 0xFC) == static_cast<f32>(yml.resolution.height)) {
                                return;
                            }
                        }
                    }
                    f32 scaler = static_cast<f32>(yml.resolution.aspectRatio) / static_cast<f32>(nativeAspectRatio);
                    ctx.xmm0.f32[0] = 2.0f / (static_cast<f32>(yml.resolution.width) * scaler);
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
    Utils::injectHook(enable, module, hook7,
        [](SafetyHookContext& ctx) {
            ctx.xmm0.f32[0] = static_cast<f32>(yml.resolution.width) / (1280.0f * (yml.resolution.aspectRatio / nativeAspectRatio));
        }
    );
    Utils::injectHook(enable, module, hook8,
        [](SafetyHookContext& ctx) {
            ctx.xmm9.f32[0] = 1280.0f * (yml.resolution.aspectRatio / nativeAspectRatio);
        }
    );
    Utils::injectHook(enable, module, hook9,
        [](SafetyHookContext& ctx) {
            ctx.xmm0.f32[0] = 1280.0f / (yml.resolution.aspectRatio / nativeAspectRatio);
        }
    );
    Utils::injectHook(enable, module, hook10,
        [](SafetyHookContext& ctx) {
            isCostumeView.store(true);
            {
                std::lock_guard<std::mutex> lock(mtx);
                resetTimer = true;
            }
            cv.notify_one();
        }
    );
}

/**
 * @brief Fixes the cutscenes.
 *
 * @details
 * This function injects 3 hooks which all work together to fix the cutscenes by making them render at the
 * correct aspect ratio and not be stretched. This also fixes the squished rendering of 3D models when viewing
 * costumes. Note that this doesn't fix that issue entirely, but at least they won't be horizontally squished
 * anymore. All in all this is needed because the cutscenes being squished are a side effect of both the
 * resolution and HUD fixes.
 *
 * How was this found?
 * This was relatively simple to find, albeit understanding the problem why this could be happening took longer.
 * But eventually I landed on the idea what happens if I just change the aspect ratio which the game is using
 * and viola unstretched cutscenes. From there I really just needed to figure out where I can hook in a flag
 * that will tell me if I am in a cutscene or not.
 *
 * The game has the string `INPUT_SCENE` hardcoded into the binary twice. I breakpointed all instances of it
 * and found that this string does get accessed the game during cutscenes. The peice(s) of code that accesses this
 * string depend on what type of cutscene is playing, a prerendered cutscene does not access this string the same
 * amount of times and in the same places as an in engine cutscene would. So the hooking location should be where
 * in engine cutscenes access this string. This is where hook2 is placed which will set the cutscene flag.
 *
 * @return void
 */
void fixCutscenes() {
    Utils::SignatureHook hook1 {
        .tag = __func__,
        .signature = "41 8B E8    8B F2    48 8B D9    E8 ?? ?? ?? ??",
    };
    Utils::SignatureHook hook2 {
        .tag = __func__,
        .signature = "48 8D 0D ?? ?? ?? ??    4D 8B F8    4C 8B F2",
    };
    Utils::SignatureHook hook3 {
        .tag = __func__,
        .signature = "F3 44 0F 5E 0D ?? ?? ?? ??    F3 44 0F 59 8C 24 38 01 00 00",
    };

    bool enable = yml.masterEnable & yml.hud.enable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");

    static bool resetTimer1{false};
    static std::mutex mtx1;
    static std::condition_variable cv1;
    static bool resetTimer2{false};
    static std::mutex mtx2;
    static std::condition_variable cv2;

    std::thread([&]() {
        std::unique_lock<std::mutex> lock(mtx1);
        while (true) {
            resetTimer1 = false;
            if (cv1.wait_for(lock, std::chrono::milliseconds(20), [&] { return resetTimer1; }) == false) {
                isCutscene.store(false);
            }
        }
    }).detach();
    std::thread([&]() {
        std::unique_lock<std::mutex> lock(mtx2);
        while (true) {
            resetTimer2 = false;
            if (cv2.wait_for(lock, std::chrono::milliseconds(20), [&] { return resetTimer2; }) == false) {
                isGameplay.store(false);
            }
        }
    }).detach();

    Utils::injectHook(enable, module, hook1,
        [](SafetyHookContext& ctx) {
            if (isGameplay.load() == false) {
                if (isCutscene.load() == true || isCostumeView.load() == true) {
                    ctx.xmm1.f32[0] = nativeAspectRatio;
                }
            }
        }
    );
    Utils::injectHook(enable, module, hook2,
        [](SafetyHookContext& ctx) {
            isCutscene.store(true);
            {
                std::lock_guard<std::mutex> lock(mtx1);
                resetTimer1 = true;
            }
            cv1.notify_one();
        }
    );
    Utils::injectHook(enable, module, hook3,
        [](SafetyHookContext& ctx) {
            isGameplay.store(true);
            {
                std::lock_guard<std::mutex> lock(mtx2);
                resetTimer2 = true;
            }
            cv2.notify_one();
        }
    );
}

/**
 * @brief Forces borderless fullscreen.
 *
 * @details
 * When the game is run on a 21:9 monitor, the game refuses to let you go into fullscreen mode for whatever
 * reason. In the options you are unable to select fullscreen at all. This function forces the game to go
 * into borderless fullscreen.
 *
 * How was this found?
 * Windows has multiple window APIs that work together that let you play around with windows and how they
 * behave. In the game code we need to look for calls to the following functions:
 *  - SetWindowLongA
 *  - SetWindowPos
 *
 * There are a few others, but these are the big ones for this x64 game, x86 is a bit different as well.
 *
 * Anyway, we looked for these calls and basically I needed to first hook into all of them to see the
 * order that they are called and whichever the last one was that is the correct hook point, which turned
 * out to be a call to `SetWindowPos`.
 *
 * So before we can even hook first that needed to be done was that call needed to be patched out so we
 * preserve the window handle stored in RCX register rather than it being overwritten by some instruction
 * in that call. Post patch we can hook at that location now and we have a valid window handle and we can
 * start making all the right calls to force the game into borderless fullscreen.
 *
 * @return void
 */
void fixFullscreen() {
    Utils::SignaturePatch patch1 {
        .tag = __func__,
        .signature = "FF 15 ?? ?? ?? ??    48 8B 7C 24 68    48 8B 5C 24 70    48 8B 4C 24 50",
        .patch = "90 90 90 90 90 90"
    };
    Utils::SignatureHook hook1 {
        .tag = __func__,
        .signature = "48 8B 7C 24 68    48 8B 5C 24 70    48 8B 4C 24 50",
    };

    bool enable = yml.masterEnable & yml.fullscreen.enable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");
    Utils::injectPatch(enable, module, patch1);
    Utils::injectHook(enable, module, hook1,
        [](SafetyHookContext& ctx) {
            HWND hWnd = reinterpret_cast<HWND>(ctx.rcx);

            // 1. Get current styles
            LONG style = GetWindowLongA(hWnd, GWL_STYLE);
            LONG exStyle = GetWindowLongA(hWnd, GWL_EXSTYLE);

            // 2. Strip normal window chrome (title bar, borders, resize frame, sysmenu)
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU);
            // 3. Make it a popup-style window (borderless)
            style |= WS_POPUP;

            // 4. Strip some extended edges
            exStyle &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_OVERLAPPEDWINDOW);

            // 5. Apply new styles
            SetWindowLongA(hWnd, GWL_STYLE, style);
            SetWindowLongA(hWnd, GWL_EXSTYLE, exStyle);

            // 6. Tell Windows the frame has changed so it recalculates non-client area
            SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

            // 7. Get the monitor info to know the correct dimensions for fullscreen
            HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfoA(mon, &mi);
            int x = mi.rcMonitor.left;
            int y = mi.rcMonitor.top;
            int w = mi.rcMonitor.right - mi.rcMonitor.left;
            int h = mi.rcMonitor.bottom - mi.rcMonitor.top;

            // 8. Resize and show the window to cover the entire monitor
            SetWindowPos(hWnd, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
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
    fixCutscenes();
    fixFullscreen();
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
