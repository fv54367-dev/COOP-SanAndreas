set_project("COOPSanAndreas")
-- no build.ccache: with MSVC and the precompiled headers it never hit on a fresh CI machine and made every compile
-- several times slower (642 files: 4 min without it, 18-51 min with it, 2026-11-14/16)

set_languages("cxx17")
set_arch("x86")

add_rules("mode.debug", "mode.release")

if is_os("windows") then
    if is_mode("debug") then
        set_runtimes("MTd")
    else 
        set_runtimes("MT")
    end
end

target("client", function()
    set_kind("shared")
    set_plat("windows")

    set_toolchains("msvc")

    set_basename("CoopAndreasSA")
    set_arch("x86")
    set_plat("windows")

    local gta_sa_dir = os.getenv("GTA_SA_DIR")
    if gta_sa_dir then
        set_targetdir(gta_sa_dir)
    end

    --add_rules("c++.unity_build")
    
    add_files("client/src/*.cpp")
    add_files("client/src/Commands/*.cpp")
    add_files("client/src/Commands/Commands/*.cpp")
    add_files("client/src/game_sa/*.cpp")
    add_files("client/src/Hooks/*.cpp")
    add_files("client/src/PacketHandlers/*.cpp")
    add_files("client/src/Debug/*.cpp")
    add_files("client/src/UI/*.cpp")
    add_files("client/version.rc")

    add_headerfiles("client/src/*.h")
    add_headerfiles("client/src/Commands/*.h")
    add_headerfiles("client/src/Commands/Commands/*.h")
    add_headerfiles("client/src/game_sa/*.h")
    add_headerfiles("client/src/Hooks/*.h")
    add_headerfiles("client/src/Debug/*.h")
    add_headerfiles("client/src/UI/*.h")
    add_headerfiles("shared/**.h")

    add_includedirs("client/src")
    add_includedirs("shared", "third_party")
    add_includedirs("third_party/DiscordRPC/SDK/include")

    add_files("third_party/imgui/**.cpp")
    add_headerfiles("third_party/imgui/**.h")
    add_includedirs("third_party/imgui")

    add_includedirs(
        "third_party/plugin-sdk/shared",
        "third_party/plugin-sdk/shared/game",
        "third_party/plugin-sdk/shared/dxsdk",
        "third_party/plugin-sdk/plugin_sa",
        "third_party/plugin-sdk/plugin_sa/game_sa"
    )

    set_pcxxheader("client/src/stdafx.h")


    add_defines(
        "COOP_CLIENT", 
        "NOMINMAX", 
        "_CRT_SECURE_NO_WARNINGS", 
        "_CRT_NON_CONFORMING_SWPRINTFS", 
        "_USE_MATH_DEFINES", 
        "_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING", 
        "GTASA", 
        "PLUGIN_SGV_10US", 
        "RW"
    )

    if is_mode("debug") then
        add_defines("DEBUG")
    else
        add_defines("NDEBUG")
        set_strip("all")
        set_optimize("fastest")
    end

    add_syslinks("kernel32", "user32", "ws2_32", "winmm", "Advapi32")

    add_linkdirs(
        "third_party/plugin-sdk/shared/bass",
        "third_party/plugin-sdk/shared/dxsdk"
    )

    add_deps("plugin_sa")
    add_deps("enet")
    add_deps("discordrpc")

    add_links("d3dx9") -- TODO STATIC

    -- version.rc embeds client/data/*.txt as RCDATA, but rc.exe only runs again when the .rc
    -- itself changes: an edited table would be silently ignored. Rewrite the .rc (same bytes)
    -- when a data file is newer, so the change reaches the binary.
    -- Note: os.touch() is a no-op on Windows here (opens and closes without writing).
    before_build(function (target)
        local rc = path.join(os.projectdir(), "client/version.rc")
        local rcTime = os.mtime(rc)
        if not rcTime then
            return
        end
        for _, name in ipairs({ "client/data/cutscenes.txt", "client/data/cutscene_ways.txt" }) do
            local fileTime = os.mtime(path.join(os.projectdir(), name))
            if fileTime and fileTime > rcTime then
                local reader = io.open(rc, "rb")
                local content = reader:read("*a")
                reader:close()
                local writer = io.open(rc, "wb")
                writer:write(content)
                writer:close()
                cprint("${yellow}version.rc: %s cambio, se recompila el recurso", name)
                return
            end
        end
    end)
end)

target("server", function ()
    set_kind("binary")

    set_arch("x86")
    set_languages("c++17")

    add_files("server/src/**.cpp")
    add_files("third_party/INIReader/cpp/INIReader.cpp")
    add_files("third_party/INIReader/ini.c")

    add_headerfiles("server/src/*.h")
    add_headerfiles("shared/**.h")
    add_headerfiles("third_party/enet/*.h")

    add_includedirs(
        "server/src/",
        "third_party/plugin-sdk/shared",
        "third_party/plugin-sdk/shared/game",
        "third_party/plugin-sdk/plugin_sa",
        "third_party/plugin-sdk/plugin_sa/game_sa"
    )

    add_includedirs("shared", "third_party")

    set_pcxxheader("server/src/stdafx.h")


    add_defines(
        "COOP_SERVER", 
        "NOMINMAX", 
        "GTASA"
    )
    
    if is_mode("debug") then
        add_defines("_DEBUG")
        set_optimize("none")
        set_symbols("debug")
    else
        add_defines("NDEBUG")
        set_optimize("fastest")
        set_strip("all")
    end

    add_deps("enet")

    if is_os("windows") then
        add_files("server/version.rc")
        add_defines("_CRT_SECURE_NO_WARNINGS", "WIN32", "_CONSOLE")
    elseif is_os("linux") then
        -- TODO
    end
end)

-- B3: bot de prueba del server (tools/testbot). Habla el mismo protocolo que el cliente real
-- (ENet + shared/network/packets), asi que copia las dependencias del target `server`.
target("testbot", function ()
    set_kind("binary")

    set_arch("x86")
    set_languages("c++17")

    add_files("tools/testbot/*.cpp")
    add_headerfiles("tools/testbot/*.h")
    add_headerfiles("shared/**.h")
    add_headerfiles("third_party/enet/*.h")

    add_includedirs(
        "tools/testbot",
        "third_party/plugin-sdk/shared",
        "third_party/plugin-sdk/shared/game",
        "third_party/plugin-sdk/plugin_sa",
        "third_party/plugin-sdk/plugin_sa/game_sa"
    )

    add_includedirs("shared", "third_party")

    add_defines(
        "COOP_CLIENT",
        "NOMINMAX",
        "GTASA"
    )

    if is_mode("debug") then
        add_defines("_DEBUG")
        set_optimize("none")
        set_symbols("debug")
    else
        add_defines("NDEBUG")
        set_optimize("fastest")
        set_strip("all")
    end

    add_deps("enet")

    if is_os("windows") then
        add_defines("_CRT_SECURE_NO_WARNINGS", "WIN32", "_CONSOLE")
        add_syslinks("ws2_32", "winmm")
    end
end)

target("proxy", function ()
    set_kind("shared")

    set_toolchains("msvc")

    set_arch("x86")
    set_plat("windows")

    add_files("proxy/src/*.cpp")
    
    add_headerfiles("proxy/src/*.h")
    
    add_files("proxy/version.rc")

    add_defines(
        "PROXY_EXPORTS", 
        "_WINDOWS", 
        "_USRDLL"
    )

    add_syslinks("kernel32", "user32")

    if is_mode("debug") then
        add_defines("_DEBUG")
        set_symbols("debug")
    else
        add_defines("NDEBUG")
        set_optimize("fastest")
        set_strip("all")
    end
end)

target("plugin_sa", function ()
    set_kind("static")

    set_toolchains("msvc")

    set_arch("x86")
    set_plat("windows")

    add_files("third_party/plugin-sdk/plugin_sa/**.cpp")
    add_files("third_party/plugin-sdk/shared/**.cpp")
    
    add_headerfiles("third_party/plugin-sdk/plugin_sa/**.h")
    add_headerfiles("third_party/plugin-sdk/shared/**.h")

    add_includedirs("third_party/plugin-sdk/plugin_sa/game_sa")
    add_includedirs("third_party/plugin-sdk/plugin_sa/game_sa/rw")
    add_includedirs("third_party/plugin-sdk/shared")
    add_includedirs("third_party/plugin-sdk/shared/dxsdk")
    add_includedirs("third_party/plugin-sdk/shared/game")

    add_defines("GTASA")
    add_defines("PLUGIN_SGV_10US")
    add_defines("RW")

    set_pcxxheader("third_party/plugin-sdk/plugin_sa/plugin_sa.h")
end)

target("enet", function ()
    set_kind("static")

    add_files("third_party/enet/*.c")
    
    add_headerfiles("third_party/enet/*.h")

    add_includedirs("third_party/enet/")
    add_includedirs("third_party/")

    set_pcxxheader("third_party/enet/enet.h")
end)

target("discordrpc", function ()
    set_kind("static")
    
    set_languages("c++17")

    set_toolchains("msvc")
    
    add_includedirs("third_party/DiscordRPC/SDK/include")
    add_files("third_party/DiscordRPC/SDK/src/*.cpp")
    add_headerfiles("third_party/DiscordRPC/SDK/src/*.h")
end)

-- Launcher CoopSA (Rust + Win32 puro, sin serial): xmake invoca cargo y copia el resultado
-- a build\windows\x86\release\CoopSA.exe (+ .manifest). El exe es x64 con CRT estatico;
-- el resto del repo sigue x86. Requiere cargo en PATH (rust) y rc.exe (Windows SDK).
target("launcher")
    set_kind("phony")

    on_build(function (target)
        local outdir = path.join(os.projectdir(), "build", "windows", "x86", "release")
        os.execv("cargo", { "build", "--release", "--manifest-path", "Cargo.toml" }, { curdir = path.join(os.projectdir(), "launcher"), envs = { RUSTFLAGS = "-C target-feature=+crt-static" } })
        if not os.isdir(outdir) then
            os.mkdir(outdir)
        end
        os.cp("launcher/target/release/CoopSA.exe", outdir)
        os.cp("launcher/CoopSA.exe.manifest", outdir)
    end)
