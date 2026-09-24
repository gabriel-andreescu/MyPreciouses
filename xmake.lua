set_xmakever("3.1.1")
set_project("MyPreciouses")
set_license("GPL-3.0")
set_policy("package.requires_lock", true)

local version = "1.0.0"
local papyrus_imports = (get_config("papyrus_imports") or ""):split(";", { plain = true })
local bath_patch_imports = table.join({
    path.join(os.projectdir(), "src/papyrus"),
    path.join(os.projectdir(), "third_party/papyrus/diziet"),
    path.join(os.projectdir(), "third_party/papyrus/bathing-in-skyrim"),
}, papyrus_imports)

add_repositories("bmk https://github.com/gabriel-andreescu/BethesdaModKit.git")
add_addons("bmk 0.3.0")
includes("@addon/bmk/project")
includes("@addon/bmk/native")

-- Dependencies
add_requires("commonlibsse-ng 8.0.1", { system = false })
add_requires("clib-util 1.5.0", { system = false })
add_requires("bmk", "devbench-api 2026.09.13", { system = false })
add_requires("caprica", { host = true })
add_requires("skyrim-papyrus-sdk", {
    configs = { skse = true, mcm = true, papyrus_extender = true },
})
add_requires("ffdec 26.3.0", { host = true })

option("papyrus_imports", { description = "Papyrus import directories separated by ;" })
option("papyrus_flags", { description = "Optional Papyrus flags file override" })

-- Build targets
target("Native", function()
    set_default(false)
    set_basename("MyPreciouses")
    set_version(version)
    add_rules("@commonlibsse-ng/plugin", {
        author = "GabonZ",
        description = "Wear rings on every finger",
    })
    add_rules("@addon/bmk/skyrim.plugin")
    add_rules("@devbench-api/integration")
    add_files("src/native/**.cpp")
    add_includedirs("src/native")
    set_pcxxheader("src/native/PCH.h")
    add_defines("WIN32_LEAN_AND_MEAN", "NOGDI")
    add_packages("commonlibsse-ng", "clib-util", "bmk", "devbench-api")
end)

target("Papyrus", function()
    set_default(false)
    add_rules("@addon/bmk/skyrim.papyrus", {
        root = "src/papyrus",
        imports = papyrus_imports,
        flags = get_config("papyrus_flags"),
        arguments = { "--strict", "--enable-language-extensions=true" },
    })
    add_packages("caprica", "skyrim-papyrus-sdk")
    add_files("src/papyrus/*.psc")
    add_installfiles("src/papyrus/(**.psc)|mcm/**|bath/**", { prefixdir = "Source/Scripts" })
end)

target("MCMScripts", function()
    set_default(false)
    add_rules("@addon/bmk/skyrim.papyrus", {
        root = "src/papyrus/mcm",
        imports = table.join({ path.join(os.projectdir(), "src/papyrus") }, papyrus_imports),
        flags = get_config("papyrus_flags"),
        arguments = { "--strict", "--enable-language-extensions=true" },
    })
    add_packages("caprica", "skyrim-papyrus-sdk")
    add_files("src/papyrus/mcm/**.psc")
    add_installfiles("src/papyrus/mcm/(**.psc)", { prefixdir = "Source/Scripts" })
end)

target("BathPatchScripts", function()
    set_default(false)
    add_rules("@addon/bmk/skyrim.papyrus", {
        root = "src/papyrus/bath",
        imports = bath_patch_imports,
        flags = get_config("papyrus_flags"),
        arguments = { "--strict", "--enable-language-extensions=true" },
    })
    add_packages("caprica", "skyrim-papyrus-sdk")
    add_files("src/papyrus/bath/**.psc")
    add_installfiles("src/papyrus/bath/(**.psc)", { prefixdir = "Source/Scripts" })
end)

target("InterfaceVanilla", function()
    set_default(false)
    set_configdir("$(builddir)/generated/interface/vanilla/frame_1")
    set_configvar("MYPRECIOUSES_USE_SKYUI_BUTTON_ART", "false")
    add_configfiles("src/interface/fingerselect/actionscript/FingerSelectMenu.as.in", {
        filename = "DoAction.as",
        pattern = "@(.-)@",
    })
    add_rules("@addon/bmk/ffdec", {
        xml = "src/interface/fingerselect/swf/fingerselect.xml",
        scripts = "$(builddir)/generated/interface/vanilla",
        output = "mypreciouses_fingerselect.swf",
    })
    add_packages("ffdec")
end)

target("InterfaceSkyUI", function()
    set_default(false)
    set_configdir("$(builddir)/generated/interface/skyui/frame_1")
    set_configvar("MYPRECIOUSES_USE_SKYUI_BUTTON_ART", "true")
    add_configfiles("src/interface/fingerselect/actionscript/FingerSelectMenu.as.in", {
        filename = "DoAction.as",
        pattern = "@(.-)@",
    })
    add_rules("@addon/bmk/ffdec", {
        xml = "src/interface/fingerselect/swf/fingerselect_skyui.xml",
        scripts = "$(builddir)/generated/interface/skyui",
        output = "mypreciouses_fingerselect_skyui.swf",
    })
    add_packages("ffdec")
end)

target("Mutagen", function()
    set_default(false)
    add_extrafiles("src/mutagen/MyPreciouses/FormIDs.txt")
    add_rules("@addon/bmk/dotnet", {
        project = "src/mutagen/MyPreciouses/MyPreciouses.csproj",
        arguments = { "$(outputdir)", path.absolute("src/mutagen/MyPreciouses/FormIDs.txt") },
    })
end)

-- Packages
target("MyPreciouses", function()
    set_version(version)
    add_rules("@addon/bmk/skyrim.package", {
        targets = { "Native", "Papyrus", "InterfaceVanilla", "InterfaceSkyUI" },
        nexus = {
            mod_id = "7318624452399",
            file_id = "7442558",
            category = "main",
            primary = true,
            description = "Updating from an older version? Follow the Updating to 1.0.0 instructions in the mod description before installing.",
        },
    })
    add_installfiles("assets/(**)|optional/**")
end)

target("MyPreciousesMCM", function()
    set_version(version)
    add_deps("Mutagen", { inherit = false })
    add_rules("@addon/bmk/skyrim.package", {
        targets = { "MCMScripts" },
        package_name = "MyPreciouses - MCM Addon",
        nexus = {
            mod_id = "7318624452399",
            file_id = "7442579",
            category = "optional",
            description = "MCM addon for MyPreciouses 1.0.0. Requires SkyUI and MCM Helper. VR also requires [url=https://www.nexusmods.com/skyrimspecialedition/mods/106712]Skyrim VR ESL Support[/url].",
        },
    })
    add_installfiles("$(builddir)/artifacts/Mutagen/mcm/(MyPreciouses.esp)")
    add_installfiles("assets/optional/mcm/(**)")
end)

target("DizietBathPatch", function()
    set_version(version)
    add_rules("@addon/bmk/skyrim.package", {
        targets = { "BathPatchScripts" },
        package_name = "MyPreciouses - Diziet Bath Patch",
        nexus = {
            mod_id = "7318624452399",
            file_id = "7995933",
            category = "optional",
            description = "Restores extra rings after bathing. Requires MyPreciouses 1.0.0 and Diziet's Player Home Bath Undressing 7.1.2.7. Install after both mods and let this patch overwrite Diziet's script.",
        },
    })
    add_installfiles("assets/optional/bath/(**)")
end)
